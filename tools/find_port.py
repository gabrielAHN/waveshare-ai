#!/usr/bin/env python3
"""Print the serial port of the single attached Espressif USB device (VID 0x303a).

Enumeration only: no port is opened. Exits non-zero when zero or several devices match, so a
flash never goes to the wrong board. Needs pyserial (available inside the ESP-IDF Python env).
"""
import sys

ESPRESSIF_VID = 0x303A


def espressif_ports(ports):
    return sorted(p.device for p in ports if getattr(p, 'vid', None) == ESPRESSIF_VID)


def main(argv=None):
    from serial.tools import list_ports
    found = espressif_ports(list_ports.comports())
    if len(found) != 1:
        print(f'expected exactly one Espressif USB serial device (VID 0x303a), found {len(found)}: '
              f'{", ".join(found) or "none"}; set PORT explicitly', file=sys.stderr)
        return 1
    print(found[0])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
