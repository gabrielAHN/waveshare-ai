#!/usr/bin/env python3
"""On-device enrollment probe (hardware test; nothing secret is sent or printed).

1. Opens the running bridge's enrollment window over its local control socket (same path as
   `waveshare-bridge enroll`).
2. Drives the board's Settings > Connect to Hermes page through the disclosed WPC1 USB hook
   (open+scan, then pick the row whose address matches --bridge-base). The board logs these as
   source=usb_serial; they are equivalent to taps and carry no secret.
3. The host-side y/n prompt is answered programmatically: 'y' ONLY if the comparison code the board
   logs (ENROLL_CODE) equals the code the bridge shows, otherwise 'n'.
4. Waits for PAIR_SAVED and the first LIVE_SAMPLE http=200. Writes a line log to --log.

Needs pyserial (ESP-IDF python env). Port: --port or the single Espressif device (VID 0x303a).
"""
import argparse
import asyncio
import pathlib
import re
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2] / 'tools'))
sys.path.insert(0, str(HERE.parents[2] / 'bridge'))
from serial_util import frame, resolve_port  # noqa: E402
from waveshare_bridge import enroll  # noqa: E402

KEYS = ('PAIR_', 'ENROLL_', 'DEVICE_ID', 'MDNS', 'LIVE_SAMPLE', 'STA_CONNECTED', 'PAIR_COMMAND')


class Board:
    def __init__(self, port):
        import serial
        self.s = serial.Serial(port, 115200, timeout=0.2)
        self.lines, self.stop = [], False
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b''
        while not self.stop:
            buf += self.s.read(4096)
            *done, buf = buf.split(b'\n')
            for raw in done:
                self.lines.append((time.time(), raw.decode('utf-8', 'replace').rstrip()))

    def send(self, action):
        self.s.write(frame(b'WPC1', bytes([action])))
        self.s.flush()

    def wait(self, pattern, timeout, since=0.0):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            for t, line in list(self.lines):
                if t >= since and rx.search(line):
                    return line
            time.sleep(0.1)
        return None


async def main():
    p = argparse.ArgumentParser()
    p.add_argument('--config-dir', type=pathlib.Path, required=True)
    p.add_argument('--bridge-base', required=True, help='https://<bridge ip>:<port> expected in the scan list')
    p.add_argument('--port')
    p.add_argument('--log', type=pathlib.Path, required=True)
    args = p.parse_args()
    board = Board(resolve_port(args.port))
    t0 = time.time()
    result = {'answer': None}

    def ask(prompt):
        pending = next((l for l in reversed(mac_lines) if l.startswith('Comparison code:')), '')
        mac_code = re.search(r'(\d{3} \d{3})', pending).group(1)
        line = board.wait(r'ENROLL_CODE code=\d{3} \d{3}', 30, t0)
        dev_code = re.search(r'code=(\d{3} \d{3})', line).group(1) if line else None
        result.update(mac=mac_code, device=dev_code)
        result['answer'] = 'y' if dev_code == mac_code else 'n'
        mac_lines.append(f'[probe] device code={dev_code} mac code={mac_code} -> answer {result["answer"]}')
        return result['answer']

    mac_lines = []
    client = asyncio.create_task(enroll.run_enroll_client(args.config_dir / 'control.sock', 180, ask=ask,
                                                          emit=mac_lines.append))
    await asyncio.sleep(1.0)
    board.send(1)  # open Connect page + mDNS scan
    scan = await asyncio.to_thread(board.wait, r'PAIR_SCAN code=', 30, t0)
    items = [l for _, l in board.lines if 'PAIR_SCAN_ITEM' in l]
    index = next((int(re.search(r'index=(\d+)', l).group(1)) for l in items if f'base={args.bridge_base} ' in l), None)
    if index is None:
        client.cancel()
        outcome = 'bridge-not-discovered'
    else:
        board.send(0x10 + index)
        outcome = await client
    saved = await asyncio.to_thread(board.wait, r'PAIR_SAVED ok=1', 20, t0) if outcome == 'accepted' else None
    live = await asyncio.to_thread(board.wait, r'LIVE_SAMPLE phase=3 http=200', 30, t0) if saved else None
    board.stop = True
    keep = [f'{t - t0:7.2f} {l}' for t, l in board.lines if any(k in l for k in KEYS)]
    args.log.write_text('\n'.join(['# board'] + keep + ['# mac (waveshare-bridge enroll client)'] + mac_lines
                                  + [f'# outcome={outcome} scan={scan!r} saved={bool(saved)} live200={bool(live)}']) + '\n')
    print(args.log.read_text())
    return 0 if outcome == 'accepted' and saved and live else 1


if __name__ == '__main__':
    raise SystemExit(asyncio.run(main()))
