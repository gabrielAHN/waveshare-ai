"""WLS4 lists every open session; the working bit drives each glow and density stays global."""
import struct
import unittest

from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import live_bridge as bridge
from tests.support import authorizer

KEY = b'k' * 32
ROWS = [{'id': 'a1', 'status': 'working', 'provider': 'anthropic'},
        {'id': 'b2', 'status': 'idle', 'provider': 'openrouter'},
        {'id': 'c3', 'status': 'waiting', 'provider': None},
        {'id': 'd4', 'status': 'starting', 'provider': 'openai-codex'}]


def decode4(body):
    assert body[:4] == b'WLS4'
    count, level, flags = struct.unpack_from('<HBB', body, 4)
    assert len(body) == 8 + 10 * count
    rows = [struct.unpack_from('<QBB', body, 8 + 10 * i) for i in range(count)]
    return level, flags, rows


class Wls4Tests(unittest.TestCase):
    def test_lists_every_open_session_with_its_working_bit(self):
        body = bridge.encode_sessions(ROWS, KEY, 3, bridge.FLAG_MEASURED)
        level, flags, rows = decode4(body)
        self.assertEqual((level, flags, len(rows)), (3, bridge.FLAG_MEASURED, 4))
        ids = [r[0] for r in rows]
        self.assertEqual(ids, sorted(ids))
        working = bridge.encode_sessions([ROWS[0]], KEY, 3, bridge.FLAG_MEASURED)
        working_id = struct.unpack_from('<Q', working, 8)[0]
        self.assertEqual({r[0]: r[2] for r in rows}[working_id], 1)
        self.assertEqual(sum(r[2] for r in rows), 1)
        self.assertEqual({r[2] for r in rows}, {0, 1})
        self.assertIn(bridge.PROVIDER_CODES['anthropic'], [r[1] for r in rows])

    def test_idle_only_roster_keeps_the_glows_and_level_zero(self):
        idle = [dict(r, status='idle') for r in ROWS]
        level, flags, rows = decode4(bridge.encode_sessions(idle, KEY, 4, bridge.FLAG_MEASURED))
        self.assertEqual((level, len(rows)), (0, 4))
        self.assertFalse(any(r[2] for r in rows))
        self.assertEqual(bridge.encode_sessions([], KEY, 5, 1), b'WLS4\0\0\0\0')



class LiveFormatEndpointTests(unittest.IsolatedAsyncioTestCase):
    async def test_live_always_returns_wls4_ignoring_format_header(self):
        token = 'c' * 64
        sample = bridge.Sample(KEY)
        self.assertTrue(sample.update(ROWS))
        app = bridge.make_app(sample, authorizer(token))
        client = TestClient(TestServer(app))
        await client.start_server()
        try:
            auth = {'Authorization': 'Bearer ' + token}
            old = await client.get('/v1/live', headers=auth)
            self.assertEqual(old.status, 200)
            self.assertEqual((await old.read())[:4], b'WLS4')
            new = await client.get('/v1/live', headers={**auth, 'X-Live-Format': '4'})
            self.assertEqual(new.status, 200)
            level, flags, rows = decode4(await new.read())
            self.assertEqual(len(rows), 4)
            odd = await client.get('/v1/live', headers={**auth, 'X-Live-Format': '9'})
            self.assertEqual((await odd.read())[:4], b'WLS4')
        finally:
            await client.close()


if __name__ == '__main__':
    unittest.main()
