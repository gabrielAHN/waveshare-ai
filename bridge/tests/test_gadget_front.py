"""LAN front for the Hermes gateway's stock Gadget SDK platform (see waveshare_bridge/gadget_front.py)."""
import asyncio
import hashlib
import json
import os
import pathlib
import ssl
import tempfile
import unittest
from unittest import mock

from waveshare_bridge import config, enroll, gadget_front, phone_pair
from tests.test_enroll import make_cert

TOKEN = bytes(range(32))                 # same vector as firmware tests/host/gadget_wire_test.c
DEVICE = 'hg-98730b5fff9164f0'
PRIOR_DEVICE = 'hg-a252430fbc0c0fff'
BEARER = 'Bearer ' + TOKEN.hex()


def head(path='/gadget/helper', bearer=BEARER, extra=''):
    auth = f'Authorization: {bearer}\r\n' if bearer else ''
    return (f'GET {path} HTTP/1.1\r\nHost: 10.0.0.5:8768\r\n{auth}Upgrade: websocket\r\nConnection: Upgrade\r\n'
            f'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n'
            f'Sec-WebSocket-Protocol: hermes-gadget.v1\r\n{extra}\r\n').encode()


class IdentityTests(unittest.TestCase):
    def test_device_id_matches_the_firmware_derivation(self):
        self.assertEqual(gadget_front.device_id_for_token(TOKEN), DEVICE)


