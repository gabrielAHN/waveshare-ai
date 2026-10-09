"""Per-provider phone sign-in (SPEC Contract B): X-Provider on the phone routes, one device flow per
gateway shared by the providers that use it, per-provider groups, WPH1 flags 1 and 8, the registry
layout (flat fields for the Hermes gateway, ``sign_ins`` for any other) and the route gates.

Runs the real HTTP code against the scripted fake identity provider of test_phone_pair (loopback only).
"""
import asyncio
import json
import pathlib
import tempfile
import time
import unittest

import aiohttp
from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import enroll
from waveshare_bridge import live_bridge as bridge
from waveshare_bridge import phone_pair
from tests.test_home_sensors import FakeHome
from tests.test_phone_pair import APPROVED, PENDING, PUBLIC_HOST, TOKEN_A, TOKEN_B, FakeAuthelia, bearer
from tests.test_transport import ROOT, serve
from tests.support import live_sample

HERMES, HOME = {'X-Provider': 'hermes'}, {'X-Provider': 'home_assistant'}


class ProviderBase(unittest.IsolatedAsyncioTestCase):
    home_client_id = 'waveshare-pairing'     # same gateway as Hermes unless a test class changes it
    providers = ('hermes', 'home_assistant')

    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.registry = enroll.Registry(pathlib.Path(self.tmp.name) / 'boards.json')
        self.board_a = self.registry.add(TOKEN_A, 'Desk')
        self.board_b = self.registry.add(TOKEN_B, 'Kitchen')
        self.idp = FakeAuthelia()
        self.idp_runner, self.idp_base = await serve(self.idp.app())
        self.http = aiohttp.ClientSession()
        common = {'issuer_base': self.idp_base, 'host_header': PUBLIC_HOST, 'public_host': PUBLIC_HOST,
                  'ca_file': '', 'paths': None}
        specs = []
        if 'hermes' in self.providers:
            specs.append(dict(common, provider='hermes', client_id='waveshare-pairing',
                              groups=['admins', 'hermes_users'], require=True))
        if 'home_assistant' in self.providers:
            specs.append(dict(common, provider='home_assistant', client_id=self.home_client_id,
                              groups=['admins'], require=True))
        self.phone = phone_pair.PhonePairing.from_specs(self.registry, self.http, specs, time_scale=0.002,
                                                        log=lambda *a, **k: None)
        self.home = FakeHome()
        self.client = TestClient(TestServer(bridge.make_app(
            live_sample(), enroll.Authorizer(self.registry), phone=self.phone, home=self.home)))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        await self.phone.close()
        await self.http.close()
        await self.idp_runner.cleanup()
        self.tmp.cleanup()

    async def call(self, method, route, headers=HERMES, token=TOKEN_A):
        response = await self.client.request(method, '/v1/pair/phone/' + route, headers={'Authorization': 'Bearer ' + token.hex(), **headers})
        return response.status, (await response.read())

    async def status(self, headers=HERMES, token=TOKEN_A):
        status, body = await self.call('GET', 'status', headers, token)
        self.assertEqual(status, 200)
        return phone_pair.decode_status(body)

    async def wait_state(self, want, headers=HERMES, seconds=3.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            got = await self.status(headers)
            if got['state'] == want:
                return got
            await asyncio.sleep(0.01)
        self.fail(f'state never became {want}: {got}')

    def hermes_allowed(self, token=TOKEN_A):
        return self.phone.allowed(self.registry.match(token))

    async def home_status(self, token=TOKEN_A):
        response = await self.client.get('/v1/home', headers=bearer(token))
        return response.status, await response.text() if response.status != 200 else ''


class SharedGatewayTests(ProviderBase):
    async def test_one_qr_signs_into_both_and_each_checks_its_groups(self):
        self.idp.script = [PENDING, APPROVED]                      # user 'samlee' in admins
        status, body = await self.call('POST', 'start', HOME)
        self.assertEqual(status, 200)
        got = phone_pair.decode_status(body)
        self.assertEqual(got['state'], 'pending')
        self.assertEqual(got['flags'], phone_pair.FLAG_REQUIRED | phone_pair.FLAG_SHARED)
        hermes = await self.status(HERMES)                          # the same flow, the same QR
        self.assertEqual((hermes['state'], hermes['user_code']), ('pending', got['user_code']))
        self.assertTrue(hermes['flags'] & phone_pair.FLAG_SHARED)
        await self.wait_state('authorized', HOME)
        self.assertEqual((await self.status(HERMES))['state'], 'authorized')
        self.assertEqual([r[0] for r in self.idp.requests].count('device'), 1)
        # The Hermes gateway keeps the flat registry fields (Hermes approval).
        raw = json.loads(pathlib.Path(self.registry.path).read_text())['boards'][0]
        self.assertEqual((raw['phone_user'], raw['phone_groups']), ('samlee', ['admins']))
        self.assertNotIn('sign_ins', raw)
        self.assertEqual(self.hermes_allowed(), True)
        self.assertEqual(await self.home_status(), (200, ''))
        # Signing out from either provider signs out both.
        status, body = await self.call('POST', 'forget', HOME)
        self.assertEqual((status, phone_pair.decode_status(body)['state']), (200, 'none'))
        self.assertEqual((await self.status(HERMES))['state'], 'none')
        self.assertEqual(self.hermes_allowed(), False)
        self.assertEqual(await self.home_status(), (409, 'home_auth'))

    async def test_user_outside_home_groups_is_refused_there_only(self):
        self.idp.userinfo = {'preferred_username': 'samlee', 'name': 'Sam Lee', 'groups': ['hermes_users']}
        self.idp.script = [APPROVED]
        await self.call('POST', 'start', HERMES)
        got = await self.wait_state('authorized', HERMES)
        self.assertEqual(got['name'], 'Sam Lee')
        home = await self.status(HOME)
        self.assertEqual((home['state'], home['name']), ('refused', ''))
        self.assertEqual(self.hermes_allowed(), True)
        self.assertEqual(await self.home_status(), (409, 'home_auth'))
        self.assertEqual(self.home.calls, 0)

    async def test_home_only_user_signs_into_home_assistant_and_is_refused_for_hermes(self):
        self.phone.providers['hermes'].groups = frozenset({'hermes_users'})
        self.idp.script = [APPROVED]                               # groups ['admins']: Home Assistant only
        await self.call('POST', 'start', HOME)
        await self.wait_state('authorized', HOME)
        self.assertEqual((await self.status(HERMES))['state'], 'refused')
        self.assertEqual(self.hermes_allowed(), False)
        self.assertEqual(await self.home_status(), (200, ''))
        # A refused provider may start a new flow (switch user); the authorized one is not disturbed.
        self.phone.min_restart = 0
        status, body = await self.call('POST', 'start', HERMES)
        self.assertEqual((status, phone_pair.decode_status(body)['state']), (200, 'pending'))
        self.assertEqual((await self.status(HOME))['state'], 'authorized')

    async def test_user_in_no_provider_group_is_refused_and_not_stored(self):
        self.idp.userinfo = {'preferred_username': 'guest', 'name': 'Guest', 'groups': ['family']}
        self.idp.script = [APPROVED]
        await self.call('POST', 'start', HOME)
        await self.wait_state('refused', HOME)
        self.assertEqual((await self.status(HERMES))['state'], 'refused')
        self.assertIsNone(self.registry.match(TOKEN_A).get('phone_user'))

    async def test_existing_flat_sign_in_keeps_working_without_a_rescan(self):
        # Persisted Hermes sign-in uses the flat fields and recorded groups.
        self.registry.set_phone(self.board_a['id'], 'samlee', 'Sam Lee', groups=['admins', 'hermes_users'])
        self.assertEqual((await self.status())['state'], 'authorized')
        self.assertEqual((await self.status(HOME))['state'], 'authorized')
        self.assertEqual(self.hermes_allowed(), True)
        self.assertEqual(await self.home_status(), (200, ''))
        # Missing groups cannot satisfy provider authorization.
        self.registry.clear_phone(self.board_a['id'])
        self.registry._update(self.board_a['id'], lambda b: b.update({'phone_user': 'samlee', 'phone_at': 1}))
        self.assertEqual((await self.status())['state'], 'refused')
        self.assertEqual(self.hermes_allowed(), False)
        self.assertEqual(await self.home_status(), (409, 'home_auth'))


class SeparateGatewayTests(ProviderBase):
    home_client_id = 'home-pairing'          # another phone client: another gateway, another QR

    async def test_home_assistant_sign_in_is_its_own_and_stored_under_sign_ins(self):
        self.idp.script = [APPROVED]
        status, body = await self.call('POST', 'start', HOME)
        got = phone_pair.decode_status(body)
        self.assertEqual((status, got['state'], got['flags']), (200, 'pending', phone_pair.FLAG_REQUIRED))
        self.assertEqual((await self.status(HERMES))['state'], 'none')    # not shared: no pending QR there
        await self.wait_state('authorized', HOME)
        device_forms = [r[1] for r in self.idp.requests if r[0] == 'device']
        self.assertEqual([f['client_id'] for f in device_forms], ['home-pairing'])
        board = self.registry.match(TOKEN_A)
        key = self.phone.providers['home_assistant'].gateway.key
        self.assertNotIn('phone_user', board)
        self.assertEqual(board['sign_ins'][key]['user'], 'samlee')
        self.assertEqual(board['sign_ins'][key]['groups'], ['admins'])
        self.assertEqual(await self.home_status(), (200, ''))
        self.assertEqual(self.hermes_allowed(), False)                  # Hermes still needs its own sign-in
        # Signing in to Hermes too, then out of Home Assistant leaves Hermes alone.
        self.idp.script = [APPROVED]
        await self.call('POST', 'start', HERMES)
        await self.wait_state('authorized', HERMES)
        self.assertEqual(self.hermes_allowed(), True)
        status, body = await self.call('POST', 'forget', HOME)
        self.assertEqual(phone_pair.decode_status(body)['state'], 'none')
        board = self.registry.match(TOKEN_A)
        self.assertNotIn('sign_ins', board)
        self.assertEqual(board['phone_user'], 'samlee')
        self.assertEqual(self.hermes_allowed(), True)
        self.assertEqual(await self.home_status(), (409, 'home_auth'))

    async def test_flows_at_two_gateways_run_side_by_side(self):
        await self.call('POST', 'start', HOME)
        await self.call('POST', 'start', HERMES)
        home, hermes = await self.status(HOME), await self.status(HERMES)
        self.assertEqual((home['state'], hermes['state']), ('pending', 'pending'))
        self.assertNotEqual(home['user_code'], hermes['user_code'])


class RouteTests(ProviderBase):
    providers = ('hermes',)

    async def test_unknown_or_unconfigured_provider_is_404_before_anything_else(self):
        for headers in ({}, {'X-Provider': 'home_assistant'}, {'X-Provider': 'matter'}, {'X-Provider': 'Hermes'},
                        {'X-Provider': ''}):
            for method, route in (('POST', 'start'), ('GET', 'status'), ('POST', 'forget')):
                with self.subTest(headers=headers, route=route):
                    self.assertEqual((await self.call(method, route, headers))[0], 404)
                    self.assertEqual((await self.call(method, route, headers, token=bytes(32)))[0], 404)
        response = await self.client.get('/v1/pair/phone/status',
                                         headers=[('Authorization', 'Bearer ' + TOKEN_A.hex()),
                                                  ('X-Provider', 'hermes'), ('X-Provider', 'hermes')])
        self.assertEqual(response.status, 404)
        self.assertEqual(self.idp.requests, [])
        got = await self.status(HERMES)           # alone on its gateway: never flag 8
        self.assertEqual(got['flags'], phone_pair.FLAG_REQUIRED)
        self.assertEqual((await self.status())['flags'], phone_pair.FLAG_REQUIRED)

    async def test_home_route_refuses_without_a_home_assistant_provider(self):
        self.registry.set_phone(self.board_a['id'], 'samlee', 'Sam Lee', groups=['admins'])
        self.assertEqual(await self.home_status(), (409, 'home_auth'))
        self.assertEqual((await self.status())['flags'], phone_pair.FLAG_REQUIRED)

    async def test_sign_in_follows_the_providers_not_the_tiles(self):
        client = TestClient(TestServer(bridge.make_app(live_sample(), enroll.Authorizer(self.registry),
                                                       phone=self.phone, plugins=['sparkles'])))
        await client.start_server()
        try:
            response = await client.get('/v1/pair/phone/status', headers={**bearer(TOKEN_A), **HERMES})
            self.assertEqual(response.status, 200)
            response = await client.get('/v1/pair/phone/status', headers={**bearer(TOKEN_A), **HOME})
            self.assertEqual(response.status, 404)
            response = await client.get('/v1/bots', headers=bearer(TOKEN_A))
            self.assertEqual(response.status, 404)
        finally:
            await client.close()


class HomeOnlyTests(ProviderBase):
    providers = ('home_assistant',)

    async def test_home_assistant_alone_uses_sign_ins_and_no_hermes(self):
        self.assertEqual((await self.call('GET', 'status'))[0], 404)       # no header = hermes: not served
        self.idp.script = [APPROVED]
        await self.call('POST', 'start', HOME)
        await self.wait_state('authorized', HOME)
        board = self.registry.match(TOKEN_A)
        self.assertNotIn('phone_user', board)
        self.assertEqual(len(board['sign_ins']), 1)
        self.assertEqual(await self.home_status(), (200, ''))
        self.assertFalse(self.phone.allowed(board))


class RegistryTests(unittest.TestCase):
    def test_sign_ins_validate_persist_and_clear(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            path = pathlib.Path(tmp) / 'boards.json'
            reg = enroll.Registry(path)
            entry = reg.add(TOKEN_A, 'Desk')
            reg.set_phone(entry['id'], 'samlee', 'Sam Lee', groups=['admins'])
            self.assertTrue(reg.set_sign_in(entry['id'], 'ab' * 8, 'alex', 'Alex\nX', now=5, groups=['admins', 'bad group']))
            again = enroll.Registry(path).match(TOKEN_A)
            self.assertEqual(again['sign_ins'], {'ab' * 8: {'user': 'alex', 'name': 'AlexX', 'at': 5, 'groups': ['admins']}})
            self.assertEqual(again['phone_user'], 'samlee')
            with self.assertRaises(ValueError):
                reg.set_sign_in(entry['id'], 'not-a-key', 'alex', 'Alex')
            raw = json.loads(path.read_text())
            raw['boards'][0]['sign_ins']['zz'] = {'user': 'x'}            # junk is dropped on load
            raw['boards'][0]['sign_ins']['cd' * 8] = {'name': 'no user'}
            enroll.write_private_json(path, raw)
            self.assertEqual(list(enroll.Registry(path).match(TOKEN_A)['sign_ins']), ['ab' * 8])
            self.assertTrue(reg.clear_sign_in(entry['id'], 'ab' * 8))
            again = enroll.Registry(path).match(TOKEN_A)
            self.assertNotIn('sign_ins', again)
            self.assertEqual(again['phone_user'], 'samlee')
            reg.set_sign_in(entry['id'], 'ab' * 8, 'alex', 'Alex')
            self.assertTrue(reg.sign_out(entry['id']))                       # `boards --revoke-phone`
            again = enroll.Registry(path).match(TOKEN_A)
            self.assertFalse({'phone_user', 'sign_ins', 'phone_groups'} & set(again))


if __name__ == '__main__':
    unittest.main()
