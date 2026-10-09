"""Retained private-file and board HMAC key boundary coverage."""
import os
import pathlib
import tempfile
import unittest
from waveshare_bridge import common, live_bridge


class PrivateFileTests(unittest.TestCase):
    def test_private_reader_rejects_public_symlink_nonregular_and_oversized_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            path = root / 'board.key'
            path.write_text('12' * 32)
            path.chmod(0o600)
            self.assertEqual(live_bridge.load_board_key(path), bytes.fromhex('12' * 32))
            self.assertEqual(common.read_private(path, 64), b'12' * 32)
            with self.assertRaises(ValueError): common.read_private(path, 63)
            path.chmod(0o644)
            with self.assertRaises(ValueError): common.read_private(path)
            path.chmod(0o600)
            link = root / 'link'
            link.symlink_to(path)
            with self.assertRaises(OSError): common.read_private(link)
            with self.assertRaises((ValueError, OSError)): common.read_private(root)
            for value in ('AA' * 32, '12' * 31, 'x' * 64):
                path.write_text(value)
                with self.subTest(value_type='invalid hex'), self.assertRaises(ValueError):
                    live_bridge.load_board_key(path)
