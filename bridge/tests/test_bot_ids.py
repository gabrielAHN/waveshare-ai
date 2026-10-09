"""Waveshare AI profile ids must fit WBT1 without changing their literal value."""
import pathlib
import tempfile
import unittest

from waveshare_bridge import bots, config

MAX_ID = 'abcdefghijk'
COLLIDING_IDS = ('abcdefghijk1', 'abcdefghijk2')


class Capability:
    def __init__(self, ids):
        self.ids = list(ids)

    async def bots(self):
        return 200, {'version': 1, 'bots': self.ids}


class TransportIdTests(unittest.TestCase):
    def test_sdk_config_rejects_ids_that_cannot_fit_the_bot_frame(self):
        for bot in (*COLLIDING_IDS, 'default', ' helper', 'helper '):
            with self.subTest(bot=bot), self.assertRaisesRegex(ValueError, 'profile'):
                config.gadget_gateway({'gadget_sdk': {'port': 8768, 'profiles': {bot: 8775}}})

    def test_sdk_request_rejects_ids_that_cannot_fit_the_bot_frame(self):
        from waveshare_bridge import gadget_front
        from tests.test_gadget_front import head
        for bot in (*COLLIDING_IDS, 'default'):
            with self.subTest(bot=bot), self.assertRaises(ValueError):
                gadget_front.parse_head(head(path='/gadget/' + bot))

    def test_sdk_setup_rejects_twelve_character_ids_without_running_commands(self):
        from waveshare_bridge import cli
        for bot in COLLIDING_IDS:
            with self.subTest(bot=bot), self.assertRaises(ValueError):
                cli._parse_bots([bot + '=8775'], 8768)

    def test_maximum_identifier_is_preserved_in_sdk_config_setup_and_request(self):
        from waveshare_bridge import cli, gadget_front
        from tests.test_gadget_front import head
        value = {'gadget_sdk': {'port': 8768, 'profiles': {MAX_ID: 8775}}}
        expected = {'port': 8768, 'upstream_host': '127.0.0.1', 'profiles': {MAX_ID: 8775}}
        self.assertEqual(config.gadget_gateway(value), expected)
        self.assertEqual(cli._parse_bots([MAX_ID + '=8775'], 8768), expected)
        self.assertEqual(gadget_front.parse_head(head(path='/gadget/' + MAX_ID)).bot, MAX_ID)


class EncoderTests(unittest.TestCase):
    def row(self, bot):
        return {'id': bot, 'name': 'Bot', 'provider_label': '', 'available': True,
                'reason': 'none', 'reset_in_s': None, 'stale': False}

    def test_encoder_rejects_ids_instead_of_truncating_or_repairing(self):
        for bot in (*COLLIDING_IDS, ' helper', 'helper ', 'hé lper', 'default', '', None):
            with self.subTest(bot=bot), self.assertRaisesRegex(ValueError, 'bot id'):
                bots.encode_frame([self.row(bot)], 0)
        with self.assertRaisesRegex(ValueError, 'bot id'):
            bots.encode_frame([self.row(bot) for bot in COLLIDING_IDS], 0)

    def test_maximum_identifier_round_trip_retains_terminator_and_layout(self):
        frame = bots.encode_frame([self.row(MAX_ID)], 0)
        self.assertEqual(frame[8:20], MAX_ID.encode('ascii') + b'\0')
        self.assertEqual(len(frame), 144)
        self.assertEqual(bots.decode_frame(frame)['bots'][0]['id'], MAX_ID)


class AdmissionTests(unittest.IsolatedAsyncioTestCase):
    def test_config_and_source_reject_twelve_character_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for ids in ([COLLIDING_IDS[0]], [COLLIDING_IDS[1]], list(COLLIDING_IDS)):
                with self.subTest(boundary='config', ids=ids), self.assertRaises(ValueError):
                    config.parse({'bind': '10.0.0.2', 'providers': {'hermes': {
                        'bots': ids, 'profiles_dir': str(root / 'profiles'), 'quota_cache': False}}}, root)
                with self.subTest(boundary='source', ids=ids), self.assertRaises(ValueError):
                    bots.BotSource(None, root / 'profiles', bots=ids)

    async def test_capability_rejects_twelve_character_ids(self):
        for ids in ([COLLIDING_IDS[0]], [COLLIDING_IDS[1]], list(COLLIDING_IDS)):
            with self.subTest(ids=ids):
                probe = bots.PluginProbe(Capability(ids), log=lambda *_: None)
                self.assertIsNone(await probe.refresh())
                self.assertTrue(all(not probe.routable(bot) for bot in ids))
                await probe.close()

    async def test_maximum_id_is_preserved_by_config_source_and_capability(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            cfg = config.parse({'bind': '10.0.0.2', 'providers': {'hermes': {
                'bots': [MAX_ID], 'profiles_dir': str(root / 'profiles'), 'quota_cache': False}}}, root)
            self.assertEqual(cfg['bots'], [MAX_ID])
            probe = bots.PluginProbe(Capability([MAX_ID]), log=lambda *_: None)
            self.assertEqual(await probe.refresh(), (MAX_ID,))
            source = bots.BotSource(None, cfg['profiles_dir'], bots=cfg['bots'], probe=probe, log=lambda *_: None)
            self.assertEqual(source.snapshot()['bots'][0]['id'], MAX_ID)
            self.assertTrue(probe.routable(MAX_ID))
            await source.close()
