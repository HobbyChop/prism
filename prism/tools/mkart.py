#!/usr/bin/env python3
"""Draw the LiveArea artwork: icon0.png (128 x 128), bg.png (840 x 500) and
startup.png (280 x 158), in the panel's palette with the Plex fonts.

Usage: mkart.py <fonts/src dir> <sce_sys dir> [preview.png]
Needs Pillow. The images are written as 8-bit palette PNGs.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

BG, PANEL, PANEL2, LINE = (15, 17, 20), (23, 26, 31), (30, 35, 43), (38, 43, 51)
TEXT, TEXT2, GREY = (232, 234, 238), (184, 190, 200), (139, 146, 158)
MINT, MINT2, AMBER = (67, 200, 181), (111, 220, 204), (240, 179, 91)

fonts_dir = sys.argv[1]
out_dir = sys.argv[2]
preview = sys.argv[3] if len(sys.argv) > 3 else None

def font(name, px):
    return ImageFont.truetype(os.path.join(fonts_dir, name), px)

def gradient(w, h, top, bottom):
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return im

def tracked(draw, x, y, text, f, fill, spacing):
    for ch in text:
        draw.text((x, y), ch, font=f, fill=fill)
        x += f.getlength(ch) + spacing
    return x

def tracked_width(text, f, spacing):
    return sum(f.getlength(ch) + spacing for ch in text) - spacing

def glow_line(base, p0, p1, colour, width, glow):
    """a line with a soft halo, composited onto base (RGB)"""
    layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.line([p0, p1], fill=colour + (70,), width=glow)
    layer = layer.filter(ImageFilter.GaussianBlur(glow / 2))
    d = ImageDraw.Draw(layer)
    d.line([p0, p1], fill=colour + (255,), width=width)
    base.paste(layer, (0, 0), layer)

def prism(base, cx, cy, size, s, beam_from, fan_to, fan_spread):
    """the mark: a triangle, a beam in from the left, three lines out to the right.
    s is the supersampling factor; coordinates are in final pixels."""
    S = lambda v: v * s
    h = size * 0.866
    apex = (S(cx), S(cy - h * 0.6))
    left = (S(cx - size / 2), S(cy + h * 0.4))
    right = (S(cx + size / 2), S(cy + h * 0.4))
    # the entry point on the left face and the exit point on the right face
    t_in, t_out = 0.55, 0.5
    pin = (apex[0] + (left[0] - apex[0]) * t_in, apex[1] + (left[1] - apex[1]) * t_in)
    pout = (apex[0] + (right[0] - apex[0]) * t_out, apex[1] + (right[1] - apex[1]) * t_out)
    d = ImageDraw.Draw(base)
    d.polygon([apex, left, right], fill=PANEL2)
    # the beam in, and the path inside
    glow_line(base, (S(beam_from), pin[1] + S(size * 0.02)), pin, TEXT2, max(1, int(S(size * 0.035))), int(S(size * 0.12)))
    ImageDraw.Draw(base).line([pin, pout], fill=LINE, width=max(1, int(S(size * 0.03))))
    # the fan out: mint above, white in the middle, amber below
    for colour, k in ((MINT, -1), (TEXT, 0), (AMBER, 1)):
        end = (S(fan_to), pout[1] + S(fan_spread) * k)
        glow_line(base, pout, end, colour, max(1, int(S(size * 0.035))), int(S(size * 0.14)))
    d = ImageDraw.Draw(base)
    d.polygon([apex, left, right], outline=MINT, width=max(1, int(S(size * 0.045))))

def finish(im, s, path):
    im = im.resize((im.width // s, im.height // s), Image.LANCZOS)
    im = im.convert('RGB').quantize(256, dither=Image.Dither.NONE)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)
    return im

# ---- the icon ----------------------------------------------------------------
def make_icon(path):
    s = 4
    im = gradient(128 * s, 128 * s, PANEL, BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([3 * s, 3 * s, 125 * s, 125 * s], radius=22 * s, outline=LINE, width=2 * s)
    prism(im, 56, 66, 64, s, beam_from=8, fan_to=122, fan_spread=18)
    return finish(im, s, path)

# ---- the gate ------------------------------------------------------------------
def make_startup(path):
    s = 4
    im = gradient(280 * s, 158 * s, PANEL2, PANEL)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 280 * s - 1, 158 * s - 1], outline=LINE, width=2 * s)
    prism(im, 64, 82, 62, s, beam_from=10, fan_to=118, fan_spread=15)
    f1 = font('IBMPlexSans-SemiBold.ttf', 40 * s)
    f2 = font('IBMPlexMono-Medium.ttf', 12 * s)
    tracked(d, 124 * s, 46 * s, 'PRISM', f1, TEXT, 2 * s)
    tracked(d, 127 * s, 100 * s, 'SOUND MODULE', f2, MINT, 3 * s)
    d.line([(127 * s, 122 * s), (262 * s, 122 * s)], fill=LINE, width=s)
    return finish(im, s, path)

# ---- the background ------------------------------------------------------------
def make_bg(path):
    s = 2
    im = gradient(840 * s, 500 * s, BG, (19, 23, 30))
    d = ImageDraw.Draw(im)
    # faint hairlines echoing the panel
    for y in (44, 456):
        d.line([(0, y * s), (840 * s, y * s)], fill=LINE, width=s)
    # the mark on the left, its fan reaching across behind the gate
    prism(im, 170, 292, 180, s, beam_from=0, fan_to=840, fan_spread=42)
    # the wordmark, top right, clear of the gate
    f1 = font('IBMPlexSans-SemiBold.ttf', 84 * s)
    f2 = font('IBMPlexMono-Medium.ttf', 20 * s)
    f3 = font('IBMPlexMono-Regular.ttf', 15 * s)
    w = tracked_width('PRISM', f1, 6 * s)
    x = 812 * s - w
    tracked(d, x, 52 * s, 'PRISM', f1, TEXT, 6 * s)
    tracked(d, x + 4 * s, 150 * s, 'SOUND MODULE', f2, MINT, 4 * s)
    tracked(d, 812 * s - tracked_width('SOUNDFONT GM  +  MT-32  +  PSP-MIDI', f3, s), 424 * s, 'SOUNDFONT GM  +  MT-32  +  PSP-MIDI', f3, GREY, s)
    return finish(im, s, path)

icon = make_icon(os.path.join(out_dir, 'icon0.png'))
gate = make_startup(os.path.join(out_dir, 'livearea', 'contents', 'startup.png'))
bg = make_bg(os.path.join(out_dir, 'livearea', 'contents', 'bg.png'))

with open(os.path.join(out_dir, 'livearea', 'contents', 'template.xml'), 'w', newline='\n') as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n'
            '<livearea style="a1" format-ver="01.00" content-rev="1">\n'
            '  <livearea-background>\n'
            '    <image>bg.png</image>\n'
            '  </livearea-background>\n'
            '  <gate>\n'
            '    <startup-image>startup.png</startup-image>\n'
            '  </gate>\n'
            '</livearea>\n')

if preview:
    # the LiveArea as the Vita lays it out: the gate over the middle of the
    # background, the icon shown beside it for reference
    p = Image.new('RGB', (840, 640), BG)
    p.paste(bg.convert('RGB'), (0, 0))
    p.paste(gate.convert('RGB'), (280, 171))
    ImageDraw.Draw(p).rectangle([280, 171, 559, 328], outline=LINE)
    p.paste(icon.convert('RGB'), (24, 512))
    f = font('IBMPlexMono-Regular.ttf', 14)
    ImageDraw.Draw(p).text((168, 560), 'icon0.png 128 x 128   bg.png 840 x 500   startup.png 280 x 158 (the gate, in the middle)', font=f, fill=GREY)
    p.save(preview)
print('icon', icon.size, icon.mode, 'gate', gate.size, gate.mode, 'bg', bg.size, bg.mode)
