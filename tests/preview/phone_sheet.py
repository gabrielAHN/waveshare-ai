#!/usr/bin/env python3
"""Tile evidence/phone-*.ppm into a labelled contact sheet and DECODE the QR of the waiting panel.

Needs PIL plus a QR decoder (pyzbar+zbar or opencv). Put them in a scratch venv, e.g.
  python3 -m venv $SCRATCH/qrvenv && $SCRATCH/qrvenv/bin/pip install pillow opencv-python-headless
  $SCRATCH/qrvenv/bin/python tests/preview/phone_sheet.py
Exit 1 if the decoded payload differs from the URL the preview encoded. Host simulation only.
"""
import glob
import os
import sys
from PIL import Image, ImageDraw

EXPECTED = 'https://auth.example.com/consent/openid/device-authorization?user_code=QWRT7KXM'
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'evidence', 'phone-sheet.png')
files = sorted(glob.glob(os.path.join(root, 'evidence', 'phone-*.ppm')))
if not files:
    sys.exit('no previews: run build/phone_preview first')
cols, pad, label = 6, 10, 22
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (368 + pad) + pad, rows * (448 + label + pad) + pad), (40, 40, 44))
draw = ImageDraw.Draw(sheet)
for i, f in enumerate(files):
    x = pad + (i % cols) * (368 + pad)
    y = pad + (i // cols) * (448 + label + pad)
    draw.text((x + 2, y + 4), os.path.basename(f)[len('phone-'):-4], fill=(235, 235, 235))
    sheet.paste(Image.open(f), (x, y + label))
sheet.save(out)
print(out, len(files))


def decode(path):
    img = Image.open(path).convert('RGB')
    try:
        from pyzbar.pyzbar import decode as zdecode
        found = [r.data.decode() for r in zdecode(img)]
        if found:
            return found[0], 'pyzbar'
    except ImportError:
        pass
    import cv2
    import numpy
    arr = cv2.cvtColor(numpy.array(img), cv2.COLOR_RGB2BGR)
    # OpenCV 4.8+/5: the ArUco-based detector is the robust one; the classic detector misses some
    # perfectly valid synthetic symbols (verified: it returned '' on this panel while ArUco decoded it).
    if hasattr(cv2, 'QRCodeDetectorAruco'):
        text = cv2.QRCodeDetectorAruco().detectAndDecode(arr)[0]
        if text:
            return text, 'opencv-aruco'
    text, _, _ = cv2.QRCodeDetector().detectAndDecode(arr)
    return text, 'opencv'


waiting = [f for f in files if 'waiting-qr' in f]
text, how = decode(waiting[0])
ok = text == EXPECTED
print(f'QR_DECODE decoder={how} ok={ok} payload_host_path={text.split("?")[0]}')
sys.exit(0 if ok else 1)
