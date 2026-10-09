"""Unit + in-app tests for the waveshare-sessions plugin.

Run from the hermes-agent checkout so ``hermes_cli`` imports:
    cd ~/.hermes/hermes-agent && .venv/bin/python -m pytest ~/.hermes/plugins/waveshare-sessions/tests -q
"""
from __future__ import annotations

import base64
import importlib.util
import json
import sys
import time
from pathlib import Path

import httpx
import jwt
import pytest
from cryptography.hazmat.primitives.asymmetric import rsa

PLUGIN_DIR = Path(__file__).resolve().parents[1]
ISSUER = "https://auth.example.test"
AUDIENCE = "https://hermes.example.com/api/plugins/waveshare-sessions"
CLIENT_ID = "waveshare-sessions"
SCOPE = "hermes.sessions.read"


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


core = _load("waveshare_sessions_core", PLUGIN_DIR / "core.py")


def _b64(n: int) -> str:
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode()


class IdP:
    def __init__(self, kid="k1"):
        self.kid = kid
        self.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        self.down = False
        self.fetches = 0

    def jwks(self):
        numbers = self.key.public_key().public_numbers()
        return {"keys": [{"kty": "RSA", "kid": self.kid, "alg": "RS256", "use": "sig",
                          "n": _b64(numbers.n), "e": _b64(numbers.e)}]}

    def transport(self):
        def handler(request: httpx.Request):
            self.fetches += 1
            if self.down:
                raise httpx.ConnectError("down", request=request)
            return httpx.Response(200, json=self.jwks())
        return httpx.MockTransport(handler)

    def token(self, *, key=None, alg="RS256", kid=None, headers=None, **overrides):
        now = int(time.time())
        claims = {"iss": ISSUER, "aud": [AUDIENCE], "client_id": CLIENT_ID, "scp": [SCOPE],
                  "iat": now, "nbf": now, "exp": now + 3600, "jti": "x"}
        for name, value in overrides.items():
            if value is None:
                claims.pop(name, None)
            else:
                claims[name] = value
        hdr = {"kid": kid or self.kid, "typ": "at+jwt", **(headers or {})}
        return jwt.encode(claims, key if key is not None else self.key, algorithm=alg, headers=hdr)


SETTINGS = {"issuer": ISSUER, "audience": AUDIENCE, "client_id": CLIENT_ID, "scope": SCOPE,
            "jwks_uri": ISSUER + "/jwks.json", "jwks_host_header": "", "ca_file": ""}


@pytest.fixture
def idp():
    return IdP()


@pytest.fixture
def provider(idp):
    return core.build_provider(SETTINGS, transport=idp.transport())


@pytest.fixture
def on_route():
    token = core.CURRENT_PATH.set(core.USAGE_ROUTE)
    yield
    core.CURRENT_PATH.reset(token)


# ---------------------------------------------------------------------------
# Provider: claim/alg/scope pinning
# ---------------------------------------------------------------------------

def test_valid_token_accepted(provider, idp, on_route):
    principal = provider.verify_token(token=idp.token())
    assert principal is not None
    assert principal.provider == core.PROVIDER_NAME
    assert principal.scopes == (SCOPE,)
    assert principal.principal == "authelia-client:waveshare-sessions"


def test_space_separated_scope_claim_accepted(provider, idp, on_route):
    assert provider.verify_token(token=idp.token(scp=None, scope=f"other {SCOPE}")) is not None


@pytest.mark.parametrize("overrides", [
    {"iss": "https://evil.example"},
    {"iss": ISSUER + "/"},
    {"aud": ["https://other.example"]},
    {"aud": None},
    {"client_id": "another-app"},
    {"client_id": None},
    {"scp": ["openid"]},
    {"scp": None},
    {"exp": int(time.time()) - 120},
    {"exp": None},
    {"iat": None},
    {"iat": int(time.time()) + 3600},
    {"nbf": int(time.time()) + 3600},
])
def test_wrong_claims_rejected(provider, idp, on_route, overrides):
    assert provider.verify_token(token=idp.token(**overrides)) is None


def test_wrong_signing_key_rejected(provider, idp, on_route):
    other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    assert provider.verify_token(token=idp.token(key=other)) is None


def test_unknown_kid_rejected(provider, idp, on_route):
    other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    assert provider.verify_token(token=idp.token(key=other, kid="nope")) is None


