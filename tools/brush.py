"""Brush-painted calligraphy and woodblock-print stills for Kōmō.

All images are palette-index arrays. Calligraphy uses the title palette (BG bank 8):
1 ink, 2 dark red, 3 red, 4 orange-red, 5 gold, 6 cream.
"""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from backgrounds import CJK_BLACK

R = np.random.default_rng(1651)


def glyphs(text, size, spacing=0):
    f = ImageFont.truetype(CJK_BLACK, size)
    im = Image.new("L", (size * len(text) * 2 + 16, size * 2 + 16), 0)
    d = ImageDraw.Draw(im)
    x = 8
    for ch in text:
        d.text((x, 8), ch, font=f, fill=255)
        bb = d.textbbox((x, 8), ch, font=f)
        x = bb[2] + spacing
    a = np.array(im) > 110
    ys, xs = np.nonzero(a)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def dilate(m, n=1):
    out = m.copy()
    for _ in range(n):
        g = out.copy()
        g[1:] |= out[:-1]; g[:-1] |= out[1:]; g[:, 1:] |= out[:, :-1]; g[:, :-1] |= out[:, 1:]
        out = g
    return out


def brushify(mask, seed=0):
    """Rough edges, dry-brush streaks running along the strokes, and ink splatter."""
    rng = np.random.default_rng(seed)
    h, w = mask.shape
    ys, xs = np.mgrid[0:h, 0:w]
    # dry brush: thin horizontal streaks of missing ink, strongest at the right ends of strokes
    streak = np.sin(ys * 1.9 + np.sin(xs * 0.11) * 2.0) > 0.93
    right_end = np.zeros_like(mask)
    right_end[:, :-3] = mask[:, :-3] & ~mask[:, 3:]
    right_end = dilate(right_end, 3) & mask
    m = mask & ~(streak & right_end)
    # ragged edges
    edge = mask & ~(np.roll(mask, 1, 0) & np.roll(mask, -1, 0) & np.roll(mask, 1, 1) & np.roll(mask, -1, 1))
    m &= ~(edge & (rng.random(mask.shape) < 0.12))
    # splatter
    sp = np.zeros_like(mask)
    for _ in range(int(mask.sum() / 90)):
        y, x = rng.integers(0, h), rng.integers(0, w)
        if not mask[y, x] and dilate(mask, 6)[y, x]:
            sp[y, x] = True
            if rng.random() < 0.4 and x + 1 < w:
                sp[y, x + 1] = True
    return m, sp


def calligraphy(text, size, style="red", spacing=2, seed=0, pad=4):
    """Returns an index image (h, w multiple of 8) on the title palette."""
    g = dilate(glyphs(text, size, spacing), 1)
    m, sp = brushify(g, seed)
    h, w = m.shape
    H = (h + pad * 2 + 7) // 8 * 8
    W = (w + pad * 2 + 7) // 8 * 8
    img = np.zeros((H, W), int)
    oy, ox = (H - h) // 2, (W - w) // 2
    M = np.zeros((H, W), bool); M[oy:oy + h, ox:ox + w] = m
    SP = np.zeros((H, W), bool); SP[oy:oy + h, ox:ox + w] = sp
    out = dilate(M | SP, 1)
    img[out] = 1
    if style == "red":
        ys = np.arange(H)[:, None]
        img[M] = 3
        img[M & ~np.roll(M, 1, 0)] = 4                  # lit top edges
        img[M & ~np.roll(M, -1, 0)] = 2                 # dark lower edges
        img[SP] = 2
    else:                                               # cream on ink
        img[M] = 6
        img[M & ~np.roll(M, -1, 0)] = 5
        img[SP] = 6
    return img


# ======================================================================= woodblock stills (240x160, 16 colours)
PAL_STORM = [(0, 0, 0), (10, 12, 26), (24, 30, 62), (36, 52, 100), (56, 82, 140), (96, 128, 178),
             (160, 186, 214), (236, 232, 214), (250, 246, 232), (70, 44, 28), (120, 78, 44),
             (176, 132, 80), (232, 196, 80), (150, 30, 24), (30, 24, 20), (200, 210, 220)]
PAL_DAWN = [(0, 0, 0), (20, 16, 30), (60, 40, 70), (130, 60, 70), (214, 92, 60), (246, 150, 80),
            (252, 206, 130), (252, 236, 196), (70, 84, 120), (110, 128, 160), (164, 178, 196),
            (40, 52, 80), (200, 40, 30), (90, 60, 40), (150, 110, 70), (240, 244, 250)]


