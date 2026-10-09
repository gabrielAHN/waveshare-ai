"""Authelia machine-credential verification + read-only session usage for the Waveshare bridge.

Loaded under the fixed module name ``waveshare_sessions_core`` (see ``load_core`` in
``__init__.py`` / ``dashboard/plugin_api.py``) so the plugin entry point and the dashboard
API module share ONE ContextVar / provider instance.

Security model
--------------
* The bridge authenticates with an Authelia OAuth2 ``client_credentials`` access token
  (RFC 9068 JWT, RS256). There is no user subject: the credential is a machine client
  declared in Authelia's configuration by the admin.
* :class:`AutheliaClientTokenProvider` is token-only (``supports_session = False``) and
  vouches for a bearer ONLY when the current request path is the exact registered route
  (``USAGE_ROUTE`` or ``BOTS_ROUTE``). The dashboard's token seam tries every token provider on every
  token route (e.g. ``/api/gateway/drain`` when the drain secret is set), so without the
  path pin this read-only credential could authorise drain control.
* Verification pins: algorithm (RS256), ``iss``, ``aud``, ``client_id``, required scope,
  ``exp``/``iat`` required, small leeway. JWKS is fetched from Authelia and cached; a JWKS
  outage raises ``ProviderError`` (the seam answers 503, never 200).
"""
from __future__ import annotations

import contextvars
import json
import logging
import os
import re
import ssl
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional

logger = logging.getLogger("waveshare-sessions")

PLUGIN_NAME = "waveshare-sessions"
USAGE_ROUTE = "/api/plugins/waveshare-sessions/usage"
BOTS_ROUTE = "/api/plugins/waveshare-sessions/bots"
TOKEN_ROUTES = (USAGE_ROUTE, BOTS_ROUTE)
PROVIDER_NAME = "waveshare-sessions-authelia"
ALLOWED_ALGS = ("RS256",)
JWKS_TTL_SECONDS = 300.0
JWKS_MISS_REFETCH_SECONDS = 30.0
JWKS_TIMEOUT_SECONDS = 5.0
LEEWAY_SECONDS = 30
MAX_TOKEN_CHARS = 8192
MAX_SESSIONS = 64
MAX_STATUS_TEXT = 65536
MAX_TOKENS = 10 ** 15
MAX_PROVIDER = 64
_TOKENS_LINE = re.compile(r"Tokens: ([0-9]{1,3}(?:,[0-9]{3})*|[0-9]{1,16})")
_MODEL_ROUTE_LINE = re.compile(rf"Model: .+ \(([A-Za-z0-9_-]{{1,{MAX_PROVIDER}}})\)")

# Path of the request currently being authenticated (set by PathContextMiddleware).
CURRENT_PATH: contextvars.ContextVar[Optional[str]] = contextvars.ContextVar(
    "waveshare_sessions_current_path", default=None)


class SettingsError(ValueError):
    pass


def _provider_error(message: str) -> Exception:
    from hermes_cli.dashboard_auth.base import ProviderError
    return ProviderError(message)


# ---------------------------------------------------------------------------
# Settings (non-secret): issuer, audience, client_id, scope, JWKS location.
# ---------------------------------------------------------------------------

def settings_path() -> Path:
    override = os.environ.get("WAVESHARE_SESSIONS_SETTINGS", "").strip()
    return Path(override).expanduser() if override else Path(__file__).with_name("settings.json")


def _https_or_loopback(url: str, field: str) -> str:
    from urllib.parse import urlsplit
    parts = urlsplit(url)
    if parts.username or parts.password or parts.fragment or not parts.hostname:
        raise SettingsError(f"{field} must be an absolute URL without credentials")
    if parts.scheme == "https":
        return url
    if parts.scheme == "http" and parts.hostname in ("127.0.0.1", "localhost", "::1"):
        return url
    raise SettingsError(f"{field} must be https:// or http:// on loopback")


