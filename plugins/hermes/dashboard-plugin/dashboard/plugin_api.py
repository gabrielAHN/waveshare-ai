"""Token-only /usage and /bots routes, pinned to their dedicated machine scopes."""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

from fastapi import APIRouter, HTTPException, Query, Request
from fastapi.responses import JSONResponse


def _load(name: str, filename: str):
    module = sys.modules.get(name)
    path = Path(__file__).resolve().parents[1] / filename
    if module is not None and getattr(module, "__file__", None) == str(path):
        return module
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


core = _load("waveshare_sessions_core", "core.py")
_NO_STORE = {"Cache-Control": "no-store"}
router = APIRouter()


def _install_path_context() -> None:
    try:
        from hermes_cli.web_server import app
    except Exception:  # noqa: BLE001 — imported outside the dashboard (tests)
        return
    core.install_path_context(app)


_install_path_context()


def _json(body, status=200):
    return JSONResponse(body, status_code=status, headers=_NO_STORE)


@router.get("/usage")
def usage(request: Request, tokens: int = Query(0, ge=0, le=1)):
    if not core.principal_has_scope(request, core.load_settings()["scope"]):
        raise HTTPException(status_code=403, detail="Forbidden")
    try:
        body = core.usage_snapshot(include_tokens=bool(tokens))
    except Exception:  # noqa: BLE001 — never leak internals
        return _json({"detail": "sessions unavailable"}, 503)
    return _json(body)


@router.get("/bots")
def bots(request: Request):
    settings = core.load_settings()
    if not core.principal_has_scope(request, settings.get("command_scope", "")):
        raise HTTPException(status_code=403, detail="Forbidden")
    if request.query_params:
        return _json({"detail": "no parameters accepted"}, 400)
    profiles = settings["command_profiles"]
    return _json({"version": 1, "bots": profiles, "default": profiles[0]})
