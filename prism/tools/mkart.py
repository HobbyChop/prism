#!/usr/bin/env python3
"""Draw the LiveArea artwork: icon0.png (128 x 128), bg.png (840 x 500) and
startup.png (280 x 158), in the panel's palette with the Plex fonts.

Usage: mkart.py <fonts/src dir> <sce_sys dir> [preview.png] [--style crystal|spectrum]
Needs Pillow. The images are written as 8-bit palette PNGs.
"""
import os, sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

BG, PANEL, PANEL2, LINE = (15, 17, 20), (23, 26, 31), (30, 35, 43), (38, 43, 51)
TEXT, TEXT2, GREY = (232, 234, 238), (184, 190, 200), (139, 146, 158)
MINT, MINT2, AMBER = (67, 200, 181), (111, 220, 204), (240, 179, 91)

args = [a for a in sys.argv[1:] if not a.startswith('--')]
style = 'crystal'
for a in sys.argv[1:]:
    if a.startswith('--style'):
        style = a.split('=', 1)[1] if '=' in a else sys.argv[sys.argv.index(a) + 1]
        if style in args: args.remove(style)
fonts_dir = args[0]
out_dir = args[1]
preview = args[2] if len(args) > 2 else None

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

def glow(base, box, colour, alpha, blur):
    layer = Image.new('RGBA', base.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse(box, fill=colour + (alpha,))
    layer = layer.filter(ImageFilter.GaussianBlur(blur))
    base.paste(layer, (0, 0), layer)

# ---- the marks -------------------------------------------------------------------
def mark_crystal(base, cx, cy, h, s):
    """a six-sided shard, lit from the left: mint on the top facet, amber at the foot"""
    S = lambda v: v * s
    w = h * 0.44
    T, B = (S(cx), S(cy - h)), (S(cx), S(cy + h))
    UL, UR = (S(cx - w), S(cy - h / 3)), (S(cx + w), S(cy - h / 3))
    LL, LR = (S(cx - w), S(cy + h / 3)), (S(cx + w), S(cy + h / 3))
    C1, C2 = (S(cx), S(cy - h / 3)), (S(cx), S(cy + h / 3))
    glow(base, [S(cx - w * 1.6), S(cy - h * 1.1), S(cx + w * 1.6), S(cy + h * 1.1)], MINT, 40, S(h * 0.25))
    d = ImageDraw.Draw(base, 'RGBA')
    d.polygon([T, UL, C1], fill=MINT + (140,))
    d.polygon([T, C1, UR], fill=(34, 40, 48, 255))
    d.polygon([UL, LL, C2, C1], fill=(44, 50, 60, 255))
    d.polygon([C1, C2, LR, UR], fill=(24, 28, 34, 255))
    d.polygon([LL, B, C2], fill=(30, 35, 43, 255))
    d.polygon([C2, B, LR], fill=AMBER + (205,))
    thin = max(1, int(S(h * 0.014)))
    for p, q in ((T, B), (UL, UR), (LL, LR)):
        d.line([p, q], fill=MINT2 + (120,), width=thin)
    d.line([T, UR, LR, B, LL, UL, T], fill=MINT + (255,), width=max(1, int(S(h * 0.04))), joint='curve')
    # the glint on the lit facet
    d.line([(S(cx - w * 0.42), S(cy - h * 0.48)), (S(cx - w * 0.16), S(cy - h * 0.78))], fill=TEXT + (230,), width=max(1, int(S(h * 0.035))))

def mark_spectrum(base, cx, cy, h, s):
    """eleven bars in a diamond silhouette, mint through white to amber"""
    S = lambda v: v * s
    n = 11
    bw, gap = h * 0.11, h * 0.07
    total = n * bw + (n - 1) * gap
    x0 = cx - total / 2
    glow(base, [S(cx - total * 0.7), S(cy - h * 1.1), S(cx + total * 0.7), S(cy + h * 1.1)], MINT, 36, S(h * 0.25))
    d = ImageDraw.Draw(base, 'RGBA')
    for i in range(n):
        env = 1.0 - abs(i - 5) / 6.0
        bh = h * (0.28 + 0.72 * env)
        x = x0 + i * (bw + gap)
        colour = MINT if i < 4 else TEXT if i < 7 else AMBER
        d.rounded_rectangle([S(x), S(cy - bh), S(x + bw), S(cy + bh)], radius=S(bw / 2), fill=colour + (255,))

def mark(base, cx, cy, h, s):
    (mark_crystal if style == 'crystal' else mark_spectrum)(base, cx, cy, h, s)

def finish(im, s, path):
    im = im.resize((im.width // s, im.height // s), Image.LANCZOS)
    im = im.convert('RGB').quantize(256, dither=Image.Dither.NONE)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)
    return im

def strip(base, s, y, x0, x1, count, height):
    """a quiet meter strip along a hairline"""
    d = ImageDraw.Draw(base, 'RGBA')
    step = (x1 - x0) / count
    for i in range(count):
        t = i / count
        v = 0.5 + 0.5 * math.sin(t * 19.0) * math.cos(t * 7.3 + 1.0)
        bh = height * (0.15 + 0.85 * v * v)
        x = x0 + i * step
        d.rectangle([x * s, (y - bh) * s, (x + step * 0.55) * s, y * s], fill=LINE + (255,))

# ---- the icon ----------------------------------------------------------------
def make_icon(path):
    s = 4
    im = gradient(128 * s, 128 * s, PANEL, BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([3 * s, 3 * s, 125 * s, 125 * s], radius=22 * s, outline=LINE, width=2 * s)
    mark(im, 64, 64, 44 if style == 'crystal' else 38, s)
    return finish(im, s, path)

# ---- the gate ------------------------------------------------------------------
def make_startup(path):
    s = 4
    im = gradient(280 * s, 158 * s, PANEL2, PANEL)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 280 * s - 1, 158 * s - 1], outline=LINE, width=2 * s)
    mark(im, 62, 79, 50 if style == 'crystal' else 42, s)
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
    for y in (44, 456):
        d.line([(0, y * s), (840 * s, y * s)], fill=LINE, width=s)
    strip(im, s, 456, 60, 780, 96, 26)
    mark(im, 150, 250, 150 if style == 'crystal' else 120, s)
    f1 = font('IBMPlexSans-SemiBold.ttf', 84 * s)
    f2 = font('IBMPlexMono-Medium.ttf', 20 * s)
    f3 = font('IBMPlexMono-Regular.ttf', 15 * s)
    w = tracked_width('PRISM', f1, 6 * s)
    x = 812 * s - w
    tracked(d, x, 52 * s, 'PRISM', f1, TEXT, 6 * s)
    tracked(d, x + 4 * s, 150 * s, 'SOUND MODULE', f2, MINT, 4 * s)
    line = 'SOUNDFONT GM  +  MT-32  +  PSP-MIDI'
    tracked(d, 812 * s - tracked_width(line, f3, s), 424 * s, line, f3, GREY, s)
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
    ImageDraw.Draw(p).text((168, 560), style + ': icon0 128 x 128   bg 840 x 500   startup 280 x 158 (the gate)', font=f, fill=GREY)
    p.save(preview)
print(style, 'icon', icon.size, icon.mode, 'gate', gate.size, gate.mode, 'bg', bg.size, bg.mode)
