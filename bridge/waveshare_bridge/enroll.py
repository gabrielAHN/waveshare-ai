"""On-device enrollment for Waveshare AI boards.

The board generates its own 32-byte identity, discovers this bridge over mDNS, pins the TLS leaf
certificate fingerprint advertised in TXT, and POSTs ``/v1/enroll`` (``WEN1`` + token + name).
Both the board screen and ``waveshare-bridge enroll`` show the same 6-digit comparison code
``SHA-256("waveshare-ai-enroll-v1\\0" || fingerprint || SHA-256(token))[0:4] mod 10^6``; a
man-in-the-middle with a different certificate yields a different code. The operator confirms on
the bridge host. Only SHA-256(token) is stored (multi-board registry, 0600); raw tokens are never logged,
written, or kept after the request that carries them.

HTTP status contract (firmware ``pair_enroll_status``):
  200 accepted / already enrolled   202 pending confirmation   400 malformed
  403 window closed                 409 denied                 410 pending expired
  429 busy or rate limited
"""
import asyncio
import hashlib
import hmac
import json
import os
import pathlib
import re
import secrets
import ssl
import stat
import time

LABEL = b'waveshare-ai-enroll-v1\0'
BODY_SIZE = 4 + 32 + 33
PENDING_SECONDS = 120.0
DECIDED_SECONDS = 180.0
PER_IP_PER_MINUTE = 60        # the board polls every 1.5 s while waiting
MAX_WINDOW_SECONDS = 900
DEFAULT_NAME = 'Waveshare AI'


def comparison_code(fingerprint, token):
    if len(fingerprint) != 32 or len(token) != 32:
        raise ValueError('fingerprint and token must be 32 bytes')
    digest = hashlib.sha256(LABEL + fingerprint + hashlib.sha256(token).digest()).digest()
    return int.from_bytes(digest[:4], 'big') % 1_000_000


def code_text(code):
    return f'{code // 1000 % 1000:03d} {code % 1000:03d}'


def short_id(token):
    return hashlib.sha256(token).hexdigest()[:8]


def clean_name(raw):
    text = ''.join(ch for ch in raw if 0x20 <= ord(ch) < 0x7f).strip()
    return text[:32] or DEFAULT_NAME


def parse_enroll_body(data):
    if type(data) is not bytes or len(data) != BODY_SIZE or data[:4] != b'WEN1' or data[-1] != 0:
        raise ValueError('malformed enroll body')
    token = data[4:36]
    if not any(token):
        raise ValueError('invalid identity')
    name = data[36:].split(b'\0', 1)[0].decode('ascii', 'replace')
    return token, clean_name(name)


def cert_fingerprint(cert_path):
    """SHA-256 of the DER leaf certificate (what the board pins)."""
    pem = pathlib.Path(cert_path).read_text()
    begin, finish = '-----BEGIN ' + 'CERTIFICATE-----', '-----END ' + 'CERTIFICATE-----'
    start = pem.index(begin)
    end = pem.index(finish, start) + len(finish)
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem[start:end])).digest()