def test_alg_confusion_hs256_with_public_key_rejected(provider, idp, on_route):
    from cryptography.hazmat.primitives import serialization
    pem = idp.key.public_key().public_bytes(serialization.Encoding.PEM,
                                           serialization.PublicFormat.SubjectPublicKeyInfo)
    now = int(time.time())
    claims = {"iss": ISSUER, "aud": [AUDIENCE], "client_id": CLIENT_ID, "scp": [SCOPE],
              "iat": now, "exp": now + 60}
    header = base64.urlsafe_b64encode(json.dumps({"alg": "HS256", "kid": idp.kid}).encode()).rstrip(b"=")
    body = base64.urlsafe_b64encode(json.dumps(claims).encode()).rstrip(b"=")
    import hashlib
    import hmac as _hmac
    sig = base64.urlsafe_b64encode(_hmac.new(pem, header + b"." + body, hashlib.sha256).digest()).rstrip(b"=")
    assert provider.verify_token(token=(header + b"." + body + b"." + sig).decode()) is None


def test_alg_none_rejected(provider, idp, on_route):
    now = int(time.time())
    token = jwt.encode({"iss": ISSUER, "aud": [AUDIENCE], "client_id": CLIENT_ID, "scp": [SCOPE],
                        "iat": now, "exp": now + 60}, None, algorithm="none", headers={"kid": idp.kid})
    assert provider.verify_token(token=token) is None


def test_rs512_not_in_allowlist_rejected(provider, idp, on_route):
    assert provider.verify_token(token=idp.token(alg="RS512")) is None


@pytest.mark.parametrize("garbage", ["", "x", "not.a.jwt", "a" * 100, "a.b.c.d", "é" * 40, "x" * 9000])
def test_garbage_rejected(provider, on_route, garbage):
    assert provider.verify_token(token=garbage) is None


def test_other_path_never_vouches(provider, idp):
    token = idp.token()
    for path in (None, "/api/gateway/drain", "/api/sessions", "/api/plugins/waveshare-sessions/usage/"):
        ctx = core.CURRENT_PATH.set(path)
        try:
            assert provider.verify_token(token=token) is None
        finally:
            core.CURRENT_PATH.reset(ctx)
    assert idp.fetches == 0  # rejected before any network I/O


def test_jwks_unreachable_raises_provider_error(idp, on_route):
    from hermes_cli.dashboard_auth.base import ProviderError
    idp.down = True
    provider = core.build_provider(SETTINGS, transport=idp.transport())
    with pytest.raises(ProviderError):
        provider.verify_token(token=idp.token())


def test_jwks_cached_and_refreshed_after_ttl(idp, on_route):
    clock = [1000.0]
    provider = core.build_provider(SETTINGS, transport=idp.transport(), jwks_clock=lambda: clock[0])
    token = idp.token()
    for _ in range(5):
        assert provider.verify_token(token=token) is not None
    assert idp.fetches == 1
    clock[0] += core.JWKS_TTL_SECONDS + 1
    assert provider.verify_token(token=token) is not None
    assert idp.fetches == 2


def test_unknown_kid_refetch_is_rate_limited(idp, on_route):
    clock = [1000.0]
    provider = core.build_provider(SETTINGS, transport=idp.transport(), jwks_clock=lambda: clock[0])
    other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    for _ in range(10):
        assert provider.verify_token(token=idp.token(key=other, kid="rotated")) is None
    assert idp.fetches == 2  # initial + one miss-refetch, not ten


def test_interactive_surface_disabled(provider):
    assert provider.supports_token is True and provider.supports_session is False
    assert provider.verify_session(access_token="anything") is None
    with pytest.raises(NotImplementedError):
        provider.start_login(redirect_uri="https://x")


# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------

def _write(tmp_path, **overrides):
    data = {k: v for k, v in SETTINGS.items() if v}
    data.update(overrides)
    path = tmp_path / "settings.json"
    path.write_text(json.dumps({k: v for k, v in data.items() if v is not None}))
    return path


def test_settings_roundtrip(tmp_path):
    assert core.load_settings(_write(tmp_path))["issuer"] == ISSUER


@pytest.mark.parametrize("overrides", [
    {"issuer": "http://auth.example.test"},
    {"scope": "openid"},
    {"scope": "offline_access"},
    {"audience": ""},
    {"client_id": None},
    {"jwks_uri": "http://10.0.0.1/jwks.json"},
    {"jwks_host_header": "evil/path"},
])
def test_settings_rejected(tmp_path, overrides):
    with pytest.raises(core.SettingsError):
        core.load_settings(_write(tmp_path, **overrides))


# ---------------------------------------------------------------------------
# Usage snapshot: read-only RPCs, pseudonymous output
# ---------------------------------------------------------------------------

