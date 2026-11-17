"""The five floors of the pagoda, plus the stairwell between them.

Every stage is drawn in the same layout as the first-floor dojo (backgrounds.py):
  wall    512x120, BG2 bank 0  (index 0 = see-through to the outside layer)
  floor   512x40,  BG2 bank 1  (perspective rows, scrolled per line by HDMA)
  outside 256x128, BG3 bank 3  (parallax view out of the windows; lightning brightens it)
Wall palette indices 11 and 12 are the lantern glow (they flicker in the game).
Each floor is higher than the last, so the view outside drops away: garden, treetops,
the town, the harbour, and finally nothing but storm.
"""
import math
import numpy as np
import backgrounds as B
from backgrounds import dither, rng, k_of, HORIZON, K_WALL

W, WH = 512, HORIZON


def grid():
    return np.mgrid[0:WH, 0:W]


def pillar(img, px, x0=1, tones=3, width=8, top=0, bot=WH):
    for dx in range(-width, width):
        x = px + dx
        if 0 <= x < W:
            t = (dx + width) / (2.0 * width)
            lit = 0.35 + 0.9 * math.sin(t * math.pi) - 0.4 * t
            col = x0 + dither(np.full(bot - top, lit * tones), np.full(bot - top, x), np.arange(top, bot)).clip(0, tones)
            img[top:bot, x] = col
    img[top:bot, max(0, px - width)] = 1
    img[top:bot, min(W - 1, px + width - 1)] = 1


def lantern(img, lx, cy, ry=15, rx=11, string_top=22, xs=None, ys=None):
    if xs is None:
        ys, xs = grid()
    img[(ys >= string_top) & (ys < cy - ry) & (xs == lx)] = 1
    e = ((xs - lx) / rx) ** 2 + ((ys - cy) / ry) ** 2
    body = e <= 1.0
    g = np.clip(1.0 - e, 0, 1)
    tone = np.where(g > 0.55, 12, np.where(g > 0.18, 11, 10))
    img[body] = tone[body]
    ribs = body & (((ys - cy) % 4) == 0) & (e > 0.08)
    img[ribs] = np.where(g[ribs] > 0.4, 10, 9)
    img[body & (e > 0.82)] = 9
    cap = (abs(xs - lx) < rx * 0.6) & ((abs(ys - (cy - ry)) < 2) | (abs(ys - (cy + ry)) < 2))
    img[cap] = 1


def light_from(points, xs, ys, reach=120.0, sx=1.25):
    l = np.zeros(xs.shape)
    for lx, ly in points:
        d = np.sqrt(((xs - lx) / sx) ** 2 + (ys - ly) ** 2)
        l += np.clip(1.15 - d / reach, 0, 1)
    return np.clip(l, 0, 1)


def brighten(img, mask, mapping, dither=True):
    """Light falling through a window: shift tones up inside mask (checker-dithered)."""
    ys, xs = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    sel = mask & (((xs + ys) & 1) == 0 if dither else True)
    out = img.copy()
    for a, b in mapping.items():
        out[sel & (img == a)] = b
    return out


def perspective_floor(fn):
    """Call fn(wx, zz, y, depth) per row -> tone row; returns the 512x40 floor."""
    rows = np.arange(HORIZON, 160)
    img = np.zeros((len(rows), W), int)
    xs = np.arange(W)
    for i, y in enumerate(rows):
        k = k_of(y)
        wx = (xs - 256) / k
        img[i] = fn(wx, 1.0 / k, y, (y - HORIZON) / 40.0, k, xs)
    img[0] = 1
    return img


def reflect(wx, xs, y, depth, lamps, width=9.0, gain=2.2):
    refl = np.zeros(W)
    for lx in lamps:
        lwx = (lx - 256) / K_WALL
        refl += np.clip(1 - np.abs(wx - lwx) / width, 0, 1) * (0.5 + 0.8 * (1 - depth))
    return dither(refl * gain, xs, np.full(W, y))


# ======================================================================= floor 2: the bamboo room
PAL_BAMBOO_WALL = [(0, 0, 0), (14, 16, 10), (46, 40, 24), (82, 70, 40), (124, 104, 60), (170, 146, 88),
                   (42, 70, 34), (78, 118, 56), (132, 170, 88), (124, 26, 18), (200, 56, 30),
                   (250, 146, 52), (255, 224, 126), (196, 178, 120), (226, 214, 160), (238, 230, 196)]
PAL_BAMBOO_FLOOR = [(0, 0, 0), (34, 30, 14), (98, 98, 46), (128, 128, 62), (156, 152, 80), (186, 180, 104),
                    (226, 170, 80), (250, 214, 130), (112, 132, 136), (156, 176, 176), (30, 44, 30),
                    (54, 70, 40)] + [(0, 0, 0)] * 4
PAL_BAMBOO_OUT = [(0, 0, 0), (34, 40, 76), (46, 56, 98), (62, 76, 124), (88, 102, 150), (120, 132, 176),
                  (156, 168, 206), (224, 230, 242), (252, 252, 255), (48, 58, 86), (36, 44, 66),
                  (10, 24, 16), (22, 48, 30), (44, 82, 48), (74, 116, 70), (244, 176, 86)]
BAMBOO_LAMPS = [120, 392]


