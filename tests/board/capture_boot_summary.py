#!/usr/bin/env python3
"""Hard-reset the board, capture one boot, optionally select a service page (WLV1), summarise.

  python tests/board/capture_boot_summary.py --seconds 40 --page 3 --log boot.log [--swipes next,next,next,prev]
Prints PAIR_BOOT / LIVE_SAMPLE http codes / density levels / BOTS_SAMPLE + BOT_* rows / FPS from
DIRECT_TIMING per (page, helper_state, bot, mic_block) (first interval of each run dropped).
--swipes sends test-only WBS1 frames (tools/swipe_bot.py) after the first BOTS_SAMPLE on the Ask page.
Needs esptool + pyserial (ESP-IDF python env).
"""
import argparse
import pathlib
import re
import sys
import time

HERE = pathlib.Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[2] / 'tools'))
from serial_util import frame, resolve_port  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port')
    p.add_argument('--seconds', type=float, default=40)
    p.add_argument('--page', type=int, help='WLV1 service page after boot (1 Sparkles, 3 Ask/voice)')
    p.add_argument('--log', type=pathlib.Path, required=True)
    p.add_argument('--swipes', default='', help='comma list of next/prev WBS1 swipes (Ask page only)')
    p.add_argument('--swipe-gap', type=float, default=4.0)
    p.add_argument('--voice', default='', help='WVC1 action after the swipes: "press" = press then release after 1.5 s')
    a = p.parse_args()
    import serial
    from esptool.reset import HardReset
    port = resolve_port(a.port)
    s = serial.Serial(port, 115200, timeout=0.15)
    s.dtr = False
    buf, sent = bytearray(), False
    swipes = [d for d in a.swipes.split(',') if d]
    next_swipe = None
    HardReset(s, uses_usb=True)()
    end = time.monotonic() + a.seconds
    while time.monotonic() < end:
        try:
            if not s.is_open:
                s.open()
            buf.extend(s.read(8192))
            if a.page is not None and not sent and b'STA_CONNECTED' in buf:
                s.write(frame(b'WLV1', bytes([a.page])))
                s.flush()
                sent = True
            if swipes and sent and b'BOTS_SAMPLE' in buf[buf.rfind(b'ESP-ROM:'):]:
                now = time.monotonic()
                if next_swipe is None:
                    next_swipe = now + a.swipe_gap
                elif now >= next_swipe:
                    d = swipes.pop(0)
                    s.write(frame(b'WBS1', bytes([1 if d == 'next' else 2])))
                    s.flush()
                    next_swipe = now + a.swipe_gap
                    if not swipes and a.voice == 'press':
                        time.sleep(a.swipe_gap)
                        s.write(frame(b'WVC1', bytes([1])))
                        s.flush()
                        time.sleep(1.5)
                        s.write(frame(b'WVC1', bytes([2])))
                        s.flush()
        except serial.SerialException:
            s.close()
            time.sleep(0.2)
    s.close()
    text = bytes(buf).decode('utf-8', 'replace')
    marker = text.rfind('ESP-ROM:')
    text = text[marker:] if marker >= 0 else text
    a.log.write_text(text)
    lines = text.splitlines()
    for l in lines:
        if any(k in l for k in ('PAIR_BOOT', 'SERVICE_SELECT', 'app_elf_sha256', 'ELF file SHA256', 'Guru', 'abort()')):
            print(l[:220])
    codes = [int(m.group(1)) for m in (re.search(r'LIVE_SAMPLE phase=\d+ http=(\d+)', l) for l in lines) if m]
    levels = [int(m.group(1)) for m in (re.search(r'LIVE_SAMPLE phase=3 http=200 .* level=(-?\d+)', l) for l in lines) if m]
    print(f'LIVE_SAMPLE http codes: {sorted(set(codes))} count200={codes.count(200)} levels_seen={sorted(set(levels))}')
    for l in lines:
        for key in ('BOTS_SAMPLE', 'BOTS_SELECTED', 'BOT_SWIPE', 'BOT_LOADED', 'BOT_SAVED', 'VOICE_BUTTON', 'VOICE_UPLOAD',
                    'VOICE_REFUSED', 'VOICE_REC', 'VOICE_ERROR'):
            if key in l:
                print(l[l.find(key):][:320])
    rows = []
    for l in lines:
        m = re.search(r'DIRECT_TIMING n=(\d+) stamp_us=(\d+) .* page=(\d+)', l)
        if m:
            h = re.search(r'helper_state=(-?\d+)', l)
            b = re.search(r' bot=(-?\d+) mic_block=(\d+)', l)
            key = (int(m.group(3)), int(h.group(1)) if h else -1, int(b.group(1)) if b else -1, int(b.group(2)) if b else -1)
            rows.append((int(m.group(1)), int(m.group(2)), key))
    by = {}
    for (n0, t0, p0), (n1, t1, p1) in zip(rows, rows[1:]):
        if p0 == p1 and n1 > n0 and t1 > t0:
            by.setdefault(p1, []).append((n1 - n0, t1 - t0))
    for key, pairs in sorted(by.items()):
        pairs = pairs[1:] or pairs  # drop the first (page-change/first-paint) interval
        frames, us = sum(f for f, _ in pairs), sum(t for _, t in pairs)
        print(f'page={key[0]} helper_state={key[1]} bot={key[2]} mic_block={key[3]} fps={frames / (us / 1e6):.2f} frames={frames}')


if __name__ == '__main__':
    main()
