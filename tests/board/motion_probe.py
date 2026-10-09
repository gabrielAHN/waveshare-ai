"""Motion + theme checks on the board (USB-injected gestures; nothing physical).

  ~/.espressif/python_env/idf6.0_py3.12_env/bin/python tests/board/motion_probe.py LOG
boot -> HOME_SNAPSHOT ok=1, THEME_LOADED -> Hermes signed in -> WLV1 3 (Ask) -> 3x [WGS1 3 edge swipe Home
-> HOME_GESTURE kind=home, page 0 | WGS1 5 Home tile swipe + tap -> page 3] -> frame timing during slides
-> no panic. Prints a summary; the log is saved with URLs/IPs masked."""
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
port = resolve_port(None)
buf = bytearray()
s = serial.Serial(port, 115200, timeout=0.15)
s.dtr = False
HardReset(s, uses_usb=True)()
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


def send(magic, value):
    s.write(frame(magic, bytes([value])))
    s.flush()


def step(name, ok, detail=''):
    steps.append((name, bool(ok)))
    print('%-30s %s %s' % (name, 'OK  ' if ok else 'FAIL', detail), flush=True)


def pages_after(mark):
    return [int(p) for p in re.findall(r'HOME_PAGE page=(\d+)', text()[mark:])]


m = wait_for(r'HOME_SNAPSHOT [^\r\n]*', 30)
step('page snapshot frame', m and 'ok=1' in m.group(0), m.group(0) if m else '')
m = wait_for(r'THEME_LOADED [^\r\n]*', 30)
step('theme loaded', m, m.group(0) if m else '')
m = wait_for(r'PHONE_STATUS [^\r\n]*provider=hermes', 60)
step('hermes signed in', m and 'state=authorized' in m.group(0))
m2 = wait_for(r'PHONE_STATUS [^\r\n]*provider=home_assistant[^\r\n]*', 60)
step('home assistant signed in', m2 and 'state=authorized' in m2.group(0))
pump(3)
sh = re.findall(r'PHONE_STATUS [^\r\n]*shared_tab=(\d)', text())
step('one shared account tab', sh and sh[-1] == '1', 'shared_tab=%s' % (sh[-1] if sh else '-'))
mark = len(text())
send(b'WLV1', 3)
m = wait_for(r'SERVICE_SELECT page=3', 10, mark)
step('WLV1 3 -> Ask', m)
pump(6)

slide_rows = []
for i in range(3):
    mark = len(text())
    send(b'WGS1', 3)
    g = wait_for(r'HOME_GESTURE [^\r\n]*', 8, mark)
    pump(1.5)
    pg = pages_after(mark)
    step('edge swipe -> Home #%d' % (i + 1), g and 'kind=home' in g.group(0) and pg and pg[-1] == 0,
         '%s last_page=%s' % (g.group(0) if g else '-', pg[-1] if pg else '-'))
    slide_rows.append(text()[mark:])
    pump(2)
    mark = len(text())
    send(b'WGS1', 5)
    pump(2.5)
    pg = pages_after(mark)
    step('Home swipe+tap -> Ask #%d' % (i + 1), pg and pg[-1] == 3, 'last_page=%s' % (pg[-1] if pg else '-'))
    slide_rows.append(text()[mark:])
    pump(3)

# Ask page frame rate at rest (Kotaro animating): DIRECT_TIMING rows on page 3 after the last gesture
mark = len(text())
pump(12)
ask = [int(v) for v in re.findall(r'DIRECT_TIMING [^\r\n]*interval_us=(\d+)[^\r\n]*', text()[mark:])]
ask = sorted(v for v in ask if 0 < v < 2_000_000)
if ask:
    step('Ask page frames (Kotaro)', True, 'n=%d median=%.1f ms (%.1f fps) worst=%.1f ms' % (len(ask), ask[len(ask)//2]/1000, 1e6/ask[len(ask)//2], ask[-1]/1000))
# frame timing while things move: DIRECT_TIMING interval_us inside the 1.5 s after each gesture
iv = []
for seg in slide_rows:
    for r in re.findall(r'DIRECT_TIMING [^\r\n]*interval_us=(\d+)', seg)[:40]:
        iv.append(int(r))
iv = [v for v in iv if 0 < v < 2_000_000]
if iv:
    iv.sort()
    med = iv[len(iv) // 2]
    step('frames during slides', True, 'n=%d median_interval=%.1f ms (%.1f fps) worst=%.1f ms' % (
        len(iv), med / 1000, 1e6 / med, iv[-1] / 1000))
else:
    step('frames during slides', False, 'no DIRECT_TIMING rows')
slides = re.findall(r'HOME_SLIDE [^\r\n]*', text())
print('HOME_SLIDE lines:', len(slides), slides[:3])

t = text()
bad = re.findall(r'Guru|panic|FREEZE task|abort\(\)|stack overflow', t)
roms = t.count('ESP-ROM:')
step('no panic/reset', not bad and roms <= 1, 'roms=%d bad=%s' % (roms, sorted(set(bad))))
masked = re.sub(r'https?://\S+', '<url>', t)
masked = re.sub(r'\b(\d{1,3}\.){3}\d{1,3}\b', '<ip>', masked)
log.write_text(masked)
print('PASS %d/%d' % (sum(ok for _, ok in steps), len(steps)))
