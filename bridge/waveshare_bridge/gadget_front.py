"""LAN front for the Hermes gateway's stock Gadget SDK platform.

The Hermes Gadget SDK (github.com/Adolanium/hermes-gadget-sdk) runs unmodified inside the Hermes
messaging gateway as the ``gadget`` platform of each bot profile (helper, atlas, coding), each on its
own loopback port. Boards on the LAN reach the selected bot through this front:

    board --wss://<bind>:<port>/gadget/<bot> (bridge cert, pinned by the board; Authorization: Bearer <board token hex>)
      -> front: the board must be enrolled AND signed in with the phone QR (PhonePairing.allowed)
      -> the bot must be a configured profile (404 otherwise); the board's SDK device id (derived
         from its token, as the firmware does) is approved in that profile's gadget pairing store,
         so the device QR login is the only sign-in
      -> the upgrade request is forwarded WITHOUT the Authorization header to the gateway's loopback
         listener, then bytes are piped both ways; the SDK protocol (hello/challenge/auth, audio,
         replies) is spoken end to end between the board and the stock adapter.

A sweep revokes every approval this front made whose board was removed or signed out; the stock
adapter mirrors the revocation onto a connected device (``unpaired``) and Hermes refuses its turns.
Approvals made by anyone else (``hermes pairing approve``) are never touched.
"""
import asyncio
import hashlib
import hmac
import json
import math
import os
import pathlib
import re
import secrets
import stat
import threading
import time

from .bots import valid_bot

KEY_CONTEXT = b'waveshare-ai-gadget-key-v1'   # firmware gadget_wire.h gw_identity_from_token
HEAD_MAX = 8192
HEAD_TIMEOUT_S = 10.0
MAX_CONNECTIONS = 8
PATH = '/gadget'


def device_id_for_token(token):
    key = hmac.new(token, KEY_CONTEXT, hashlib.sha256).digest()
    return 'hg-' + hashlib.sha256(key).hexdigest()[:16]


def _read_json_object(path):
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return {}
    if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode):
        raise ValueError(f'{path.name} must be a regular file')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ValueError(f'{path.name} is unreadable; not modifying it') from exc
    if not isinstance(data, dict):
        raise ValueError(f'{path.name} must hold a JSON object; not modifying it')
    return data


