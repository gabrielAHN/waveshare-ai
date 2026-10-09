#!/usr/bin/env python3
"""E2E: phone sign-in (RFC 8628 device grant) against a THROWAWAY Authelia, using the real bridge code.

Starts a disposable Authelia in docker (tests/e2e/e2e_authelia.py; 127.0.0.1:<port>, fresh keys, public
client `waveshare-pairing` exactly like the live registration), then drives the bridge's
`phone_pair.PhonePairing` with a real `enroll.Registry` in a temp dir and scripts the phone's part
(login -> consent -> user code) through Authelia's own HTTP API:

  approve   pair-admin (groups admins, hermes_users) approves -> board authorized, registry shows user
  deny      pair-admin presses Deny -> Authelia answers 500 server_error -> bridge reports denied
  guest     pair-guest (no groups) is refused at consent -> grant stays pending (bridge keeps polling)
  forget    sign-out clears the registry entry

Never touches the live Authelia. Needs docker + authelia/authelia image; ~1 min (Authelia interval 10 s).
Usage: bridge/.venv/bin/python tests/e2e/e2e_phone_pair.py [out.json]   (or the live venv's python)
Prints only statuses/states; no secrets, no codes.
"""
import asyncio
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'bridge'))
sys.path.insert(0, str(ROOT / 'tests' / 'e2e'))
import aiohttp  # noqa: E402
from e2e_authelia import Authelia  # noqa: E402
from waveshare_bridge import enroll, phone_pair  # noqa: E402

SCRATCH = Path(os.environ.get('WAVESHARE_AI_SCRATCH') or os.environ.get('TMPDIR') or tempfile.gettempdir())


async def wait_state(phone, reg, board, want, seconds):
    loop = asyncio.get_running_loop()
    end = loop.time() + seconds
    while loop.time() < end:
        board = reg.match_hash(board['token_sha256'])   # the bridge route re-reads the registry per request
        st = phone_pair.decode_status(phone.status(board))
        if st['state'] in want:
            return st
        await asyncio.sleep(0.5)
    return phone_pair.decode_status(phone.status(reg.match_hash(board['token_sha256'])))


async def scenario(a, name, work):
    reg = enroll.Registry(work / f'{name}-boards.json')
    board = reg.add(os.urandom(32), 'E2E board')
    async with aiohttp.ClientSession() as client:
        phone = phone_pair.PhonePairing.from_specs(reg, client, [
            {'provider': 'hermes', 'issuer_base': a.base, 'client_id': 'waveshare-pairing',
             'public_host': '127.0.0.1', 'groups': ['admins', 'hermes_users'], 'require': True,
             'ca_file': str(a.dir / 'tls.crt')}])
        out = {'allowed_before': phone.allowed(board)}
        status, frame = await phone.start(board)
        st = phone_pair.decode_status(frame)
        out.update(start_http=status, start_state=st['state'], expires_in=st['expires_in'],
                   code_len=len(st['user_code']), uri_path=st['uri'].split('?')[0].split('/', 3)[-1])
        loop = asyncio.get_running_loop()
        if name == 'approve':
            out['phone'] = await loop.run_in_executor(None, a.approve_device, st['user_code'], 'pair-admin', True)
            st = await wait_state(phone, reg, board, {'authorized', 'denied', 'error', 'refused'}, 40)
            fresh = reg.match_hash(board['token_sha256'])
            out.update(final=st['state'], name=st['name'], registry_user=fresh.get('phone_user'),
                       allowed_after=phone.allowed(fresh))
            back = phone_pair.decode_status(phone.forget(fresh))
            out.update(after_forget=back['state'],
                       registry_cleared=not reg.match_hash(board['token_sha256']).get('phone_user'))
        elif name == 'deny':
            out['phone'] = await loop.run_in_executor(None, a.approve_device, st['user_code'], 'pair-admin', False)
            st = await wait_state(phone, reg, board, {'authorized', 'denied', 'error', 'refused'}, 45)
            out.update(final=st['state'], allowed_after=phone.allowed(reg.match_hash(board['token_sha256'])))
        else:  # guest: not in an allowed group -> consent refused, grant stays pending
            out['phone'] = await loop.run_in_executor(None, a.approve_device, st['user_code'], 'pair-guest', True)
            await asyncio.sleep(25)
            st = phone_pair.decode_status(phone.status(board))
            out.update(final=st['state'], allowed_after=phone.allowed(reg.match_hash(board['token_sha256'])))
        await phone.close()
        return out


async def main(out_path):
    work = Path(tempfile.mkdtemp(prefix='phone-e2e-', dir=SCRATCH))
    work.chmod(0o700)
    a = Authelia(work / 'authelia', port=19094, container='phone-e2e-authelia',
                 pairing_policy='one_factor').write()
    result = {}
    try:
        if not a.start():
            print(a.logs()[-3000:])
            raise SystemExit('throwaway Authelia did not start')
        for name in ('approve', 'deny', 'guest'):
            result[name] = await scenario(a, name, work)
            print(name, json.dumps(result[name]), flush=True)
    finally:
        a.stop()
    ok = (result['approve']['start_http'] == 200 and result['approve']['final'] == 'authorized'
          and result['approve']['registry_user'] == 'pair-admin' and result['approve']['allowed_after']
          and not result['approve']['allowed_before'] and result['approve']['registry_cleared']
          and result['deny']['final'] == 'denied' and not result['deny']['allowed_after']
          and result['guest']['final'] == 'pending' and not result['guest']['allowed_after'])
    result['ok'] = ok
    if out_path:
        Path(out_path).write_text(json.dumps(result, indent=2))
    print('PHONE_E2E', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 else None)))
