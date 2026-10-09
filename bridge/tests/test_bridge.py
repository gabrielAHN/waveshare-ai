import importlib.util
import struct
import unittest


class WireTests(unittest.TestCase):
    def test_invalid_samples_fail_closed(self):
        from waveshare_bridge.live_bridge import encode_sessions
        invalid = [None, {}, [None], [{'id': '', 'status': 'working'}],
                   [{'id': 'x', 'status': 'unknown'}], [{'id': 'x'}],
                   [{'id': 'x', 'status': 'idle'}] * 2,
                   [{'id': str(i), 'status': 'working'} for i in range(129)]]
        for rows in invalid:
            with self.subTest(rows_type=type(rows).__name__):
                with self.assertRaises(ValueError):
                    encode_sessions(rows, b'k' * 32)
        with self.assertRaises(ValueError):
            encode_sessions([], b'short')
        self.assertEqual(encode_sessions([], b'k' * 32), b'WLS4\0\0\0\0')

    def test_open_sessions_stable_sorted_private_wire(self):
        self.assertIsNotNone(importlib.util.find_spec('waveshare_bridge.live_bridge'), 'bridge implementation missing')
        from waveshare_bridge.live_bridge import encode_sessions
        rows = [{'id': 'b', 'status': 'working', 'title': 'PRIVATE', 'preview': 'PRIVATE'},
                {'id': 'a', 'status': 'working'},
                *[{'id': x, 'status': x} for x in ('idle', 'waiting', 'starting')]]
        wire = encode_sessions(rows, b'k' * 32)
        self.assertEqual(wire[:8], b'WLS4\x05\x00\x00\x00')
        ids = [struct.unpack_from('<Q', wire, 8 + 10 * i)[0] for i in range(5)]
        self.assertTrue(ids == sorted(ids) and all(i > 0 for i in ids))
        self.assertEqual(wire, encode_sessions(list(reversed(rows)), b'k' * 32))
        self.assertNotEqual(wire, encode_sessions(rows, b'z' * 32))
        self.assertNotIn(b'PRIVATE', wire)

    def test_provider_metadata_is_aligned_with_sorted_working_ids(self):
        from waveshare_bridge.live_bridge import encode_sessions
        rows = [{'id': 'z', 'status': 'working', 'provider': 'anthropic'},
                {'id': 'a', 'status': 'working', 'provider': 'openai-codex'},
                {'id': 'u', 'status': 'working', 'provider': 'made-up'},
                {'id': 'i', 'status': 'idle', 'provider': 'openrouter'}]
        wire = encode_sessions(rows, b'k' * 32, 2, 0)
        self.assertEqual(wire[:4], b'WLS4')
        records = [wire[8 + i * 10:18 + i * 10] for i in range(4)]  # every open session
        self.assertEqual([int.from_bytes(r[:8], 'little') for r in records],
                         sorted(int.from_bytes(r[:8], 'little') for r in records))
        by_id = {int.from_bytes(r[:8], 'little'): r[8] for r in records}
        expected = {}
        for row, code in ((rows[0], 1), (rows[1], 2), (rows[2], 0), (rows[3], 3)):
            one = encode_sessions([row], b'k' * 32, 1, 0)
            expected[int.from_bytes(one[8:16], 'little')] = code
        self.assertEqual(by_id, expected)
        self.assertEqual(wire[4:6], b'\x04\x00')
        self.assertEqual(sum(wire[17 + 10 * i] for i in range(4)), 3)



if __name__ == '__main__':
    unittest.main()
