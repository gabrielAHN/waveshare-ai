"""Disposable Authelia for E2E runs (docker, TLS on 127.0.0.1:<port>, fresh throwaway keys).

Clients:
  waveshare-sessions  confidential, client_credentials (scopes hermes.sessions.read + hermes.helper.command)
  waveshare-pairing   public, RFC 8628 device_code grant (openid profile groups), explicit consent,
                      authorization_policy waveshare_pairing (group hermes_users/admins, one factor)
Users: `pair-admin` (groups admins, hermes_users) and `pair-guest` (no groups), throwaway passwords.

`approve_device()` performs the phone's part of the device flow headlessly through Authelia's own
HTTP API (first factor login -> device-authorization user-code PUT -> consent POST), exactly what the
Authelia web UI does in a browser. Nothing live is touched.
"""
import http.client
import json
import os
import secrets
import ssl
import subprocess
import time
import urllib.parse
from pathlib import Path

AUDIENCE = 'https://hermes.example.com/api/plugins/waveshare-sessions'
READ, CMD = 'hermes.sessions.read', 'hermes.helper.command'
PAIR_SCOPE = 'openid profile groups'


def _run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True)


def _cert(prefix, san, cn):
    _run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(prefix) + '.key', '-out',
          str(prefix) + '.crt', '-days', '2', '-subj', '/CN=' + cn, '-addext', 'subjectAltName=' + san])
    os.chmod(str(prefix) + '.key', 0o600)


def _hash(password):
    out = _run(['docker', 'run', '--rm', 'authelia/authelia:latest', 'authelia', 'crypto', 'hash', 'generate',
                'pbkdf2', '--variant', 'sha512', '--password', password]).stdout
    return out.strip().split('Digest: ')[-1]


