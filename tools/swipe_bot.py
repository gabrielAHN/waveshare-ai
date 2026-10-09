#!/usr/bin/env python3
"""One-shot Ask-page bot swipe over the board's USB serial (TEST-ONLY hook, WBS1).

Sends CRC-checked frames and exits (no daemon, no network):
    b"WBS1" + bytes([dir]) + crc32(bytes([dir])) little-endian   (9 bytes)
dir 1 = next bot (a swipe to the left), 2 = previous bot (a swipe to the right).

The board applies it with the SAME helper_swipe() as a finger (clamped at the ends, refused while
listening/sending/running) only while the Ask page is shown, and logs
    BOT_SWIPE dir=next ok=1 from=0 to=1 bot=atlas mic=enabled|no_quota|signin|plugin_update source=usb_serial
It is never counted as a touch sample.

    python tools/swipe_bot.py next next next prev --gap 1.5
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from serial_util import frame  # noqa: E402

DIRS = {'next': 1, 'prev': 2}


def build_frame(direction):
    return frame(b'WBS1', bytes([DIRS[direction]]))


def main():
    p = argparse.ArgumentParser(description='Test-only: swipe the Ask page to the next/previous bot over USB.')
    p.add_argument('dirs', nargs='+', choices=sorted(DIRS))
    p.add_argument('--port', help='serial port (default: the single attached Espressif device)')
    p.add_argument('--gap', type=float, default=1.0, help='seconds between swipes')
    p.add_argument('--dry-run', action='store_true', help='print the frames and exit')
    a = p.parse_args()
    frames = [build_frame(d) for d in a.dirs]
    for d, f in zip(a.dirs, frames):
        print(f'swipe={d} frame={f.hex()}')
    if a.dry_run:
        return 0
    import serial
    from serial_util import resolve_port
    with serial.Serial(resolve_port(a.port), 115200, timeout=1) as s:
        for i, f in enumerate(frames):
            if i:
                time.sleep(a.gap)
            s.write(f)
            s.flush()
    return 0


if __name__ == '__main__':
    sys.exit(main())
