"""Spoken conversation probe: the REAL board mic hears the Mac's voice (user-authorized).

Playback: `say` on WAVESHARE_AI_SAY_DEVICE (`say -a <device>`; unset = default output) only; the Mac
volume is never changed. USB WVC1
presses/releases the mic, WBS1 switches bots, WGS1 replays chat/Home gestures as touch samples, WLV1
opens Ask once at start: all USB-injected, NOT a finger. Replies are read from the helper profile's
state.db (read-only). Read-only questions only (no tools).

Checks, in order:
  1 swipe-up -> fresh chat; turn 1 "remember <word>"            thread=new
  2 turn 2 "what word?"                                           thread=continue, reply has word
  3 scroll older in the chat                                      scroll > 0
  4 switch bot and back                                           same chat log, cont=1
  5 Home and back to Ask                                          same chat log, cont=1
  6 turn 3 "what word?"                                           thread=continue, reply has word
  7 swipe-up                                                      chat cleared, cont=0
  8 turn 4 "what word?"                                           thread=new, same session NOT reused

  $IDF_PY tests/board/spoken_convo_probe.py --log evidence/spoken-convo-HHMMSS.log
"""
import argparse, json, os, re, sqlite3, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import serial
from esptool.reset import HardReset
from serial_util import frame, resolve_port

SPK = os.environ.get('WAVESHARE_AI_SAY_DEVICE', '')   # `say -a` output device; empty = default output
WORD = 'pineapple'
DB = Path.home() / '.hermes/profiles/helper/state.db'
REMEMBER = f'Helper, please remember the word {WORD}. Just reply okay.'
ASK = 'Helper, what word did I ask you to remember? Answer with just the word.'