class Authelia:
    def __init__(self, workdir, port=19091, container='phone-e2e-authelia',
                 pairing_policy='one_factor'):
        self.dir, self.port, self.container = Path(workdir), port, container
        self.pairing_policy = pairing_policy
        self.base = f'https://127.0.0.1:{port}'
        self.sessions_secret = secrets.token_urlsafe(40)
        self.users = {'pair-admin': secrets.token_urlsafe(18), 'pair-guest': secrets.token_urlsafe(18)}
        self.ctx = None

    def write(self):
        d = self.dir
        if d.exists():
            import shutil
            shutil.rmtree(d)
        d.mkdir(parents=True)
        _cert(d / 'tls', 'IP:127.0.0.1', '127.0.0.1')
        _run(['openssl', 'genrsa', '-out', str(d / 'oidc.key'), '2048'])
        key = '\n'.join(' ' * 10 + line for line in (d / 'oidc.key').read_text().strip().splitlines())
        pp = self.pairing_policy
        config = f"""
server:
  address: 'tcp://0.0.0.0:9091/'
  tls:
    key: /config/tls.key
    certificate: /config/tls.crt
log:
  level: info
identity_validation:
  reset_password:
    jwt_secret: '{secrets.token_hex(32)}'
authentication_backend:
  file:
    path: /config/users_database.yml
session:
  secret: '{secrets.token_hex(32)}'
  cookies:
    # Authelia derives the OIDC issuer from the cookie whose domain matches the request URL.
    - domain: '127.0.0.1'
      authelia_url: '{self.base}'
storage:
  encryption_key: '{secrets.token_hex(32)}'
  local:
    path: /config/db.sqlite3
notifier:
  filesystem:
    filename: /config/notification.txt
access_control:
  default_policy: one_factor
identity_providers:
  oidc:
    hmac_secret: '{secrets.token_hex(32)}'
    jwks:
      - key_id: 'e2e-rs256'
        algorithm: 'RS256'
        use: 'sig'
        key: |
{key}
    authorization_policies:
      waveshare_pairing:
        default_policy: deny
        rules:
          - policy: {pp}
            subject: 'group:hermes_users'
          - policy: {pp}
            subject: 'group:admins'
    clients:
      - client_id: 'waveshare-sessions'
        client_name: 'Waveshare session bridge (E2E)'
        client_secret: '$plaintext${self.sessions_secret}'
        public: false
        authorization_policy: one_factor
        lifespan: waveshare_sessions
        audience:
          - '{AUDIENCE}'
        scopes:
          - '{READ}'
          - '{CMD}'
        grant_types:
          - client_credentials
        response_types: []
        token_endpoint_auth_method: client_secret_basic
        access_token_signed_response_alg: RS256
      - client_id: 'waveshare-pairing'
        client_name: 'Waveshare board pairing (E2E)'
        public: true
        authorization_policy: waveshare_pairing
        consent_mode: explicit
        lifespan: waveshare_pairing
        scopes:
          - openid
          - profile
          - groups
        grant_types:
          - 'urn:ietf:params:oauth:grant-type:device_code'
        response_types: []
        token_endpoint_auth_method: none
    lifespans:
      custom:
        waveshare_sessions:
          access_token: 1h
        waveshare_pairing:
          access_token: 5m
          id_token: 5m
"""
        (d / 'configuration.yml').write_text(config)
        os.chmod(d / 'configuration.yml', 0o600)
        groups = {'pair-admin': "['admins', 'hermes_users']", 'pair-guest': '[]'}
        users = 'users:\n' + ''.join(
            f"  {name}:\n    disabled: false\n    displayname: '{'Pair Admin' if name == 'pair-admin' else 'Guest'}'\n"
            f"    password: '{_hash(pw)}'\n    email: {name}@e2e.test\n    groups: {groups[name]}\n"
            for name, pw in self.users.items())
        (d / 'users_database.yml').write_text(users)
        return self

    def start(self, seconds=60):
        subprocess.run(['docker', 'rm', '-f', self.container], capture_output=True)
        _run(['docker', 'run', '-d', '--name', self.container, '-p', f'127.0.0.1:{self.port}:9091',
              '-v', f'{self.dir}:/config', 'authelia/authelia:latest'])
        self.ctx = ssl.create_default_context(cafile=str(self.dir / 'tls.crt'))
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                if self.request('GET', '/api/health')[0] == 200:
                    return True
            except OSError:
                pass
            time.sleep(0.5)
        return False

    def stop(self):
        subprocess.run(['docker', 'rm', '-f', self.container], capture_output=True)

    def logs(self):
        r = subprocess.run(['docker', 'logs', self.container], capture_output=True, text=True)
        return r.stdout + r.stderr

    def request(self, method, path, body=None, headers=None, cookie=None):
        conn = http.client.HTTPSConnection('127.0.0.1', self.port, context=self.ctx, timeout=15)
        h = dict(headers or {})
        if cookie:
            h['Cookie'] = cookie
        if isinstance(body, dict):
            body = json.dumps(body).encode()
            h.setdefault('Content-Type', 'application/json')
        h.setdefault('Accept', 'application/json')
        conn.request(method, path, body=body, headers=h)
        r = conn.getresponse()
        data = r.read()
        set_cookie = [v for k, v in r.getheaders() if k.lower() == 'set-cookie']
        conn.close()
        return r.status, data, set_cookie

    def approve_device(self, user_code, username, consent=True):
        """The phone's side: sign in, submit the user code, then Approve (or Deny) on the consent screen.
        Returns a dict of the HTTP statuses observed (no secrets)."""
        trace = {}
        target = f'{self.base}/consent/openid/device-authorization?user_code={user_code}'
        status, _, cookies = self.request('POST', '/api/firstfactor', {
            'username': username, 'password': self.users[username], 'keepMeLoggedIn': False,
            'targetURL': target, 'flow': 'openid_connect', 'subflow': 'device_authorization', 'userCode': user_code})
        trace['firstfactor'] = status
        cookie = '; '.join(c.split(';', 1)[0] for c in cookies)
        # Same order as Authelia's web UI (DecisionFormView): consent info -> decision -> user-code PUT.
        status, data, _ = self.request('GET', f'/api/oidc/consent?user_code={user_code}', cookie=cookie)
        trace['consent_get'] = status
        info = (_json(data) or {}).get('data') or {}
        trace['consent_client'] = info.get('client_id')
        trace['consent_scopes'] = info.get('scopes')
        status, data, _ = self.request('POST', '/api/oidc/consent', {
            'client_id': 'waveshare-pairing', 'consent': bool(consent), 'pre_configure': False,
            'claims': info.get('claims') or [], 'subflow': 'device_authorization', 'user_code': user_code},
            cookie=cookie)
        trace['consent_post'] = status
        result = (_json(data) or {}).get('data') or {}
        flow_id = result.get('flow_id')
        trace['flow_id_found'] = bool(flow_id)
        if not consent or not flow_id:
            if status != 200:
                trace['consent_post_body'] = data[:300].decode('utf-8', 'replace')
            return trace
        form = urllib.parse.urlencode({'flow_id': flow_id, 'user_code': user_code}).encode()
        status, data, _ = self.request('PUT', '/api/oidc/device-authorization', form,
                                       {'Content-Type': 'application/x-www-form-urlencoded'}, cookie)
        trace['device_put'] = status
        if status != 200:
            trace['device_put_body'] = data[:300].decode('utf-8', 'replace')
        return trace


def _json(data):
    try:
        return json.loads(data)
    except ValueError:
        return None
