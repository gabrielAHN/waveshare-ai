#!/usr/bin/env python3
"""Tile evidence/bots-*.ppm into one labelled PNG contact sheet (needs PIL: /usr/bin/python3)."""
import glob
import os
import sys
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'evidence', 'bots-sheet.png')
files = sorted(f for f in glob.glob(os.path.join(root, 'evidence', 'bots-*.ppm')))
if not files:
    sys.exit('no previews')
cols, pad, label = 6, 10, 22
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (368 + pad) + pad, rows * (448 + label + pad) + pad), (40, 40, 44))
draw = ImageDraw.Draw(sheet)
for i, f in enumerate(files):
    x = pad + (i % cols) * (368 + pad)
    y = pad + (i // cols) * (448 + label + pad)
    draw.text((x + 2, y + 4), os.path.basename(f)[len('bots-'):-4], fill=(235, 235, 235))
    sheet.paste(Image.open(f), (x, y + label))
sheet.save(out)
print(out, len(files))
