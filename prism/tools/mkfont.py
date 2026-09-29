#!/usr/bin/env python3
"""Rasterise the panel fonts into alpha atlases (.fnt) for the Vita build.

Usage: mkfont.py <fonts/src dir> <out dir>
Needs Pillow.
"""
import os, struct
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'fonts', 'src')
OUT = os.path.join(HERE, '..', 'fonts')

FACES = {
    'sans':  'IBMPlexSans-Regular.ttf',
    'sansm': 'IBMPlexSans-Medium.ttf',
    'sanss': 'IBMPlexSans-SemiBold.ttf',
    'mono':  'IBMPlexMono-Regular.ttf',
    'monom': 'IBMPlexMono-Medium.ttf',
}
# sizes used by the panel (px at 960 x 544)
WANT = [('sans', 11), ('sans', 12), ('sans', 13), ('sansm', 15), ('sanss', 17), ('sanss', 34),
        ('mono', 10), ('mono', 11), ('mono', 12), ('monom', 14), ('monom', 44)]
FIRST, COUNT = 32, 95


def build(face, size):
    font = ImageFont.truetype(os.path.join(SRC, FACES[face]), size)
    ascent, descent = font.getmetrics()
    glyphs = []
    pad = 1
    # measure every glyph relative to its baseline origin
    for i in range(COUNT):
        ch = chr(FIRST + i)
        l, t, r, b = font.getbbox(ch, anchor='ls')
        w, h = max(0, r - l), max(0, b - t)
        adv = font.getlength(ch)
        glyphs.append((ch, l, t, w, h, adv))
    # pack in rows
    aw = 256
    x = y = 0
    rowh = 0
    place = []
    for ch, l, t, w, h, adv in glyphs:
        if x + w + pad > aw:
            x = 0
            y += rowh + pad
            rowh = 0
        place.append((x, y))
        x += w + pad
        rowh = max(rowh, h)
    ah = y + rowh + pad
    atlas = Image.new('L', (aw, ah), 0)
    draw = ImageDraw.Draw(atlas)
    for (ch, l, t, w, h, adv), (px, py) in zip(glyphs, place):
        if w and h:
            # draw so the glyph's bbox lands at (px, py): origin at (px - l, py - t), baseline anchor
            draw.text((px - l, py - t), ch, font=font, fill=255, anchor='ls')
    height = ascent + descent
    out = bytearray()
    out += b'PFNT'
    out += struct.pack('<HHHHHH', height, ascent, FIRST, COUNT, aw, ah)
    for (ch, l, t, w, h, adv), (px, py) in zip(glyphs, place):
        out += struct.pack('<HHHHhhH', px, py, w, h, l, t, int(round(adv * 256)))
    out += atlas.tobytes()
    name = '%s%d.fnt' % (face, size)
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(out)
    print('%-12s %4d x %-4d %6d bytes' % (name, aw, ah, len(out)))


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for face, size in WANT:
        build(face, size)
