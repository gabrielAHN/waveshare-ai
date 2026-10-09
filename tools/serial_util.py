"""Shared serial helpers for tools/: Espressif USB autodetect (VID 0x303a) and CRC frames.

No port is hardcoded. ``--port`` overrides; otherwise exactly one attached Espressif device must
be present. Requires pyserial (available inside the ESP-IDF Python environment).
"""
import sys
import zlib

ESPRESSIF_VID = 0x303A


def espressif_ports():
    from serial.tools import list_ports
    return sorted(p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID)


def resolve_port(explicit=None):
    if explicit:
        return explicit
    ports = espressif_ports()
    if len(ports) != 1:
        sys.exit(f'Expected exactly one Espressif USB device (VID 0x303a), found {len(ports)}: '
                 f'{", ".join(ports) or "none"}. Pass --port.')
    return ports[0]


def frame(magic, payload):
    """``magic`` (4 bytes) + payload + CRC-32(payload) little-endian (firmware provision_crc)."""
    if len(magic) != 4:
        raise ValueError('magic must be 4 bytes')
    return magic + payload + zlib.crc32(payload).to_bytes(4, 'little')


def send(port, data, baud=115200):
    import serial
    with serial.Serial(port, baud, timeout=1) as s:
        s.write(data)
        s.flush()