class ConfigTests(unittest.TestCase):
    def test_off_unless_configured(self):
        self.assertIsNone(config.gadget_gateway({}))

    def test_valid_front(self):
        got = config.gadget_gateway({'gadget_sdk': {'port': 8768, 'profiles': {'helper': 8775, 'atlas': 8776}}})
        self.assertEqual(got, {'port': 8768, 'upstream_host': '127.0.0.1', 'profiles': {'helper': 8775, 'atlas': 8776}})

    def test_rejects_unsafe_values(self):
        for bad in ({'port': 0, 'profiles': {'helper': 8775}},
                    {'port': 8768, 'profiles': {}},
                    {'port': 8768, 'profiles': {'../default': 8775}},
                    {'port': 8768, 'profiles': {'helper': 8768}},
                    {'port': 8768, 'profiles': {'helper': 8775, 'atlas': 8775}},
                    {'port': 8768, 'profiles': {'helper': '8775'}},
                    'yes'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                config.gadget_gateway({'gadget_sdk': bad})


class GrantTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.ledger = self.root / 'gadget-grants.json'
        self.grants = gadget_front.Grants(self.path, self.ledger, clock=lambda: 1000.0)
        self.approved = self.path('helper')

    def path(self, profile):
        return self.root / 'profiles' / profile / 'platforms' / 'pairing' / 'gadget-approved.json'

    def tearDown(self):
        self.tmp.cleanup()

    def test_grant_approves_and_records_without_touching_other_devices(self):
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({'hg-0000000000000001': {'user_name': 'kitchen', 'approved_at': 1}}))
        self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk board')
        data = json.loads(self.approved.read_text())
        self.assertEqual(data['hg-0000000000000001'], {'user_name': 'kitchen', 'approved_at': 1})
        self.assertEqual(data[DEVICE], {'user_name': 'Waveshare AI (Desk board)', 'approved_at': 1000.0})
        self.assertEqual(json.loads(self.ledger.read_text()), {
            'version': 2, 'grants': {f'helper|{DEVICE}': {'board': 'ab' * 32}},
            'retiring': {}, 'history': [],
        })
        self.assertEqual(os.stat(self.approved).st_mode & 0o777, 0o600)
        self.assertEqual(os.stat(self.ledger).st_mode & 0o777, 0o600)

    def test_each_bot_profile_gets_its_own_approval(self):
        self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        self.grants.grant('atlas', DEVICE, 'ab' * 32, 'Desk')
        self.assertIn(DEVICE, json.loads(self.path('helper').read_text()))
        self.assertIn(DEVICE, json.loads(self.path('atlas').read_text()))
        self.assertFalse(self.path('coding').exists())

    def test_grant_is_idempotent(self):
        self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        before = self.approved.stat().st_mtime_ns
        self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        self.assertEqual(self.approved.stat().st_mtime_ns, before)

    def test_sweep_revokes_only_our_grants_for_signed_out_boards(self):
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({'hg-0000000000000001': {'user_name': 'kitchen', 'approved_at': 1}}))
        self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        self.grants.grant('atlas', DEVICE, 'ab' * 32, 'Desk')
        self.grants.grant('helper', 'hg-00000000000000ff', 'cd' * 32, 'Other')
        revoked = self.grants.sweep(lambda sha: sha == 'cd' * 32)
        self.assertEqual(revoked, [f'atlas|{DEVICE}', f'helper|{DEVICE}'])
        self.assertEqual(sorted(json.loads(self.approved.read_text())), ['hg-0000000000000001', 'hg-00000000000000ff'])
        self.assertEqual(json.loads(self.path('atlas').read_text()), {})
        ledger = json.loads(self.ledger.read_text())
        self.assertEqual(ledger['grants'], {'helper|hg-00000000000000ff': {'board': 'cd' * 32}})
        self.assertEqual([event['reason'] for event in ledger['history']],
                         ['authorization-revoked', 'authorization-revoked'])

    def test_unreadable_pairing_file_is_never_clobbered(self):
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text('{not json')
        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        self.assertEqual(self.approved.read_text(), '{not json')

    def test_domain_rollover_is_scoped_and_retains_policy_quota_and_audit_evidence(self):
        board_hash = hashlib.sha256(TOKEN).hexdigest()
        other_hash = 'cd' * 32
        other_device = 'hg-0000000000000001'
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9}
        other_approval = {'user_name': 'operator approved', 'approved_at': 3}
        for profile in ('helper', 'atlas'):
            path = self.path(profile)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps({PRIOR_DEVICE: prior_approval, other_device: other_approval}))
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3}
        other_grant = {'board': other_hash, 'counter': 2}
        self.ledger.write_text(json.dumps({
            f'helper|{PRIOR_DEVICE}': prior_grant,
            f'atlas|{PRIOR_DEVICE}': prior_grant,
            f'helper|{other_device}': other_grant,
        }))
        board_policy = self.root / 'boards.json'
        board_policy.write_text(json.dumps({'boards': [{
            'token_sha256': board_hash, 'added': 11, 'phone_user': 'sam',
            'phone_groups': ['admins'], 'sign_ins': {'ab' * 8: {'groups': ['home_users']}},
        }]}))
        quota_latch = self.root / 'quota-blocks.json'
        quota_latch.write_text(json.dumps({'anthropic': 'exhausted'}))
        policy_before, quota_before = board_policy.read_bytes(), quota_latch.read_bytes()

        self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        helper = json.loads(self.path('helper').read_text())
        atlas = json.loads(self.path('atlas').read_text())
        ledger = json.loads(self.ledger.read_text())
        self.assertEqual(helper[DEVICE], prior_approval)
        self.assertNotIn(PRIOR_DEVICE, helper)
        self.assertEqual(helper[other_device], other_approval)
        self.assertEqual(atlas, {PRIOR_DEVICE: prior_approval, other_device: other_approval})
        self.assertEqual(ledger['version'], 2)
        self.assertEqual(ledger['grants'], {
            f'helper|{DEVICE}': prior_grant,
            f'atlas|{PRIOR_DEVICE}': prior_grant,
            f'helper|{other_device}': other_grant,
        })
        self.assertEqual(ledger['retiring'], {})
        self.assertEqual(ledger['history'], [{
            'key': f'helper|{PRIOR_DEVICE}', 'reason': 'identity-domain-rollover', 'at': 1000.0,
            'grant': prior_grant, 'approval': prior_approval,
            'successor': {'key': f'helper|{DEVICE}', 'grant': prior_grant, 'approval': prior_approval},
        }])
        self.assertEqual((board_policy.read_bytes(), quota_latch.read_bytes()), (policy_before, quota_before))

        revoked = self.grants.sweep(lambda sha: sha == other_hash)
        self.assertEqual(revoked, [f'atlas|{PRIOR_DEVICE}', f'helper|{DEVICE}'])
        ledger = json.loads(self.ledger.read_text())
        self.assertEqual(ledger['grants'], {f'helper|{other_device}': other_grant})
        self.assertEqual(ledger['retiring'], {})
        self.assertEqual([event['reason'] for event in ledger['history']],
                         ['identity-domain-rollover', 'authorization-revoked', 'authorization-revoked'])
        self.assertEqual(ledger['history'][1]['grant'], prior_grant)
        self.assertEqual(ledger['history'][1]['approval'], prior_approval)
        self.assertEqual(ledger['history'][2]['approval'], prior_approval)
        self.assertEqual(json.loads(self.path('helper').read_text()), {other_device: other_approval})
        self.assertEqual(json.loads(self.path('atlas').read_text()), {other_device: other_approval})
        self.assertEqual((board_policy.read_bytes(), quota_latch.read_bytes()), (policy_before, quota_before))

    def test_rollover_preserves_active_grant_and_sdk_metadata(self):
        board_hash = hashlib.sha256(TOKEN).hexdigest()
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3,
                       'policy_extension': {'mode': 'strict'}}
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))

        self.grants.grant('helper', DEVICE, board_hash, 'Renamed desk')

        ledger = json.loads(self.ledger.read_text())
        self.assertEqual(ledger['grants'][f'helper|{DEVICE}'], prior_grant)
        self.assertEqual(json.loads(self.approved.read_text())[DEVICE], prior_approval)

    def test_existing_unowned_identity_is_never_claimed_or_overwritten(self):
        existing = {'user_name': 'operator approved', 'approved_at': 1, 'counter': 4}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({DEVICE: existing}))
        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, 'ab' * 32, 'Desk')
        self.assertEqual(json.loads(self.approved.read_text()), {DEVICE: existing})
        self.assertFalse(self.ledger.exists())

    def test_interrupted_rollover_keeps_proof_and_recovers_without_old_approval(self):
        board_hash = 'ab' * 32
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9}
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 4}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
        write = gadget_front._write_private

        def interrupt(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic interruption')
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt), self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        transitional = json.loads(self.ledger.read_text())
        self.assertEqual(transitional['grants'][f'helper|{DEVICE}'], prior_grant)
        self.assertEqual(transitional['retiring'][f'helper|{PRIOR_DEVICE}']['approval'], prior_approval)
        self.assertEqual(transitional['retiring'][f'helper|{PRIOR_DEVICE}']['successor'], {
            'key': f'helper|{DEVICE}', 'grant': prior_grant, 'approval': prior_approval})
        self.assertIn(PRIOR_DEVICE, json.loads(self.approved.read_text()))

        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        recovered = json.loads(self.ledger.read_text())
        self.assertEqual(recovered['retiring'], {})
        self.assertEqual(recovered['history'][0]['approval'], prior_approval)
        approvals = json.loads(self.approved.read_text())
        self.assertEqual(approvals[DEVICE], prior_approval)
        self.assertNotIn(PRIOR_DEVICE, approvals)
        self.assertEqual(recovered['grants'][f'helper|{DEVICE}'], prior_grant)

    def test_revoke_after_interrupted_rollover_never_activates_successor(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 5}
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
        write = gadget_front._write_private

        def interrupt(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic interruption')
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt), self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        transitional = json.loads(self.ledger.read_text())
        self.assertEqual(transitional['retiring'][f'helper|{PRIOR_DEVICE}']['successor']['grant'], prior_grant)

        approval_writes = []

        def observe(path, value):
            if pathlib.Path(path) == self.approved:
                approval_writes.append(value)
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=observe):
            revoked = self.grants.sweep(lambda _board: False)
        self.assertEqual(revoked, [f'helper|{DEVICE}'])
        self.assertTrue(all(DEVICE not in value for value in approval_writes))
        self.assertNotIn(PRIOR_DEVICE, json.loads(self.approved.read_text()))
        self.assertNotIn(DEVICE, json.loads(self.approved.read_text()))
        self.assertEqual(json.loads(self.ledger.read_text())['grants'], {})

    def test_denial_is_durable_if_sweep_is_interrupted_before_sdk_write(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 8}
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
        write = gadget_front._write_private

        def stop_rollover_at_sdk(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic rollover interruption')
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=stop_rollover_at_sdk), \
                self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        def stop_denial_before_sdk(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic denial interruption')
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=stop_denial_before_sdk), \
                self.assertRaises(OSError):
            self.grants.sweep(lambda _board: False)

        denied = json.loads(self.ledger.read_text())
        self.assertNotIn(f'helper|{DEVICE}', denied['grants'])
        self.assertTrue(denied['retiring'][f'helper|{PRIOR_DEVICE}']['successor_denied'])
        self.assertEqual(denied['retiring'][f'helper|{DEVICE}']['reason'], 'authorization-revoked')

        # Any later recovery sees the durable denial and can never install the successor.
        self.grants.sweep(lambda _board: False)
        approvals = json.loads(self.approved.read_text())
        self.assertNotIn(PRIOR_DEVICE, approvals)
        self.assertNotIn(DEVICE, approvals)

    def test_restart_after_successor_sdk_write_is_idempotent_and_preserves_metadata(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 6}
        prior_approval = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 42, 'counter': 9,
                          'sdk_extension': 'kept'}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
        write_ledger = gadget_front._write_ledger
        calls = 0

        def interrupt_final(*args):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('synthetic final-ledger interruption')
            return write_ledger(*args)

        with mock.patch.object(gadget_front, '_write_ledger', side_effect=interrupt_final), self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual(json.loads(self.approved.read_text())[DEVICE], prior_approval)

        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        first_ledger = self.ledger.read_bytes()
        first_approval = self.approved.read_bytes()
        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual(self.ledger.read_bytes(), first_ledger)
        self.assertEqual(self.approved.read_bytes(), first_approval)
        recovered = json.loads(first_ledger)
        self.assertEqual(recovered['retiring'], {})
        self.assertEqual(recovered['grants'][f'helper|{DEVICE}'], prior_grant)

    def test_multiple_predecessors_for_one_profile_and_board_fail_closed(self):
        board_hash = 'ab' * 32
        other_old = 'hg-0000000000000002'
        grants = {
            f'helper|{PRIOR_DEVICE}': {'board': board_hash, 'counter': 7},
            f'helper|{other_old}': {'board': board_hash, 'counter': 8},
        }
        approvals = {
            PRIOR_DEVICE: {'user_name': 'first', 'approved_at': 1},
            other_old: {'user_name': 'second', 'approved_at': 2},
        }
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps(approvals))
        self.ledger.write_text(json.dumps(grants))
        before_ledger, before_approved = self.ledger.read_bytes(), self.approved.read_bytes()
        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()),
                         (before_ledger, before_approved))

    def test_foreign_successor_collision_during_recovery_fails_closed(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7}
        prior_approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9}
        foreign = {'user_name': 'manual operator', 'approved_at': 99}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': prior_grant, 'approval': prior_approval,
                 'successor': {'key': new_key, 'grant': prior_grant, 'approval': prior_approval}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval, DEVICE: foreign}))
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: prior_grant},
                                           'retiring': {old_key: event}, 'history': []}))
        before_ledger, before_approved = self.ledger.read_bytes(), self.approved.read_bytes()
        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()),
                         (before_ledger, before_approved))

    def test_present_null_successor_approval_is_not_treated_as_missing(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7}
        approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': grant, 'approval': approval,
                 'successor': {'key': new_key, 'grant': grant, 'approval': approval}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: approval, DEVICE: None}))
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: grant},
                                           'retiring': {old_key: event}, 'history': []}))
        before = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_successor_approval_authority_must_match_predecessor_without_writes(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3}
        prior_approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9,
                          'sdk_extension': 'keep'}
        malicious = {**prior_approval, 'approved_at': 1000, 'counter': 0}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': prior_grant, 'approval': prior_approval,
                 'successor': {'key': new_key, 'grant': prior_grant, 'approval': malicious}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: prior_grant},
                                           'retiring': {old_key: event}, 'history': []}))
        before_ledger, before_approved = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()),
                         (before_ledger, before_approved))

    def test_successor_approval_authority_comparison_is_json_type_exact(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7}
        prior_approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 1}
        bool_counter = {**prior_approval, 'counter': True}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': grant, 'approval': prior_approval,
                 'successor': {'key': new_key, 'grant': grant, 'approval': bool_counter}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: grant},
                                           'retiring': {old_key: event}, 'history': []}))
        before = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_missing_retirement_approval_field_is_not_explicit_null(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7}
        fresh = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 1000.0}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': grant,
                 'successor': {'key': new_key, 'grant': grant, 'approval': fresh}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text('{}')
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: grant},
                                           'retiring': {old_key: event}, 'history': []}))
        before = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_versioned_ledger_requires_integer_version_without_writes(self):
        board_hash = 'ab' * 32
        key = f'helper|{DEVICE}'
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text('{}')
        self.ledger.write_text(json.dumps({'version': 2.0, 'grants': {key: {'board': board_hash}},
                                           'retiring': {}, 'history': []}))
        before = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_explicit_null_predecessor_allows_only_bounded_fresh_approval(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7}
        fresh = {'user_name': 'Waveshare AI (Desk)', 'approved_at': 1000.0}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'

        def state(approval):
            event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                     'grant': grant, 'approval': None,
                     'successor': {'key': new_key, 'grant': grant, 'approval': approval}}
            return {'version': 2, 'grants': {new_key: grant},
                    'retiring': {old_key: event}, 'history': []}

        self.approved.parent.mkdir(parents=True)
        self.approved.write_text('{}')
        self.ledger.write_text(json.dumps(state(fresh)))
        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual(json.loads(self.approved.read_text()), {DEVICE: fresh})
        self.assertEqual(json.loads(self.ledger.read_text())['grants'][new_key], grant)

        invalid = {
            'authority-extension': {**fresh, 'counter': 0},
            'boolean-time': {**fresh, 'approved_at': True},
            'array-time': {**fresh, 'approved_at': (1000.0,)},
            'unbound-name': {**fresh, 'user_name': 'operator supplied'},
        }
        for name, approval in invalid.items():
            with self.subTest(name=name):
                self.approved.write_text('{}')
                self.ledger.write_text(json.dumps(state(approval)))
                before = self.ledger.read_bytes(), self.approved.read_bytes()
                with self.assertRaises(ValueError):
                    self.grants.grant('helper', DEVICE, board_hash, 'Desk')
                self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_single_incomplete_rollover_is_rejected_without_writes(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7}
        approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': grant, 'approval': approval}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: approval}))
        self.ledger.write_text(json.dumps({'version': 2, 'grants': {new_key: {'board': board_hash}},
                                           'retiring': {old_key: event}, 'history': []}))
        before = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), before)

    def test_conflicting_incomplete_rollovers_are_rejected_without_rewriting_ledger(self):
        board_hash = 'ab' * 32
        other_old = 'hg-0000000000000002'
        old_key, other_old_key = f'helper|{PRIOR_DEVICE}', f'helper|{other_old}'
        new_key = f'helper|{DEVICE}'
        first_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3}
        second_grant = {**first_grant, 'counter': 8}
        first_approval = {'user_name': 'first', 'approved_at': 42, 'counter': 9}
        second_approval = {**first_approval, 'user_name': 'second', 'counter': 10}

        def incomplete_event(key, grant, approval):
            return {'key': key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                    'grant': grant, 'approval': approval}

        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: first_approval,
                                              other_old: second_approval}))
        self.ledger.write_text(json.dumps({
            'version': 2, 'grants': {new_key: {'board': board_hash}},
            'retiring': {
                old_key: incomplete_event(old_key, first_grant, first_approval),
                other_old_key: incomplete_event(other_old_key, second_grant, second_approval),
            },
            'history': [],
        }))
        before_ledger, before_approved = self.ledger.read_bytes(), self.approved.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()),
                         (before_ledger, before_approved))

    def test_later_invalid_profile_plan_cannot_write_earlier_valid_profile(self):
        helper_board, atlas_board = 'ab' * 32, 'cd' * 32
        helper_grant = {'board': helper_board, 'counter': 7}
        atlas_grant = {'board': atlas_board, 'counter': 8}
        helper_approval = {'user_name': 'helper owner', 'approved_at': 42, 'counter': 9}
        atlas_approval = {'user_name': 'atlas owner', 'approved_at': 43, 'counter': 10}
        foreign = {'user_name': 'manual operator', 'approved_at': 99, 'counter': 12}
        helper_old, helper_new = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        atlas_old, atlas_new = f'atlas|{PRIOR_DEVICE}', f'atlas|{DEVICE}'

        def event(old_key, new_key, grant, approval):
            return {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                    'grant': grant, 'approval': approval,
                    'successor': {'key': new_key, 'grant': grant, 'approval': approval}}

        atlas_path = self.path('atlas')
        self.approved.parent.mkdir(parents=True)
        atlas_path.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: helper_approval}))
        atlas_path.write_text(json.dumps({PRIOR_DEVICE: atlas_approval, DEVICE: foreign}))
        self.ledger.write_text(json.dumps({
            'version': 2,
            'grants': {helper_new: helper_grant, atlas_new: atlas_grant},
            'retiring': {
                helper_old: event(helper_old, helper_new, helper_grant, helper_approval),
                atlas_old: event(atlas_old, atlas_new, atlas_grant, atlas_approval),
            },
            'history': [],
        }))
        before = (self.ledger.read_bytes(), self.approved.read_bytes(), atlas_path.read_bytes())

        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, helper_board, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes(), atlas_path.read_bytes()),
                         before)

    def test_sweep_preflights_all_profiles_before_denial_journal_write(self):
        helper_board, atlas_board = 'ab' * 32, 'cd' * 32
        helper_grant = {'board': helper_board, 'counter': 7}
        atlas_grant = {'board': atlas_board, 'counter': 8}
        helper_approval = {'user_name': 'helper owner', 'approved_at': 42, 'counter': 9}
        atlas_approval = {'user_name': 'atlas owner', 'approved_at': 43, 'counter': 10}
        foreign = {'user_name': 'manual operator', 'approved_at': 99, 'counter': 12}
        helper_old, helper_new = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        atlas_old, atlas_new = f'atlas|{PRIOR_DEVICE}', f'atlas|{DEVICE}'

        def event(old_key, new_key, grant, approval):
            return {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                    'grant': grant, 'approval': approval,
                    'successor': {'key': new_key, 'grant': grant, 'approval': approval}}

        atlas_path = self.path('atlas')
        self.approved.parent.mkdir(parents=True)
        atlas_path.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: helper_approval}))
        atlas_path.write_text(json.dumps({PRIOR_DEVICE: atlas_approval, DEVICE: foreign}))
        self.ledger.write_text(json.dumps({
            'version': 2,
            'grants': {helper_new: helper_grant, atlas_new: atlas_grant},
            'retiring': {
                helper_old: event(helper_old, helper_new, helper_grant, helper_approval),
                atlas_old: event(atlas_old, atlas_new, atlas_grant, atlas_approval),
            },
            'history': [],
        }))
        before = self.ledger.read_bytes(), self.approved.read_bytes(), atlas_path.read_bytes()

        with self.assertRaises(ValueError):
            self.grants.sweep(lambda _board: False)

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes(), atlas_path.read_bytes()),
                         before)

    def test_malformed_successor_intent_is_refused_without_writes(self):
        board_hash = 'ab' * 32
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        malformed = {'version': 2, 'grants': {new_key: {'board': board_hash}}, 'history': [],
                     'retiring': {old_key: {'key': old_key, 'reason': 'identity-domain-rollover',
                                            'grant': {'board': board_hash}, 'approval': {},
                                            'successor': {'key': new_key, 'grant': {'board': board_hash}}}}}
        self.ledger.write_text(json.dumps(malformed))
        before = self.ledger.read_bytes()
        with self.assertRaises(ValueError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.assertEqual(self.ledger.read_bytes(), before)

    def test_successor_binding_conflicts_are_refused_without_writes(self):
        board_hash, other_hash = 'ab' * 32, 'cd' * 32
        other_old = 'hg-0000000000000002'
        other_new = 'hg-0000000000000003'
        prior = {'board': board_hash, 'counter': 7}
        approval = {'user_name': 'owned', 'approved_at': 42}

        def event(old_key, new_key, old_grant=prior, new_grant=prior):
            return {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                    'grant': old_grant, 'approval': approval,
                    'successor': {'key': new_key, 'grant': new_grant, 'approval': approval}}

        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        cases = {
            'self-successor': {'version': 2, 'grants': {old_key: prior},
                               'retiring': {old_key: event(old_key, old_key)}, 'history': []},
            'different-board-successor': {'version': 2, 'grants': {new_key: {'board': other_hash}},
                                          'retiring': {old_key: event(
                                              old_key, new_key, prior, {'board': other_hash})}, 'history': []},
            'active-conflicts-with-intent': {'version': 2, 'grants': {new_key: {'board': other_hash}},
                                             'retiring': {old_key: event(old_key, new_key)}, 'history': []},
            'ambiguous-retirements': {
                'version': 2, 'grants': {new_key: prior, f'helper|{other_new}': prior}, 'history': [],
                'retiring': {old_key: event(old_key, new_key),
                             f'helper|{other_old}': event(f'helper|{other_old}', f'helper|{other_new}')},
            },
        }
        for name, value in cases.items():
            with self.subTest(name=name):
                self.ledger.write_text(json.dumps(value))
                before = self.ledger.read_bytes()
                with self.assertRaises(ValueError):
                    self.grants.grant('helper', DEVICE, board_hash, 'Desk')
                self.assertEqual(self.ledger.read_bytes(), before)

    def test_recovery_preserves_manual_replacement_of_old_approval(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7}
        prior_approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9}
        manual = {'user_name': 'manual replacement', 'approved_at': 99, 'extension': 'keep'}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
        write = gadget_front._write_private

        def interrupt(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic interruption')
            return write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt), self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        self.approved.write_text(json.dumps({PRIOR_DEVICE: manual}))
        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        approvals = json.loads(self.approved.read_text())
        self.assertEqual(approvals[PRIOR_DEVICE], manual)
        self.assertEqual(approvals[DEVICE], prior_approval)

    def test_rollover_recovers_at_every_atomic_write_boundary(self):
        board_hash = 'ab' * 32
        prior_grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 9}
        prior_approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9, 'extension': 'keep'}
        real_write = gadget_front._write_private
        for target in (1, 2, 3):
            for timing in ('before', 'after'):
                with self.subTest(write=target, timing=timing), tempfile.TemporaryDirectory() as tmp:
                    root = pathlib.Path(tmp)
                    ledger = root / 'gadget-grants.json'
                    approved = root / 'profiles' / 'helper' / 'platforms' / 'pairing' / 'gadget-approved.json'
                    approved.parent.mkdir(parents=True)
                    approved.write_text(json.dumps({PRIOR_DEVICE: prior_approval}))
                    ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': prior_grant}))
                    grants = gadget_front.Grants(
                        lambda _profile: approved, ledger, clock=lambda: 1000.0)
                    calls = 0

                    def interrupt(path, value):
                        nonlocal calls
                        calls += 1
                        if calls == target and timing == 'before':
                            raise OSError('synthetic before-write interruption')
                        result = real_write(path, value)
                        if calls == target and timing == 'after':
                            raise OSError('synthetic after-write interruption')
                        return result

                    with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt), \
                            self.assertRaises(OSError):
                        grants.grant('helper', DEVICE, board_hash, 'Desk')
                    grants.grant('helper', DEVICE, board_hash, 'Desk')
                    recovered = json.loads(ledger.read_text())
                    self.assertEqual(recovered['retiring'], {})
                    self.assertEqual(recovered['grants'][f'helper|{DEVICE}'], prior_grant)
                    self.assertEqual(json.loads(approved.read_text())[DEVICE], prior_approval)

    def _assert_role_conflict_refused_without_writes(self, conflict, operation):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            ledger = root / 'gadget-grants.json'

            def profile_path(profile):
                return root / 'profiles' / profile / 'platforms' / 'pairing' / 'gadget-approved.json'

            helper_old = 'hg-0000000000000011'
            helper_new = 'hg-0000000000000012'
            atlas_old = 'hg-0000000000000021'
            atlas_new = 'hg-0000000000000022'
            helper_key, helper_successor = f'helper|{helper_old}', f'helper|{helper_new}'
            atlas_key, atlas_successor = f'atlas|{atlas_old}', f'atlas|{atlas_new}'
            helper_grant = {'board': 'ab' * 32, 'counter': 7, 'denied_attempts': 3,
                            'grant_extension': {'keep': [1, True, None]}}
            atlas_grant = {'board': 'cd' * 32, 'counter': 17, 'denied_attempts': 5,
                           'grant_extension': {'keep': 'atlas'}}
            helper_approval = {'user_name': 'helper owner', 'approved_at': 42, 'counter': 9,
                               'approval_extension': {'keep': [2, False, None]}}
            atlas_approval = {'user_name': 'atlas owner', 'approved_at': 52, 'counter': 19,
                              'approval_extension': {'keep': 'atlas'}}

            def rollover(key, successor, grant, approval):
                return {'key': key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                        'grant': grant, 'approval': approval, 'event_extension': {'keep': key},
                        'successor': {'key': successor, 'grant': grant, 'approval': approval}}

            grants = {helper_successor: helper_grant, atlas_successor: atlas_grant}
            retiring = {
                helper_key: rollover(helper_key, helper_successor, helper_grant, helper_approval),
                atlas_key: rollover(atlas_key, atlas_successor, atlas_grant, atlas_approval),
            }
            if conflict == 'active-successor-revoked':
                retiring[atlas_successor] = {
                    'key': atlas_successor, 'reason': 'authorization-revoked', 'at': 1001.0,
                    'grant': atlas_grant, 'approval': atlas_approval,
                    'event_extension': {'keep': 'revocation'},
                }
            elif conflict == 'active-predecessor-retiring':
                grants[atlas_key] = atlas_grant
            else:  # pragma: no cover - test helper contract
                raise AssertionError(f'unknown conflict: {conflict}')
            ledger.write_text(json.dumps({
                'version': 2, 'grants': grants, 'retiring': retiring,
                'history': [{'history_extension': {'keep': True}}],
                'root_extension': {'keep': [3, 2, 1]},
            }))
            for profile, old, approval in (
                    ('helper', helper_old, helper_approval), ('atlas', atlas_old, atlas_approval)):
                path = profile_path(profile)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps({
                    old: approval,
                    'hg-00000000000000f0': {'user_name': f'{profile} manual', 'approved_at': 1,
                                            'manual_extension': {'keep': profile}},
                }))
            coding = profile_path('coding')
            coding.parent.mkdir(parents=True, exist_ok=True)
            coding.write_text(json.dumps({
                'hg-00000000000000f1': {'user_name': 'coding manual', 'approved_at': 2,
                                        'manual_extension': {'keep': 'coding'}},
            }))
            paths = (ledger, profile_path('helper'), profile_path('atlas'), coding)
            before = tuple(path.read_bytes() for path in paths)
            actor = gadget_front.Grants(profile_path, ledger, clock=lambda: 1002.0)
            caught = None
            try:
                if operation == 'grant':
                    actor.grant('helper', helper_new, helper_grant['board'], 'Desk')
                elif operation == 'sweep-allowed':
                    actor.sweep(lambda _board: True)
                else:
                    actor.sweep(lambda _board: False)
            except Exception as exc:  # exact type is part of the regression contract
                caught = exc
            after = tuple(path.read_bytes() for path in paths)
            self.assertEqual((type(caught), after), (ValueError, before))

    def test_active_successor_also_authorization_revoked_is_refused_without_any_writes(self):
        for operation in ('grant', 'sweep-allowed', 'sweep-revoked'):
            with self.subTest(operation=operation):
                self._assert_role_conflict_refused_without_writes(
                    'active-successor-revoked', operation)

    def test_active_predecessor_also_rollover_retiring_is_refused_without_any_writes(self):
        for operation in ('grant', 'sweep-allowed', 'sweep-revoked'):
            with self.subTest(operation=operation):
                self._assert_role_conflict_refused_without_writes(
                    'active-predecessor-retiring', operation)

    def test_valid_interrupted_rollover_recovery_is_idempotent_with_unknown_metadata(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3,
                 'grant_extension': {'keep': True}}
        approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9,
                    'approval_extension': {'keep': [1, None]}}
        manual_device = 'hg-0000000000000001'
        manual = {'user_name': 'manual', 'approved_at': 1, 'manual_extension': {'keep': True}}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        event = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                 'grant': grant, 'approval': approval, 'event_extension': {'keep': True},
                 'successor': {'key': new_key, 'grant': grant, 'approval': approval}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: approval, manual_device: manual}))
        self.ledger.write_text(json.dumps({
            'version': 2, 'grants': {new_key: grant}, 'retiring': {old_key: event},
            'history': [], 'root_extension': {'keep': True},
        }))

        self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        first = self.ledger.read_bytes(), self.approved.read_bytes()
        self.grants.grant('helper', DEVICE, board_hash, 'Desk')

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), first)
        recovered = json.loads(first[0])
        approvals = json.loads(first[1])
        self.assertEqual(recovered['grants'][new_key], grant)
        self.assertEqual(recovered['root_extension'], {'keep': True})
        self.assertEqual(recovered['history'][0], event)
        self.assertEqual(approvals, {DEVICE: approval, manual_device: manual})

    def test_valid_denied_successor_revocation_recovers_without_regrant(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3,
                 'grant_extension': {'keep': True}}
        approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9,
                    'approval_extension': {'keep': True}}
        manual_device = 'hg-0000000000000001'
        manual = {'user_name': 'manual', 'approved_at': 1, 'manual_extension': {'keep': True}}
        old_key, new_key = f'helper|{PRIOR_DEVICE}', f'helper|{DEVICE}'
        successor = {'key': new_key, 'grant': grant, 'approval': approval}
        rollover = {'key': old_key, 'reason': 'identity-domain-rollover', 'at': 1000.0,
                    'grant': grant, 'approval': approval, 'successor': successor,
                    'successor_denied': True, 'event_extension': {'keep': 'rollover'}}
        revocation = {'key': new_key, 'reason': 'authorization-revoked', 'at': 1001.0,
                      'grant': grant, 'approval': approval,
                      'event_extension': {'keep': 'revocation'}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: approval, DEVICE: approval,
                                             manual_device: manual}))
        self.ledger.write_text(json.dumps({
            'version': 2, 'grants': {}, 'retiring': {old_key: rollover, new_key: revocation},
            'history': [], 'root_extension': {'keep': True},
        }))

        self.grants.sweep(lambda _board: True)
        first = self.ledger.read_bytes(), self.approved.read_bytes()
        self.grants.sweep(lambda _board: True)

        self.assertEqual((self.ledger.read_bytes(), self.approved.read_bytes()), first)
        recovered = json.loads(first[0])
        self.assertEqual(recovered['grants'], {})
        self.assertEqual(recovered['retiring'], {})
        self.assertEqual(recovered['history'], [revocation, rollover])
        self.assertEqual(recovered['root_extension'], {'keep': True})
        self.assertEqual(json.loads(first[1]), {manual_device: manual})

    def test_revoke_then_recovery_never_regrants_successor_with_unknown_metadata(self):
        board_hash = 'ab' * 32
        grant = {'board': board_hash, 'counter': 7, 'denied_attempts': 3,
                 'grant_extension': {'keep': True}}
        approval = {'user_name': 'owned', 'approved_at': 42, 'counter': 9,
                    'approval_extension': {'keep': True}}
        manual_device = 'hg-0000000000000001'
        manual = {'user_name': 'manual', 'approved_at': 1, 'manual_extension': {'keep': True}}
        self.approved.parent.mkdir(parents=True)
        self.approved.write_text(json.dumps({PRIOR_DEVICE: approval, manual_device: manual}))
        self.ledger.write_text(json.dumps({f'helper|{PRIOR_DEVICE}': grant}))
        real_write = gadget_front._write_private

        def interrupt_sdk(path, value):
            if pathlib.Path(path) == self.approved:
                raise OSError('synthetic SDK interruption')
            return real_write(path, value)

        with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt_sdk), \
                self.assertRaises(OSError):
            self.grants.grant('helper', DEVICE, board_hash, 'Desk')
        with mock.patch.object(gadget_front, '_write_private', side_effect=interrupt_sdk), \
                self.assertRaises(OSError):
            self.grants.sweep(lambda _board: False)

        gadget_front.Grants(self.path, self.ledger, clock=lambda: 1002.0).sweep(lambda _board: True)
        recovered = json.loads(self.ledger.read_text())
        self.assertEqual(recovered['grants'], {})
        self.assertEqual(recovered['retiring'], {})
        self.assertEqual(json.loads(self.approved.read_text()), {manual_device: manual})
        self.assertEqual(recovered['history'][0]['grant'], grant)
        self.assertEqual(recovered['history'][0]['approval'], approval)


