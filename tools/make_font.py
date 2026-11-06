#!/usr/bin/env python3
"""Builds assets/font.ntex: 16x6 grid of 16px cells, ASCII 32..127, white with alpha."""
import struct, sys
from PIL import Image, ImageDraw, ImageFont
out = sys.argv[1] if len(sys.argv) > 1 else 'assets/font.ntex'
try:
    font = ImageFont.load_default(size=14)
except TypeError:
    font = ImageFont.load_default()
im = Image.new('RGBA', (256, 96), (255, 255, 255, 0))
d = ImageDraw.Draw(im)
for k in range(96):
    ch = chr(32 + k)
    x, y = (k % 16) * 16, (k // 16) * 16
    d.text((x + 1, y), ch, font=font, fill=(255, 255, 255, 255))
im.save(out.replace('.ntex', '_preview.png'))
widths = bytes(max(3, min(15, int(round(font.getlength(chr(32 + k)))))) for k in range(96))
open(out.replace('.ntex', '.w'), 'wb').write(widths)
with open(out, 'wb') as f:
    f.write(b'NTEX' + struct.pack('<II', *im.size) + im.tobytes())
