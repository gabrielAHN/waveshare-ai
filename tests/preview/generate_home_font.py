"""Rasterize the local Menlo face to a bounded ASCII-only native atlas."""
from pathlib import Path
from PIL import Image, ImageFont, ImageDraw
font=ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',14)
rows=[]
for code in range(32,127):
 im=Image.new('L',(9,17));ImageDraw.Draw(im).text((0,0),chr(code),font=font,fill=255)
 rows.append(' {'+','.join(str(sum((1<<x) for x in range(9) if im.getpixel((x,y))>=100)) for y in range(17))+'}')
out=Path(__file__).resolve().parents[2]/'devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/home_font.h'
out.write_text('#pragma once\n#include <stdint.h>\n/* Local Menlo 14px raster, printable ASCII, 9x17. */\nstatic const uint16_t home_font[95][17]={\n'+',\n'.join(rows)+'\n};\n')
print(out)