class HeadTests(unittest.TestCase):
    def test_rewrite_drops_credentials_and_targets_the_gateway(self):
        req = gadget_front.parse_head(head())
        self.assertEqual(req.bot, 'helper')
        self.assertEqual(req.authorization, BEARER)
        out = req.upstream_head('127.0.0.1', 8775).decode()
        self.assertNotIn('Authorization', out)
        self.assertNotIn(TOKEN.hex(), out)
        self.assertTrue(out.startswith('GET /gadget HTTP/1.1\r\nHost: 127.0.0.1:8775\r\n'))
        self.assertIn('Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n', out)
        self.assertIn('Sec-WebSocket-Protocol: hermes-gadget.v1\r\n', out)
        self.assertTrue(out.endswith('\r\n\r\n'))

    def test_rejects_malformed_requests(self):
        for raw in (b'POST /gadget/helper HTTP/1.1\r\n\r\n', b'GET /gadget/helper?x=1 HTTP/1.1\r\n\r\n',
                    b'GET /gadget/helper\r\n\r\n', b'GET /gadget HTTP/1.1\r\n\r\n', b'GET /gadget/../x HTTP/1.1\r\n\r\n',
                    head(extra='Bad header line\r\n'), head(extra='Authorization: Bearer x\r\n')):
            with self.subTest(raw=raw[:40]), self.assertRaises(ValueError):
                gadget_front.parse_head(raw)




class FrontTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.tmp.name)
        cert, key = make_cert(root)
        boards = root / 'boards.json'
        enroll.write_private_json(boards, {'boards': [{'id': 'b1', 'name': 'Desk', 'added': 1,
                                                        'token_sha256': hashlib.sha256(TOKEN).hexdigest()}]})
        self.registry = enroll.Registry(boards)
        self.registry.set_phone('b1', 'sam', 'Sam', groups=['admins'])
        self.phone = phone_pair.PhonePairing.from_specs(self.registry, None, [
            {'provider': 'hermes', 'issuer_base': 'https://auth.example.com', 'groups': ['admins']}])
        self.heads = []

        async def upstream(reader, writer):
            self.heads.append(await reader.readuntil(b'\r\n\r\n'))
            writer.write(b'HTTP/1.1 101 Switching Protocols\r\n\r\n')
            while data := await reader.read(100):
                writer.write(data.upper())
                await writer.drain()
            writer.close()

        self.up = await asyncio.start_server(upstream, '127.0.0.1', 0)
        self.approved = root / 'helper' / 'gadget-approved.json'
        settings = {'port': 0, 'upstream_host': '127.0.0.1',
                    'profiles': {'helper': self.up.sockets[0].getsockname()[1], 'atlas': 1}}
        self.front = await gadget_front.start(settings, bind='127.0.0.1', cert=str(cert), key=str(key),
                                              authorizer=enroll.Authorizer(self.registry), phone=self.phone,
                                              grants=gadget_front.Grants(lambda p: root / p / 'gadget-approved.json',
                                                                         root / 'grants.json'),
                                              sweep_s=0.05)
        self.client = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        self.client.check_hostname, self.client.verify_mode = False, ssl.CERT_NONE

    async def asyncTearDown(self):
        await self.front.close()
        self.up.close()
        await self.up.wait_closed()
        self.tmp.cleanup()

    async def connect(self, raw):
        reader, writer = await asyncio.open_connection('127.0.0.1', self.front.port, ssl=self.client)
        writer.write(raw)
        await writer.drain()
        return reader, writer

    async def test_missing_phone_provider_fails_closed_before_sdk_approval(self):
        self.front.phone = None
        reader, writer = await self.connect(head())
        try:
            self.assertTrue((await asyncio.wait_for(reader.read(200), 5)).startswith(b'HTTP/1.1 403'))
            self.assertEqual(self.heads, [])
            self.assertFalse(self.approved.exists())
        finally:
            writer.close()

    async def test_signed_in_board_is_approved_and_piped_without_its_token(self):
        reader, writer = await self.connect(head())
        status = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 5)
        self.assertTrue(status.startswith(b'HTTP/1.1 101'))
        writer.write(b'hello')
        await writer.drain()
        self.assertEqual(await asyncio.wait_for(reader.readexactly(5), 5), b'HELLO')
        self.assertNotIn(b'Authorization', self.heads[0])
        self.assertNotIn(TOKEN.hex().encode(), self.heads[0])
        self.assertIn(DEVICE, json.loads(self.approved.read_text()))
        writer.close()

    async def test_unknown_bot_is_refused_before_any_approval(self):
        reader, writer = await self.connect(head(path='/gadget/coding'))
        self.assertTrue((await asyncio.wait_for(reader.read(200), 5)).startswith(b'HTTP/1.1 404'))
        self.assertEqual(self.heads, [])
        self.assertFalse(self.approved.exists())
        writer.close()

    async def test_unknown_board_never_reaches_the_gateway(self):
        reader, writer = await self.connect(head(bearer='Bearer ' + 'ff' * 32))
        self.assertTrue((await asyncio.wait_for(reader.read(200), 5)).startswith(b'HTTP/1.1 401'))
        self.assertEqual(self.heads, [])
        self.assertFalse(self.approved.exists())
        writer.close()

    async def test_signed_out_board_is_refused_and_its_grant_revoked(self):
        _, writer = await self.connect(head())
        for _ in range(100):
            if self.approved.exists():
                break
            await asyncio.sleep(0.02)
        writer.close()
        self.registry.clear_phone('b1')
        reader, writer = await self.connect(head())
        self.assertTrue((await asyncio.wait_for(reader.read(200), 5)).startswith(b'HTTP/1.1 403'))
        writer.close()
        for _ in range(100):
            if DEVICE not in json.loads(self.approved.read_text()):
                break
            await asyncio.sleep(0.02)
        self.assertNotIn(DEVICE, json.loads(self.approved.read_text()))


if __name__ == '__main__':
    unittest.main()
