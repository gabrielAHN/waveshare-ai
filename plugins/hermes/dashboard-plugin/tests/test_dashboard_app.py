"""The plugin wired into the REAL dashboard app (hermes_cli.web_server.app) in-process.

Proves: exact-path token route works under the gated (OAuth) mode AND the loopback mode, the
same JWT never unlocks any other dashboard path, JWKS outage -> 503, and a token principal from a
different provider (e.g. the drain secret) cannot read the usage route.
"""
from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

PLUGIN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).parent))
from test_provider import AUDIENCE, CLIENT_ID, ISSUER, SCOPE, SETTINGS, IdP  # noqa: E402


@pytest.fixture(scope="module")
def wired(tmp_path_factory):
    settings = tmp_path_factory.mktemp("ws") / "settings.json"
    settings.write_text(json.dumps({k: v for k, v in SETTINGS.items() if v}))
    import os
    os.environ["WAVESHARE_SESSIONS_SETTINGS"] = str(settings)

    from fastapi.testclient import TestClient
    from hermes_cli import web_server
    from hermes_cli.dashboard_auth import clear_providers
    from hermes_cli.dashboard_auth.token_auth import clear_token_routes

    clear_providers()
    clear_token_routes()
    spec = importlib.util.spec_from_file_location("hermes_dashboard_plugin_waveshare-sessions",
                                                  PLUGIN_DIR / "dashboard" / "plugin_api.py")
    api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(api)  # installs the path-context middleware on the real app
    before = list(web_server.app.router.routes)
    web_server.app.include_router(api.router, prefix="/api/plugins/waveshare-sessions")
    # The real server mounts plugin routers at import time, BEFORE its SPA/api catch-all; a late
    # include in a test lands after it, so hoist the new routes to preserve production order.
    added = [r for r in web_server.app.router.routes if r not in before]
    web_server.app.router.routes[:] = added + before
    core = api.core

    idp = IdP()
    provider = core.build_provider(core.load_settings(), transport=idp.transport())

    from hermes_cli.dashboard_auth.registry import register_global_provider
    register_global_provider(provider)
    from hermes_cli.dashboard_auth.token_auth import register_token_route
    register_token_route(core.USAGE_ROUTE)

    state = web_server.app.state
    saved = {k: getattr(state, k, None) for k in ("bound_host", "bound_port", "auth_required")}
    state.bound_host, state.bound_port = "127.0.0.1", 9119
    client = TestClient(web_server.app, base_url="http://127.0.0.1:9119")
    yield {"client": client, "idp": idp, "core": core, "state": state, "provider": provider}
    for k, v in saved.items():
        setattr(state, k, v)
    clear_providers()
    clear_token_routes()
    os.environ.pop("WAVESHARE_SESSIONS_SETTINGS", None)


@pytest.fixture(autouse=True)
def _enabled(monkeypatch):
    import hermes_cli.plugins_cmd as pc
    monkeypatch.setattr(pc, "_get_enabled_set", lambda: {"waveshare-sessions"})
    monkeypatch.setattr(pc, "_get_disabled_set", lambda: set())


@pytest.fixture(params=[True, False], ids=["gated", "loopback"])
def mode(request, wired):
    wired["state"].auth_required = request.param
    wired["idp"].down = False
    return wired


ROUTE = "/api/plugins/waveshare-sessions/usage"


def test_valid_token_200_json(mode):
    token = mode["idp"].token()
    response = mode["client"].get(ROUTE, headers={"Authorization": f"Bearer {token}"})
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["version"] == 1 and "aggregate" in body
    assert response.headers["cache-control"] == "no-store"
    response = mode["client"].get(ROUTE + "?tokens=1", headers={"Authorization": f"Bearer {token}"})
    assert response.status_code == 200 and response.json()["aggregate"]["tokens_included"] is True


@pytest.mark.parametrize("variant", ["none", "garbage", "expired", "wrong_aud", "wrong_scope", "wrong_iss"])
def test_bad_tokens_401(mode, variant):
    idp = mode["idp"]
    import time
    tokens = {"none": None, "garbage": "abc.def.ghi",
              "expired": idp.token(exp=int(time.time()) - 300),
              "wrong_aud": idp.token(aud=["https://other"]),
              "wrong_scope": idp.token(scp=["openid"]),
              "wrong_iss": idp.token(iss="https://evil")}
    token = tokens[variant]
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    assert mode["client"].get(ROUTE, headers=headers).status_code == 401


def test_jwks_outage_is_503_not_200(mode):
    fresh = mode["core"].build_provider(SETTINGS, transport=mode["idp"].transport())
    from hermes_cli.dashboard_auth.registry import register_global_provider as register_provider
    register_provider(fresh)  # upsert: same name, cold JWKS cache
    mode["idp"].down = True
    try:
        response = mode["client"].get(ROUTE, headers={"Authorization": f"Bearer {mode['idp'].token()}"})
        assert response.status_code == 503
    finally:
        mode["idp"].down = False
        register_provider(mode["provider"])


@pytest.mark.parametrize("path", [
    "/api/config", "/api/sessions", "/api/status/detail", "/api/env", "/api/gateway/drain",
    "/api/plugins/waveshare-sessions/usage/", "/api/plugins/waveshare-sessions/other",
    "/api/plugins/provider-quota/quota", "/api/auth/ws-ticket", "/api/ws",
])
def test_token_does_not_unlock_other_paths(mode, path):
    token = mode["idp"].token()
    for method in ("get", "post"):
        response = getattr(mode["client"], method)(path, headers={"Authorization": f"Bearer {token}"},
                                                   follow_redirects=False)
        assert response.status_code in (401, 403, 404, 405, 302, 303, 307), (method, path, response.status_code)
        assert response.status_code not in (200, 201, 204)


def test_foreign_token_principal_is_forbidden(mode):
    """A different token provider (e.g. drain) accepted on this route must not read usage."""
    from hermes_cli.dashboard_auth import TokenPrincipal
    from hermes_cli.dashboard_auth.registry import register_global_provider as register_provider
    from hermes_cli.dashboard_auth.registry import unregister_global_provider

    class Other(type(mode["provider"]).__mro__[1]):
        name = "other-token"
        display_name = "Other"
        supports_token = True
        supports_session = False

        def verify_token(self, *, token):
            return TokenPrincipal(principal="drain-control", provider=self.name, scopes=("drain",)) \
                if token == "other-secret-value" else None

        def start_login(self, *, redirect_uri): raise NotImplementedError
        def complete_login(self, **kw): raise NotImplementedError
        def verify_session(self, *, access_token): return None
        def refresh_session(self, *, refresh_token): raise NotImplementedError
        def revoke_session(self, *, refresh_token): return None

    other = Other()
    register_provider(other)
    try:
        response = mode["client"].get(ROUTE, headers={"Authorization": "Bearer other-secret-value"})
        assert response.status_code == 403
    finally:
        unregister_global_provider("other-token", other)