def main():
    # Mac voice playback needs the owner's explicit permission: this probe refuses to
    # run unless explicitly re-authorized. Use the silent tests/board/rapid_convo_probe.py instead.
    if os.environ.get('WAVESHARE_AI_ALLOW_MAC_VOICE') != '1':
        raise SystemExit('Mac voice playback is not authorized; use tests/board/rapid_convo_probe.py (silent)')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--seconds', type=float, default=600)
    a = p.parse_args()
    port = resolve_port()
    if subprocess.run(['lsof', '-t', port], capture_output=True, text=True).stdout.strip():
        raise SystemExit('Serial endpoint already owned')
    os.umask(0o077)
    f = a.log.open('xb')
    s = serial.Serial(port, 115200, timeout=.08); s.dtr = False
    deadline = time.monotonic() + a.seconds
    raw = bytearray(); steps = []; failure = None; first_action = None; original = None
    started = time.time()

    def pump(sec=.15):
        end = min(deadline, time.monotonic() + sec)
        while time.monotonic() < end:
            try:
                if not s.is_open: s.open()
                d = s.read(max(1, s.in_waiting))
                if d: raw.extend(d); f.write(d); f.flush()
            except serial.SerialException:
                s.close(); time.sleep(.15)
        if first_action is not None:
            t = raw[first_action:].decode(errors='replace')
            if re.search(r'ESP-ROM:|Guru Meditation|Backtrace:|Stack canary|abort\(\)|CORRUPT HEAP', t):
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
            cur, _st = latest_bot()
            if cur == i: return
            pos = len(raw); send(b'WBS1', 1 if cur < i else 2)
            wait(r'BOTS_SELECTED index=', pos, 15); pump(1)
        raise RuntimeError('select failed')

    def chat(g):
        pos = len(raw); send(b'WGS1', g)
        m = wait(r'HELPER_CHAT gesture=%d page=(-?\d+) bot=(-?\d+) state=(\d+) chat=(\d) cont=(\d) '
                 r'log_bytes=(\d+) scroll=(-?\d+) scroll_max=(-?\d+)' % g, pos, 10)
        keys = ('page', 'bot', 'state', 'chat', 'cont', 'log_bytes', 'scroll', 'scroll_max')
        return dict(zip(keys, map(int, m.groups())))

    def last_reply():
        con = sqlite3.connect(f'file:{DB}?mode=ro', uri=True)
        try:
            row = con.execute("select m.session_id, m.content from messages m join sessions s on s.id=m.session_id "
                              "where s.source='web' and m.role='assistant' and m.timestamp>=? "
                              "order by m.id desc limit 1", (started,)).fetchone()
        finally:
            con.close()
        return (row[0], (row[1] or '')[:200]) if row else (None, '')

    def turn(phrase):
        pos = len(raw)
        send(b'WVC1', 1)
        wait(r'VOICE_BUTTON action=1 ok=1 state=1', pos, 8)
        pump(.8)
        sp = subprocess.Popen(['say'] + (['-a', SPK] if SPK else []) + ['-r', '165', phrase])
        while sp.poll() is None: pump(.1)
        pump(.7)
        send(b'WVC1', 2)
        up = wait(r'VOICE_UPLOAD http=(\d+)[^\r\n]*thread=(\w+)', pos, 40)
        done = wait(r'VOICE_DONE transport=gateway status=(\d+)', pos, 150)
        pump(3)
        sid, reply = last_reply()
        return dict(phrase=phrase, http=int(up.group(1)), thread=up.group(2), done=int(done.group(1)),
                    session=sid, reply=reply)

    def step(name, value, ok):
        steps.append(dict(step=name, ok=bool(ok), **value))
        print(json.dumps(steps[-1]), flush=True)

    try:
        HardReset(s, uses_usb=True)()
        wait(r'USB_PROVISION_READY', sec=35)
        original = int(wait(r'BOT_LOADED index=(\d+)').group(1))
        send(b'WLV1', 3)
        wait(r'BOTS_SELECTED index=\d+ mic=\w+', sec=55)
        select(0)
        wait(r'BOTS_SELECTED index=0 mic=enabled', sec=35)
        pump(3)
        first_action = len(raw)
        chat(1)                                                     # start from a fresh chat
        t1 = turn(REMEMBER); step('turn1_new', t1, t1['thread'] == 'new' and t1['done'] == 4)
        pump(3)
        t2 = turn(ASK)
        step('turn2_continue', t2, t2['thread'] == 'continue' and t2['session'] == t1['session']
             and WORD in t2['reply'].lower())
        before = chat(4)
        older = chat(2); step('scroll_older', older, older['scroll'] > 0 or before['scroll_max'] <= 0)
        chat(4)
        pos = len(raw); send(b'WBS1', 1); wait(r'BOTS_SELECTED index=1', pos, 15); pump(1)
        pos = len(raw); send(b'WBS1', 2); wait(r'BOTS_SELECTED index=0', pos, 15); pump(1)
        back = chat(4)
        step('switch_bot_keeps', back, back['bot'] == 0 and back['cont'] == 1
             and back['log_bytes'] == before['log_bytes'] and back['chat'] == 1)
        home = chat(3)
        pump(1.5); chat(5); pump(1.5)
        again = chat(4)
        step('home_and_back_keeps', dict(home_page=home['page'], **again), home['page'] == 0 and again['page'] == 3
             and again['cont'] == 1 and again['log_bytes'] == before['log_bytes'])
        t3 = turn(ASK)
        step('turn3_continue', t3, t3['thread'] == 'continue' and t3['session'] == t1['session']
             and WORD in t3['reply'].lower())
        fresh = chat(1); step('swipe_up_new', fresh, fresh['cont'] == 0 and fresh['log_bytes'] == 0)
        t4 = turn(ASK)
        step('turn4_new_session', t4, t4['thread'] == 'new' and t4['session'] not in (None, t1['session']))
        pump(5)
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
    after = txt[first_action:] if first_action else ''
    out = dict(source='real board mic hearing the Mac speaker (user-authorized); USB press/gesture frames, not a finger',
               steps=[(x['step'], x['ok']) for x in steps], failure=failure,
               panic=bool(re.search(r'Guru Meditation|Backtrace:|Stack canary|abort\(\)|CORRUPT HEAP', after)),
               runtime_resets=len(re.findall(r'ESP-ROM:', after)),
               sessions=sorted({x.get('session') for x in steps if x.get('session')}))
    a.log.with_suffix('.json').write_text(json.dumps(dict(out, detail=steps), indent=2))
    print(json.dumps(out, indent=2))
    ok = not failure and not out['panic'] and not out['runtime_resets'] and len(steps) == 8 and all(x['ok'] for x in steps)
    print('SPOKEN_CONVO_PASS' if ok else 'SPOKEN_CONVO_FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
