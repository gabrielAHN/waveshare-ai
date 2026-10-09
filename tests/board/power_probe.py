"""Power button checks on the board with the VOICE_SELFTEST build (WPK1 test frames; nothing physical).

  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python tests/board/power_probe.py LOG [--port P]
Needs a VOICE_SELFTEST build (`NO_FLASH=1 VOICE_SELFTEST=ON ./dev.sh`, then tools/flash.sh) and both providers
signed in; flash the normal build afterwards.
Steps (each waits for its markers): boot -> both providers' PHONE_STATUS -> WPK1 5 state -> WPK1 2 hold
-> countdown -> POWER_OFF dry_run=1 -> WPK1 2 + WPK1 3 cancel -> WPK1 1 sleep (USB: light=0) -> 8 s ->
WPK1 1 wake -> Wi-Fi rejoin -> WPK1 4 light sleep 20 s (USB drops; reopen) -> wake -> rejoin.
Prints a summary only (marker lines with URLs/IPs masked)."""
import pathlib
import re
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[2]  # the repo root (tests/board/<this file>)
sys.path.insert(0, str(REPO / 'tools'))
from serial_util import frame, resolve_port  # noqa: E402
import serial  # noqa: E402
from esptool.reset import HardReset  # noqa: E402

log = pathlib.Path(sys.argv[1])
port_arg = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--port' else None
port = resolve_port(port_arg)
buf = bytearray()
s = serial.Serial(port, 115200, timeout=0.15)
s.dtr = False
HardReset(s, uses_usb=True)()
t0 = time.monotonic()
steps = []


def pump(seconds):
    global s
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            if s is None or not s.is_open:
                s = serial.Serial(port, 115200, timeout=0.15)
                s.dtr = False
            buf.extend(s.read(8192))
        except Exception:
            try:
                s.close()
            except Exception:
                pass
            s = None
            time.sleep(0.5)


def text():
    return buf.decode('utf-8', 'replace')


def wait_for(pattern, seconds, start=0):
    end = time.monotonic() + seconds
    rx = re.compile(pattern)
    while time.monotonic() < end:
        m = rx.search(text(), start)
        if m:
            return m
        pump(0.3)
    return None


def send(value):
    global s
    for _ in range(20):
        try:
            if s is None or not s.is_open:
                s = serial.Serial(port, 115200, timeout=0.15)
                s.dtr = False
            s.write(frame(b'WPK1', bytes([value])))
            s.flush()
            return True
        except Exception:
            s = None
            time.sleep(0.5)
    return False


def step(name, ok, detail=''):
    steps.append((name, bool(ok), detail))
    print('%-34s %s %s' % (name, 'OK  ' if ok else 'FAIL', detail), flush=True)


m = wait_for(r'POWER_PMU [^\r\n]*', 30)
step('boot POWER_PMU', m and 'ok=1' in m.group(0), m.group(0) if m else '')
m = wait_for(r'HOME_READY [^\r\n]*', 30)
step('HOME_READY', m, m.group(0)[:110] if m else '')
m1 = wait_for(r'PHONE_STATUS [^\r\n]*provider=hermes', 60)
m2 = wait_for(r'PHONE_STATUS [^\r\n]*provider=home_assistant', 60)
for name, mm in (('hermes status', m1), ('home_assistant status', m2)):
    line = mm.group(0) if mm else ''
    st = re.search(r'state=(\S+) flags=(\d+)', line)
    step(name, mm and 'state=authorized' in line, st.group(0) if st else line[:100])
m = wait_for(r'HOME_TILES [^\r\n]*Sensor=on[^\r\n]*', 40)
step('tiles on', m, m.group(0)[:120] if m else '')

mark = len(text())
send(5)
m = wait_for(r'POWER_STATE [^\r\n]*', 10, mark)
step('WPK1 5 state', m, m.group(0)[:150] if m else '')

mark = len(text())
send(2)
m = wait_for(r'POWER_OFF held_ms=\d+ dry_run=\d', 12, mark)
seg = text()[mark:]
secs = re.findall(r'POWER_COUNTDOWN secs=(\d)', seg)
step('hold -> countdown -> dry off', m and m.group(0).endswith('dry_run=1'), 'secs=%s %s' % (','.join(secs), m.group(0) if m else ''))
pump(2)

mark = len(text())
send(2)
pump(2.2)
send(3)
m = wait_for(r'POWER_OFF_CANCEL held_ms=\d+', 8, mark)
off = re.search(r'POWER_OFF held_ms', text()[mark:])
step('hold 2 s + release -> cancel', m and not off, m.group(0) if m else '')
pump(2)

mark = len(text())
send(1)
m = wait_for(r'POWER_SLEEP enter [^\r\n]*', 10, mark)
step('short -> sleep (USB)', m and 'light=0' in m.group(0), m.group(0) if m else '')
w = wait_for(r'WIFI_SLEEP stopped', 10, mark)
step('Wi-Fi stopped', w)
pump(8)
quiet = len(re.findall(r'LIVE_SAMPLE|BOTS_SAMPLE|SENSORS_POLL|PHONE_STATUS', text()[w.end() if w else mark:]))
step('no bridge polling while asleep', quiet == 0, 'polls=%d' % quiet)
mark = len(text())
send(1)
m = wait_for(r'POWER_WAKE [^\r\n]*', 10, mark)
step('short -> wake', m, m.group(0) if m else '')
t_wake = time.monotonic()
c = wait_for(r'STA_CONNECTED', 30, mark)
step('Wi-Fi rejoined', c, '%.1f s' % (time.monotonic() - t_wake) if c else '')
l = wait_for(r'LIVE_SAMPLE phase=\d+ http=200', 40, mark)
step('bridge live again', l)
pump(3)

mark = len(text())
send(4)
m = wait_for(r'POWER_SLEEP enter [^\r\n]*', 10, mark)
step('WPK1 4 -> light sleep', m and 'light=1' in m.group(0), m.group(0) if m else '')
m = wait_for(r'POWER_WAKE source=test [^\r\n]*', 45, mark)
step('light sleep -> timer wake', m, m.group(0) if m else '')
c = wait_for(r'STA_CONNECTED', 30, mark)
step('Wi-Fi rejoined after light sleep', c)
l = wait_for(r'LIVE_SAMPLE phase=\d+ http=200', 40, mark)
step('bridge live after light sleep', l)
pump(5)

t = text()
bad = re.findall(r'Guru|panic|FREEZE task|POWER_OFF_FAILED|abort\(\)', t)
roms = t.count('ESP-ROM:')
step('no panic/freeze/reset', not bad and roms <= 1, 'roms=%d bad=%s' % (roms, sorted(set(bad))))
masked = re.sub(r'https?://\S+', '<url>', t)
masked = re.sub(r'\b(\d{1,3}\.){3}\d{1,3}\b', '<ip>', masked)
log.write_text(masked)
print('PASS %d/%d' % (sum(ok for _, ok, _ in steps), len(steps)))
