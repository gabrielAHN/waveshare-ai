#!/usr/bin/env python3
"""Send a bridge candidate (WLB2) or a Connect-page action (WPC1) to the board over USB serial.

WLB2 is the documented fallback for networks where mDNS does not work. It adds the bridge to the
board's bridge list as if discovered; nothing is trusted until you tap it and confirm
the comparison code with `waveshare-bridge enroll`. It carries no secret: an address and the
PUBLIC certificate fingerprint (`waveshare-bridge status` prints it).

  python tools/pair_usb.py bridge https://10.20.30.40:8098 <fingerprint-hex> --name "Studio bridge"
  python tools/pair_usb.py scan         # WPC1 open Connect page + search (like tapping)
  python tools/pair_usb.py pick 0       # WPC1 pick list row 0
  python tools/pair_usb.py cancel|forget|phone
  python tools/pair_usb.py button-a|button-b   # press the upper/lower large Settings button (test hook)
  python tools/pair_usb.py forget-wifi         # forget the saved Wi-Fi (no on-screen control)
Port: --port or the single attached Espressif device. --dry-run prints the frame only.
"""
import argparse
import re
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from serial_util import frame, resolve_port, send  # noqa: E402

URL_MAX, FP_LEN, NAME_MAX = 96, 65, 33   # pair_usb_bridge in the named device's main/pair_usb.h
ACTIONS = {'scan': 1, 'cancel': 2, 'forget': 3, 'phone': 4,   # phone = open the phone sign-in QR
           'button-a': 5, 'button-b': 6, 'forget-wifi': 7}  # named-device main/pair_usb.h PAIR_CMD_*


def field(text, size):
    raw = text.encode('ascii')
    if len(raw) >= size:
        raise SystemExit(f'value too long (max {size - 1} bytes): {text!r}')
    return raw + b'\0' * (size - len(raw))


def bridge_frame(base, fp, name):
    if not re.fullmatch(r'https://[0-9.]+(:\d{1,5})?', base):
        raise SystemExit('base must look like https://<ipv4>[:port]')
    if not re.fullmatch(r'[0-9a-f]{64}', fp):
        raise SystemExit('fingerprint must be 64 lowercase hex characters')
    return frame(b'WLB2', field(base, URL_MAX) + field(fp, FP_LEN) + field(name[:32], NAME_MAX))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port')
    p.add_argument('--dry-run', action='store_true')
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('bridge')
    b.add_argument('base')
    b.add_argument('fingerprint')
    b.add_argument('--name', default='Waveshare AI bridge')
    k = sub.add_parser('pick')
    k.add_argument('row', type=int, choices=range(4))
    for name in ACTIONS:
        sub.add_parser(name)
    a = p.parse_args()
    if a.cmd == 'bridge':
        data = bridge_frame(a.base, a.fingerprint.lower(), a.name)
    elif a.cmd == 'pick':
        data = frame(b'WPC1', bytes([0x10 + a.row]))
    else:
        data = frame(b'WPC1', bytes([ACTIONS[a.cmd]]))
    print(f'{a.cmd}: {len(data)}-byte frame {data[:4].decode()}')
    if a.dry_run:
        return 0
    port = resolve_port(a.port)
    send(port, data)
    print(f'sent to {port}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