def write_private_json(path, value):
    path = pathlib.Path(path)
    tmp = path.with_name(f'.{path.name}.{secrets.token_hex(4)}.tmp')
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    try:
        with os.fdopen(fd, 'w') as handle:
            json.dump(value, handle, indent=2, sort_keys=True)
            handle.write('\n')
        os.replace(tmp, path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


def _printable(value, limit):
    return ''.join(ch for ch in str(value) if 0x20 <= ord(ch) < 0x7f).strip()[:limit]


def _phone_fields(b):
    """Optional phone sign-in fields of a registry entry (validated; unknown keys are dropped)."""
    out = {}
    user = _printable(b.get('phone_user') or '', 64)
    if user:
        out['phone_user'] = user
        out['phone_name'] = clean_name(str(b.get('phone_name') or '')) if b.get('phone_name') else user[:32]
        at = b.get('phone_at')
        out['phone_at'] = at if type(at) is int and at >= 0 else 0
        groups = b.get('phone_groups')
        if isinstance(groups, list):
            clean = sorted({g for g in groups if isinstance(g, str) and re.fullmatch(r'[A-Za-z0-9._@-]{1,64}', g)})[:16]
            if clean:
                out['phone_groups'] = clean
    sign_ins = _sign_ins(b.get('sign_ins'))
    if sign_ins:
        out['sign_ins'] = sign_ins
    return out


_GATEWAY_KEY = re.compile(r'[0-9a-f]{16}')
MAX_SIGN_INS = 8


def _sign_in_record(rec):
    """One ``sign_ins`` entry {user, name, at[, groups]} (validated), or None."""
    if not isinstance(rec, dict):
        return None
    user = _printable(rec.get('user') or '', 64)
    if not user:
        return None
    out = {'user': user, 'name': clean_name(str(rec.get('name') or '')) if rec.get('name') else user[:32]}
    at = rec.get('at')
    out['at'] = at if type(at) is int and at >= 0 else 0
    groups = rec.get('groups')
    if isinstance(groups, list):
        clean = sorted({g for g in groups if isinstance(g, str) and re.fullmatch(r'[A-Za-z0-9._@-]{1,64}', g)})[:16]
        if clean:
            out['groups'] = clean
    return out


def _sign_ins(value):
    """``sign_ins``: {gateway key (16 hex): record} for sign-ins at gateways other than the Hermes one."""
    if not isinstance(value, dict):
        return {}
    out = {}
    for key in sorted(value):
        record = _sign_in_record(value[key]) if isinstance(key, str) and _GATEWAY_KEY.fullmatch(key) else None
        if record is not None and len(out) < MAX_SIGN_INS:
            out[key] = record
    return out


class Registry:
    """Multi-board registry: {id, name, token_sha256, added[, phone_user, phone_name, phone_at, phone_groups,
    sign_ins]}. File must be 0600 and not a symlink. The flat phone_* fields record
    which user approved the board at the Hermes provider's sign-in gateway (device grant); ``sign_ins`` = {gateway key: {user, name, at, groups}} holds
    sign-ins at any other gateway (e.g. a Home Assistant provider with its own). No token is stored."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        self._stamp = None
        self._boards = []

    def _load(self):
        try:
            st = os.lstat(self.path)
        except FileNotFoundError:
            self._stamp, self._boards = None, []
            return
        if stat.S_ISLNK(st.st_mode) or not stat.S_ISREG(st.st_mode) or st.st_mode & 0o077:
            raise ValueError('board registry must be a private (0600) regular file')
        stamp = (st.st_mtime_ns, st.st_size, st.st_ino)
        if stamp == self._stamp:
            return
        data = json.loads(self.path.read_text())
        boards = data.get('boards') if isinstance(data, dict) else None
        if not isinstance(boards, list):
            raise ValueError('invalid board registry')
        clean = []
        for b in boards:
            h = b.get('token_sha256') if isinstance(b, dict) else None
            if type(h) is not str or len(h) != 64 or any(c not in '0123456789abcdef' for c in h):
                raise ValueError('invalid board registry entry')
            entry = {'id': str(b.get('id', h[:8]))[:16], 'name': clean_name(str(b.get('name', ''))),
                     'token_sha256': h, 'added': b.get('added', 0)}
            entry.update(_phone_fields(b))
            clean.append(entry)
        self._stamp, self._boards = stamp, clean

    def _save(self):
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        write_private_json(self.path, {'version': 1, 'boards': self._boards})
        self._stamp = None
        self._load()

    def boards(self):
        self._load()
        return [dict(b) for b in self._boards]

    def add_hash(self, token_sha256, name):
        self._load()
        entry = {'id': token_sha256[:8], 'name': clean_name(name), 'token_sha256': token_sha256,
                 'added': int(time.time())}
        self._boards = [b for b in self._boards if b['token_sha256'] != token_sha256] + [entry]
        self._save()
        return dict(entry)

    def add(self, token, name):
        return self.add_hash(hashlib.sha256(token).hexdigest(), name)

    def remove(self, board_id):
        self._load()
        kept = [b for b in self._boards if b['id'] != board_id]
        if len(kept) == len(self._boards):
            return False
        self._boards = kept
        self._save()
        return True

    def _update(self, board_id, change):
        self._load()
        for b in self._boards:
            if b['id'] == board_id:
                change(b)
                self._save()
                return True
        return False

    def set_phone(self, board_id, user, name, now=None, groups=None):
        """Mark a board phone-authorized by user ``user``.
        ``groups``: the user's Authelia groups at sign-in (decides e.g. the Home Assistant tile)."""
        def change(b):
            b.pop('phone_groups', None)
            b.update(_phone_fields({'phone_user': user, 'phone_name': name, 'phone_groups': list(groups or []),
                                    'phone_at': int(time.time() if now is None else now)}))
        return self._update(board_id, change)

    def clear_phone(self, board_id):
        """Sign the board out of the Hermes gateway. False if no such board."""
        def change(b):
            for key in ('phone_user', 'phone_name', 'phone_at', 'phone_groups'):
                b.pop(key, None)
        return self._update(board_id, change)

    def set_sign_in(self, board_id, gateway_key, user, name, now=None, groups=None):
        """Record a sign-in at another gateway (``sign_ins[gateway_key]``)."""
        if not isinstance(gateway_key, str) or not _GATEWAY_KEY.fullmatch(gateway_key):
            raise ValueError('invalid gateway key')

        def change(b):
            record = _sign_in_record({'user': user, 'name': name, 'groups': list(groups or []),
                                      'at': int(time.time() if now is None else now)})
            sign_ins = {k: v for k, v in (b.get('sign_ins') or {}).items() if k != gateway_key}
            if record is not None:
                sign_ins[gateway_key] = record
            b['sign_ins'] = _sign_ins(sign_ins)
            if not b['sign_ins']:
                b.pop('sign_ins')
        return self._update(board_id, change)

    def clear_sign_in(self, board_id, gateway_key):
        """Sign the board out of one other gateway. False if no such board."""
        def change(b):
            sign_ins = {k: v for k, v in (b.get('sign_ins') or {}).items() if k != gateway_key}
            b.pop('sign_ins', None)
            if sign_ins:
                b['sign_ins'] = sign_ins
        return self._update(board_id, change)

    def sign_out(self, board_id):
        """Sign the board out everywhere (every gateway)."""
        def change(b):
            for key in ('phone_user', 'phone_name', 'phone_at', 'phone_groups', 'sign_ins'):
                b.pop(key, None)
        return self._update(board_id, change)

    def match_hash(self, token_sha256):
        self._load()
        found = None
        for b in self._boards:  # constant-time per entry, no early exit
            if hmac.compare_digest(b['token_sha256'], token_sha256):
                found = b
        return dict(found) if found else None

    def match(self, token):
        return self.match_hash(hashlib.sha256(token).hexdigest())


class Authorizer:
    """``Authorization: Bearer <64 lowercase hex>`` -> an enrolled board."""

    def __init__(self, registry):
        self.registry = registry

    def check(self, supplied):
        return self.board(supplied) is not None

    def board(self, supplied):
        """The registry entry for an ``Authorization`` value, or None."""
        if type(supplied) is not bytes:
            return None
        if len(supplied) != 71 or not supplied.startswith(b'Bearer '):
            return None
        hexpart = supplied[7:]
        if any(c not in b'0123456789abcdef' for c in hexpart):
            return None
        try:
            return self.registry.match(bytes.fromhex(hexpart.decode('ascii')))
        except ValueError:
            return None


class EnrollWindow:
    """Operator-opened enrollment window with at most one pending board at a time."""

    def __init__(self, registry, fingerprint, clock=time.monotonic):
        if len(fingerprint) != 32:
            raise ValueError('fingerprint must be 32 bytes')
        self.registry, self.fingerprint, self.clock = registry, fingerprint, clock
        self.until = 0.0
        self._pending = None
        self._decided = {}
        self._hits = {}

    # -- operator side -------------------------------------------------------
    def open(self, seconds):
        self.until = self.clock() + max(1.0, min(float(seconds), MAX_WINDOW_SECONDS))
        self._pending = None

    def close(self):
        self.until = 0.0
        self._pending = None

    def is_open(self):
        return self.clock() < self.until

    def pending(self):
        p = self._pending
        if p is None or self.clock() - p['created'] > PENDING_SECONDS:
            return None
        return {k: p[k] for k in ('id', 'name', 'code', 'ip')}

    def decide(self, accept):
        p = self._pending
        if p is None or self.clock() - p['created'] > PENDING_SECONDS:
            return False
        if accept:
            self.registry.add_hash(p['hash'], p['name'])
        self._decided[p['hash']] = ('accepted' if accept else 'denied', self.clock())
        self._pending = None
        return True

    # -- board side ----------------------------------------------------------
    def _rate_ok(self, ip):
        now = self.clock()
        hits = [t for t in self._hits.get(ip, ()) if now - t < 60.0]
        hits.append(now)
        self._hits[ip] = hits
        if len(self._hits) > 256:
            self._hits = {k: v for k, v in self._hits.items() if v and now - v[-1] < 60.0}
        return len(hits) <= PER_IP_PER_MINUTE

    def submit(self, data, ip):
        """Returns (http_status, outcome). ``data`` is the raw request body."""
        if not self._rate_ok(ip):
            return 429, 'rate-limited'
        try:
            token, name = parse_enroll_body(data)
        except ValueError:
            return 400, 'malformed'
        digest = hashlib.sha256(token).hexdigest()
        now = self.clock()
        self._decided = {k: v for k, v in self._decided.items() if now - v[1] < DECIDED_SECONDS}
        if self.registry.match_hash(digest) is not None:
            return 200, 'accepted'
        decided = self._decided.get(digest)
        if decided and decided[0] == 'denied':
            return 409, 'denied'
        if decided and decided[0] == 'expired':
            return 410, 'expired'
        p = self._pending
        if p is not None and now - p['created'] > PENDING_SECONDS:
            self._decided[p['hash']] = ('expired', now)
            self._pending = None
            if p['hash'] == digest:
                return 410, 'expired'
            p = None
        if not self.is_open():
            return 403, 'closed'
        if p is not None:
            return (202, 'pending') if hmac.compare_digest(p['hash'], digest) else (429, 'busy')
        self._pending = {'hash': digest, 'name': name, 'ip': ip, 'created': now, 'id': digest[:8],
                         'code': comparison_code(self.fingerprint, token)}
        return 202, 'pending'


# ---------------------------------------------------------------------------
# Local control socket: `waveshare-bridge enroll` <-> running bridge (0600 AF_UNIX).
# ---------------------------------------------------------------------------

async def start_control(path, window):
    path = pathlib.Path(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        if stat.S_ISSOCK(os.lstat(path).st_mode):
            path.unlink()
    except FileNotFoundError:
        pass

    async def handle(reader, writer):
        async def send(obj):
            writer.write((json.dumps(obj) + '\n').encode())
            await writer.drain()

        opened = False
        try:
            line = await asyncio.wait_for(reader.readline(), 10)
            request = json.loads(line or b'{}')
            if request.get('op') == 'status':
                await send({'event': 'status', 'open': window.is_open(), 'boards': len(window.registry.boards())})
                return
            if request.get('op') != 'open':
                await send({'event': 'error', 'error': 'unknown op'})
                return
            window.open(request.get('seconds', 120))
            opened = True
            await send({'event': 'open', 'seconds': round(window.until - window.clock())})
            announced = None
            decision = asyncio.ensure_future(reader.readline())
            while window.is_open():
                pending = window.pending()
                if pending and pending['id'] != announced:
                    announced = pending['id']
                    await send({'event': 'pending', **pending, 'code_text': code_text(pending['code'])})
                if decision.done():
                    line = decision.result()
                    if not line:
                        return  # CLI went away
                    msg = json.loads(line)
                    if msg.get('op') == 'decide':
                        accept = msg.get('accept') is True
                        ok = window.decide(accept)
                        outcome = ('accepted' if accept else 'denied') if ok else 'expired'
                        await send({'event': 'result', 'outcome': outcome})
                        if ok and accept:
                            return
                        announced = None
                    decision = asyncio.ensure_future(reader.readline())
                await asyncio.sleep(0.1)
            await send({'event': 'result', 'outcome': 'timeout'})
        except (asyncio.TimeoutError, ValueError, ConnectionError):
            pass
        finally:
            if opened:
                window.close()
            writer.close()

    old = os.umask(0o177)
    try:
        server = await asyncio.start_unix_server(handle, path=str(path))
    finally:
        os.umask(old)
    os.chmod(path, 0o600)
    return server


async def run_enroll_client(path, seconds, ask=input, emit=print):
    """Open the window, show the code, ask the operator y/n. Returns the final outcome string."""
    reader, writer = await asyncio.open_unix_connection(str(path))
    loop = asyncio.get_running_loop()
    try:
        writer.write((json.dumps({'op': 'open', 'seconds': seconds}) + '\n').encode())
        await writer.drain()
        while True:
            line = await reader.readline()
            if not line:
                return 'disconnected'
            event = json.loads(line)
            kind = event.get('event')
            if kind == 'open':
                emit(f"Enrollment open for {event['seconds']} s. On the device: Settings > Connect to bridge, "
                     'pick this bridge.')
            elif kind == 'pending':
                emit(f"Board '{event['name']}' (id {event['id']}, from {event['ip']}) wants to enroll.")
                emit(f"Comparison code: {event['code_text']}  -- it must match the code on the device screen.")
                answer = await loop.run_in_executor(None, ask, 'Codes match? Enroll this board [y/N]: ')
                accept = str(answer).strip().lower() in ('y', 'yes')
                writer.write((json.dumps({'op': 'decide', 'accept': accept}) + '\n').encode())
                await writer.drain()
            elif kind == 'result':
                emit(f"Result: {event['outcome']}")
                if event['outcome'] in ('accepted', 'timeout'):
                    return event['outcome']
                if event['outcome'] == 'denied':
                    return 'denied'
            elif kind == 'error':
                return 'error'
    finally:
        writer.close()
