import unittest
from waveshare_bridge.live_bridge import UsageGateway


class ProviderFreshnessTests(unittest.TestCase):
    def test_provider_survives_roster_only_polls_but_not_missing_expired_or_removed_evidence(self):
        g = UsageGateway('http://127.0.0.1:8000', None, None, None)
        def rows(provider=None, sid='a', status='working'):
            return [{'id': sid, 'status': status, 'provider': provider}]
        self.assertEqual(g.provider_rows(rows('anthropic'), True, 10)[0]['provider'], 'anthropic')
        self.assertEqual(g.provider_rows(rows(), False, 11)[0]['provider'], 'anthropic')
        self.assertIsNone(g.provider_rows(rows(), False, 16)[0]['provider'])
        g.provider_rows(rows('openai-codex'), True, 20)
        self.assertIsNone(g.provider_rows(rows(), True, 21)[0]['provider'])
        g.provider_rows(rows('anthropic'), True, 30)
        g.provider_rows([], False, 31)
        self.assertIsNone(g.provider_rows(rows(), False, 32)[0]['provider'])
        g.provider_rows(rows('anthropic'), True, 40)
        self.assertIsNone(g.provider_rows(rows(sid='b'), False, 41)[0]['provider'])
        self.assertIsNone(g.provider_rows(rows(status='idle'), False, 42)[0]['provider'])
