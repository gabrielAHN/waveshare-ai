"""Enrolled-board fixtures for the current bridge API."""
import pathlib
import tempfile
import weakref
from waveshare_bridge import enroll, live_bridge

TOKEN = '62' * 32
KEY = b'k' * 32


def authorizer(token=TOKEN):
    tmp = tempfile.TemporaryDirectory()
    reg = enroll.Registry(pathlib.Path(tmp.name) / 'boards.json')
    weakref.finalize(reg, tmp.cleanup)
    reg.add(bytes.fromhex(token), 'Test board')
    return enroll.Authorizer(reg)


def live_sample():
    sample = live_bridge.Sample(KEY)
    sample.update([])
    return sample
