#!/usr/bin/env python3
"""Give the board its Wi-Fi network over USB (the board has no on-screen keyboard).

    python tools/set_wifi.py                 # asks for the network name and password here, on the host
    python tools/set_wifi.py --env           # home Wi-Fi AND the iPhone hotspot from the repo's .env
    python tools/set_wifi.py --hotspot       # asks for the iPhone Personal Hotspot instead of home Wi-Fi

The board joins home Wi-Fi first and falls back to the iPhone hotspot when home is out of reach
(it checks for home again every 3 minutes while on the hotspot).
    python tools/set_wifi.py --forget        # forget the saved network (WPC1 action 7)

The board is reset, and the one CRC-checked WSP1 frame is sent inside its 90 s boot provisioning
window (``USB_PROVISION_READY``). The password is read with getpass (or from stdin when piped), is
never a command-line argument, is never printed or logged, and the board logs only
``USB_PROVISION_SAVED`` / ``STA_CONNECTED``. Use the ESP-IDF Python (it has pyserial + esptool):

    ~/.espressif/python_env/idf6.0_py3.12_env/bin/python tools/set_wifi.py
Port: --port or the single attached Espressif device (USB VID 0x303a).
"""
import argparse
import getpass
import pathlib
import sys
import time
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from serial_util import frame, resolve_port  # noqa: E402

SSID_FIELD, PASSWORD_FIELD, RESERVED = 33, 64, 225   # provision_config in named-device main/provision.h


NET_HOME, NET_HOTSPOT = 0, 1   # provision_config.reserved[0] (PROVISION_NET_* in provision.h)


def wifi_frame(ssid: str, password: str, net: int = NET_HOME) -> bytes:
    s, p = ssid.encode(), password.encode()
    if not 1 <= len(s) <= 32:
        raise ValueError('network name must be 1-32 bytes')
    if p and not 8 <= len(p) <= 63:
        raise ValueError('password must be 8-63 characters (or empty for an open network)')
    body = s.ljust(SSID_FIELD, b'\0') + p.ljust(PASSWORD_FIELD, b'\0') + bytes([net]) + bytes(RESERVED - 1)
    return b'WSP1' + body + zlib.crc32(body).to_bytes(4, 'little')


def read_env(path: pathlib.Path) -> list[tuple[int, str, str]]:
    """Networks from a KEY=VALUE file (optional quotes): WAVESHARE_AI_WIFI_SSID/_PASSWORD (home) and
    WAVESHARE_AI_HOTSPOT_SSID/_PASSWORD (iPhone Personal Hotspot); at least one must be set.
    The file must not be readable by other users; values are never printed."""
    if not path.is_file():
        raise ValueError(f'{path.name} not found: copy .env.example to .env and fill it in')
    if path.stat().st_mode & 0o077:
        raise ValueError(f'{path.name} is readable by other users: run chmod 600 {path.name}')
    values = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, value = line.split('=', 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
            value = value[1:-1]
        values[key.strip()] = value
    nets = []
    for net, prefix in ((NET_HOME, 'WAVESHARE_AI_WIFI'), (NET_HOTSPOT, 'WAVESHARE_AI_HOTSPOT')):
        ssid = values.get(prefix + '_SSID', '')
        if ssid:
            nets.append((net, ssid, values.get(prefix + '_PASSWORD', '')))
    if not nets:
        raise ValueError(f'set WAVESHARE_AI_WIFI_SSID and/or WAVESHARE_AI_HOTSPOT_SSID in {path.name}')
    return nets


def wait_for(ser, markers, seconds):
    buf, end = bytearray(), time.monotonic() + seconds
    while time.monotonic() < end:
        buf.extend(ser.read(4096))
        text = bytes(buf[buf.rfind(b'ESP-ROM:'):] if b'ESP-ROM:' in buf else buf)
        for m in markers:
            if m in text:
                return m, text
    return None, bytes(buf)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port')
    ap.add_argument('--forget', action='store_true', help='forget the saved network instead')
    ap.add_argument('--hotspot', action='store_true', help='the network typed here is the iPhone Personal Hotspot')
    ap.add_argument('--env', nargs='?', const=str(pathlib.Path(__file__).resolve().parent.parent / '.env'),
                    help='read the network from this .env file (default: the repo .env)')
    ap.add_argument('--dry-run', action='store_true', help='build the frame, print its length, send nothing')
    a = ap.parse_args()
    frames = []   # (label, bytes)
    if a.forget:
        frames.append(('forget', frame(b'WPC1', bytes([7]))))
    else:
        try:
            if a.env:
                nets = read_env(pathlib.Path(a.env))
            else:
                what = 'iPhone hotspot name' if a.hotspot else 'Wi-Fi network name'
                ssid = input(f'{what}: ').strip() if sys.stdin.isatty() else sys.stdin.readline().rstrip('\n')
                password = getpass.getpass('Password (empty for open): ') if sys.stdin.isatty() else sys.stdin.readline().rstrip('\n')
                nets = [(NET_HOTSPOT if a.hotspot else NET_HOME, ssid, password)]
            for net, ssid, password in nets:
                frames.append(('hotspot' if net == NET_HOTSPOT else 'home', wifi_frame(ssid, password, net)))
        except ValueError as e:
            print(f'error: {e}', file=sys.stderr)
            return 2
        nets = password = None
    for label, data in frames:
        print(f'{label}: {len(data)}-byte frame {data[:4].decode()}')
    if a.dry_run:
        return 0
    import serial
    from esptool.reset import HardReset
    port = resolve_port(a.port)
    # The board takes ONE frame per boot provisioning window (saving closes it), so each network
    # gets its own reset. The home network goes last, so the board joins it right away.
    frames.sort(key=lambda f: f[0] == 'home')
    status = 0
    for label, data in frames:
        ser = serial.Serial(port, 115200, timeout=0.1)
        ser.dtr = False
        try:
            HardReset(ser, uses_usb=True)()
            for _ in range(50):          # the USB CDC endpoint re-enumerates after the reset
                try:
                    if not ser.is_open:
                        ser.open()
                    break
                except serial.SerialException:
                    ser.close()
                    time.sleep(0.2)
            hit, _ = wait_for(ser, [b'USB_PROVISION_READY'], 30)
            if not hit:
                print('board did not open its provisioning window (is this Waveshare AI firmware?)', file=sys.stderr)
                return 1
            ser.write(data)
            ser.flush()
            data = None
            want = [b'PAIR_COMMAND action=7 ok=1', b'PAIR_COMMAND action=7 ok=0'] if a.forget else \
                   [b'USB_PROVISION_SAVED', b'USB_PROVISION_FAILED', b'USB_PROVISION_REJECTED']
            hit, _ = wait_for(ser, want, 10)
            print(f'board ({label}): {hit.decode() if hit else "no answer"}')
            if a.forget:
                return 0 if hit and b'ok=1' in hit else 1
            if hit != b'USB_PROVISION_SAVED':
                status = 1
                continue
            hit, _ = wait_for(ser, [b'STA_CONNECTED', b'Auth rejected'], 30)
            print(f'board ({label}): ' + ('joined a network' if hit == b'STA_CONNECTED' else
                  'saved, not connected yet (in range? name/password right?)'))
        finally:
            ser.close()
    frames = None
    return status


if __name__ == '__main__':
    raise SystemExit(main())
