"""Scope separation between usage and bot-capability credentials.

Provider-level (no dashboard) + real dashboard app (hermes_cli.web_server.app) in-process.
"""
from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
from test_provider import SCOPE, SETTINGS, IdP, core  # noqa: E402

PLUGIN_DIR = Path(__file__).resolve().parents[1]
CMD_SCOPE = "hermes.helper.command"
SETTINGS_CMD = {**SETTINGS, "command_scope": CMD_SCOPE, "command_profiles": ["helper", "atlas", "coding"]}
VOICE = "/api/plugins/waveshare-sessions/voice"
COMMAND = "/api/plugins/waveshare-sessions/command"
STOP = "/api/plugins/waveshare-sessions/command/stop"
USAGE = "/api/plugins/waveshare-sessions/usage"
BOTS = "/api/plugins/waveshare-sessions/bots"




def at(path):
    class _Ctx:
        def __enter__(self):
            self.t = core.CURRENT_PATH.set(path)

        def __exit__(self, *a):
            core.CURRENT_PATH.reset(self.t)
    return _Ctx()


# ---------------------------------------------------------------------------
# Provider
# ---------------------------------------------------------------------------

def test_token_routes_are_exact_and_complete():
    assert set(core.TOKEN_ROUTES) == {USAGE, BOTS}


@pytest.mark.parametrize("path", [BOTS])
def test_read_token_refused_on_command_routes(path):
    idp = IdP()
    provider = core.build_provider(SETTINGS_CMD, transport=idp.transport())
    with at(path):
        assert provider.verify_token(token=idp.token(scp=[SCOPE])) is None


def test_command_token_refused_on_usage_route():
    idp = IdP()
    provider = core.build_provider(SETTINGS_CMD, transport=idp.transport())
    with at(USAGE):
        assert provider.verify_token(token=idp.token(scp=[CMD_SCOPE])) is None


@pytest.mark.parametrize("path", [BOTS])
def test_command_token_accepted_with_only_that_scope_on_principal(path):
    idp = IdP()
    provider = core.build_provider(SETTINGS_CMD, transport=idp.transport())
    with at(path):
        principal = provider.verify_token(token=idp.token(scp=[SCOPE, CMD_SCOPE]))
    assert principal is not None and principal.scopes == (CMD_SCOPE,)
    with at(USAGE):
        assert provider.verify_token(token=idp.token(scp=[SCOPE, CMD_SCOPE])).scopes == (SCOPE,)


def test_command_routes_closed_when_scope_not_configured():
    idp = IdP()
    provider = core.build_provider(SETTINGS, transport=idp.transport())
    for path in (BOTS,):
        with at(path):
            assert provider.verify_token(token=idp.token(scp=[SCOPE, CMD_SCOPE])) is None


@pytest.mark.parametrize("value", ["openid", "hermes.sessions.read", "bad scope", ""])
def test_command_scope_setting_validated(tmp_path, value):
    path = tmp_path / "s.json"
    path.write_text(json.dumps({**{k: v for k, v in SETTINGS.items() if v}, "command_scope": value}))
    if value == "":
        assert core.load_settings(path)["command_scope"] == ""
    else:
        with pytest.raises(core.SettingsError):
            core.load_settings(path)


# ---------------------------------------------------------------------------
# Real dashboard app
# ---------------------------------------------------------------------------



