"""Rapid back-and-forth mic stress probe. NO Mac audio: the board mic records the quiet room.

USB WVC1 press/release (not a finger) drives fast turns the way a conversation does:
  * re-press almost immediately after each VOICE_DONE (gap 0.2-1.0 s),
  * every 3rd turn: press again while the reply is still running (= Stop tap, then talk again),
  * every 4th turn: press during the upload/transcribing window (must be ignored, not crash),
  * short taps (< min hold) between turns.
Silent recordings usually come back as 'Recording too short' / empty transcript / an error reply;
the point is the device state machine, TLS and heap under rapid turn-taking, not the answer.
Watches for any reset/panic after setup; reports heap/stack minimums. Restores the selected bot.
"""
import argparse, json, os, random, re, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import serial
from esptool.reset import HardReset
from serial_util import frame, resolve_port

PANIC = r'ESP-ROM:|Guru Meditation|Backtrace:|Stack canary|abort\(\)|CORRUPT HEAP|task_wdt'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--turns', type=int, default=24)
    p.add_argument('--seconds', type=float, default=900)
    p.add_argument('--seed', type=int, default=7)
    a = p.parse_args()
    rnd = random.Random(a.seed)
    port = resolve_port()
    if subprocess.run(['lsof', '-t', port], capture_output=True, text=True).stdout.strip():
        raise SystemExit('Serial endpoint already owned')
    os.umask(0o077)
    f = a.log.open('xb')
    s = serial.Serial(port, 115200, timeout=.05); s.dtr = False
    deadline = time.monotonic() + a.seconds
    raw = bytearray(); turns = []; failure = None; first_action = None; original = None

    def pump(sec=.1):
        end = min(deadline, time.monotonic() + sec)
        while time.monotonic() < end:
            try:
                if not s.is_open: s.open()
                d = s.read(max(1, s.in_waiting))
                if d: raw.extend(d); f.write(d); f.flush()
            except serial.SerialException:
                s.close(); time.sleep(.1)
        if first_action is not None and re.search(PANIC, raw[first_action:].decode(errors='replace')):
            raise RuntimeError('Runtime reset/panic detected')
        if time.monotonic() >= deadline: raise TimeoutError('deadline')

    def wait(pat, off=0, sec=40):
        until = min(deadline, time.monotonic() + sec)
        while time.monotonic() < until:
            m = re.search(pat, raw[off:].decode(errors='replace'))
            if m: return m
            pump()
        raise TimeoutError('missing ' + pat)

    def send(magic, v): s.write(frame(magic, bytes([v]))); s.flush()

    def latest_bot():
        ms = re.findall(r'BOTS_SELECTED index=(\d+) mic=(\w+)', raw.decode(errors='replace'))
        return (int(ms[-1][0]), ms[-1][1]) if ms else (None, None)

    def select(i):
        for _ in range(4):
            cur, st = latest_bot()
            if cur == i: return st
            pos = len(raw); send(b'WBS1', 1 if cur < i else 2)
            wait(r'BOTS_SELECTED index=', pos, 15); pump(1)
        raise RuntimeError('select failed')

    try:
        HardReset(s, uses_usb=True)()
        wait(r'USB_PROVISION_READY', sec=35)
        original = int(wait(r'BOT_LOADED index=(\d+)').group(1))
        send(b'WLV1', 3)
        wait(r'BOTS_SELECTED index=\d+ mic=\w+', sec=55)
        select(0)
        wait(r'BOTS_SELECTED index=0 mic=enabled', sec=35)
        pump(2)
        first_action = len(raw)
        for t in range(a.turns):
            pos = len(raw); t0 = time.monotonic(); kind = 'talk'
            if t % 5 == 4:  # a short tap first (below the minimum hold), then the real turn
                send(b'WVC1', 1); pump(.15); send(b'WVC1', 2); pump(.2); kind = 'tap+talk'
            send(b'WVC1', 1)
            wait(r'VOICE_BUTTON action=1 ok=\d state=\d', pos, 8)
            pump(1.6 + rnd.random() * 1.2)  # hold like a spoken sentence
            send(b'WVC1', 2)
            wait(r'VOICE_REC[^\r\n]*', pos, 12)
            if t % 4 == 3:  # press during the upload / transcribing window: must be a no-op
                pump(.3); send(b'WVC1', 1); pump(.4); send(b'WVC1', 2); kind += '+press_during_upload'
            if t % 3 == 2:  # barge in while the reply runs (Stop tap), like interrupting
                try:
                    wait(r'VOICE_STATE transport=gateway status=2', pos, 25); pump(.3)
                    send(b'WVC1', 1); pump(.15); send(b'WVC1', 2); kind += '+barge_stop'
                except TimeoutError:
                    pass
            m = None
            try:
                m = wait(r'VOICE_DONE transport=gateway status=\d+[^\r\n]*|VOICE_ERROR reason="[^"]*"', pos, 150)
            except TimeoutError:
                pass
            seg = raw[pos:].decode(errors='replace')
            turns.append(dict(turn=t, kind=kind, end=m.group(0)[:160] if m else None,
                              upload=(re.findall(r'VOICE_UPLOAD http=\d+', seg) or [None])[-1],
                              seconds=round(time.monotonic() - t0, 1)))
            print(json.dumps(turns[-1]), flush=True)
            if m and 'VOICE_DONE' not in m.group(0):
                try: wait(r'VOICE_DONE transport=gateway status=\d+', pos, 30)
                except TimeoutError: pass
            pump(.2 + rnd.random() * .8)  # talk back quickly
        pump(15)
    except Exception as e:
        failure = f'{type(e).__name__}: {e}'
    finally:
        try:
            if original is not None and failure is None:
                select(original); pump(2)
        except Exception as e:
            failure = failure or f'restore: {e}'
        s.close(); f.close()
    txt = raw.decode(errors='replace')
    after = txt[first_action:] if first_action is not None else ''
    ints = lambda pat: list(map(int, re.findall(pat, txt))) or [0]
    out = dict(source='USB WVC1 press/release; board mic in a quiet room; NO Mac audio; not a finger',
               turns=len(turns), turn_log=turns, failure=failure,
               panic=bool(re.search(r'Guru Meditation|Backtrace:|Stack canary|abort\(\)|CORRUPT HEAP|task_wdt', after)),
               runtime_resets=len(re.findall(r'ESP-ROM:', after)),
               voice_done=len(re.findall(r'VOICE_DONE', after)), uploads=len(re.findall(r'VOICE_UPLOAD transport=gateway', after)),
               voice_stack_min=min(ints(r'voice_stack_free=(\d+)')), owner_stack_min=min(ints(r'owner_stack_free=(\d+)')),
               internal_free_min=min(ints(r'internal_free=(\d+)')), internal_min_watermark=min(ints(r'internal_min=(\d+)')))
    a.log.with_suffix('.json').write_text(json.dumps(out, indent=2))
    print(json.dumps({k: v for k, v in out.items() if k != 'turn_log'}, indent=2))
    sys.exit(1 if failure or out['panic'] or out['runtime_resets'] else 0)


if __name__ == '__main__':
    main()