def load_settings(path: Optional[Path] = None) -> Dict[str, Any]:
    path = path or settings_path()
    try:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise SettingsError(f"settings file not found: {path}") from None
    except (OSError, ValueError) as exc:
        raise SettingsError(f"settings file unreadable: {exc}") from None
    if not isinstance(raw, dict):
        raise SettingsError("settings must be a JSON object")

    def text(key: str, required: bool = True) -> str:
        value = raw.get(key, "")
        if not isinstance(value, str) or (required and not value.strip()):
            raise SettingsError(f"settings.{key} must be a non-empty string")
        return value.strip()

    issuer = text("issuer").rstrip("/")
    _https_or_loopback(issuer, "issuer")
    jwks_uri = text("jwks_uri", required=False) or issuer + "/jwks.json"
    _https_or_loopback(jwks_uri, "jwks_uri")
    host_header = text("jwks_host_header", required=False)
    if host_header and not re.fullmatch(r"[A-Za-z0-9.-]{1,253}(:[0-9]{1,5})?", host_header):
        raise SettingsError("settings.jwks_host_header must be a bare host[:port]")
    ca_file = text("ca_file", required=False)
    scope = text("scope")
    if scope in ("openid", "offline_access") or not re.fullmatch(r"[A-Za-z0-9._:-]{1,128}", scope):
        raise SettingsError("settings.scope must be a dedicated custom scope")
    # Separate scope for bot capabilities; empty keeps /bots closed.
    command_scope = text("command_scope", required=False)
    if command_scope and (command_scope in ("openid", "offline_access", scope)
                          or not re.fullmatch(r"[A-Za-z0-9._:-]{1,128}", command_scope)):
        raise SettingsError("settings.command_scope must be a separate dedicated custom scope")
    # Bot capability ids fit WBT1's 11 characters plus NUL. Never repair an id.
    profiles = raw.get("command_profiles", ["helper", "atlas", "coding"])
    if (not isinstance(profiles, list) or not 1 <= len(profiles) <= 8 or len(set(map(str, profiles))) != len(profiles)
            or not all(type(p) is str and re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,10}", p) and p != "default"
                       for p in profiles)):
        raise SettingsError("settings.command_profiles must be 1..8 Hermes profile ids of 1..11 characters (not 'default')")
    return {
        "command_profiles": list(profiles),
        "issuer": issuer,
        "audience": text("audience"),
        "client_id": text("client_id"),
        "scope": scope,
        "command_scope": command_scope,
        "jwks_uri": jwks_uri,
        "jwks_host_header": host_header,
        "ca_file": ca_file,
    }


# ---------------------------------------------------------------------------
# JWKS cache
# ---------------------------------------------------------------------------

class JwksCache:
    """Fetch + cache the IdP's signing keys. ``transport`` is injectable for tests."""

    def __init__(self, uri: str, *, host_header: str = "", ca_file: str = "", transport=None,
                 clock=time.monotonic) -> None:
        self.uri, self.host_header, self.ca_file = uri, host_header, ca_file
        self.transport, self.clock = transport, clock
        self._keys: Dict[str, Any] = {}
        self._fetched_at: Optional[float] = None
        self._last_miss_refetch = float("-inf")
        self._lock = threading.Lock()

    def _fetch(self) -> Dict[str, Any]:
        import httpx
        import jwt
        headers = {"Accept": "application/json", "User-Agent": "HermesWaveshareSessions/1.0"}
        if self.host_header:
            headers["Host"] = self.host_header
        kwargs: Dict[str, Any] = {"timeout": JWKS_TIMEOUT_SECONDS, "follow_redirects": False,
                                  "trust_env": False}
        if self.transport is not None:
            kwargs["transport"] = self.transport
        elif self.ca_file:
            kwargs["verify"] = ssl.create_default_context(cafile=self.ca_file)
        try:
            with httpx.Client(**kwargs) as client:
                response = client.get(self.uri, headers=headers)
        except httpx.HTTPError as exc:
            raise _provider_error(f"Authelia JWKS unreachable: {type(exc).__name__}") from None
        if response.status_code != 200 or len(response.content) > 262144:
            raise _provider_error(f"Authelia JWKS returned HTTP {response.status_code}")
        try:
            document = response.json()
            entries = document["keys"]
            if not isinstance(entries, list):
                raise TypeError("keys")
        except (ValueError, KeyError, TypeError):
            raise _provider_error("Authelia JWKS returned a malformed document") from None
        keys: Dict[str, Any] = {}
        for entry in entries:
            if not isinstance(entry, dict) or entry.get("use", "sig") != "sig":
                continue
            kid, alg, kty = entry.get("kid"), entry.get("alg"), entry.get("kty")
            if not isinstance(kid, str) or kty != "RSA" or alg not in (None, *ALLOWED_ALGS):
                continue
            try:
                keys[kid] = jwt.PyJWK(entry, algorithm="RS256").key
            except Exception:  # noqa: BLE001 — skip unusable keys, never trust them
                continue
        return keys

    def get(self, kid: str) -> Optional[Any]:
        """Signing key for ``kid``; ``None`` when the (fresh) JWKS lacks it. Raises ProviderError
        when the JWKS must be fetched and cannot be."""
        with self._lock:
            now = self.clock()
            if self._fetched_at is None or now - self._fetched_at >= JWKS_TTL_SECONDS:
                self._keys = self._fetch()
                self._fetched_at = now
            elif kid not in self._keys and now - self._last_miss_refetch >= JWKS_MISS_REFETCH_SECONDS:
                self._last_miss_refetch = now  # key rotation: at most one refetch per window
                self._keys = self._fetch()
                self._fetched_at = now
            return self._keys.get(kid)


