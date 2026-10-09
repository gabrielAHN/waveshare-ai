"""Generate compiled RGB565 tile art from the product's actual native renderer."""
from pathlib import Path
from PIL import Image,ImageDraw
import math
R=Path(__file__).resolve().parents[2]
FIRMWARE=R/'devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware'
image=Image.open(R/'evidence/session-tile-source.ppm').crop((0,70,368,306)).resize((280,180),Image.Resampling.LANCZOS).convert('RGB')
gear=Image.new('RGB',(96,96),(190,214,209));d=ImageDraw.Draw(gear)
points=[]
for i in range(48):
 angle=i*math.tau/48;radius=40 if i%6 in (1,2,3) else 32
 points.append((48+math.cos(angle)*radius,48+math.sin(angle)*radius))
d.polygon(points,fill=(51,75,82));d.ellipse((34,34,62,62),fill=(190,214,209))
parts=['#pragma once\n#include <stdint.h>\n/* Generated scene still and settings gear. No user/network data. */\n']
for name,im in [('session_tile_art',image),('session_gear_art',gear)]:
 values=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in im.getdata()]
 parts.append('static const uint16_t '+name+'['+str(len(values))+']={\n')
 parts.extend(','.join(hex(v) for v in values[i:i+24])+',\n' for i in range(0,len(values),24));parts.append('};\n')
(FIRMWARE/'main/tile_art.h').write_text(''.join(parts))
image.save(R/'evidence/session-tile-art.png');gear.save(R/'evidence/session-gear-art.png')
print('Generated bounded 280x180 scene still and 96x96 gear')