def _write_private(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f'.{path.name}.{secrets.token_hex(4)}.tmp')
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    try:
        with os.fdopen(fd, 'w') as handle:
            json.dump(value, handle, indent=2, sort_keys=True)
        os.replace(tmp, path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


LEDGER_VERSION = 2
_DEVICE_ID = re.compile(r'hg-[0-9a-f]{16}')
_BOARD_HASH = re.compile(r'[0-9a-f]{64}')


def _grant_key(key):
    profile, sep, device_id = key.partition('|') if isinstance(key, str) else ('', '', '')
    if not sep or not valid_bot(profile) or not _DEVICE_ID.fullmatch(device_id):
        raise ValueError('gadget grant ledger has an invalid grant key; not modifying it')
    return profile, device_id


def _grant_record(record):
    if not isinstance(record, dict) or not isinstance(record.get('board'), str) \
            or not _BOARD_HASH.fullmatch(record['board']):
        raise ValueError('gadget grant ledger has an invalid grant record; not modifying it')
    return record


def _json_equal(left, right):
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(_json_equal(left[key], right[key]) for key in left)
    if isinstance(left, list):
        return len(left) == len(right) and all(_json_equal(a, b) for a, b in zip(left, right))
    return left == right


def _ledger_state(path):
    """Read the original flat ledger or the versioned rollover/audit ledger without losing extensions."""
    raw = _read_json_object(path)
    if 'version' in raw:
        if type(raw['version']) is not int or raw['version'] != LEDGER_VERSION:
            raise ValueError('gadget grant ledger has an unsupported version; not modifying it')
        grants, retiring, history = raw.get('grants'), raw.get('retiring'), raw.get('history')
        if not isinstance(grants, dict) or not isinstance(retiring, dict) or not isinstance(history, list):
            raise ValueError('gadget grant ledger version 2 is malformed; not modifying it')
        extra = {k: v for k, v in raw.items() if k not in ('version', 'grants', 'retiring', 'history')}
    else:
        grants, retiring, history, extra = raw, {}, [], {}
    for key, record in grants.items():
        _grant_key(key)
        _grant_record(record)
    if set(grants).intersection(retiring):
        raise ValueError('gadget identity has conflicting active and retiring roles; not modifying it')
    successor_keys = set()
    rollover_owners = set()
    for key, event in retiring.items():
        old_profile, old_device = _grant_key(key)
        if not isinstance(event, dict) or event.get('key') != key or event.get('reason') not in (
                'identity-domain-rollover', 'authorization-revoked') or not isinstance(event.get('grant'), dict) \
                or 'approval' not in event:
            raise ValueError('gadget grant ledger has invalid retirement evidence; not modifying it')
        _grant_record(event['grant'])
        successor = event.get('successor')
        denied = event.get('successor_denied', False)
        if type(denied) is not bool or (denied and successor is None):
            raise ValueError('gadget grant ledger has invalid successor denial; not modifying it')
        if successor is None and event['reason'] == 'identity-domain-rollover':
            raise ValueError('gadget rollover has no explicit successor; not modifying it')
        if successor is not None:
            if (event['reason'] != 'identity-domain-rollover' or not isinstance(successor, dict)
                    or set(successor) != {'key', 'grant', 'approval'}):
                raise ValueError('gadget grant ledger has invalid successor intent; not modifying it')
            successor_profile, successor_device = _grant_key(successor.get('key'))
            owner = (old_profile, event['grant'].get('board'))
            if (successor_profile != old_profile or successor_device == old_device
                    or successor['key'] in successor_keys or owner in rollover_owners
                    or not isinstance(successor.get('grant'), dict)
                    or not isinstance(successor.get('approval'), dict)):
                raise ValueError('gadget grant ledger has invalid successor intent; not modifying it')
            _grant_record(successor['grant'])
            if not _json_equal(successor['grant'], event['grant']):
                raise ValueError('gadget grant ledger successor authority conflicts with its predecessor; not modifying it')
            predecessor_approval = event.get('approval')
            successor_approval = successor['approval']
            if isinstance(predecessor_approval, dict):
                if not _json_equal(successor_approval, predecessor_approval):
                    raise ValueError('gadget ledger successor approval conflicts with its predecessor; not modifying it')
            elif predecessor_approval is None:
                approved_at = successor_approval.get('approved_at')
                if (set(successor_approval) != {'user_name', 'approved_at'}
                        or not isinstance(successor_approval.get('user_name'), str)
                        or not successor_approval['user_name'].startswith('Waveshare AI (')
                        or not successor_approval['user_name'].endswith(')')
                        or len(successor_approval['user_name']) > len('Waveshare AI ()') + 32
                        or type(approved_at) not in (int, float) or not math.isfinite(approved_at)
                        or approved_at < 0):
                    raise ValueError('gadget ledger has invalid fresh successor approval; not modifying it')
            else:
                raise ValueError('gadget grant ledger has invalid predecessor approval; not modifying it')
            if not denied and not _json_equal(grants.get(successor['key']), successor['grant']):
                raise ValueError('gadget grant ledger successor does not match its active grant; not modifying it')
            if denied and successor['key'] in grants:
                raise ValueError('denied gadget successor is still active; not modifying it')
            successor_keys.add(successor['key'])
            rollover_owners.add(owner)
    for event in retiring.values():
        if event.get('successor_denied'):
            successor = event['successor']
            denial = retiring.get(successor['key'])
            if (not isinstance(denial, dict) or denial.get('reason') != 'authorization-revoked'
                    or not _json_equal(denial.get('grant'), successor['grant'])
                    or not _json_equal(denial.get('approval'), successor['approval'])):
                raise ValueError('gadget successor denial has no matching revocation intent; not modifying it')
    return dict(grants), dict(retiring), list(history), extra


def _write_ledger(path, grants, retiring, history, extra):
    _write_private(path, {**extra, 'version': LEDGER_VERSION, 'grants': grants,
                          'retiring': retiring, 'history': history})


class Grants:
    """Approvals this bridge made in the profiles' gadget pairing stores (Hermes PairingStore format:
    ``{device_id: {"user_name", "approved_at"}}``), plus a private ledger of which board owns each:
    ``{"<profile>|<device_id>": {"board": token_sha256}}``."""

    def __init__(self, approved_for, ledger_path, clock=time.time):
        self.approved_for = approved_for            # profile -> path of its gadget-approved.json
        self.ledger_path = pathlib.Path(ledger_path)
        self.clock = clock
        self._lock = threading.Lock()

    def _retirement_updates(self, retiring, revoked=frozenset()):
        """Validate every profile preimage and return the complete SDK write plan."""
        by_profile = {}
        for key, event in retiring.items():
            profile, device_id = _grant_key(key)
            by_profile.setdefault(profile, []).append((device_id, event))
        updates = []
        for profile, events in by_profile.items():
            path = pathlib.Path(self.approved_for(profile))
            approved = _read_json_object(path)
            updated = dict(approved)
            for old_device, event in events:
                successor = event.get('successor')
                if old_device in updated and _json_equal(updated[old_device], event.get('approval')):
                    updated.pop(old_device)
                if successor is None:
                    continue
                successor_key = successor['key']
                _, new_device = _grant_key(successor_key)
                has_existing = new_device in updated
                existing = updated.get(new_device)
                expected = successor['approval']
                if has_existing and not _json_equal(existing, expected):
                    raise ValueError('successor device identity is approved outside this rollover; not modifying it')
                if successor_key in revoked or event.get('successor_denied'):
                    if has_existing and _json_equal(existing, expected):
                        updated.pop(new_device)
                else:
                    updated[new_device] = expected
            updates.append((path, approved, updated))
        return updates

    def _finish_retiring(self, grants, retiring, history, extra, revoked=frozenset(), updates=None):
        """Finish crash-safe retirements, installing an authorized successor before archiving intent."""
        if not retiring:
            return grants, retiring, history
        updates = self._retirement_updates(retiring, revoked) if updates is None else updates
        for path, approved, updated in updates:
            if not _json_equal(updated, approved):
                _write_private(path, updated)
        history.extend(retiring[key] for key in sorted(retiring))
        retiring = {}
        _write_ledger(self.ledger_path, grants, retiring, history, extra)
        return grants, retiring, history

    def _retirement(self, key, reason, grant, approval):
        return {'key': key, 'reason': reason, 'at': self.clock(),
                'grant': grant, 'approval': approval}

    def grant(self, profile, device_id, token_sha256, name):
        key = f'{profile}|{device_id}'
        path = pathlib.Path(self.approved_for(profile))
        with self._lock:
            grants, retiring, history, extra = _ledger_state(self.ledger_path)
            grants, retiring, history = self._finish_retiring(grants, retiring, history, extra)
            approved = _read_json_object(path)
            current = grants.get(key)
            if current is not None and current.get('board') != token_sha256:
                raise ValueError('derived device identity belongs to another ledger entry; not modifying it')
            if current is None and device_id in approved:
                raise ValueError('derived device identity is already approved outside this ledger; not modifying it')
            stale = sorted(k for k, record in grants.items()
                           if _grant_key(k)[0] == profile and k != key and record.get('board') == token_sha256)
            if stale:
                if len(stale) != 1:
                    raise ValueError('multiple device identities claim this board; not modifying them')
                old_key = stale[0]
                old_device = _grant_key(old_key)[1]
                inherited = approved.get(old_device)
                if inherited is not None and not isinstance(inherited, dict):
                    raise ValueError('owned SDK approval metadata is malformed; not modifying it')
                successor_grant = dict(grants.pop(old_key))
                successor_approval = dict(inherited) if inherited is not None else {
                    'user_name': f'Waveshare AI ({str(name)[:32]})', 'approved_at': self.clock()}
                grants[key] = successor_grant
                retiring[old_key] = self._retirement(
                    old_key, 'identity-domain-rollover', successor_grant, inherited)
                retiring[old_key]['successor'] = {
                    'key': key, 'grant': dict(successor_grant), 'approval': successor_approval}
                # This intent contains the complete authority for both stores before either old record moves.
                _write_ledger(self.ledger_path, grants, retiring, history, extra)
                self._finish_retiring(grants, retiring, history, extra)
                return
            if current is None:
                grants[key] = {'board': token_sha256}
                _write_ledger(self.ledger_path, grants, retiring, history, extra)
            if device_id not in approved:
                approved[device_id] = {'user_name': f'Waveshare AI ({str(name)[:32]})', 'approved_at': self.clock()}
                _write_private(path, approved)

    def sweep(self, still_allowed):
        """Revoke our grants whose board fails ``still_allowed(token_sha256)``; returns the ledger keys."""
        with self._lock:
            grants, retiring, history, extra = _ledger_state(self.ledger_path)
            gone = sorted(key for key, record in grants.items() if not still_allowed(record['board']))
            # Persist every denial before touching an SDK store. For an interrupted rollover this
            # both suppresses its successor and journals revocation of that successor as one write.
            for old_key, event in list(retiring.items()):
                successor = event.get('successor')
                if successor is None or successor['key'] not in gone:
                    continue
                event['successor_denied'] = True
                successor_key = successor['key']
                grant = grants.pop(successor_key)
                retiring[successor_key] = self._retirement(
                    successor_key, 'authorization-revoked', grant, successor['approval'])
            for key in gone:
                if key not in grants:
                    continue
                profile, device_id = _grant_key(key)
                approved = _read_json_object(pathlib.Path(self.approved_for(profile)))
                retiring[key] = self._retirement(
                    key, 'authorization-revoked', grants.pop(key), approved.get(device_id))
            updates = self._retirement_updates(retiring)
            if gone:
                _write_ledger(self.ledger_path, grants, retiring, history, extra)
            self._finish_retiring(grants, retiring, history, extra, updates=updates)
            return gone


class Request:
    def __init__(self, bot, authorization, headers):
        self.bot, self.authorization, self._headers = bot, authorization, headers

    def upstream_head(self, host, port):
        lines = [f'GET {PATH} HTTP/1.1', f'Host: {host}:{port}']
        lines += [f'{k}: {v}' for k, v in self._headers if k.lower() not in ('host', 'authorization')]
        return ('\r\n'.join(lines) + '\r\n\r\n').encode('latin-1')


def parse_head(raw):
    try:
        text = raw.decode('latin-1')
    except UnicodeDecodeError as exc:  # pragma: no cover - latin-1 decodes every byte
        raise ValueError('bad request') from exc
    if not text.endswith('\r\n\r\n'):
        raise ValueError('incomplete request')
    first, *rest = text[:-4].split('\r\n')
    parts = first.split(' ')
    bot = parts[1][len(PATH) + 1:] if len(parts) == 3 and parts[1].startswith(PATH + '/') else ''
    if len(parts) != 3 or parts[0] != 'GET' or parts[2] != 'HTTP/1.1' or not valid_bot(bot):
        raise ValueError('not a gadget upgrade')
    headers, auth = [], []
    for line in rest:
        name, sep, value = line.partition(':')
        if not sep or not name or name != name.strip() or any(c in name for c in ' \t'):
            raise ValueError('bad header')
        value = value.strip()
        if name.lower() == 'authorization':
            auth.append(value)
        headers.append((name, value))
    if len(auth) > 1:
        raise ValueError('duplicate authorization')
    return Request(bot, auth[0] if auth else '', headers)


def _refuse(writer, status, reason):
    body = reason.encode()
    writer.write(f'HTTP/1.1 {status} {reason}\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n'
                 .encode() + body)


async def _pipe(reader, writer):
    try:
        while data := await reader.read(65536):
            writer.write(data)
            await writer.drain()
    except (ConnectionError, OSError, asyncio.IncompleteReadError):
        pass
    finally:
        try:
            writer.close()
        except Exception:  # noqa: BLE001
            pass


class Front:
    def __init__(self, settings, authorizer, phone, grants, sweep_s):
        self.settings, self.authorizer, self.phone, self.grants = settings, authorizer, phone, grants
        self.sweep_s = sweep_s
        self.server = self._sweeper = None
        self.port = None
        self._slots = asyncio.Semaphore(MAX_CONNECTIONS)
        self.counts = {'piped': 0, 'refused': 0}

    def board_allowed(self, board):
        return board is not None and self.phone is not None and self.phone.allowed(board)

    async def handle(self, reader, writer):
        if self._slots.locked():
            writer.close()
            return
        async with self._slots:
            up_writer = None
            try:
                try:
                    raw = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), HEAD_TIMEOUT_S)
                    req = parse_head(raw)
                except (asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError,
                        ConnectionError, OSError):
                    return
                except ValueError:
                    self.counts['refused'] += 1
                    return _refuse(writer, 400, 'Bad Request')
                board = self.authorizer.board(req.authorization.encode('latin-1'))
                if board is None:
                    self.counts['refused'] += 1
                    return _refuse(writer, 401, 'Unauthorized')
                if not self.board_allowed(board):
                    self.counts['refused'] += 1
                    return _refuse(writer, 403, 'Sign in on the board')
                upstream_port = self.settings['profiles'].get(req.bot)
                if upstream_port is None:
                    self.counts['refused'] += 1
                    return _refuse(writer, 404, 'Unknown bot')
                token = bytes.fromhex(req.authorization[len('Bearer '):])
                device_id = device_id_for_token(token)
                token = b''
                await asyncio.to_thread(self.grants.grant, req.bot, device_id, board['token_sha256'],
                                        board.get('name') or '')
                host = self.settings['upstream_host']
                try:
                    up_reader, up_writer = await asyncio.wait_for(asyncio.open_connection(host, upstream_port), 5)
                except (OSError, asyncio.TimeoutError):
                    self.counts['refused'] += 1
                    return _refuse(writer, 503, 'Hermes gateway offline')
                up_writer.write(req.upstream_head(host, upstream_port))
                self.counts['piped'] += 1
                await asyncio.gather(_pipe(reader, up_writer), _pipe(up_reader, writer))
            except ValueError:
                _refuse(writer, 500, 'Pairing store unavailable')
            finally:
                for w in (writer, up_writer):
                    if w is not None:
                        try:
                            w.close()
                        except Exception:  # noqa: BLE001
                            pass

    def _still_allowed(self, token_sha256):
        return self.board_allowed(self.authorizer.registry.match_hash(token_sha256) if token_sha256 else None)

    async def _sweep_forever(self):
        while True:
            try:
                await asyncio.to_thread(self.grants.sweep, self._still_allowed)
            except (OSError, ValueError):
                pass
            await asyncio.sleep(self.sweep_s)

    async def close(self):
        if self._sweeper is not None:
            self._sweeper.cancel()
            await asyncio.gather(self._sweeper, return_exceptions=True)
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()


def server_context(cert, key):
    import ssl
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    return context


async def start(settings, *, bind, cert, key, authorizer, phone, grants, sweep_s=5.0):
    front = Front(settings, authorizer, phone, grants, sweep_s)
    front.server = await asyncio.start_server(front.handle, bind, settings['port'], ssl=server_context(cert, key),
                                              limit=HEAD_MAX, backlog=16)
    front.port = front.server.sockets[0].getsockname()[1]
    front._sweeper = asyncio.create_task(front._sweep_forever())
    return front


def approved_path(profiles_dir, profile):
    """Where Hermes's PairingStore(profile=...) keeps approved gadget devices."""
    return pathlib.Path(profiles_dir) / profile / 'platforms' / 'pairing' / 'gadget-approved.json'
