from tests.support import authorizer
import time
import unittest
from aiohttp.test_utils import TestClient, TestServer
from waveshare_bridge.live_bridge import Sample, make_app

class PeerPolicyTests(unittest.IsolatedAsyncioTestCase):
    async def test_direct_peer_allowlist_not_forwarded_headers(self):
        sample = Sample(b'k' * 32)
        sample.update([])
        sample.auth_expires = time.time() + 60
        for allowed, expected in [({'127.0.0.1'}, 200), ({'192.0.2.1'}, 403), (set(), 403)]:
            async with TestClient(TestServer(make_app(sample, authorizer('a' * 64), allowed_ips=allowed))) as client:
                response = await client.get('/v1/live', headers={
                    'Authorization': 'Bearer ' + 'a' * 64,
                    'X-Forwarded-For': '192.0.2.1', 'Forwarded': 'for=192.0.2.1'})
                self.assertEqual(response.status, expected)
                self.assertEqual(response.headers['Cache-Control'], 'no-store')

    def test_invalid_peer_policy_rejected(self):
        for peers in [{'localhost'}, {'*'}, {'192.0.2.1/24'}]:
            with self.assertRaises(ValueError):
                make_app(Sample(b'k' * 32), authorizer('a' * 64), allowed_ips=peers)
