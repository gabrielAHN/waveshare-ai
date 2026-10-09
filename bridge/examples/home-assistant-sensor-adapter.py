#!/usr/bin/env python3
"""Loopback Home Assistant state adapter for the Waveshare AI Sensor contract."""

import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


PATH = "/api/waveshare-sensors"
KEYS = ("temperature", "humidity", "pressure", "pm1", "pm25", "pm10")
ENTITY = re.compile(r"[a-z0-9_]+\.[a-z0-9_]+")
MAX_UPSTREAM_BYTES = 65536
MAX_STATE_BYTES = 256


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, new_url):
        return None


OPENER = urllib.request.build_opener(NoRedirectHandler)


def settings():
    base = os.environ.get("HOME_ASSISTANT_URL", "").rstrip("/")
    token = os.environ.get("HOME_ASSISTANT_TOKEN", "")
    entities = {key: os.environ.get(f"SENSOR_{key.upper()}", "") for key in KEYS}
    if not base.startswith("https://") or not token or not all(ENTITY.fullmatch(v) for v in entities.values()):
        raise SystemExit("set HTTPS HOME_ASSISTANT_URL, HOME_ASSISTANT_TOKEN and all six SENSOR_* entity ids")
    return base, token, entities


BASE, TOKEN, ENTITIES = settings()


def state(entity_id):
    url = f"{BASE}/api/states/{urllib.parse.quote(entity_id, safe='.') }"
    request = urllib.request.Request(
        url,
        headers={"Authorization": f"Bearer {TOKEN}", "Accept": "application/json"},
    )
    with OPENER.open(request, timeout=5) as response:
        raw_body = response.read(MAX_UPSTREAM_BYTES + 1)
    if len(raw_body) > MAX_UPSTREAM_BYTES:
        raise ValueError("Home Assistant state response exceeds byte limit")
    body = json.loads(raw_body)
    if not isinstance(body, dict):
        raise ValueError("Home Assistant state response must be a JSON object")
    value = body.get("state")
    if not isinstance(value, str):
        return "unavailable"
    if len(value.encode("utf-8")) > MAX_STATE_BYTES:
        raise ValueError("Home Assistant state value exceeds byte limit")
    return value


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != PATH:
            self.send_error(404)
            return
        try:
            sensors = {key: {"state": state(entity)} for key, entity in ENTITIES.items()}
            payload = json.dumps({"now": int(time.time()), "sensors": sensors}, separators=(",", ":")).encode()
        except (OSError, ValueError, urllib.error.URLError, json.JSONDecodeError):
            self.send_error(502, "Home Assistant unavailable")
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8099), Handler).serve_forever()
