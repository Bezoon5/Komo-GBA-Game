"""Dojo, garden, rain, font and effects art for The Crimson Pagoda (indexed)."""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) + 0.5) / 16.0

VP, HORIZON, FEET = 64, 120, 152


def k_of(y):
    return (y - VP) / (FEET - VP)


K_WALL = k_of(HORIZON)
K_OUT = 0.30


def dither(val, xs, ys):
    """val: float tone index; returns int tone using 4x4 ordered dither."""
    return np.floor(val + BAYER[ys & 3, xs & 3]).astype(int)


def rng(seed):
    return np.random.default_rng(seed)


# ------------------------------------------------------------------ palettes
PAL_WALL = [(0, 0, 0), (22, 13, 10), (52, 31, 22), (92, 56, 34), (134, 86, 50), (178, 122, 68),
            (156, 118, 76), (212, 170, 106), (244, 212, 146), (124, 26, 18), (200, 56, 30),
            (250, 146, 52), (255, 224, 126), (218, 204, 174), (14, 11, 14), (176, 32, 30)]
PAL_FLOOR = [(0, 0, 0), (26, 15, 11), (64, 39, 24), (96, 61, 36), (128, 86, 50), (164, 116, 68),
             (206, 128, 60), (244, 188, 102), (104, 112, 146), (146, 156, 190), (0, 0, 0),
             (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)]
PAL_OUT = [(0, 0, 0), (30, 38, 74), (42, 54, 96), (58, 74, 122), (80, 100, 150), (110, 130, 178),
           (146, 164, 204), (222, 228, 240), (252, 252, 255), (44, 54, 88), (30, 38, 64),
           (12, 22, 20), (24, 44, 36), (46, 76, 58), (70, 76, 92), (244, 176, 86)]
PAL_RAIN = [(0, 0, 0), (186, 204, 232), (130, 150, 190)] + [(0, 0, 0)] * 13
PAL_TEXT = {
    5: [(0, 0, 0), (10, 6, 8), (244, 240, 230), (160, 150, 140)] + [(0, 0, 0)] * 12,
    6: [(0, 0, 0), (10, 6, 8), (236, 70, 44), (150, 30, 20)] + [(0, 0, 0)] * 12,
    7: [(0, 0, 0), (10, 6, 8), (252, 206, 96), (176, 120, 40)] + [(0, 0, 0)] * 12,
}
PAL_FX = [(0, 0, 0), (255, 255, 255), (226, 236, 255), (255, 214, 96), (244, 124, 44),
          (204, 30, 30), (0, 0, 0), (120, 14, 14)] + [(0, 0, 0)] * 8
PAL_FLASH = [(0, 0, 0)] + [(255, 255, 255)] * 15

# wall layout (image coordinates, 512 wide)
PILLARS = [16, 96, 176, 336, 416, 496]
LANTERNS = [176, 336]
OPEN = (184, 328)


def lantern_light(xs, ys):
    """0..1 warm light from the lanterns at wall positions."""
    l = np.zeros(xs.shape)
    for lx in LANTERNS:
        d = np.sqrt(((xs - lx) / 1.25) ** 2 + (ys - 54) ** 2)
        l += np.clip(1.15 - d / 120.0, 0, 1)
    return np.clip(l, 0, 1)