def _poly(img, pts, col):
    im = Image.fromarray(np.zeros(img.shape, np.uint8))
    ImageDraw.Draw(im).polygon([tuple(p) for p in pts], fill=1)
    m = np.array(im) > 0
    img[m] = col
    return m


def _outline(img, m, col=1):
    e = dilate(m, 1) & ~m
    img[e & (img != 0)] = col
    img[e & (img == 0)] = col


def still_storm():
    """Pieter's ship in the storm: a great curling wave in the woodblock manner."""
    H, W = 160, 240
    ys, xs = np.mgrid[0:H, 0:W]
    img = np.full((H, W), 2, int)
    # sky in flat bands, a ragged cloud mass, lightning
    img[ys < 40] = 1
    img[(ys >= 40) & (ys < 64)] = 2
    cl = (ys < 60 + 10 * np.sin(xs * 0.05) + 6 * np.sin(xs * 0.13)) & (ys > 14 + 8 * np.sin(xs * 0.07 + 1))
    img[cl] = 3
    img[cl & (((xs // 3) + (ys // 3)) % 7 == 0)] = 4
    x, y = 182.0, 0.0
    while y < 60:
        nx, ny = x + R.uniform(-6, 5), y + R.uniform(5, 9)
        for t in np.linspace(0, 1, 12):
            px, py = int(x + (nx - x) * t), int(y + (ny - y) * t)
            if 0 <= py < H and 0 <= px < W - 1:
                img[py, px] = 12; img[py, px + 1] = 8
        x, y = nx, ny
    # the sea, rising into the great wave from the left
    sea_top = 104 + 6 * np.sin(xs * 0.09)
    img[ys >= sea_top] = 3
    img[(ys >= sea_top) & (((ys - sea_top).astype(int) % 7) == 0)] = 4
    # great wave: a curl rising to the upper left, its crest breaking into claws of foam
    cx, cy, r = 60.0, 70.0, 58.0
    ang = np.arctan2(ys - cy, xs - cx)
    d = np.hypot(xs - cx, ys - cy)
    body = (d < r) & (d > r - 26) & (ang > -2.9) & (ang < 0.9)
    body |= (xs < 120) & (ys > cy + 10) & (ys < H)
    img[body] = 4
    inner = body & (d < r - 10) & (d > r - 20)
    img[inner] = 5
    img[body & (ys > cy + 18)] = 3                          # the darker trough below the curl
    stripes = body & (d > r - 22) & ((np.floor(ang * 26) % 4) == 0) & (ys < cy + 24)
    img[stripes] = 5                                        # streaks running up the wave face
    # foam crest with claws
    crest = (d >= r - 4) & (d < r + 3) & (ang > -2.9) & (ang < -0.2)
    img[crest] = 7
    for k in range(14):
        a0 = -2.8 + k * 0.19
        bx, by = cx + math.cos(a0) * (r + 2), cy + math.sin(a0) * (r + 2)
        for j in range(5):
            px = int(bx + math.cos(a0 + 1.9) * j * 1.6 + math.cos(a0) * j * 0.8)
            py = int(by + math.sin(a0 + 1.9) * j * 1.6 + math.sin(a0) * j * 0.8)
            if 0 <= px < W and 0 <= py < H:
                img[py, px] = 8
                if px + 1 < W: img[py, px + 1] = 7
    # spray dots
    for _ in range(140):
        a0 = R.uniform(-2.9, -0.4)
        rr = r + R.uniform(4, 16)
        px, py = int(cx + math.cos(a0) * rr), int(cy + math.sin(a0) * rr)
        if 0 <= px < W and 0 <= py < H:
            img[py, px] = 8
    # the Dutch ship, heeled over on the wave's back
    sx, sy, tilt = 160, 102, -0.28
    ct, st_ = math.cos(tilt), math.sin(tilt)

    def rot(p):
        return (sx + p[0] * ct - p[1] * st_, sy + p[0] * st_ + p[1] * ct)
    hull = _poly(img, [rot(p) for p in ((-34, -6), (30, -10), (40, -18), (36, 4), (-26, 10), (-38, 2))], 10)
    _outline(img, hull)
    _poly(img, [rot(p) for p in ((-30, -4), (32, -9), (33, -6), (-30, -1))], 11)
    for mx_, mh in ((-18, 44), (2, 54), (22, 40)):
        top = rot((mx_, -10 - mh))
        base = rot((mx_, -8))
        for t in np.linspace(0, 1, 60):
            px, py = int(base[0] + (top[0] - base[0]) * t), int(base[1] + (top[1] - base[1]) * t)
            if 0 <= px < W and 0 <= py < H:
                img[py, px] = 14
        for k, sw in ((0.35, 14), (0.65, 11)):
            a = rot((mx_ - sw, -10 - mh * k - 6)); b = rot((mx_ + sw, -10 - mh * k - 6))
            c = rot((mx_ + sw - 2, -10 - mh * k + 8)); dd = rot((mx_ - sw + 3, -10 - mh * k + 8))
            sm = _poly(img, [a, b, c, dd], 7)
            _outline(img, sm)
            img[sm & ((xs + ys) % 9 == 0)] = 6       # torn, sodden canvas
    fl = rot((2, -66))
    _poly(img, [fl, (fl[0] + 8, fl[1] - 1), (fl[0] + 8, fl[1] + 4), (fl[0], fl[1] + 4)], 13)
    # driving rain
    rain = ((xs + ys * 2) % 13 == 0) & ((ys * 7 + xs) % 5 < 3)
    img[rain & (img < 7)] = 6
    # a cartouche, top right
    _poly(img, [(206, 6), (232, 6), (232, 50), (206, 50)], 7)
    img[6:51, 206] = 1; img[6:51, 232] = 1; img[6, 206:233] = 1; img[50, 206:233] = 1
    g = glyphs("嵐", 20)
    img[12:12 + g.shape[0], 219 - g.shape[1] // 2:219 - g.shape[1] // 2 + g.shape[1]][g] = 1
    img[img == 0] = 1
    return img


def still_dawn():
    """Dawn over Dejima: the fan-shaped island, a Dutch ship, the sun coming up red."""
    H, W = 160, 240
    ys, xs = np.mgrid[0:H, 0:W]
    img = np.zeros((H, W), int)
    bands = [(0, 2), (18, 3), (34, 4), (48, 5), (60, 6), (70, 7)]
    for y0, col in bands:
        img[ys >= y0] = col
    sun = np.hypot(xs - 168, ys - 40) < 15
    # cloud streaks
    for cy_, x0, x1 in ((24, 20, 120), (30, 140, 230), (44, 30, 90), (52, 180, 236)):
        img[cy_:cy_ + 2, x0:x1] = 7
        img[cy_ + 2, x0 + 6:x1 - 6] = 6
    img[sun] = 12
    # mountains in layers
    m1 = ys > 66 + 14 * np.sin(xs * 0.03 + 0.6) - 10 * np.cos(xs * 0.07)
    img[m1] = 9
    m2 = ys > 78 + 8 * np.sin(xs * 0.05 + 2.0) - 6 * np.cos(xs * 0.11)
    img[m2] = 8
    # the sea with woodblock line ripples
    sea = ys >= 90
    img[sea] = 10
    img[sea & (((ys - 90) % 5) == 0) & (((xs + ys * 3) // 9) % 2 == 0)] = 15
    img[sea & (np.abs(xs - 168) < 26 - (ys - 90) * 0.6) & ((ys % 3) == 0)] = 6     # sun on water
    # Dejima: the fan
    fx_, fy = 96, 118
    fan = ((xs - fx_) / 54.0) ** 2 + ((ys - fy - 26) / 26.0) ** 2
    isl = (fan <= 1.0) & (ys <= fy + 6) & (ys >= fy - 8)
    img[isl] = 14
    img[isl & (ys > fy + 2)] = 13
    _outline(img, isl)
    # warehouses along the fan
    for i in range(9):
        hx = 54 + i * 10
        top = fy - 14 + int(abs(i - 4) * 1.2)
        img[top:fy - 6, hx:hx + 8] = 7
        img[top - 2:top, hx - 1:hx + 9] = 11
        img[top + 3, hx + 3] = 1
    # the bridge to the shore
    img[fy - 4:fy - 2, 150:200] = 13
    img[fy - 2, 150:200] = 1
    # flagpole with the Dutch colours
    img[fy - 40:fy - 14, 96] = 1
    img[fy - 40:fy - 37, 97:105] = 12
    img[fy - 37:fy - 34, 97:105] = 15
    img[fy - 34:fy - 31, 97:105] = 8
    # a ship at anchor
    sx, sy = 196, 128
    hull = _poly(img, [(sx - 24, sy), (sx + 20, sy - 2), (sx + 26, sy - 8), (sx + 22, sy + 6), (sx - 18, sy + 8), (sx - 26, sy + 2)], 13)
    _outline(img, hull)
    for mx_, mh in ((-12, 30), (2, 38), (16, 28)):
        img[sy - mh:sy, sx + mx_] = 1
        sm = _poly(img, [(sx + mx_ - 8, sy - mh + 6), (sx + mx_ + 8, sy - mh + 6), (sx + mx_ + 6, sy - mh + 18), (sx + mx_ - 6, sy - mh + 18)], 7)
        _outline(img, sm)
    # Lieve on the bridge: a tiny figure with red hair
    lx, ly = 178, fy - 5
    img[ly - 8:ly, lx:lx + 3] = 15
    img[ly - 4:ly, lx:lx + 3] = 8
    img[ly - 11:ly - 8, lx:lx + 3] = 4
    img[ly - 9:ly - 3, lx - 1] = 4
    # cartouche
    _poly(img, [(8, 6), (34, 6), (34, 50), (8, 50)], 7)
    img[6:51, 8] = 1; img[6:51, 34] = 1; img[6, 8:35] = 1; img[50, 8:35] = 1
    g = glyphs("出島", 14)
    img[12:12 + g.shape[0], 21 - g.shape[1] // 2:21 - g.shape[1] // 2 + g.shape[1]][g] = 1
    img[img == 0] = 1
    return img


# ======================================================================= death poems (jisei), shown when Lieve falls
PAL_POEM = [(0, 0, 0), (6, 6, 10), (16, 14, 22), (28, 24, 34), (150, 30, 24), (206, 56, 38),
            (176, 164, 132), (240, 230, 200), (60, 50, 44), (0, 0, 0), (0, 0, 0), (0, 0, 0),
            (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)]
POEMS = [   # 5-7-5, written for the game; one for each place she can die
    ("Red leaf on the floor", "the wooden blade taught nothing", "I learned it too late"),
    ("Bamboo bends and stands", "I was the reed that would not", "the staff swept me down"),
    ("Two blades in the light", "one lantern gutters and dies", "the second is mine"),
    ("Rain on the long boards", "I never saw the knife fall", "only the raindrops"),
    ("Father, the storm wins", "the giant's shadow covers", "the sea where you sleep"),
    ("Salt wind on the pier", "a lantern drowns in black tide", "the ship leaves alone"),
    ("Gold coin, powder smoke", "the Company keeps its books", "my name crossed out, red"),
]
SERIF = "/usr/share/fonts/opentype/noto/NotoSerifCJK-SemiBold.ttc"


def _line_mask(text, size):
    f = ImageFont.truetype(SERIF, size)
    im = Image.new("L", (size * len(text) + 32, size * 2 + 16), 0)
    d = ImageDraw.Draw(im)
    d.text((8, 4), text, font=f, fill=255)
    a = np.array(im) > 120
    ys, xs = np.nonzero(a)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def still_poem(i):
    H, Wd = 160, 240
    ys, xs = np.mgrid[0:H, 0:Wd]
    img = np.ones((H, Wd), int)
    img[(xs * 7 + ys * 13) % 31 == 0] = 2
    # a single dry brush stroke behind the text
    sw = (np.abs(ys - 74 - 6 * np.sin(xs * 0.03)) < 38 - np.abs(xs - 120) * 0.13) & (xs > 10) & (xs < 230)
    img[sw & (((xs + ys) % 3) != 0)] = 2
    img[sw & (np.sin(ys * 1.7 + np.sin(xs * 0.05) * 3) > 0.7)] = 3
    y = 30
    size = 15
    while max(_line_mask(l, size).shape[1] for l in POEMS[i]) > 214:
        size -= 1
    for k, line in enumerate(POEMS[i]):
        mm = _line_mask(line, size)
        h, w = mm.shape
        x0 = (Wd - w) // 2 + (k - 1) * 12
        box = img[y:y + h, x0:x0 + w]
        o = dilate(mm, 1)
        box[o & ~mm] = 1
        box[mm] = 7
        box[mm & ~np.roll(mm, -1, 0)] = 6
        y += 26
    # the seal: 辞世 (death poem), red
    sx, sy = 194, 122
    img[sy:sy + 24, sx:sx + 24] = 4
    img[sy, sx:sx + 24] = 5; img[sy:sy + 24, sx] = 5
    g = glyphs("辞世", 10, 0)
    gh, gw = g.shape
    gx, gy = sx + (24 - gw) // 2, sy + (24 - gh) // 2
    img[gy:gy + gh, gx:gx + gw][g] = 7
    return img
