#!/usr/bin/env python3
"""Hard-reset the board, open Settings over USB and walk its large targets with the WPC1 test hooks.

  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python tests/board/capture_settings_walk.py --log walk.log

Boot -> first `LIVE_SAMPLE ... http=200` -> WLV1 page 2 (Settings) -> then, one step every --gap s:
  button-b (Session sparkle OFF), button-b (ON), button-a (Hermes row: sign-out question),
  button-b (Keep signed in), phone (WPC1 4: refused on a signed-in board, by design).
Never presses "Yes, sign out". Prints every SETTINGS_SCREEN row plus PHONE_STATUS / BOTS_SAMPLE lines
and the per-paint render/transfer cost. USB-hook presses are NOT finger taps. Needs pyserial+esptool.
"""
import argparse
import pathlib
import re
import sys
import time

HERE = pathlib.Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2] / 'tools'))
from serial_util import frame, resolve_port  # noqa: E402

STEPS = [('button-b', 6), ('button-b', 6), ('button-a', 5), ('button-b', 6), ('phone', 4)]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port')
    p.add_argument('--seconds', type=float, default=75)
    p.add_argument('--gap', type=float, default=4.0)
    p.add_argument('--log', type=pathlib.Path, required=True)
    a = p.parse_args()
    import serial
    from esptool.reset import HardReset
    port = resolve_port(a.port)
    s = serial.Serial(port, 115200, timeout=0.15)
    s.dtr = False
    buf, opened, steps, next_at = bytearray(), False, list(STEPS), None
    HardReset(s, uses_usb=True)()
    end = time.monotonic() + a.seconds
    while time.monotonic() < end:
        try:
            if not s.is_open:
                s.open()
            buf.extend(s.read(8192))
            boot = buf[buf.rfind(b'ESP-ROM:'):]
            if not opened and re.search(rb'LIVE_SAMPLE[^\n]*http=200', boot):
                s.write(frame(b'WLV1', bytes([2])))
                s.flush()
                opened, next_at = True, time.monotonic() + a.gap
                buf.extend(b'\n# HOST sent WLV1 page=2 (Settings)\n')
            if opened and steps and b'SETTINGS_SCREEN' in boot and time.monotonic() >= next_at:
                name, action = steps.pop(0)
                s.write(frame(b'WPC1', bytes([action])))
                s.flush()
                buf.extend(f'\n# HOST sent WPC1 {action} ({name})\n'.encode())
                next_at = time.monotonic() + a.gap
            if opened and not steps and time.monotonic() >= next_at + 2:
                break
        except (serial.SerialException, OSError):
            try:
                s.close()
            except Exception:
                pass
            time.sleep(0.3)
    s.close()
    a.log.write_bytes(bytes(buf))
    text = buf[buf.rfind(b'ESP-ROM:'):].decode('utf-8', 'replace')
    for line in text.splitlines():
        if re.search(r'App version|ELF file SHA256|SERVICE_SELECT|SETTINGS_SCREEN|PHONE_STATUS|PHONE_START|BOTS_SAMPLE|# HOST|PAIR_CMD|WPC1|usb_serial|HOME_PAGE', line):
            print(line.strip()[:260])
    costs = [(int(r), int(t)) for r, t in re.findall(r'SETTINGS_SCREEN .*?render_us=(\d+) transfer_us=(\d+)', text)]
    if costs:
        tot = [r + t for r, t in costs]
        print(f'SETTINGS_PAINTS n={len(costs)} render_us_max={max(r for r, _ in costs)} '
              f'paint_us_mean={sum(tot) // len(tot)} paint_us_max={max(tot)} '
              f'=> max repaint rate {1e6 / (sum(tot) / len(tot)):.1f} FPS (still screen: repaints only on change)')
    return 0 if opened else 1


if __name__ == '__main__':
    raise SystemExit(main())
