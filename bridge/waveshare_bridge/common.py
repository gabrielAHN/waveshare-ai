"""Private-file reads and bounded, no-redirect HTTP helpers."""
import json
import os
import stat
from urllib.parse import urlsplit
import aiohttp

class Unavailable(Exception):
    """Deliberately contains no upstream payload or credentials."""


class AuthUnavailable(Unavailable):
    pass


def parse_json(data):
    def pairs(items):
        result = {}
        seen = set()
        for key, value in items:
            if key in seen:
                raise ValueError('duplicate JSON key')
            seen.add(key)
            if key not in ('title', 'preview'):
                result[key] = value
        return result
    def invalid_constant(_):
        raise ValueError('invalid JSON constant')
    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid_constant)


def local_base(raw):
    parsed = urlsplit(raw)
    if (parsed.scheme != 'http' or parsed.hostname != '127.0.0.1' or not parsed.port
            or parsed.username or parsed.password or parsed.path not in ('', '/')
            or parsed.query or parsed.fragment):
        raise ValueError('gateway must be explicit http://127.0.0.1:PORT')
    return raw.rstrip('/')


def new_client():
    async def reject_redirect(session, context, params):
        params.response.close()
        raise Unavailable()
    trace = aiohttp.TraceConfig()
    trace.on_request_redirect.append(reject_redirect)
    return aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=3),
        trust_env=False, cookie_jar=aiohttp.DummyCookieJar(), auto_decompress=False,
        max_line_size=4096, max_field_size=4096, trace_configs=[trace])


def read_private(path, limit=4096):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as f:
        info = os.fstat(f.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('secret file must be owned regular file mode 0600')
        data = f.read(limit + 1)
        if len(data) > limit:
            raise ValueError('secret file too large')
        return data