def make_wall():
    W, Hh = 512, HORIZON
    img = np.zeros((Hh, W), int)
    ys, xs = np.mgrid[0:Hh, 0:W]
    light = lantern_light(xs, ys)
    R = rng(7)

    # plaster / shoji everywhere first
    # shoji (outer + right bay): lattice over glowing paper
    paper = 6 + dither(0.6 + light * 2.2, xs, ys).clip(0, 2)
    img[:] = paper
    lat_x = ((xs % 18) == 0)
    lat_y = ((ys - 28) % 13 == 0)
    img[(lat_x | lat_y)] = 3
    # left bay: plaster alcove (tokonoma) without lattice
    lb = (xs > 104) & (xs < 168)
    plaster = 6 + dither(0.2 + light * 1.4 + R.random(xs.shape) * 0.25, xs, ys).clip(0, 1)
    img[lb] = plaster[lb]
    # opening
    op = (xs >= OPEN[0]) & (xs < OPEN[1]) & (ys >= 24)
    img[op] = 0
    # slid shoji stubs at each side of the opening
    for x0, x1 in ((OPEN[0], OPEN[0] + 14), (OPEN[1] - 14, OPEN[1])):
        st = (xs >= x0) & (xs < x1) & (ys >= 24) & (ys < 100)
        img[st] = paper[st]
        img[st & ((xs == x0) | (xs == x1 - 1))] = 2
        img[st & lat_y] = 3

    # transom (ranma): lattice openings above the beam
    tr = (ys >= 12) & (ys < 22)
    img[tr] = 0
    lat = tr & (((xs + ys) % 9 == 0) | ((xs - ys) % 9 == 0))
    img[lat] = 2
    img[(ys == 12) | (ys == 21)] = 2
    # ceiling beam
    img[ys < 9] = 2
    img[(ys >= 2) & (ys < 5)] = 3
    img[ys == 1] = 4
    img[(ys >= 9) & (ys < 12)] = 1
    # nageshi (horizontal beam)
    nb = (ys >= 22) & (ys < 28)
    img[nb] = 3
    img[ys == 22] = 5
    img[ys == 23] = 4
    img[ys == 27] = 1
    # wainscot boards
    wb = (ys >= 100) & (ys < 117)
    board = 2 + ((xs // 11) % 2) + dither(light * 0.9, xs, ys).clip(0, 1)
    img[wb] = board[wb]
    img[wb & (xs % 11 == 0)] = 1
    img[ys == 100] = 4
    img[(ys >= 117)] = 1
    img[ys == 118] = 2
    # pillars
    for px in PILLARS:
        for dx in range(-8, 8):
            x = px + dx
            if 0 <= x < W:
                t = (dx + 8) / 16.0
                lit = 0.35 + 0.9 * math.sin(t * math.pi) - 0.4 * t
                col = 1 + dither(np.full(Hh, lit * 3.2), np.full(Hh, x), np.arange(Hh)).clip(0, 3)
                img[:, x] = col
        img[:, max(0, px - 8)] = 1
        img[:, min(W - 1, px + 7)] = 1
        img[(ys < 9) & (abs(xs - px) < 8)] = 2

    # hanging scroll in the alcove
    sx0, sx1, sy0, sy1 = 120, 152, 30, 96
    sc = (xs >= sx0) & (xs < sx1) & (ys >= sy0) & (ys < sy1)
    img[sc] = 9
    inner = (xs >= sx0 + 3) & (xs < sx1 - 3) & (ys >= sy0 + 6) & (ys < sy1 - 6)
    img[inner] = 13
    img[(ys == sy0) & (xs >= sx0 - 2) & (xs < sx1 + 2)] = 1
    img[(ys == sy1 - 1) & (xs >= sx0 - 2) & (xs < sx1 + 2)] = 1
    img[(ys >= sy0 - 6) & (ys < sy0) & ((xs == (sx0 + sx1) // 2))] = 1
    ink = kanji_mask("剣", 26)
    hh, ww = ink.shape
    ox = (sx0 + sx1) // 2 - ww // 2
    oy = sy0 + 10
    img[oy:oy + hh, ox:ox + ww][ink] = 14
    img[sy1 - 16:sy1 - 11, sx1 - 12:sx1 - 7] = 15

    # lanterns (chochin)
    for lx in LANTERNS:
        cy, ry, rx = 52, 15, 11
        img[(ys >= 22) & (ys < cy - ry) & (xs == lx)] = 1
        e = ((xs - lx) / rx) ** 2 + ((ys - cy) / ry) ** 2
        body = e <= 1.0
        g = np.clip(1.0 - e, 0, 1)
        tone = np.where(g > 0.55, 12, np.where(g > 0.18, 11, 10))
        img[body] = tone[body]
        ribs = body & (((ys - cy) % 4) == 0) & (e > 0.08)
        img[ribs] = np.where(g[ribs] > 0.4, 10, 9)
        img[body & (e > 0.82)] = 9
        cap = (abs(xs - lx) < 7) & ((abs(ys - (cy - ry)) < 2) | (abs(ys - (cy + ry)) < 2))
        img[cap] = 1
    return img


def kanji_mask(ch, size):
    f = ImageFont.truetype("/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc", size)
    im = Image.new("L", (size + 4, size + 4), 0)
    ImageDraw.Draw(im).text((2, -size * 0.12), ch, fill=255, font=f)
    a = np.array(im) > 110
    ys, xs = np.nonzero(a)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def make_floor():
    W = 512
    rows = np.arange(HORIZON, 160)
    img = np.zeros((len(rows), W), int)
    xs = np.arange(W)
    for i, y in enumerate(rows):
        k = k_of(y)
        wx = (xs - 256) / k
        pw = 26.0
        pidx = np.floor(wx / pw).astype(int)
        frac = wx / pw - pidx
        h = (pidx * 2654435761) & 0xFFFF
        base = 2.3 + ((h % 7) / 7.0) * 1.3
        depth = (y - HORIZON) / 40.0
        sheen = 1.3 * (1 - depth) ** 2
        # lantern reflections
        refl = np.zeros(W)
        for lx in LANTERNS:
            lwx = (lx - 256) / K_WALL
            refl += np.clip(1 - np.abs(wx - lwx) / 9.0, 0, 1) * (0.5 + 0.8 * (1 - depth))
        cool = np.clip(1 - np.abs(wx) / ((OPEN[1] - OPEN[0]) / 2 / K_WALL), 0, 1) * (1 - depth) * 1.1
        val = base + sheen * 0.8
        t = dither(val, xs, np.full(W, y))
        t = np.clip(t, 2, 5)
        # reflections override
        rd = dither(refl * 2.2, xs, np.full(W, y))
        t = np.where(rd >= 2, 7, np.where(rd >= 1, 6, t))
        cd = dither(cool * 1.6, xs, np.full(W, y))
        t = np.where((cd >= 1) & (rd < 1), 8 + (cd >= 2), t)
        gap = (frac * pw * k) < 1.0
        # staggered board ends
        zz = 1.0 / k
        joint = (np.abs(((zz * 9.0 + (h % 5) * 0.37) % 1.0)) < 0.06 * k)
        t = np.where(gap | joint, 1, t)
        img[i] = t
    img[0] = 1
    return img


def make_outside():
    W, Hh = 256, 128
    ys, xs = np.mgrid[0:Hh, 0:W]
    R = rng(3)
    # sky gradient
    sky = 1 + dither(np.clip((ys - 6) / 92.0, 0, 1) * 4.0, xs, ys).clip(0, 4)
    img = sky.copy()
    # moon + halo
    mx, my = 150, 46
    d = np.sqrt((xs - mx) ** 2 + (ys - my) ** 2)
    halo = np.clip(1 - d / 34.0, 0, 1)
    hv = dither(halo * 2.2, xs, ys)
    img = np.where(hv >= 2, 6, np.where(hv >= 1, np.maximum(img, 5), img))
    img[d <= 10.5] = 7
    img[(d <= 10.5) & ((xs - mx) + (ys - my) < -5)] = 8
    img[(d <= 10.5) & (((xs - mx + 3) ** 2 + (ys - my - 2) ** 2) < 7)] = 6
    # mountains
    far = 76 + 9 * np.sin(xs / 256 * 2 * math.pi * 2 + 1.0) + 5 * np.sin(xs / 256 * 2 * math.pi * 5)
    near = 90 + 6 * np.sin(xs / 256 * 2 * math.pi * 3 + 2.3) + 3 * np.sin(xs / 256 * 2 * math.pi * 7)
    img[ys >= far] = 9
    img[ys >= near] = 10
    # garden ground
    img[ys >= 99] = 10
    # bamboo
    for _ in range(26):
        bx = int(R.integers(0, W))
        if abs(bx - mx) < 6:
            continue
        top = int(R.integers(-10, 40))
        w = int(R.integers(2, 4))
        dark = R.random() < 0.5
        for y in range(max(0, top), 104):
            sway = int(round((104 - y) / 104.0 * 3 * math.sin(bx)))
            for dx in range(w):
                x = (bx + dx + sway) % W
                img[y, x] = 11 if dark else (13 if dx == 0 else 12)
            if (y - top) % 14 == 0:
                for dx in range(-1, w + 1):
                    img[y, (bx + dx + sway) % W] = 11
        # leaves
        for _ in range(int(R.integers(3, 7))):
            ly = int(R.integers(max(0, top), max(1, top + 50)))
            ln = int(R.integers(5, 10))
            dirn = 1 if R.random() < 0.5 else -1
            for j in range(ln):
                y = ly + j // 3
                x = (bx + dirn * j) % W
                if 0 <= y < Hh:
                    img[y, x] = 11 if dark else 12
    # stone lantern (toro)
    tx = 96
    img[80:100, tx - 2:tx + 3] = 14
    img[76:80, tx - 6:tx + 7] = 14
    img[70:76, tx - 4:tx + 5] = 14
    img[71:75, tx - 2:tx + 3] = 15
    img[66:70, tx - 7:tx + 8] = 14
    img[64:66, tx - 2:tx + 3] = 14
    return img


def make_rain():
    S = 64
    img = np.zeros((S, S), int)
    R = rng(11)
    for _ in range(26):
        x = int(R.integers(0, S))
        y = int(R.integers(0, S))
        ln = int(R.integers(5, 10))
        c = 1 if R.random() < 0.5 else 2
        for j in range(ln):
            img[(y + j) % S, (x + j // 4) % S] = c
    return np.tile(img, (4, 4))


# ---------------------------------------------------------------- font (5x7)
FONT = {
    'A': '.###.#...##...#######...##...##...#',
    'B': '####.#...##...#####.#...##...#####.',
    'C': '.###.#...##....#....#....#...#.###.',
    'D': '####.#...##...##...##...##...#####.',
    'E': '######....#....####.#....#....#####',
    'F': '######....#....####.#....#....#....',
    'G': '.###.#...##....#.####...##...#.####',
    'H': '#...##...##...#######...##...##...#',
    'I': '.###...#....#....#....#....#...###.',
    'J': '..###...#....#....#....#.#..#..##..',
    'K': '#...##..#.#.#..##...#.#..#..#.#...#',
    'L': '#....#....#....#....#....#....#####',
    'M': '#...###.###.#.##.#.##...##...##...#',
    'N': '#...###..##.#.##..###...##...##...#',
    'O': '.###.#...##...##...##...##...#.###.',
    'P': '####.#...##...#####.#....#....#....',
    'Q': '.###.#...##...##...##.#.##..#..##.#',
    'R': '####.#...##...#####.#.#..#..#.#...#',
    'S': '.#####....#.....###.....#....#####.',
    'T': '#####..#....#....#....#....#....#..',
    'U': '#...##...##...##...##...##...#.###.',
    'V': '#...##...##...##...##...#.#.#...#..',
    'W': '#...##...##...##.#.##.#.##.#.#.#.#.',
    'X': '#...##...#.#.#...#...#.#.#...##...#',
    'Y': '#...##...#.#.#...#....#....#....#..',
    'Z': '#####....#...#...#...#...#....#####',
    '0': '.###.#..###.#.##.#.###..##...#.###.',
    '1': '..#...##....#....#....#....#...###.',
    '2': '.###.#...#....#...#...#...#...#####',
    '3': '####.....#....#.###.....#....#####.',
    '4': '...#...##..#.#.#..#.#####...#....#.',
    '5': '######....####.....#....##...#.###.',
    '6': '..##..#...#....####.#...##...#.###.',
    '7': '#####....#...#...#...#....#....#...',
    '8': '.###.#...##...#.###.#...##...#.###.',
    '9': '.###.#...##...#.####....#...#..##..',
    '.': '..........................##...##..',
    ',': '.....................##....#...#...',
    '!': '..#....#....#....#....#.........#..',
    '?': '.###.#...#....#...#...#.........#..',
    "'": '..#....#...#.......................',
    '-': '................###................',
    ':': '......##...##........##...##.......',
    '/': '....#....#...#...#...#...#....#....',
    '(': '...#...#...#....#....#.....#.....#.',
    ')': '.#.....#.....#....#....#...#...#...',
    '"': '.#.#..#.#..........................',
    ' ': '...................................',
    '^': '..#...###.#####.....#####..........',
    '=': '.....#####.....#####.....#####.....',
    '_': '..........#####.....#####.###...#..',
    '*': '.......#...###.#####.###...#.......',
    '>': '.#.....#.....#.....#...#...#...#...',
    '+': '.......#...#.#.#...#.#.#...#.......',
    '#': '.....#########################.....',
    '~': '.....######...##...##...######.....',
}
FONT_ORDER = list(" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?'-:/()\"^=_*>+#~")


def font_tiles():
    tiles = []
    for ch in FONT_ORDER:
        bits = FONT[ch]
        g = np.zeros((8, 8), int)
        m = np.array([c == '#' for c in bits]).reshape(7, 5)
        g[0:7, 1:6][m] = 2
        fill = g == 2
        out = np.zeros_like(fill)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1)):
            out |= np.roll(np.roll(fill, dy, 0), dx, 1)
        g[out & ~fill] = 1
        g[fill & (np.arange(8)[:, None] >= 5)] = 3 if ch not in "^=_*+#~" else 2
        tiles.append(g)
    return tiles


# ---------------------------------------------------------------- effects
def make_slash():
    W, Hh = 64, 32
    img = np.zeros((Hh, W), int)
    ys, xs = np.mgrid[0:Hh, 0:W]
    # crescent: thick in the middle, tapering to points at both ends
    t = (xs + 0.5) / W
    centre = 16 + 9 * (1 - (2 * t - 1) ** 2) * 0.0 - 6 * np.sin(t * math.pi) + 6
    half = 4.5 * np.sin(t * math.pi) ** 1.3
    d = np.abs(ys + 0.5 - centre)
    img[d <= half + 1.2] = 5
    img[d <= half + 0.4] = 2
    img[d <= half * 0.55] = 1
    return img


def make_shadow():
    W, Hh = 64, 16
    ys, xs = np.mgrid[0:Hh, 0:W]
    e = ((xs + 0.5 - 32) / 30.0) ** 2 + ((ys + 0.5 - 8) / 5.0) ** 2
    img = np.where(e <= 1, 6, 0)
    return img


def make_sparks():
    tiles = []
    for f in range(4):
        g = np.zeros((8, 8), int)
        r = [1, 2, 3, 2][f]
        c = 3 if f < 2 else 4
        g[4, 4 - r:4 + r + 1] = c
        g[4 - r:4 + r + 1, 4] = c
        if f < 2:
            g[4, 4] = 1
        tiles.append(g)
    drop = np.zeros((8, 8), int)
    drop[2:6, 3:5] = 5
    drop[3:5, 2:6] = 5
    drop[3, 3] = 7
    tiles.append(drop)
    return tiles


# ---------------------------------------------------------------- title logo
PAL_TITLE = [(0, 0, 0), (12, 6, 8), (110, 14, 14), (184, 30, 22), (236, 74, 38),
             (252, 204, 96), (246, 234, 206), (150, 22, 20), (60, 8, 10)] + [(0, 0, 0)] * 7
CJK_BLACK = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Black.ttc"


def _glyph_mask(text, size, spacing):
    f = ImageFont.truetype(CJK_BLACK, size)
    im = Image.new("L", (size * len(text) * 2, size * 2), 0)
    d = ImageDraw.Draw(im)
    x = 4
    for ch in text:
        d.text((x, 4), ch, fill=255, font=f)
        x += f.getlength(ch) + spacing
    a = np.array(im) > 120
    ys, xs = np.nonzero(a)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def make_title_logo():
    """'KŌMŌ' in heavy red serif with gold bevel, ink outline and drop shadow."""
    W, H = 152, 56
    m = _glyph_mask("KŌMŌ", 41, 2)
    h, w = m.shape
    img = np.zeros((H, W), int)
    ox, oy = (W - w) // 2 - 1, (H - h) // 2 - 1
    mask = np.zeros((H, W), bool)
    mask[oy:oy + h, ox:ox + w] = m
    ys, xs = np.mgrid[0:H, 0:W]
    # drop shadow
    sh = np.roll(np.roll(mask, 3, 0), 3, 1)
    grow = mask.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        grow |= np.roll(np.roll(mask, dy, 0), dx, 1)
    shg = np.roll(np.roll(grow, 3, 0), 3, 1)
    img[shg] = 8
    img[grow] = 1
    # red gradient fill
    t = (ys - oy) / max(1, h)
    tone = dither((1 - t) * 2.6, xs, ys).clip(0, 2)
    img[mask] = (2 + tone)[mask]
    # gold bevel on top edges, bright rim on left edges
    top = mask & ~np.roll(mask, 1, 0)
    top2 = mask & ~np.roll(mask, 2, 0) & ~top
    img[top] = 5
    img[top2 & (t < 0.5)] = 4
    left = mask & ~np.roll(mask, 1, 1) & ~top
    img[left & (t < 0.7)] = 4
    return img


def make_seal():
    """Vertical red hanko stamp with 紅毛 in cream."""
    W, H = 24, 48
    R = rng(21)
    img = np.zeros((H, W), int)
    img[1:H - 1, 1:W - 1] = 3
    img[2:H - 2, 2:W - 2] = 7
    img[0, :] = img[-1, :] = 0
    for ch, y0 in (("紅", 4), ("毛", 25)):
        km = kanji_mask(ch, 19)
        kh, kw = km.shape
        x0 = (W - kw) // 2
        sub = img[y0:y0 + kh, x0:x0 + kw]
        sub[km] = 6
    # worn stamp texture
    holes = (R.random((H, W)) < 0.05) & (img == 7)
    img[holes] = 3
    edge = np.zeros((H, W), bool)
    edge[1, :] = edge[-2, :] = True
    edge[:, 1] = edge[:, -2] = True
    img[edge & (R.random((H, W)) < 0.25)] = 0
    # ink outline around the stamp
    filled = img != 0
    g = filled.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        g |= np.roll(np.roll(filled, dy, 0), dx, 1)
    return img


def make_glint():
    """16x16 four-point star for blade telegraphs."""
    g = np.zeros((16, 16), int)
    for i in range(16):
        d = abs(i - 7.5)
        if d < 7:
            g[7:9, i] = 3 if d > 2 else 1
            if d < 5.5:
                g[i, 7:9] = 3 if d > 2 else 1
    for k in (-2, -1, 1, 2):
        g[7 + k, 7 + k] = 2
        g[8 - k, 7 + k] = 2
    g[6:10, 6:10][g[6:10, 6:10] == 0] = 2
    g[7:9, 7:9] = 1
    return g