def _fake_rpc(calls):
    def rpc(method, params):
        calls.append(method)
        if method == "session.active_list":
            return {"sessions": [
                # active_list does not currently emit provider. A hypothetical field must not
                # outrank the authoritative route rendered by session.status.
                {"id": "sess-a", "status": "working", "provider": "openrouter",
                 "title": "SECRET TITLE", "preview": "SECRET MSG"},
                {"id": "sess-b", "status": "idle", "title": "t"},
                {"id": "sess-c", "status": "working"},
                {"id": 7, "status": "working"}]}
        if method == "session.status":
            if params["session_id"] == "sess-a":
                return {"output": "Hermes TUI Status\n\nSession ID: sess-a\nPath: /private/home\n"
                                  "Title: SECRET\nModel: claude-sonnet-4-5 (anthropic)\n"
                                  "Created: 2026-09-26 10:00\nLast Activity: 2026-09-26 10:01\n"
                                  "Tokens: 12,345\nAgent Running: Yes"}
            return {"output": "garbage"}
        raise AssertionError(method)
    return rpc


def test_usage_snapshot_only_pseudonyms_and_totals():
    calls = []
    body = core.usage_snapshot(include_tokens=True, rpc=_fake_rpc(calls))
    text = json.dumps(body)
    assert "SECRET" not in text and "sess-a" not in text
    assert calls == ["session.active_list", "session.status", "session.status"]
    assert body["aggregate"] == {"sessions": 3, "working": 2, "tokens_included": True,
                                 "working_tokens_total": 12345, "working_tokens_unknown": 1}
    ids = {s["id"] for s in body["sessions"]}
    assert core.session_hash("sess-a") in ids and len(ids) == 3
    assert next(s for s in body["sessions"] if s["id"] == core.session_hash("sess-a"))["provider"] == "anthropic"
    assert next(s for s in body["sessions"] if s["id"] == core.session_hash("sess-c"))["provider"] is None


def test_usage_snapshot_without_tokens_makes_no_status_calls():
    calls = []
    body = core.usage_snapshot(include_tokens=False, rpc=_fake_rpc(calls))
    assert calls == ["session.active_list"]
    assert body["aggregate"]["working_tokens_total"] is None
    assert all(session["provider"] is None for session in body["sessions"])


@pytest.mark.parametrize("output", [
    "Hermes TUI Status\nModel: gpt-5 (openai-codex)\nTokens: 1\nAgent Running: Yes",
    "Hermes TUI Status\nModel: qwen (openrouter)\nTokens: 1\nAgent Running: Yes",
])
def test_status_provider_uses_actual_model_route_field(output):
    assert core.parse_status_provider(output) in ("openai-codex", "openrouter")


@pytest.mark.parametrize("output", [
    None,
    "Hermes TUI Status\nModel: gpt-5 (unknown)\nTokens: 1\nAgent Running: Yes",
    "Hermes TUI Status\nModel: gpt-5 (anthropic)\nModel: injected (openrouter)\nTokens: 1\nAgent Running: Yes",
    "Hermes TUI Status\nModel: gpt-5 (anthropic\nTokens: 1\nAgent Running: Yes",
    "Hermes TUI Status\nModel: gpt-5 (" + "a" * (core.MAX_PROVIDER + 1) + ")\nTokens: 1\nAgent Running: Yes",
    "Hermes TUI Status\nModel: gpt-5 (anthropic)\nTokens: 1\nAgent Running: Yes" + "x" * core.MAX_STATUS_TEXT,
])
def test_status_provider_malformed_or_injected_is_neutral(output):
    assert core.parse_status_provider(output) is None


def test_default_rpc_refuses_session_usage():
    with pytest.raises(ValueError):
        core._default_rpc("session.usage", {})


# ---------------------------------------------------------------------------
# Setup-time settings (written by `waveshare-bridge plugin install`)
# ---------------------------------------------------------------------------







def test_installer_settings_load(tmp_path):
    """The exact shape `waveshare-bridge plugin install` writes is accepted verbatim."""
    installed = {"issuer": ISSUER, "audience": AUDIENCE, "client_id": "waveshare-bridge", "scope": SCOPE,
                 "command_scope": "hermes.helper.command", "jwks_uri": ISSUER + "/jwks.json", "profile": "helper"}
    path = tmp_path / "settings.json"
    path.write_text(json.dumps(installed))
    loaded = core.load_settings(path)
    assert loaded["client_id"] == "waveshare-bridge" and loaded["command_scope"] == "hermes.helper.command"
    assert "secret" not in json.dumps(loaded).lower()