def bamboo_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    light = light_from([(x, 54) for x in BAMBOO_LAMPS], xs, ys)
    # walls of split bamboo, each stalk shaded round, nodes every 22 px
    stalk = (xs % 9)
    t = stalk / 8.0
    lit = 0.25 + 1.2 * np.sin(t * math.pi) * (0.55 + 0.6 * light) - 0.3 * t
    img[:] = 6 + dither(lit * 1.6, xs, ys).clip(0, 2)
    img[stalk == 0] = 1
    node = ((ys + (xs // 9) * 7) % 22 == 0) & (stalk != 0)
    img[node] = 6
    # round window (marumado) onto the treetops, and two square openings with reed blinds
    cx, cy, r = 256, 62, 46
    d = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    img[d < r] = 0
    ring = (d >= r) & (d < r + 4)
    img[ring] = (3 + dither(np.full(xs.shape, 0.4) + light, xs, ys).clip(0, 2))[ring]
    img[(d >= r + 4) & (d < r + 5)] = 1
    for x0, x1 in ((36, 92), (420, 476)):
        op = (xs >= x0) & (xs < x1) & (ys >= 30) & (ys < 96)
        img[op] = 0
        blind = op & (ys < 30 + 34)
        img[blind] = np.where(((ys[blind] - 30) % 3) == 0, 2, 13 + dither(light[blind] * 1.5, xs[blind], ys[blind]).clip(0, 1))
        img[blind & ((xs == x0 + 10) | (xs == x1 - 11))] = 3
        frame = ((xs == x0 - 1) | (xs == x1) | (ys == 29) | (ys == 96)) & (xs >= x0 - 1) & (xs <= x1) & (ys >= 29) & (ys <= 96)
        img[frame] = 1
        img[(ys >= 96) & (ys < 99) & (xs >= x0 - 4) & (xs < x1 + 4)] = 4
    # ceiling beams and wainscot of woven reed
    img[ys < 10] = 2
    img[(ys >= 3) & (ys < 6)] = 3
    img[(ys >= 10) & (ys < 13)] = 1
    wb = (ys >= 100) & (ys < 117)
    weave = 13 + (((xs // 4) + (ys // 4)) & 1)
    img[wb] = weave[wb]
    img[wb & (((xs + ys) % 8) == 0)] = 4
    img[ys == 100] = 4
    img[ys >= 117] = 1
    img[ys == 118] = 2
    for px in (16, 176, 336, 496):
        pillar(img, px, 2, 3, 7)
    # a hanging ink scroll of bamboo leaves
    sx0, sx1 = 140, 164
    img[24:94, sx0:sx1] = 4
    img[27:91, sx0 + 2:sx1 - 2] = 15
    for k in range(9):
        y = 34 + k * 6
        x = sx0 + 6 + (k * 5) % 12
        img[y, x:x + 6] = 1
        img[y + 1, x + 2:x + 7] = 1
    img[30:88, sx0 + 4] = 6
    img[24, sx0 - 2:sx1 + 2] = 1
    img[93, sx0 - 2:sx1 + 2] = 1
    for lx in BAMBOO_LAMPS:
        lantern(img, lx, 52, 14, 10, 13, xs, ys)
    beam = (ys >= 100) & (np.abs(xs - 256) < 40 + (ys - 100) * 0.9)
    img = brighten(img, beam, {13: 14, 14: 15, 4: 5, 2: 3})
    return img


BAMBOO_STALKS = (226, 290)          # wall x of the live bamboo in front of the round window; a cut leaves a stump


def bamboo_stalk(img, sx, cut=False, ys=None, xs=None):
    if ys is None:
        ys, xs = grid()
    top = 84 if cut else 14
    for y in range(top, 117):
        for dx in range(-2, 3):
            img[y, sx + dx] = (8 if dx == -1 else 7 if dx <= 0 else 6) if abs(dx) < 2 else 1
        if (y - top) % 18 == 9 and not cut:
            img[y, sx - 3:sx + 4] = 1
            img[y + 1, sx - 2:sx + 3] = 8
    if cut:                             # a fresh diagonal cut
        for dx in range(-2, 3):
            img[84 + (dx + 2) // 2, sx + dx] = 15
        img[83, sx - 2:sx + 3] = 1
    else:                               # a spray of leaves at the top and a few side sprigs
        R_ = rng(sx)
        for _ in range(26):
            a = R_.uniform(-2.6, -0.5)
            L = R_.uniform(6, 16)
            x0, y0 = sx, 16 + R_.uniform(0, 26)
            for t in np.linspace(0, 1, int(L)):
                x = int(x0 + math.cos(a) * L * t * (1 if R_.random() < 0.5 else -1))
                y = int(y0 + math.sin(a) * L * t * 0.3 + t * t * 4)
                if 0 <= x < W and 0 <= y < WH:
                    img[y, x] = 8 if t < 0.5 else 7
    return img


def bamboo_wall_full():
    img = bamboo_wall()
    for sx in BAMBOO_STALKS:
        bamboo_stalk(img, sx)
    return img


def bamboo_variants():
    """(wall image) for each stalk cut, the other standing."""
    out = []
    for i, sx in enumerate(BAMBOO_STALKS):
        img = bamboo_wall()
        for j, sx2 in enumerate(BAMBOO_STALKS):
            bamboo_stalk(img, sx2, cut=(j == i))
        out.append(img)
    return out


def bamboo_floor():
    def row(wx, zz, y, depth, k, xs):
        # tatami: 2:1 mats with dark cloth borders, alternating direction
        mw, md = 90.0, 0.9
        col = np.floor(wx / mw).astype(int)
        zi = int(math.floor(zz / md))
        horiz = ((col + zi) & 1) == 0
        frac_x = (wx / mw) % 1.0
        straw = 2.4 + 0.7 * np.where(horiz, (np.floor(wx / 3) % 2), 0.5) + 1.0 * (1 - depth) ** 2
        t = dither(straw, xs, np.full(W, y)).clip(2, 5)
        rd = reflect(wx, xs, y, depth, BAMBOO_LAMPS, 10.0, 1.8)
        t = np.where(rd >= 2, 7, np.where(rd >= 1, 6, t))
        border = (frac_x * mw * k < 1.6) | ((zz / md) % 1.0 < 0.035 / max(k, 0.3))
        t = np.where(border, 10, t)
        moon = (np.abs(wx) < 70 + 40 * (1 - depth)) & (depth < 0.85)     # moonlight pool below the window
        lit = moon & (((xs + y) & 1) == 0) & (t >= 2) & (t <= 4)
        t = np.where(lit, t + 1, t)
        return t
    return perspective_floor(row)


def bamboo_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    R = rng(5)
    img = 1 + dither(np.clip((ys - 4) / 80.0, 0, 1) * 4.0, xs, ys).clip(0, 4)
    mx, my = 96, 30
    d = np.sqrt((xs - mx) ** 2 + (ys - my) ** 2)
    hv = dither(np.clip(1 - d / 30.0, 0, 1) * 2.2, xs, ys)
    img = np.where(hv >= 2, 6, np.where(hv >= 1, np.maximum(img, 5), img))
    img[d <= 9.5] = 7
    img[(d <= 9.5) & ((xs - mx) + (ys - my) < -4)] = 8
    far = 62 + 10 * np.sin(xs / 256 * 2 * math.pi * 2 + 0.4) + 4 * np.sin(xs / 256 * 2 * math.pi * 6)
    img[ys >= far] = 9
    near = 74 + 7 * np.sin(xs / 256 * 2 * math.pi * 3 + 1.3)
    img[ys >= near] = 10
    # the tops of the bamboo grove below: billowing leaf masses
    tops = 84 + 6 * np.sin(xs / 256 * 2 * math.pi * 5 + 2.0) + 3 * np.sin(xs / 256 * 2 * math.pi * 13)
    leaf = ys >= tops
    shade = dither(np.clip((ys - tops) / 26.0, 0, 1) * -2.4 + 2.6 + 0.6 * np.sin(xs * 0.7 + ys * 0.3), xs, ys).clip(0, 3)
    img[leaf] = (11 + shade)[leaf]
    for _ in range(40):
        bx, by = int(R.integers(0, Wd)), int(R.integers(70, 100))
        for j in range(int(R.integers(4, 9))):
            x = (bx + j) % Wd
            y = by + j // 2
            if y < Hh:
                img[y, x] = 13 if (j & 1) else 12
    return img


# ======================================================================= floor 3: the lantern hall
PAL_HALL_WALL = [(0, 0, 0), (16, 6, 6), (46, 12, 12), (92, 20, 18), (146, 34, 26), (196, 60, 36),
                 (40, 30, 20), (98, 74, 36), (172, 132, 60), (124, 26, 18), (200, 56, 30),
                 (250, 146, 52), (255, 224, 126), (226, 186, 92), (22, 18, 18), (60, 50, 44)]
PAL_HALL_FLOOR = [(0, 0, 0), (14, 6, 6), (44, 16, 12), (66, 24, 16), (90, 34, 22), (118, 48, 28),
                  (214, 110, 50), (252, 196, 100), (78, 40, 40), (110, 60, 52)] + [(0, 0, 0)] * 6
PAL_HALL_OUT = [(0, 0, 0), (16, 16, 34), (22, 24, 46), (30, 32, 60), (42, 44, 76), (58, 60, 96),
                (84, 86, 124), (180, 186, 210), (240, 240, 250), (26, 30, 44), (18, 20, 30),
                (10, 12, 18), (32, 40, 60), (60, 70, 94), (200, 140, 60), (252, 210, 110)]
HALL_LAMPS = [56, 136, 216, 296, 376, 456]


def hall_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    light = light_from([(x, 50 + 10 * (i & 1)) for i, x in enumerate(HALL_LAMPS)], xs, ys, 80.0)
    # dark lacquered panels framed in gold
    img[:] = 14 + dither(light * 1.3, xs, ys).clip(0, 1)
    panel_x = (xs % 80)
    fr = ((panel_x == 22) | (panel_x == 57)) & (ys >= 26) & (ys < 98)
    fr |= ((ys == 26) | (ys == 97)) & (panel_x >= 22) & (panel_x <= 57)
    img[fr] = 7 + dither(light[fr] * 1.5, xs[fr], ys[fr]).clip(0, 1)
    # gold cloud motifs inside the panels
    for cx in range(40, W, 80):
        for (ox, oy, r) in ((-6, 52, 6), (3, 48, 7), (9, 55, 5), (-2, 74, 6), (6, 78, 5)):
            d = np.sqrt((xs - cx - ox) ** 2 + (ys - oy) ** 2)
            img[(d < r) & (d >= r - 1.2)] = 8
    # small lattice windows onto the town below
    for x0 in (172, 308):
        op = (xs >= x0) & (xs < x0 + 32) & (ys >= 34) & (ys < 70)
        img[op] = 0
        lat = op & ((((xs - x0) % 8) == 0) | (((ys - 34) % 9) == 0))
        img[lat] = 2
        img[((xs == x0 - 1) | (xs == x0 + 32)) & (ys >= 33) & (ys <= 70)] = 7
        img[((ys == 33) | (ys == 70)) & (xs >= x0 - 1) & (xs <= x0 + 32)] = 7
    # beams, red lacquer
    img[ys < 10] = 3
    img[(ys >= 3) & (ys < 6)] = 4
    img[ys == 1] = 5
    img[(ys >= 10) & (ys < 13)] = 1
    nb = (ys >= 20) & (ys < 26)
    img[nb] = 3
    img[ys == 20] = 5
    img[ys == 25] = 1
    wb = (ys >= 100) & (ys < 117)
    img[wb] = 2 + dither(light[wb] * 1.4, xs[wb], ys[wb]).clip(0, 1)
    img[ys == 100] = 8
    img[ys >= 117] = 1
    img[ys == 118] = 2
    for px in (16, 96, 176, 256, 336, 416, 496):
        pillar(img, px, 2, 3, 7)
        img[(abs(xs - px) < 7) & ((ys == 40) | (ys == 41) | (ys == 88))] = 8
    for x0 in (172, 308):
        sk = (ys - 70) * 0.55
        beam = (ys > 70) & (ys < 117) & (xs >= x0 + sk) & (xs < x0 + 32 + sk)
        img = brighten(img, beam, {14: 15, 15: 7, 2: 3, 3: 4, 1: 2})
    for i, lx in enumerate(HALL_LAMPS):
        lantern(img, lx, 46 + 10 * (i & 1), 12, 9, 13, xs, ys)
    return img


HALL_ANDON = (188, 324)


def andon(img, ax, fallen=False):
    if not fallen:
        img[118 - 2:118, ax - 7:ax + 8] = 1
        for lx in (ax - 6, ax + 6):
            img[86:118, lx] = 1
        body = img[88:112, ax - 6:ax + 7]
        body[:] = 11
        body[3:-3, 2:-2] = 12
        body[:, 0] = 9; body[:, -1] = 9
        for y in range(0, 24, 6):
            body[y, :] = 9
        img[86:88, ax - 7:ax + 8] = 1
        img[112:114, ax - 7:ax + 8] = 1
    else:                               # on its side, paper torn, scorched
        for x in range(ax - 14, ax + 12):
            img[113:117, x] = 1 if x in (ax - 14, ax + 11) else (11 if (x + 1) % 6 else 9)
        img[112, ax - 14:ax + 12] = 1
        img[117, ax - 14:ax + 12] = 1
        img[113:117, ax - 4:ax + 3] = 2
        img[114:116, ax + 4:ax + 9] = 3
        for lx in (ax + 12, ax + 16):
            img[115:117, lx:lx + 4] = 1
    return img


def hall_wall_full():
    img = hall_wall()
    for ax in HALL_ANDON:
        andon(img, ax)
    return img


def hall_variants():
    out = []
    for i in range(len(HALL_ANDON)):
        img = hall_wall()
        for j, ax in enumerate(HALL_ANDON):
            andon(img, ax, fallen=(j == i))
        out.append(img)
    return out


def hall_floor():
    def row(wx, zz, y, depth, k, xs):
        pw = 30.0
        pidx = np.floor(wx / pw).astype(int)
        frac = wx / pw - pidx
        h = (pidx * 2654435761) & 0xFFFF
        base = 2.2 + ((h % 5) / 5.0) * 1.0 + 1.2 * (1 - depth) ** 2
        t = dither(base, xs, np.full(W, y)).clip(2, 5)
        rd = reflect(wx, xs, y, depth, HALL_LAMPS, 7.0, 2.6)
        t = np.where(rd >= 2, 7, np.where(rd >= 1, 6, t))
        gap = (frac * pw * k) < 1.0
        joint = (np.abs(((zz * 7.0 + (h % 5) * 0.37) % 1.0)) < 0.05 * k)
        return np.where(gap | joint, 1, t)
    return perspective_floor(row)


def hall_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    R = rng(9)
    img = 1 + dither(np.clip((ys - 0) / 70.0, 0, 1) * 3.0, xs, ys).clip(0, 3)
    hills = 46 + 8 * np.sin(xs / 256 * 2 * math.pi * 2 + 1.1) + 4 * np.sin(xs / 256 * 2 * math.pi * 7)
    img[ys >= hills] = 9
    town = ys >= 64
    img[town] = 10
    # roofs of the town in rows, with warm windows
    for row_y in range(66, 128, 7):
        for x in range(0, Wd, 1):
            if R.random() < 0.08:
                w = int(R.integers(3, 8))
                img[row_y:row_y + 2, x:x + w] = 12
        for _ in range(14):
            x = int(R.integers(0, Wd))
            img[row_y + 3, x] = 15 if R.random() < 0.5 else 14
    # the harbour glinting at the bottom
    img[(ys >= 112)] = 11
    img[(ys >= 112) & (((xs + ys * 3) % 11) == 0)] = 13
    return img


# ======================================================================= floor 4: the rain gallery
PAL_GALLERY_WALL = [(0, 0, 0), (10, 12, 16), (34, 38, 46), (60, 64, 74), (92, 96, 106), (130, 134, 144),
                    (54, 40, 30), (88, 66, 46), (128, 98, 66), (124, 26, 18), (200, 56, 30),
                    (250, 146, 52), (255, 224, 126), (70, 80, 96), (110, 122, 140), (160, 170, 186)]
PAL_GALLERY_FLOOR = [(0, 0, 0), (12, 14, 18), (44, 48, 56), (62, 66, 76), (82, 86, 98), (104, 108, 120),
                     (150, 164, 190), (206, 216, 236), (70, 86, 112), (96, 116, 146)] + [(0, 0, 0)] * 6
PAL_GALLERY_OUT = [(0, 0, 0), (26, 30, 40), (36, 42, 54), (48, 56, 70), (62, 72, 88), (82, 92, 110),
                   (110, 120, 138), (190, 198, 212), (236, 240, 248), (34, 46, 58), (24, 34, 44),
                   (16, 22, 28), (40, 54, 64), (70, 86, 96), (120, 76, 50), (220, 180, 120)]


def gallery_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    # deep eaves overhead, the rest open to the storm
    eave = ys < 18 + 3 * np.cos((xs - 256) / 256.0 * math.pi)
    img[eave] = 2
    img[eave & (((xs) % 14) < 2)] = 1
    img[(ys >= 4) & (ys < 7)] = 3
    img[(ys == 0)] = 4
    rafter = (ys >= 14) & (ys < 22) & ((xs % 32) < 5)
    img[rafter] = 7
    img[rafter & ((xs % 32) == 0)] = 1
    # balustrade
    rail_top = (ys >= 88) & (ys < 92)
    img[rail_top] = 8
    img[ys == 88] = 8
    img[ys == 91] = 1
    rail_mid = (ys >= 104) & (ys < 107)
    img[rail_mid] = 7
    bal = (ys >= 92) & (ys < 117) & ((xs % 16) < 3)
    img[bal] = 7
    img[bal & ((xs % 16) == 0)] = 1
    img[(ys >= 110) & (ys < 117)] = 6
    img[ys == 110] = 8
    img[ys >= 117] = 1
    img[ys == 118] = 2
    # gilt caps on the posts
    for px in (16, 176, 336, 496):
        pillar(img, px, 6, 2, 8)
        img[(abs(xs - px) < 9) & (ys >= 84) & (ys < 88)] = 8
    for px in (96, 256, 416):
        pillar(img, px, 6, 2, 5)
    # wind chimes on two rafters
    for lx in (136, 376):
        img[18:40, lx] = 13
        img[40:46, lx - 3:lx + 4] = 14
        img[46, lx - 3:lx + 4] = 1
        img[46:56, lx] = 15
    return img


def gallery_floor():
    def row(wx, zz, y, depth, k, xs):
        pw = 24.0
        pidx = np.floor(wx / pw).astype(int)
        frac = wx / pw - pidx
        h = (pidx * 2654435761) & 0xFFFF
        base = 2.1 + ((h % 7) / 7.0) * 1.2 + 0.8 * (1 - depth) ** 2
        t = dither(base, xs, np.full(W, y)).clip(2, 5)
        # rain-wet planks mirroring the bright sky through the open side
        puddle = 0.5 + 0.5 * np.sin(wx * 0.045 + zz * 3.1) * np.sin(wx * 0.013 - zz * 1.7)
        pd = dither(np.clip(puddle - 0.35, 0, 1) * 3.2 * (0.4 + 0.6 * (1 - depth)), xs, np.full(W, y))
        t = np.where(pd >= 2, 7, np.where(pd >= 1, 6, t))
        cool = dither(np.clip(1 - np.abs(wx) / 600, 0, 1) * (1 - depth) * 1.4, xs, np.full(W, y))
        t = np.where((cool >= 1) & (pd < 1), 8 + (cool >= 2), t)
        gap = (frac * pw * k) < 1.0
        joint = (np.abs(((zz * 8.0 + (h % 5) * 0.37) % 1.0)) < 0.05 * k)
        return np.where(gap | joint, 1, t)
    return perspective_floor(row)


def gallery_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    R = rng(13)
    # heavy storm clouds
    cl = 0.5 + 0.25 * np.sin(xs * 0.05 + ys * 0.11) + 0.25 * np.sin(xs * 0.021 - ys * 0.07 + 1.3)
    img = 1 + dither(np.clip(cl * 3.0 + (ys - 30) / 40.0, 0, 4.5), xs, ys).clip(0, 5)
    hills = 58 + 9 * np.sin(xs / 256 * 2 * math.pi * 2 + 2.1) + 5 * np.sin(xs / 256 * 2 * math.pi * 5)
    img[ys >= hills] = 9
    # the bay far below
    sea = ys >= 72
    img[sea] = 11 + dither(np.clip((ys - 72) / 50.0, 0, 1) * 1.6, xs, ys)[sea].clip(0, 1)
    img[sea & (((xs * 3 + ys * 7) % 23) == 0)] = 13
    # Dejima: the fan-shaped Dutch island, with a ship at anchor
    ix, iy = 150, 88
    fan = ((xs - ix) / 26.0) ** 2 + ((ys - iy - 8) / 10.0) ** 2
    island = (fan <= 1.0) & (ys <= iy + 6) & (ys >= iy - 2)
    img[island] = 14
    img[island & ((xs % 5) == 0) & (ys < iy + 2)] = 15
    sx_, sy_ = 196, 96
    img[sy_:sy_ + 3, sx_ - 9:sx_ + 9] = 14
    img[sy_ + 3, sx_ - 7:sx_ + 7] = 14
    for mxx, hgt in ((sx_ - 4, 14), (sx_ + 3, 18)):
        img[sy_ - hgt:sy_, mxx] = 14
        img[sy_ - hgt + 3, mxx - 3:mxx + 4] = 7
        img[sy_ - hgt + 8, mxx - 4:mxx + 5] = 7
    img[sy_ - 20, sx_ + 3:sx_ + 7] = 15
    return img


# ======================================================================= floor 5: the storm roof
PAL_ROOF_WALL = [(0, 0, 0), (8, 8, 12), (26, 28, 38), (44, 48, 62), (66, 72, 90), (96, 104, 124),
                 (120, 84, 30), (196, 150, 56), (250, 214, 110), (60, 20, 18), (110, 30, 24),
                 (60, 20, 18), (110, 30, 24), (34, 38, 50), (52, 58, 74), (80, 88, 108)]
PAL_ROOF_FLOOR = [(0, 0, 0), (8, 10, 14), (30, 34, 44), (44, 50, 64), (60, 68, 86), (80, 90, 110),
                  (130, 142, 168), (196, 206, 228), (90, 104, 132), (120, 136, 166)] + [(0, 0, 0)] * 6
PAL_ROOF_OUT = [(0, 0, 0), (14, 16, 26), (22, 24, 38), (32, 34, 52), (44, 46, 68), (60, 62, 88),
                (86, 88, 118), (180, 186, 214), (240, 242, 255), (20, 24, 34), (14, 18, 26),
                (8, 12, 18), (24, 32, 44), (46, 56, 72), (100, 60, 120), (200, 170, 230)]


def roof_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    # the spire (sorin) of the pagoda rising behind the fight
    sx = 256
    for y in range(0, 104):
        w = 3 if y < 70 else 4 + (y - 70) // 6
        img[y, sx - w:sx + w] = 3 + (np.arange(2 * w) * 3 // (2 * w)).clip(0, 2)
        img[y, sx - w] = 1
        img[y, sx + w - 1] = 1
    for k, ry in enumerate(range(8, 64, 6)):
        img[ry:ry + 2, sx - 8:sx + 8] = 7
        img[ry + 2, sx - 8:sx + 8] = 6
        img[ry, sx - 8] = img[ry, sx + 7] = 1
    img[0:4, sx - 3:sx + 3] = 8
    for k in (-1, 1):                       # wind bells hanging from the finial
        img[64:70, sx + 12 * k] = 6
        img[70:74, sx + 12 * k - 2:sx + 12 * k + 3] = 7
    # the roof ridge, sweeping up at the corners
    ridge = 104 - 12 * ((np.abs(xs - 256) / 256.0) ** 3)
    roof = ys >= ridge
    img[roof] = (13 + dither(np.clip((ys - ridge) / 14.0, 0, 1) * 1.8, xs, ys).clip(0, 2))[roof]
    img[roof & (ys < ridge + 2)] = 5
    img[roof & (((xs + (ys // 3)) % 10) == 0)] = 2
    img[ys >= 117] = 1
    img[ys == 118] = 2
    # gilt shachi ornaments at both ends of the ridge
    for ex in (10, 502):
        img[80:96, ex - 4:ex + 4] = 7
        img[78:82, ex - 6:ex + 6] = 8
        img[94:98, ex - 6:ex + 6] = 6
    return img


def roof_floor():
    def row(wx, zz, y, depth, k, xs):
        # rows of round tiles running away from the camera, slick with rain
        pw = 16.0
        frac = (wx / pw) % 1.0
        r = np.sin(frac * math.pi)
        tone = 2.0 + 2.6 * r * (0.6 + 0.4 * (1 - depth)) - 0.6 * frac
        t = dither(tone, xs, np.full(W, y)).clip(2, 5)
        shine = (r > 0.82) & (((zz * 13.0 + (np.floor(wx / pw) % 3) * 0.3) % 1.0) < 0.3)
        t = np.where(shine, 6 + ((xs + y) & 1), t)
        gap = frac * pw * k < 1.2
        course = ((zz * 5.0) % 1.0) < 0.05 * k
        return np.where(gap | course, 1, t)
    return perspective_floor(row)


def roof_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    swirl = 0.5 + 0.3 * np.sin(xs * 0.04 + ys * 0.09 + np.sin(ys * 0.05) * 2) + 0.2 * np.sin(xs * 0.11 - ys * 0.05)
    img = 1 + dither(np.clip(swirl * 4.2 - ys / 90.0, 0, 5), xs, ys).clip(0, 5)
    # a fork of lightning frozen in the clouds
    x, y = 70.0, 0.0
    R = rng(17)
    while y < 80:
        nx, ny = x + R.uniform(-5, 5), y + R.uniform(3, 7)
        for t in np.linspace(0, 1, 8):
            px, py = int(x + (nx - x) * t), int(y + (ny - y) * t)
            if 0 <= py < Hh:
                img[py, px % Wd] = 7
        x, y = nx, ny
    # the dark sea and the far coast at the bottom
    img[ys >= 100] = 10
    img[(ys >= 96) & (ys < 100)] = 12
    img[(ys >= 100) & (((xs * 5 + ys * 3) % 19) == 0)] = 13
    return img


# ======================================================================= the prologue: a pier on Dejima at night
PAL_DOCK_WALL = [(0, 0, 0), (12, 12, 18), (34, 32, 40), (60, 56, 62), (150, 146, 140), (200, 196, 186),
                 (58, 40, 30), (96, 68, 44), (140, 104, 66), (124, 26, 18), (200, 56, 30),
                 (250, 146, 52), (255, 224, 126), (36, 50, 72), (70, 92, 120), (230, 228, 220)]
PAL_DOCK_FLOOR = [(0, 0, 0), (8, 10, 16), (40, 32, 28), (58, 46, 36), (78, 62, 46), (100, 80, 58),
                  (150, 160, 190), (250, 200, 110), (40, 56, 80), (64, 86, 116)] + [(0, 0, 0)] * 6
PAL_DOCK_OUT = [(0, 0, 0), (10, 14, 32), (14, 20, 44), (20, 28, 58), (28, 38, 74), (40, 52, 92),
                (70, 80, 120), (200, 196, 170), (250, 246, 220), (14, 22, 36), (10, 16, 28),
                (6, 10, 18), (40, 30, 26), (80, 60, 40), (250, 190, 90), (255, 230, 150)]
DOCK_LAMPS = [96, 416]


def dock_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    light = light_from([(x, 50) for x in DOCK_LAMPS], xs, ys, 110.0)
    # white plastered storehouses (kura) with dark timber and namako tiles
    kura = (xs < 168) | (xs >= 344)
    img[kura & (ys >= 24)] = (4 + dither(0.4 + light * 1.2, xs, ys).clip(0, 1))[kura & (ys >= 24)]
    img[kura & (ys >= 24) & (ys < 30)] = 2
    img[kura & (ys < 24) & (ys >= 12 + np.abs((xs % 176) - 88) * 0.12)] = 3
    img[kura & (ys < 24) & ((xs + ys) % 7 == 0) & (ys >= 12 + np.abs((xs % 176) - 88) * 0.12)] = 1
    nam = kura & (ys >= 84) & (ys < 117)
    img[nam] = 2
    diag = nam & ((((xs + ys) % 12) == 0) | (((xs - ys) % 12) == 0))
    img[diag] = 5
    for x0 in (60, 400):                           # iron-shuttered windows
        img[40:62, x0:x0 + 26] = 1
        img[42:60, x0 + 2:x0 + 24] = 2
        img[42:60, x0 + 12] = 1
    for x0 in (124, 356):                          # sliding doors
        img[50:117, x0:x0 + 34] = 6
        img[50:117, x0 + 16:x0 + 18] = 1
        img[50:52, x0:x0 + 34] = 1
    # between the warehouses the pier opens onto the harbour (see-through to the outside layer)
    img[(xs >= 168) & (xs < 344) & (ys < 104)] = 0
    # mooring posts, ropes, barrels and crates on the quay
    for px in (174, 338):
        pillar(img, px, 6, 2, 6, top=40)
        img[38:41, px - 7:px + 7] = 1
    for i in range(18):
        x = 180 + i * 9
        y = int(60 + 10 * math.sin(i / 17 * math.pi))
        img[y:y + 2, x:x + 9] = 7
    img[(xs >= 168) & (xs < 344) & (ys >= 104) & (ys < 117)] = 1
    img[(xs >= 168) & (xs < 344) & (ys >= 104) & (ys < 106)] = 6
    for bx in (20, 470):
        d = np.abs(xs - bx)
        barrel = (d < 10) & (ys >= 96) & (ys < 117)
        img[barrel] = (6 + dither(np.clip(1 - d / 10.0, 0, 1) * 2.2, xs, ys).clip(0, 2))[barrel]
        img[barrel & ((ys == 100) | (ys == 112))] = 1
    for cx_ in (40, 446):
        img[100:117, cx_:cx_ + 18] = 7
        img[100:117, cx_] = 1; img[100:117, cx_ + 17] = 1; img[100, cx_:cx_ + 18] = 1
        img[108, cx_:cx_ + 18] = 6
    img[ys >= 117] = 1
    img[ys == 118] = 2
    for lx in DOCK_LAMPS:
        img[36:117, lx] = 1
        lantern(img, lx, 46, 10, 7, 36, xs, ys)
    return img


def dock_floor():
    def row(wx, zz, y, depth, k, xs):
        pw = 13.0                                  # boards across the pier, black water between some
        zi = zz * 6.0
        frac = zi % 1.0
        h = (np.floor(zi).astype(int) * 2654435761 + np.floor(wx / 90).astype(int) * 40503) & 0xFFFF
        base = 2.2 + ((h % 5) / 5.0) * 1.2 + 0.9 * (1 - depth) ** 2
        t = dither(np.full(W, base), xs, np.full(W, y)).clip(2, 5)
        rd = reflect(wx, xs, y, depth, [l - 256 + 0 for l in DOCK_LAMPS], 8.0, 2.0)
        t = np.where(rd >= 2, 7, np.where(rd >= 1, 6, t))
        wet = 0.5 + 0.5 * np.sin(wx * 0.05 + zz * 2.7)
        t = np.where((wet > 0.8) & (((xs + y) & 1) == 0) & (rd < 1), 8 + (wet > 0.93), t)
        gap = frac < 0.06 / max(k, 0.3)
        seam = ((wx / 90.0) % 1.0) * 90 * k < 1.0
        return np.where(gap | seam, 1, t)
    return perspective_floor(row)


def dock_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    img = 1 + dither(np.clip(ys / 22.0, 0, 3.9), xs, ys).clip(0, 4)
    mx, my = 60, 26
    d = np.hypot(xs - mx, ys - my)
    img[d < 9] = 8
    img[(d < 9) & (xs - mx + (ys - my) * 0.5 > 3)] = 7
    R_ = rng(77)
    for _ in range(30):
        x, y = int(R_.integers(0, Wd)), int(R_.integers(4, 50))
        if np.hypot(x - mx, y - my) > 14:
            img[y, x] = 7
    # the hills of Nagasaki with lit windows, then the harbour
    hills = 62 + 8 * np.sin(xs / 256 * 2 * math.pi * 2 + 0.5) + 3 * np.sin(xs / 256 * 2 * math.pi * 9)
    img[ys >= hills] = 9
    for _ in range(40):
        x = int(R_.integers(0, Wd)); y = int(R_.integers(66, 82))
        if img[y, x] == 9:
            img[y, x] = 14
    img[ys >= 84] = 10
    img[(ys >= 84) & (((xs * 5 + ys * 3) % 17) == 0)] = 6
    # a Dutch ship at anchor: dark hull, lit stern windows, masts
    hx = 170
    hull = (np.abs(xs - hx) < 38 - (ys - 76) * 0.6) & (ys >= 72) & (ys < 88)
    img[hull] = 11
    img[(ys == 72) & (np.abs(xs - hx) < 40)] = 12
    for wx_ in range(hx + 20, hx + 34, 4):
        img[76:79, wx_:wx_ + 2] = 15
    for mxs, top in ((hx - 16, 22), (hx + 4, 16), (hx + 22, 30)):
        img[top:72, mxs] = 11
        for yy in range(top + 8, 66, 14):
            img[yy, mxs - 9:mxs + 10] = 11
    img[(ys >= 88) & (np.abs(xs - hx) < 30) & (((xs + ys) % 3) == 0)] = 13
    img[(ys >= 90) & (np.abs(xs - (hx + 27)) < 3) & ((ys % 2) == 0)] = 14
    return img


# ======================================================================= the secret spire: the finial at dawn
PAL_SPIRE_WALL = [(0, 0, 0), (12, 10, 16), (54, 36, 30), (98, 66, 40), (150, 104, 52), (210, 160, 80),
                  (40, 80, 74), (70, 124, 108), (120, 170, 140), (124, 26, 18), (200, 56, 30),
                  (250, 146, 52), (255, 224, 126), (60, 40, 70), (140, 80, 100), (250, 200, 160)]
PAL_SPIRE_FLOOR = [(0, 0, 0), (10, 14, 16), (34, 64, 60), (48, 88, 80), (66, 112, 100), (92, 140, 120),
                   (240, 190, 130), (255, 236, 190), (110, 80, 110), (170, 110, 120)] + [(0, 0, 0)] * 6
PAL_SPIRE_OUT = [(0, 0, 0), (40, 24, 70), (70, 36, 90), (110, 52, 104), (160, 74, 110), (210, 110, 110),
                 (240, 160, 110), (255, 210, 130), (255, 244, 200), (90, 70, 120), (150, 120, 160),
                 (200, 170, 190), (240, 220, 220), (60, 50, 90), (255, 150, 60), (255, 250, 230)]


def spire_wall():
    img = np.zeros((WH, W), int)
    ys, xs = grid()
    # the great bronze finial: nine rings on a pole, water-flame (suien) at the top
    cx = 256
    pole = np.abs(xs - cx) < 7
    shade = np.clip((xs - cx + 7) / 14.0, 0, 1)
    img[pole] = (2 + dither(np.sin(shade * math.pi) * 2.6, xs, ys).clip(0, 3))[pole]
    for k in range(9):
        ry = 22 + k * 10
        w = 26 - k * 0 + (8 - k) * 1.5
        ring = (np.abs(ys - ry) < 3) & (np.abs(xs - cx) < w)
        t = np.clip(1 - np.abs(xs - cx) / w, 0, 1)
        img[ring] = (3 + dither(t * 2.4, xs, ys).clip(0, 2))[ring]
        img[(ys == ry - 3) & (np.abs(xs - cx) < w)] = 1
        img[(ys == ry + 3) & (np.abs(xs - cx) < w)] = 2
        for bx in (-1, 1):                         # small bells
            bxx = int(cx + bx * (w - 1))
            img[ry + 3:ry + 7, bxx] = 6
            img[ry + 7:ry + 9, bxx - 1:bxx + 2] = 7
    fl = (ys < 14) & (np.abs(xs - cx) < 14 - ys * 0.6)
    img[fl] = 5
    img[fl & (((xs + ys) % 4) == 0)] = 4
    # the bowl (fukubachi) and the roof cap under it
    bowl = (np.hypot((xs - cx) / 40.0, (ys - 112) / 14.0) < 1) & (ys < 112)
    img[bowl] = (6 + dither(np.clip(1 - np.abs(xs - cx) / 40.0, 0, 1) * 2.2, xs, ys).clip(0, 2))[bowl]
    img[(ys >= 110) & (ys < 117) & (np.abs(xs - cx) < 120)] = 6
    img[(ys == 110) & (np.abs(xs - cx) < 120)] = 8
    img[ys >= 117] = 1
    img[ys == 118] = 2
    # chains running down from the rings to the roof corners
    for side in (-1, 1):
        for t in np.linspace(0, 1, 160):
            x = int(cx + side * (20 + t * 236))
            y = int(30 + t * 82 + math.sin(t * math.pi) * 18)
            if 0 <= x < W and 0 <= y < WH and img[y, x] == 0:
                img[y, x] = 1 if int(t * 160) % 3 else 3
    return img


def spire_floor():
    def row(wx, zz, y, depth, k, xs):
        pw = 18.0                                  # copper roof plates gone green, the dawn on them
        frac = (wx / pw) % 1.0
        r = np.sin(frac * math.pi)
        tone = 2.0 + 2.4 * r * (0.6 + 0.4 * (1 - depth)) - 0.5 * frac
        t = dither(tone, xs, np.full(W, y)).clip(2, 5)
        glow = np.clip(1 - np.abs(wx - 60) / 260, 0, 1) * (1 - depth)
        gd = dither(glow * 2.2, xs, np.full(W, y))
        t = np.where((gd >= 1) & (r > 0.6), 6 + (gd >= 2), t)
        gap = frac * pw * k < 1.1
        course = ((zz * 4.0) % 1.0) < 0.05 * k
        return np.where(gap | course, 1, t)
    return perspective_floor(row)


def spire_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    img = 1 + dither(np.clip(ys / 16.0, 0, 6.9), xs, ys).clip(0, 7)       # violet to gold
    sx, sy = 70, 92
    d = np.hypot(xs - sx, ys - sy)
    img[d < 16] = 15
    img[(d >= 16) & (d < 22) & (dither(np.clip((22 - d) / 6.0, 0, 1) * 1.4, xs, ys) >= 1)] = 14
    # a sea of storm clouds below, lit from beneath by the sun
    tops = 96 + 5 * np.sin(xs / 256 * 2 * math.pi * 3 + 0.7) + 3 * np.sin(xs / 256 * 2 * math.pi * 11)
    cl = ys >= tops
    sh = dither(np.clip((ys - tops) / 20.0, 0, 1) * 2.4, xs, ys).clip(0, 3)
    img[cl] = (12 - sh)[cl]
    img[cl & (ys < tops + 2) & (np.abs(xs - sx) < 60)] = 8
    for _ in range(6):
        pass
    R_ = rng(31)
    for _ in range(9):                            # long thin clouds high up
        cx = float(R_.uniform(0, Wd)); cy = float(R_.uniform(18, 70)); L = float(R_.uniform(40, 90))
        for ox in (-Wd, 0, Wd):
            e = ((xs - cx - ox) / (L / 2)) ** 2 + ((ys - cy) / 2.4) ** 2
            img[e <= 1] = 10 if cy > 45 else 9
            img[(e <= 1) & (ys < cy)] = 11 if cy > 45 else 13
    return img


# ======================================================================= the title: the roof on a clear night
PAL_TITLE_OUT = [(0, 0, 0), (6, 8, 22), (10, 14, 32), (16, 22, 46), (24, 32, 62), (34, 46, 82),
                 (36, 40, 66), (64, 70, 102), (112, 118, 154), (186, 192, 218), (196, 186, 146),
                 (238, 230, 194), (255, 250, 228), (196, 206, 255), (46, 54, 90), (176, 172, 140)]
TITLE_MOON = (204, 32, 12)          # image x, y, radius (the image is scrolled 20 px left on screen)
# horizontal bands (rows) that drift at different speeds in the game; the moon's band stays still
TITLE_BANDS = ((0, 13), (50, 65), (66, 81), (82, 99))


def title_outside():
    Wd, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    img = 1 + dither(np.clip(ys / 26.0, 0, 3.95), xs, ys).clip(0, 4)          # indigo to the horizon
    mx, my, mr = TITLE_MOON
    d = np.hypot(xs - mx, ys - my)
    halo = (d < mr + 9) & (d >= mr)
    img[halo & (dither(np.clip((mr + 9 - d) / 9.0 * 1.3, 0, 1), xs, ys) >= 1)] = 14
    disc = d < mr
    shade = np.clip((xs - mx + (ys - my) * 0.4) / mr, -1, 1)
    img[disc] = np.where(shade < -0.35, 12, np.where(shade < 0.55, 11, 10))[disc]
    for cx, cy, cr in ((mx + 4, my - 3, 3.2), (mx - 3, my + 4, 2.4), (mx + 5, my + 6, 1.8), (mx - 6, my - 4, 1.5)):
        img[disc & (np.hypot(xs - cx, ys - cy) < cr)] = 10
    R = rng(51)
    for _ in range(46):                     # stars, only where the sky is still
        x, y = int(R.integers(0, Wd)), int(R.integers(15, 48))
        if np.hypot(x - mx, y - my) > mr + 11:
            img[y, x] = 13 if R.random() < 0.7 else 9
    # kasumi: long banded clouds, lit from above by the moon
    for b, (y0, y1) in enumerate(TITLE_BANDS):
        for _ in range(4 + b):
            cx = float(R.uniform(0, Wd)); cy = float(R.uniform(y0 + 3, y1 - 3)); L = float(R.uniform(50, 110)); th = float(R.uniform(2.6, 4.6))
            for ox in (-Wd, 0, Wd):         # wrap around
                e = ((xs - cx - ox) / (L / 2)) ** 2 + ((ys - cy) / th) ** 2
                body = (e <= 1) & (ys >= y0) & (ys <= y1)
                lit = np.clip((cy - ys) / th, -1, 1)
                tone = np.where(lit > 0.45, 9, np.where(lit > 0.0, 8, np.where(lit > -0.5, 7, 6)))
                img[body] = tone[body]
                img[body & (e > 0.82) & (((xs + ys) & 1) == 0)] = 7
    # the sea far below, with the moon's path on it
    img[ys >= 100] = 2
    img[(ys >= 100) & (((xs * 7 + ys * 3) % 23) == 0)] = 4
    path = (ys >= 101) & (np.abs(xs - mx) < 3 + (ys - 100) * 0.35) & (((xs + ys * 2) % 3) == 0)
    img[path] = 15
    return img


# ======================================================================= the stairwell (between floors)
PAL_STAIR_WALL = [(0, 0, 0), (14, 9, 8), (40, 25, 18), (70, 44, 28), (106, 68, 40), (146, 98, 58),
                  (60, 50, 44), (96, 84, 72), (140, 124, 104), (124, 26, 18), (200, 56, 30),
                  (250, 146, 52), (255, 224, 126), (182, 150, 104), (226, 196, 146), (30, 20, 16)]


def stair_line(x):
    """Feet height of the stair at screen x (0..240)."""
    return 152 - (x - 8) * 0.42


def stairwell():
    """A full-screen 256x160 picture: Lieve climbs from bottom left to top right."""
    Hh, Wd = 160, 256
    ys, xs = np.mgrid[0:Hh, 0:Wd]
    light = light_from([(64, 46), (200, 40)], xs, ys, 110.0, 1.0)
    img = 2 + dither(0.6 + light * 2.2, xs, ys).clip(0, 3)
    # plank wall
    img[(xs % 19) == 0] = 1
    # a tall window at the top of the flight, open to the sky of the next floor
    wx0, wx1, wy0, wy1 = 150, 196, 12, 64
    img[(xs >= wx0) & (xs < wx1) & (ys >= wy0) & (ys < wy1)] = 0
    lat = (xs >= wx0) & (xs < wx1) & (ys >= wy0) & (ys < wy1) & ((((xs - wx0) % 15) == 0) | (((ys - wy0) % 17) == 0))
    img[lat] = 3
    img[((xs == wx0 - 1) | (xs == wx1)) & (ys >= wy0 - 1) & (ys <= wy1)] = 1
    img[((ys == wy0 - 1) | (ys == wy1)) & (xs >= wx0 - 1) & (xs <= wx1)] = 1
    img[(ys >= wy1 + 1) & (ys < wy1 + 4) & (xs >= wx0 - 4) & (xs < wx1 + 4)] = 5
    # the staircase: treads and risers on a heavy stringer
    tread, riser = 12, 5
    for x in range(Wd):
        base = stair_line(x)
        step = math.floor((x - 8) / tread)
        sy = int(round(stair_line(8 + step * tread)))
        if sy < 0:
            sy = 0
        img[sy:Hh, x] = 3
        img[sy, x] = 13 if x % tread else 1
        img[sy + 1, x] = 14 if x % tread else 1
        img[sy + 2:sy + 4, x] = 4
        under = int(base) + 12
        if under < Hh:
            img[under:Hh, x] = 15
            img[under, x] = 1
            img[under + 1:under + 6, x] = 5
            if under + 6 < Hh:
                img[under + 6, x] = 1
        if (x - 8) % tread == 0:
            img[sy:min(Hh, sy + riser + 1), x] = 1
    # handrail
    for x in range(Wd):
        y = int(stair_line(x)) - 34
        if 0 <= y < Hh - 2:
            img[y:y + 3, x] = 5
            img[y, x] = 13
            img[y + 3, x] = 1
        if x % 24 == 10 and 0 <= y < Hh:
            img[y + 3:min(Hh, int(stair_line(x))), x:x + 2] = 4
    # two lanterns
    for lx, cy in ((64, 46), (226, 36)):
        e = ((xs - lx) / 9.0) ** 2 + ((ys - cy) / 12.0) ** 2
        body = e <= 1
        g = np.clip(1 - e, 0, 1)
        img[body] = np.where(g > 0.55, 12, np.where(g > 0.18, 11, 10))[body]
        img[body & (((ys - cy) % 4) == 0) & (e > 0.1)] = 10
        img[body & (e > 0.8)] = 9
        img[(xs == lx) & (ys < cy - 12)] = 1
    return img


STAGES = [
    dict(name="dojo"),
    dict(name="bamboo", wall=bamboo_wall, floor=bamboo_floor, out=bamboo_outside,
         pw=PAL_BAMBOO_WALL, pf=PAL_BAMBOO_FLOOR, po=PAL_BAMBOO_OUT),
    dict(name="hall", wall=hall_wall, floor=hall_floor, out=hall_outside,
         pw=PAL_HALL_WALL, pf=PAL_HALL_FLOOR, po=PAL_HALL_OUT),
    dict(name="gallery", wall=gallery_wall, floor=gallery_floor, out=gallery_outside,
         pw=PAL_GALLERY_WALL, pf=PAL_GALLERY_FLOOR, po=PAL_GALLERY_OUT),
    dict(name="roof", wall=roof_wall, floor=roof_floor, out=roof_outside,
         pw=PAL_ROOF_WALL, pf=PAL_ROOF_FLOOR, po=PAL_ROOF_OUT),
    dict(name="title", wall=roof_wall, floor=roof_floor, out=title_outside,
         pw=PAL_ROOF_WALL, pf=PAL_ROOF_FLOOR, po=PAL_TITLE_OUT),
    dict(name="dock", wall=dock_wall, floor=dock_floor, out=dock_outside,
         pw=PAL_DOCK_WALL, pf=PAL_DOCK_FLOOR, po=PAL_DOCK_OUT),
    dict(name="spire", wall=spire_wall, floor=spire_floor, out=spire_outside,
         pw=PAL_SPIRE_WALL, pf=PAL_SPIRE_FLOOR, po=PAL_SPIRE_OUT),
]
STAGES[1]["wall"] = bamboo_wall_full
STAGES[2]["wall"] = hall_wall_full
STAGE_VARIANTS = {1: bamboo_variants, 2: hall_variants}     # wall pictures for the cut stalks / fallen lamps


# wall x of the lanterns on each floor (for the warm light on the fighters); none on the gallery or roof
STAGE_LAMPS = [[176, 336], BAMBOO_LAMPS, HALL_LAMPS, [], [], [], DOCK_LAMPS, []]


def stage_images(i):
    s = STAGES[i]
    if i == 0:
        return B.make_wall(), B.make_floor(), B.make_outside(), B.PAL_WALL, B.PAL_FLOOR, B.PAL_OUT
    return s["wall"](), s["floor"](), s["out"](), s["pw"], s["pf"], s["po"]