@pytest.fixture(scope="module")
def wired(tmp_path_factory):
    settings = tmp_path_factory.mktemp("wsh") / "settings.json"
    settings.write_text(json.dumps({k: v for k, v in SETTINGS_CMD.items() if v}))
    import os
    os.environ["WAVESHARE_SESSIONS_SETTINGS"] = str(settings)
    from fastapi.testclient import TestClient
    from hermes_cli import web_server
    from hermes_cli.dashboard_auth import clear_providers
    from hermes_cli.dashboard_auth.registry import register_global_provider
    from hermes_cli.dashboard_auth.token_auth import clear_token_routes, register_token_route

    clear_providers()
    clear_token_routes()
    spec = importlib.util.spec_from_file_location("hermes_dashboard_plugin_waveshare-sessions-h",
                                                  PLUGIN_DIR / "dashboard" / "plugin_api.py")
    api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(api)
    before = list(web_server.app.router.routes)
    web_server.app.include_router(api.router, prefix="/api/plugins/waveshare-sessions")
    added = [r for r in web_server.app.router.routes if r not in before]
    web_server.app.router.routes[:] = added + before
    idp = IdP()
    provider = api.core.build_provider(api.core.load_settings(), transport=idp.transport())
    register_global_provider(provider)
    for route in api.core.TOKEN_ROUTES:
        register_token_route(route)
    state = web_server.app.state
    saved = {k: getattr(state, k, None) for k in ("bound_host", "bound_port", "auth_required")}
    state.bound_host, state.bound_port, state.auth_required = "127.0.0.1", 9119, True
    client = TestClient(web_server.app, base_url="http://127.0.0.1:9119")
    yield {"client": client, "idp": idp}
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


def bearer(token):
    return {"Authorization": f"Bearer {token}"}
















def test_command_token_cannot_read_usage(wired):
    token = wired["idp"].token(scp=[CMD_SCOPE])
    assert wired["client"].get(USAGE, headers=bearer(token)).status_code == 401


@pytest.mark.parametrize("path", [
    "/api/config", "/api/sessions", "/api/env", "/api/gateway/drain", "/api/auth/ws-ticket", "/api/ws",
    "/api/plugins/waveshare-sessions/voice/", "/api/plugins/waveshare-sessions/command/stop/",
    "/api/plugins/waveshare-sessions/command/x", "/api/plugins/kanban/board", "/api/profiles",
])
def test_command_token_refused_everywhere_else(wired, path):
    token = wired["idp"].token(scp=[SCOPE, CMD_SCOPE])
    for method in ("get", "post"):
        r = getattr(wired["client"], method)(path, headers=bearer(token), follow_redirects=False)
        assert r.status_code not in (200, 201, 202, 204), (method, path, r.status_code)




# ---------------------------------------------------------------------------
# Bot capabilities through the real dashboard app
# ---------------------------------------------------------------------------











def test_bots_capability_route(wired):
    token = wired["idp"].token(scp=[CMD_SCOPE])
    r = wired["client"].get(BOTS, headers=bearer(token))
    assert r.status_code == 200 and r.headers["cache-control"] == "no-store"
    assert r.json() == {"version": 1, "bots": ["helper", "atlas", "coding"], "default": "helper"}
    assert wired["client"].get(BOTS + "?x=1", headers=bearer(token)).status_code == 400


def test_bots_capability_is_scope_guarded(wired):
    assert wired["client"].get(BOTS).status_code == 401
    assert wired["client"].get(BOTS, headers=bearer(wired["idp"].token(scp=[SCOPE]))).status_code == 401


@pytest.mark.parametrize("value", [[], ["default"], ["helper", "helper"], "helper", ["../x"], [1],
                                   ["abcdefghijk1"], ["abcdefghijk2"], ["abcdefghijk1", "abcdefghijk2"]])
def test_command_profiles_setting_validated(tmp_path, value):
    path = tmp_path / "s.json"
    path.write_text(json.dumps({**{k: v for k, v in SETTINGS.items() if v}, "command_profiles": value}))
    with pytest.raises(core.SettingsError):
        core.load_settings(path)


def test_command_profiles_maximum_id_is_preserved(tmp_path):
    path = tmp_path / "s.json"
    path.write_text(json.dumps({**{k: v for k, v in SETTINGS.items() if v},
                               "command_profiles": ["abcdefghijk"]}))
    assert core.load_settings(path)["command_profiles"] == ["abcdefghijk"]


def test_command_profiles_defaults_to_capability_bots(tmp_path):
    path = tmp_path / "s.json"
    path.write_text(json.dumps({k: v for k, v in SETTINGS.items() if v}))
    assert core.load_settings(path)["command_profiles"] == ["helper", "atlas", "coding"]
