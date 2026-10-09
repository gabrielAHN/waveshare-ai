"""The disposable device-grant harness uses the public phone client."""
import pathlib
import tempfile
import unittest
from unittest import mock
import e2e_authelia


class PhoneConfigTests(unittest.TestCase):
    def test_generated_phone_client_is_public_without_a_client_secret(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp) / 'idp'
            with mock.patch.object(e2e_authelia, '_hash', return_value='fixture-password-hash'):
                e2e_authelia.Authelia(directory).write()
            text = (directory / 'configuration.yml').read_text()
            phone = text.split("- client_id: 'waveshare-pairing'", 1)[1].split('    lifespans:', 1)[0]
            self.assertTrue('public: true' in phone)
            self.assertTrue('token_endpoint_auth_method: none' in phone)
            self.assertFalse('client_secret:' in phone)
