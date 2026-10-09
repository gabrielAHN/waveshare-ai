#!/usr/bin/env python3
"""Tile evidence/settings-*.ppm into a labelled sheet with the rounded safe area outlined, re-check
that no non-background pixel falls outside it, and decode the QR state.

    cc -O1 -Idevices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main -Iplugins/hermes/firmware -Iplugins/home_assistant/firmware tests/preview/pair_preview.c devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/components/qrcodegen/qrcodegen.c -lm -o build/pair_preview
    PREVIEW_DIR=evidence build/pair_preview
    $SCRATCH/qrvenv/bin/python tests/preview/settings_sheet.py      # needs pillow (+ opencv for the QR decode)

Exit 1 on any pixel outside the safe area or a QR that does not decode. Host simulation only.
"""
import glob
import os
import sys
from PIL import Image, ImageDraw

EDGE, PANEL_R, HOME_BAND = 16, 56, 28   # SET_EDGE / SET_PANEL_R / SET_HOME_BAND in the device settings_ui.h


def safe(x, y):
    """Mirror of set_safe_px(): >= EDGE px from every edge and from the rounded corner arc."""
    if x < EDGE or x >= 368 - EDGE or y < EDGE or y >= 448 - HOME_BAND:
        return False
    cx = PANEL_R if x < PANEL_R else (368 - PANEL_R - 1 if x >= 368 - PANEL_R else x)
    cy = PANEL_R if y < PANEL_R else (448 - PANEL_R - 1 if y >= 448 - PANEL_R else y)
    r = PANEL_R - EDGE
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


EXPECTED = 'https://auth.example.com/consent/openid/device-authorization?user_code=QWRT7KXM'
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'evidence', 'settings-sheet.png')
files = sorted(glob.glob(os.path.join(root, 'evidence', 'settings-*.ppm')))
if not files:
    sys.exit('no previews: run build/pair_preview first')
cols, pad, label = 4, 12, 22
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (368 + pad) + pad, rows * (448 + label + pad) + pad), (40, 40, 44))
draw = ImageDraw.Draw(sheet)
bad = 0
for i, f in enumerate(files):
    img = Image.open(f).convert('RGB')
    bg = img.getpixel((0, 0))
    px = img.load()
    outside = sum(1 for y in range(448) for x in range(368) if px[x, y] != bg and not safe(x, y))
    name = os.path.basename(f)[len('settings-'):-4]
    print(f'SHEET {name:32s} outside_safe={outside}')
    bad += outside > 0
    x = pad + (i % cols) * (368 + pad)
    y = pad + (i // cols) * (448 + label + pad)
    draw.text((x + 2, y + 4), name, fill=(235, 235, 235))
    sheet.paste(img, (x, y + label))
    # rounded display edge (corner radius ~40 px measured), the safe area and the swipe-Home band, for the eye only
    draw.rounded_rectangle((x, y + label, x + 367, y + label + 447), radius=40, outline=(90, 90, 96))
    draw.rounded_rectangle((x + EDGE, y + label + EDGE, x + 367 - EDGE, y + label + 447 - HOME_BAND),
                           radius=PANEL_R - EDGE, outline=(0, 170, 200))
sheet.save(out)
print(out, len(files))


def decode(path):
    import cv2
    import numpy
    arr = cv2.cvtColor(numpy.array(Image.open(path).convert('RGB')), cv2.COLOR_RGB2BGR)
    if hasattr(cv2, 'QRCodeDetectorAruco'):
        text = cv2.QRCodeDetectorAruco().detectAndDecode(arr)[0]
        if text:
            return text
    return cv2.QRCodeDetector().detectAndDecode(arr)[0]


qr = [f for f in files if f.endswith('-qr.ppm')]
ok = bool(qr) and decode(qr[0]) == EXPECTED
print(f'QR_DECODE ok={ok}')
sys.exit(0 if ok and not bad else 1)