# ---------------------------------------------------------------------------
# Token provider
# ---------------------------------------------------------------------------

def _base_classes():
    from hermes_cli.dashboard_auth import DashboardAuthProvider
    return (DashboardAuthProvider,)


def build_provider(settings: Dict[str, Any], *, transport=None, clock=time.time, jwks_clock=time.monotonic):
    """Construct the provider (class built lazily so this module imports without hermes_cli)."""
    from hermes_cli.dashboard_auth import TokenPrincipal

    class AutheliaClientTokenProvider(*_base_classes()):
        name = PROVIDER_NAME
        display_name = "Waveshare session bridge (Authelia client credential)"
        supports_token = True
        supports_session = False
        _NOT_INTERACTIVE = "Machine credential only; there is no interactive login."

        def __init__(self) -> None:
            self.settings = dict(settings)
            self.jwks = JwksCache(settings["jwks_uri"], host_header=settings.get("jwks_host_header", ""),
                                  ca_file=settings.get("ca_file", ""), transport=transport, clock=jwks_clock)
            self.clock = clock

        # -- token capability ------------------------------------------------
        def verify_token(self, *, token: str):
            # Each exact route requires its OWN scope; the principal carries only that scope.
            # Any other path (every other token route included) is never vouched for.
            path = CURRENT_PATH.get()
            if path == USAGE_ROUTE:
                required = self.settings["scope"]
            elif path == BOTS_ROUTE and self.settings.get("command_scope"):
                required = self.settings["command_scope"]
            else:
                return None
            claims = self.claims(token, required)
            if claims is None:
                return None
            return TokenPrincipal(principal=f"authelia-client:{self.settings['client_id']}",
                                  provider=self.name, scopes=(required,))

        def claims(self, token: str, required: Optional[str] = None) -> Optional[Dict[str, Any]]:
            import jwt
            if not isinstance(token, str) or not 20 <= len(token) <= MAX_TOKEN_CHARS or token.count(".") != 2:
                return None
            try:
                header = jwt.get_unverified_header(token)
            except jwt.InvalidTokenError:
                return None
            kid = header.get("kid")
            if header.get("alg") not in ALLOWED_ALGS or not isinstance(kid, str) or not kid:
                return None
            if header.get("typ") not in (None, "at+jwt", "JWT", "application/at+jwt"):
                return None
            key = self.jwks.get(kid)  # ProviderError propagates -> seam answers 503
            if key is None:
                return None
            s = self.settings
            try:
                claims = jwt.decode(
                    token, key, algorithms=list(ALLOWED_ALGS), audience=s["audience"], issuer=s["issuer"],
                    leeway=LEEWAY_SECONDS, options={"require": ["exp", "iat", "iss", "aud"]})
            except jwt.InvalidTokenError:
                return None
            now = self.clock()
            if not isinstance(claims.get("iat"), (int, float)) or claims["iat"] > now + LEEWAY_SECONDS:
                return None
            if claims.get("client_id") != s["client_id"]:
                return None
            granted = claims.get("scp")
            if isinstance(granted, str):
                granted = granted.split()
            if not isinstance(granted, list):
                scope_text = claims.get("scope")
                granted = scope_text.split() if isinstance(scope_text, str) else []
            if (required or s["scope"]) not in granted:
                return None
            return claims

        # -- interactive surface: unsupported ------------------------------------
        def start_login(self, *, redirect_uri: str):
            raise NotImplementedError(self._NOT_INTERACTIVE)

        def complete_login(self, *, code: str, state: str, code_verifier: str, redirect_uri: str):
            raise NotImplementedError(self._NOT_INTERACTIVE)

        def verify_session(self, *, access_token: str):
            return None

        def refresh_session(self, *, refresh_token: str):
            raise NotImplementedError(self._NOT_INTERACTIVE)

        def revoke_session(self, *, refresh_token: str) -> None:
            return None

    return AutheliaClientTokenProvider()


# ---------------------------------------------------------------------------
# Path context (pure ASGI, outermost) so verify_token knows which route it guards.
# ---------------------------------------------------------------------------

def principal_has_scope(request, scope: str) -> bool:
    principal = getattr(request.state, "token_principal", None)
    return bool(scope and getattr(request.state, "token_authenticated", False) and principal is not None
                and getattr(principal, "provider", None) == PROVIDER_NAME
                and scope in tuple(getattr(principal, "scopes", ()) or ()))


