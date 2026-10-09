import io
import unittest
from contextlib import redirect_stdout

from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import live_bridge
from tests.support import TOKEN, live_sample, authorizer

SLEEP = 'slept_s=1834 cycles=7336 awake_ms=412 wake=pwr vbus=0 vbat0=3871 vbat1=3866 pct0=78 pct1=77'


class BoardBootTest(unittest.TestCase):
    def setUp(self):
        live_bridge._last_boot[0] = None

    def test_logs_each_boot_once(self):
        text = 'boot=0a1b2c3d rst=brownout off=vsys_uv on=pwrkey vbus=0 bat=1 vbat=3712 pct=64'
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(live_bridge._note_board_boot([text]), text)
            live_bridge._note_board_boot([text])
        self.assertEqual(out.getvalue(), 'Board boot: ' + text + '\n')
        with redirect_stdout(out):
            live_bridge._note_board_boot(['boot=ffffffff rst=poweron off=none on=vbus vbus=1 bat=0 vbat=-1 pct=-1'])
        self.assertEqual(out.getvalue().count('Board boot:'), 2)

    def test_rejects_untrusted_text(self):
        bad = [[], ['a', 'b'], ['boot=xyz'], ['boot=0a1b2c3d rst=x\nInjected: y'], ['boot=0a1b2c3d ' + 'rst=a ' * 40],
               ['boot=0a1b2c3d token=Bearer/abc'], ['rst=panic']]
        out = io.StringIO()
        with redirect_stdout(out):
            for values in bad:
                self.assertIsNone(live_bridge._note_board_boot(values))
        self.assertEqual(out.getvalue(), '')


class BoardSleepTest(unittest.TestCase):
    """X-Board-Sleep (SPEC Contract B 4): strict full match, one line per distinct value per board."""

    def setUp(self):
        live_bridge._last_sleep.clear()

    def test_logs_each_distinct_value_once_per_board(self):
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(live_bridge._note_board_sleep([SLEEP], 'ab6f0001'), SLEEP)
            live_bridge._note_board_sleep([SLEEP], 'ab6f0001')
            live_bridge._note_board_sleep([SLEEP], 'cd120002')
            unknown = 'slept_s=0 cycles=0 awake_ms=4294967295 wake=test vbus=1 vbat0=-1 vbat1=-1 pct0=-1 pct1=-1'
            live_bridge._note_board_sleep([unknown], 'ab6f0001')
            live_bridge._note_board_sleep([SLEEP], 'ab6f0001')
        self.assertEqual(out.getvalue().splitlines(), [
            'Board sleep: ab6f0001 ' + SLEEP, 'Board sleep: cd120002 ' + SLEEP,
            'Board sleep: ab6f0001 ' + unknown, 'Board sleep: ab6f0001 ' + SLEEP])
        for wake in ('boot', 'auto', 'usb'):
            with redirect_stdout(io.StringIO()):
                self.assertIsNotNone(live_bridge._note_board_sleep([SLEEP.replace('wake=pwr', 'wake=' + wake)], 'x'))

    def test_anything_else_is_ignored_silently(self):
        bad = [[], [SLEEP, SLEEP], [SLEEP + ' '], [' ' + SLEEP], [SLEEP.replace(' ', '  ', 1)],
               [SLEEP.replace('wake=pwr', 'wake=power')], [SLEEP.replace('vbus=0', 'vbus=2')],
               [SLEEP.replace('slept_s=1834', 'slept_s=4294967296')], [SLEEP.replace('slept_s=1834', 'slept_s=01834')],
               [SLEEP.replace('vbat0=3871', 'vbat0=-2')], [SLEEP.replace('pct0=78', 'pct0=1000')],
               [SLEEP.replace('cycles=7336 ', '')], [SLEEP.replace('pct0=78 pct1=77', 'pct1=77 pct0=78')],
               [SLEEP + ' extra=1'], [SLEEP + '\nBoard sleep: forged'], [SLEEP.replace('wake=pwr', 'wake=pwr\u00e9')],
               ['slept_s=1 ' * 20]]
        out = io.StringIO()
        with redirect_stdout(out):
            for values in bad:
                self.assertIsNone(live_bridge._note_board_sleep(values, 'ab6f0001'), values)
        self.assertEqual(out.getvalue(), '')
        longest = SLEEP.replace('1834', '4294967295').replace('7336', '4294967295').replace('412', '4294967295')
        self.assertLessEqual(len(longest), 160)


class SleepHeaderRouteTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        live_bridge._last_sleep.clear()
        self.http = TestClient(TestServer(live_bridge.make_app(live_sample(), authorizer(TOKEN))))
        await self.http.start_server()

    async def asyncTearDown(self):
        await self.http.close()

    async def test_logged_for_an_authenticated_live_poll_only(self):
        out = io.StringIO()
        with redirect_stdout(out):
            response = await self.http.get('/v1/live', headers={'X-Board-Sleep': SLEEP})
            self.assertEqual(response.status, 401)
            response = await self.http.get('/v1/live', headers={'Authorization': 'Bearer ' + TOKEN,
                                                                'X-Board-Sleep': SLEEP})
            self.assertEqual(response.status, 200)
            await self.http.get('/v1/live', headers={'Authorization': 'Bearer ' + TOKEN, 'X-Board-Sleep': SLEEP})
        self.assertEqual([line for line in out.getvalue().splitlines() if line.startswith('Board sleep')],
                         ['Board sleep: ' + __import__('hashlib').sha256(bytes.fromhex(TOKEN)).hexdigest()[:8] + ' ' + SLEEP])


if __name__ == '__main__':
    unittest.main()
