"""Machine-authenticated session usage and bot capabilities for the Waveshare AI bridge."""
from __future__ import annotations

import importlib.util
import logging
import sys
from pathlib import Path

logger = logging.getLogger("waveshare-sessions")
LAST_SKIP_REASON = ""
_CORE_MODULE = "waveshare_sessions_core"


def load_core():
    module = sys.modules.get(_CORE_MODULE)
    path = Path(__file__).with_name("core.py")
    if module is not None and getattr(module, "__file__", None) == str(path):
        return module
    spec = importlib.util.spec_from_file_location(_CORE_MODULE, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[_CORE_MODULE] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        sys.modules.pop(_CORE_MODULE, None)
        raise
    return module


def register(ctx) -> None:
    global LAST_SKIP_REASON
    LAST_SKIP_REASON = ""
    core = load_core()
    try:
        settings = core.load_settings()
        provider = core.build_provider(settings)
    except Exception as exc:  # noqa: BLE001 — a bad config must never break plugin load
        LAST_SKIP_REASON = f"waveshare-sessions disabled: {exc}"
        logger.warning("%s", LAST_SKIP_REASON)
        return
    ctx.register_dashboard_auth_provider(provider)
    try:
        from hermes_cli.dashboard_auth.token_auth import register_token_route
        register_token_route(core.USAGE_ROUTE)
        if settings.get("command_scope"):
            register_token_route(core.BOTS_ROUTE)
    except Exception as exc:  # noqa: BLE001
        LAST_SKIP_REASON = f"could not register token route: {exc}"
        logger.warning("waveshare-sessions: %s", LAST_SKIP_REASON)
        return
    logger.info("waveshare-sessions: token routes %s",
                ",".join(core.TOKEN_ROUTES if settings.get("command_scope") else (core.USAGE_ROUTE,)))