class PathContextMiddleware:
    def __init__(self, app) -> None:
        self.app = app

    async def __call__(self, scope, receive, send):
        if scope.get("type") != "http":
            await self.app(scope, receive, send)
            return
        from starlette.requests import Request
        token = CURRENT_PATH.set(Request(scope).url.path)
        try:
            await self.app(scope, receive, send)
        finally:
            CURRENT_PATH.reset(token)


def install_path_context(app) -> bool:
    """Add the middleware once. False when the app already started (fail closed: the provider
    then never sees USAGE_ROUTE and rejects every token)."""
    global CURRENT_PATH
    shared = getattr(app.state, "waveshare_sessions_path_var", None)
    if isinstance(shared, contextvars.ContextVar):
        CURRENT_PATH = shared
        return True
    try:
        app.add_middleware(PathContextMiddleware)
    except RuntimeError as exc:
        logger.warning("waveshare-sessions: cannot install path context (%s); token route stays closed", exc)
        return False
    # Keep the ContextVar itself on the app. This also joins separately loaded
    # copies of a user plugin (test/source vs installed path) to one request context.
    app.state.waveshare_sessions_path_var = CURRENT_PATH
    app.state.waveshare_sessions_path_ctx = True
    return True


# ---------------------------------------------------------------------------
# Session usage snapshot (read-only in-process RPCs)
# ---------------------------------------------------------------------------

def parse_status_tokens(output: Any) -> Optional[int]:
    if not isinstance(output, str) or len(output) > MAX_STATUS_TEXT:
        return None
    lines = output.splitlines()
    if len(lines) < 2 or lines[-1] not in ("Agent Running: Yes", "Agent Running: No"):
        return None
    match = _TOKENS_LINE.fullmatch(lines[-2])
    if not match:
        return None
    value = int(match.group(1).replace(",", ""))
    return value if value <= MAX_TOKENS else None


def parse_status_provider(output: Any) -> Optional[str]:
    """Provider from the canonical ``Model: model (provider)`` status field.

    The TUI status renderer obtains this value from the live agent/metadata mirror.  A unique,
    bounded, single-line field is required so injected labels or malformed output fail neutral.
    """
    if not isinstance(output, str) or len(output) > MAX_STATUS_TEXT:
        return None
    matches = [_MODEL_ROUTE_LINE.fullmatch(line) for line in output.splitlines() if line.startswith("Model: ")]
    if len(matches) != 1 or matches[0] is None:
        return None
    provider = matches[0].group(1)
    return provider if provider != "unknown" else None


def session_hash(sid: str) -> str:
    import hashlib
    return hashlib.sha256(b"waveshare-sessions\0" + sid.encode("utf-8")).hexdigest()[:24]


def _default_rpc(method: str, params: dict) -> Optional[dict]:
    if method not in ("session.active_list", "session.status"):
        raise ValueError("method not allowed")  # never session.usage (provider network calls)
    from tui_gateway import server
    response = server.handle_request({"jsonrpc": "2.0", "id": "waveshare-sessions", "method": method,
                                      "params": params})
    if not isinstance(response, dict) or not isinstance(response.get("result"), dict):
        return None
    return response["result"]


def usage_snapshot(*, include_tokens: bool, rpc=_default_rpc) -> Dict[str, Any]:
    """Pseudonymous roster + (optionally) lifetime token totals of WORKING sessions only.
    No titles, previews, models, paths or message content leave this function."""
    listed = rpc("session.active_list", {})
    rows = listed.get("sessions") if isinstance(listed, dict) else None
    if not isinstance(rows, list):
        raise RuntimeError("session roster unavailable")
    sessions = []
    for row in rows[:MAX_SESSIONS]:
        if not isinstance(row, dict):
            continue
        sid, status = row.get("id"), row.get("status")
        if not isinstance(sid, str) or not sid or status not in ("working", "idle", "waiting", "starting"):
            continue
        item: Dict[str, Any] = {"id": session_hash(sid), "status": status,
                                "working": status == "working", "provider": None}
        if include_tokens and status == "working":
            status_result = rpc("session.status", {"session_id": sid})
            output = (status_result or {}).get("output")
            item["tokens"] = parse_status_tokens(output)
            item["provider"] = parse_status_provider(output)
        sessions.append(item)
    working = [s for s in sessions if s["working"]]
    known = [s["tokens"] for s in working if isinstance(s.get("tokens"), int)]
    return {
        "version": 1,
        "generated_at": int(time.time()),
        "sessions": sessions,
        "aggregate": {
            "sessions": len(sessions),
            "working": len(working),
            "tokens_included": bool(include_tokens),
            "working_tokens_total": sum(known) if include_tokens else None,
            "working_tokens_unknown": (len(working) - len(known)) if include_tokens else None,
        },
    }
