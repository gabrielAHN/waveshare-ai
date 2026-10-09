#!/usr/bin/env python3
"""Hardware probe for phone sign-in: boot, open the Ask page (WLV1 3) to read the mic gate, optionally
press the mic (WVC1), then open the phone sign-in QR (WPC1 4) and LEAVE the board on it.

Every input is a disclosed USB hook (logged source=usb_serial on the board), not a finger. The QR
itself is only observed through the board's own log (host+path of the URL, code length); a phone scan
and the on-glass look are not verified by this script.

  python tests/board/capture_phone_pair.py --log out.log [--voice] [--seconds 70]
Prints PAIR_BOOT, BOTS_*, PHONE_*, VOICE_* lines and Ask-page FPS. Never prints the user code.
"""
import argparse
import pathlib
import re
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
from serial_util import frame, resolve_port  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port')
    p.add_argument('--seconds', type=float, default=70)
    p.add_argument('--log', type=pathlib.Path, required=True)
    p.add_argument('--voice', action='store_true', help='press+release the Ask mic once (WVC1) before the QR')
    p.add_argument('--no-reset', action='store_true')
    a = p.parse_args()
    import serial
    # Opening the USB-Serial/JTAG port resets this board (pyserial asserts RTS/DTR on open), which is
    # the boot we capture. A HardReset + reopen here lost the WLV1 write in testing.
    s = serial.Serial(resolve_port(a.port), 115200, timeout=0.15)
    s.dtr = False
    buf = bytearray()
    step, t_step = 'boot', time.monotonic()
    end = time.monotonic() + a.seconds

    def send(data):
        s.write(data)
        s.flush()

    while time.monotonic() < end:
        try:
            if not s.is_open:
                s.open()
            buf.extend(s.read(8192))
        except serial.SerialException:
            s.close()
            time.sleep(0.2)
            continue
        tail = bytes(buf[buf.rfind(b'ESP-ROM:') if not a.no_reset else 0:])
        now = time.monotonic()
        if step == 'boot' and (b'LIVE_SAMPLE phase=3 http=200' in tail or a.no_reset):
            if not a.no_reset:
                send(frame(b'WLV1', bytes([3])))
            step, t_step = 'ask', now
        elif step == 'ask' and (b'BOTS_SELECTED' in tail or a.no_reset) and now - t_step > 12:
            if a.voice:
                send(frame(b'WVC1', bytes([1])))
                time.sleep(1.5)
                send(frame(b'WVC1', bytes([2])))
                step, t_step = 'voice', now
            else:
                step, t_step = 'voice', now - 100
        elif step == 'voice' and (now - t_step > 25 or b'VOICE_STATE transport=gateway status=5' in tail):
            send(frame(b'WPC1', bytes([4])))
            step, t_step = 'qr', now
        elif step == 'qr' and b'PHONE_START' in tail and now - t_step > 12:
            break
    s.close()   # no reset on close: the board stays on the QR screen
    text = bytes(buf).decode('utf-8', 'replace')
    if not a.no_reset and 'ESP-ROM:' in text:
        text = text[text.rfind('ESP-ROM:'):]
    a.log.write_text(text)
    lines = text.splitlines()
    keys = ('ELF file SHA256', 'PAIR_BOOT', 'SERVICE_SELECT', 'BOTS_SAMPLE', 'BOTS_SELECTED', 'PHONE_', 'PAIR_COMMAND',
            'VOICE_UPLOAD', 'VOICE_STATE', 'VOICE_ERROR', 'Guru', 'abort()')
    for l in lines:
        if any(k in l for k in keys):
            print(l.strip()[:300])
    rows = []
    for l in lines:
        m = re.search(r'DIRECT_TIMING n=(\d+) stamp_us=(\d+) .* page=(\d+)', l)
        b = re.search(r' mic_block=(\d+)', l)
        if m:
            rows.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), int(b.group(1)) if b else -1))
    by = {}
    for (n0, t0, p0, b0), (n1, t1, p1, b1) in zip(rows, rows[1:]):
        if (p0, b0) == (p1, b1) and n1 > n0 and t1 > t0:
            by.setdefault((p1, b1), []).append((n1 - n0, t1 - t0))
    for (page, block), pairs in sorted(by.items()):
        pairs = pairs[1:] or pairs
        frames, us = sum(f for f, _ in pairs), sum(t for _, t in pairs)
        print(f'page={page} mic_block={block} fps={frames / (us / 1e6):.2f} frames={frames}')
    print('final_step', step)


if __name__ == '__main__':
    main()
