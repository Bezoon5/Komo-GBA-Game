"""Build every asset into src/gen/assets.c + assets.h."""
import os
import sys
import math
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import fighters as F
import backgrounds as B
import stages as ST
import copy

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "gen")
os.makedirs(OUT, exist_ok=True)


def rgb15(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def tile_words(t):
    words = []
    for row in t:
        w = 0
        for x in range(8):
            w |= (int(row[x]) & 15) << (4 * x)
        words.append(w)
    return words


def block_tiles(img, x0, y0, w, h):
    tiles = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            tiles.append(img[y0 + ty * 8:y0 + ty * 8 + 8, x0 + tx * 8:x0 + tx * 8 + 8])
    return tiles


class TileSet:
    def __init__(self, limit):
        self.tiles = [np.zeros((8, 8), int)]
        self.lut = {self.key(self.tiles[0]): (0, 0)}
        self.limit = limit

    @staticmethod
    def key(t):
        return t.astype(np.uint8).tobytes()

    def add_raw(self, t):
        self.tiles.append(t)
        return len(self.tiles) - 1

    def add(self, t):
        for flip, tt in ((0, t), (1, t[:, ::-1]), (2, t[::-1, :]), (3, t[::-1, ::-1])):
            k = self.key(tt)
            if k in self.lut:
                idx, f0 = self.lut[k]
                return idx, flip ^ f0
        self.tiles.append(t)
        idx = len(self.tiles) - 1
        self.lut[self.key(t)] = (idx, 0)
        assert idx < self.limit, f"tileset full ({self.limit})"
        return idx, 0


def map_image(ts, img, bank, map_w_tiles):
    h, w = img.shape
    m = {}
    for ty in range(h // 8):
        for tx in range(w // 8):
            t = img[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            idx, fl = ts.add(t)
            m[(tx, ty)] = idx | (fl << 10) | (bank << 12) if idx else 0
    return m


def sb_layout(m, wt, ht=32):
    """Arrange a (tx,ty)->entry dict into GBA screenblock order."""
    out = []
    for sbx in range(wt // 32):
        for ty in range(ht):
            for tx in range(32):
                out.append(m.get((sbx * 32 + tx, ty), 0))
    return out


class Writer:
    def __init__(self):
        self.c = ['#include "assets.h"\n']
        self.h = ['#pragma once\n#include "../types.h"\n']

    def arr(self, ctype, name, data, per=12, fmt="0x%x"):
        self.h.append(f"extern const {ctype} {name}[{len(data)}];\n")
        lines = [f"const {ctype} {name}[{len(data)}] __attribute__((aligned(4))) = {{\n"]
        for i in range(0, len(data), per):
            lines.append(",".join(fmt % v for v in data[i:i + per]) + ",\n")
        lines.append("};\n")
        self.c.append("".join(lines))

    def define(self, name, val):
        self.h.append(f"#define {name} {val}\n")


W = Writer()

# ------------------------------------------------------------ fighters
BLOCKS = [(0, 0, 64, 32), (64, 0, 64, 32), (128, 0, 64, 32)] + \
         [(x, y, 64, 64) for y in (32, 96) for x in (0, 64, 128)]
anim_table = None
total = 0
for S in F.load_chars(with_variants=True):
    frames, table, gore = F.render_char_full(S)
    anim_table = table
    data, offs, masks, runs, roffs = [], [], [], [], []
    seen = {}                                   # identical frames share one copy of the tiles
    for f in frames:
        key = f.astype(np.uint8).tobytes()
        if key in seen:
            j = seen[key]
            offs.append(offs[j]); masks.append(masks[j]); roffs.append(roffs[j])
            continue
        seen[key] = len(offs)
        # only non-empty 8x8 tiles are stored; runs say copy / clear / skip, in VRAM slot order
        mask = 0
        offs.append(len(data))
        seq = []                                # (kind, n): 1 copy, 0 clear, 2 skip (block not drawn)
        def put(kind, n=1):
            if seq and seq[-1][0] == kind and seq[-1][1] + n < 16384:
                seq[-1][1] += n
            else:
                seq.append([kind, n])
        for bi, (x, y, w, h) in enumerate(BLOCKS):
            sub = f[y:y + h, x:x + w]
            if not sub.any():
                put(2, (w // 8) * (h // 8))
                continue
            mask |= 1 << bi
            for t in block_tiles(f, x, y, w, h):
                if t.any():
                    put(1); data += tile_words(t)
                else:
                    put(0)
        while seq and seq[-1][0] == 2:
            seq.pop()
        masks.append(mask)
        roffs.append(len(runs))
        runs.append(len(seq))
        runs += [(k << 14) | n for k, n in seq]
    nm = S["name"]
    W.arr("u32", f"{nm}_data", data, 8, "0x%08x")
    W.arr("u32", f"{nm}_off", offs, 12, "%d")
    W.arr("u16", f"{nm}_mask", masks, 16, "%d")
    W.arr("u16", f"{nm}_runs", runs, 16, "%d")
    W.arr("u32", f"{nm}_roff", roffs, 12, "%d")
    W.arr("u16", f"{nm}_pal", [rgb15(c) for c in S["palette"]], 16, "0x%04x")
    tips = F.frame_tips(S)
    W.arr("u8", f"{nm}_tipx", [t[0] for t in tips], 16, "%d")
    W.arr("u8", f"{nm}_tipy", [t[1] for t in tips], 16, "%d")
    assert len(tips) == len(frames), (len(tips), len(frames))
    _k = F.skeleton(F.M, S); _sc = S["scale"]
    _mx = F.RX + (_k["head"][0] + S["head_r"] * 0.9) * _sc; _my = F.GY - (_k["head"][1] - 1.5) * _sc
    W.arr("u8", f"{nm}_mouth", [int(round(_mx)), int(round(_my))], 16, "%d")     # breath on the cold roof
    anc = F.frame_anchors(S)
    assert len(anc) == len(frames)
    W.arr("u8", f"{nm}_anc", [v for a_ in anc for v in a_], 16, "%d")
    W.arr("u8", f"{nm}_gorex", [g[0] for g in gore], 16, "%d")
    W.arr("u8", f"{nm}_gorey", [g[1] for g in gore], 16, "%d")
    total += len(data) * 4 + len(runs) * 2
    print(nm, len(frames), "frames", len(data) * 4 // 1024, "KB")

W.h.append("enum {\n" + "".join(f"  A_{n.upper()},\n" for n, s, c in anim_table) + "  A_COUNT\n};\n")
W.arr("u16", "anim_start", [s for n, s, c in anim_table], 16, "%d")
W.arr("u16", "anim_len", [c for n, s, c in anim_table], 16, "%d")
W.define("FIG_CW", F.CW)
W.define("FIG_RX", F.RX)
W.define("FIG_GY", F.GY)

# ------------------------------------------------------------ stages: BG2 wall+floor, CB2 shared + outside
def bg2_set(wall, floor):
    ts2 = TileSet(1024)
    m = map_image(ts2, wall, 0, 64)
    if floor is not None:
        mf = map_image(ts2, floor, 1, 64)
        for (tx, ty), e in mf.items():
            m[(tx, ty + 15)] = e
    return ts2, sb_layout(m, 64)


# shared prefix of every CB2 set: font, rain, logo, seal, floor names
pre = TileSet(896)
for t in B.font_tiles():
    pre.add_raw(t)
W.define("FONT_FIRST", 1)
W.h.append('#define FONT_CHARS " ' + "".join(B.FONT_ORDER[1:]).replace('"', '\\"') + '"\n')
rain_map = sb_layout(map_image(pre, B.make_rain(), 4, 32), 32)
def small_map(ts, img, bank=8):
    m = map_image(ts, img, bank, 32)
    w, h = img.shape[1] // 8, img.shape[0] // 8
    return [m[(x, y)] for y in range(h) for x in range(w)], w, h
lm, lw, lh = small_map(pre, B.make_title_logo())
sm, sw, sh_ = small_map(pre, B.make_seal())
W.arr("u16", "logo_map", lm, 16, "0x%x")
W.arr("u16", "seal_map", sm, 16, "0x%x")
W.define("LOGO_W", lw); W.define("LOGO_H", lh)
W.define("SEAL_W", sw); W.define("SEAL_H", sh_)
# floor names in kanji for the stair climbs: 2F..5F (the last is the roof)
for i, txt in enumerate(("二階", "三階", "四階", "屋根")):
    mk = B._glyph_mask(txt, 22, 3)
    h, w = mk.shape
    Wd, Hd = (w + 4 + 7) // 8 * 8, (h + 4 + 7) // 8 * 8
    img = np.zeros((Hd, Wd), int)
    ox, oy = (Wd - w) // 2, (Hd - h) // 2
    msk = np.zeros((Hd, Wd), bool); msk[oy:oy + h, ox:ox + w] = mk
    grow = msk.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        grow |= np.roll(np.roll(msk, dy, 0), dx, 1)
    img[grow] = 1; img[msk] = 6
    km, kw, kh = small_map(pre, img)
    W.arr("u16", f"kanji{i}_map", km, 16, "0x%x")
    W.define(f"KANJI{i}_W", kw); W.define(f"KANJI{i}_H", kh)
W.h.append("extern const u16 *const kanji_maps[4];\nextern const u8 kanji_w[4], kanji_h[4];\n")
W.c.append("const u16 *const kanji_maps[4] = {kanji0_map, kanji1_map, kanji2_map, kanji3_map};\n"
           "const u8 kanji_w[4] = {KANJI0_W, KANJI1_W, KANJI2_W, KANJI3_W};\n"
           "const u8 kanji_h[4] = {KANJI0_H, KANJI1_H, KANJI2_H, KANJI3_H};\n")
import brush as BZ
for nm, txt, style, size, seed in (("ippon", "一本", "red", 40, 3), ("rnd1", "一", "cream", 30, 1), ("rnd2", "二", "cream", 30, 2),
                                   ("rnd3", "三", "cream", 30, 4), ("hajime", "始め", "cream", 34, 5)):
    cim = BZ.calligraphy(txt, size, style, seed=seed)
    cm, cw, chh = small_map(pre, cim)
    W.arr("u16", f"cal_{nm}_map", cm, 16, "0x%x")
    W.define(f"CAL_{nm.upper()}_W", cw); W.define(f"CAL_{nm.upper()}_H", chh)
# letterbox (bottom r rows black) and a ragged brush edge for ink wipes
bar0 = len(pre.tiles)
for r in range(8):
    t = np.zeros((8, 8), int); t[8 - r:, :] = 9 if r else 0
    pre.add_raw(t)
W.define("BAR_TILE", bar0)
rag0 = len(pre.tiles)
rr_ = np.random.default_rng(7)
for cov in range(1, 8):
    t = np.zeros((8, 8), int)
    for y in range(8):
        c_ = max(0, min(8, cov + int(rr_.integers(-1, 2))))
        t[y, :c_] = 9
        if c_ < 8 and rr_.random() < 0.4: t[y, c_] = 1
    pre.add_raw(t)
W.define("RAG_TILE", rag0)
# wipe tiles for the defeat: WIPE_TILE + r has its top r rows black; + 8 is solid
wipe0 = len(pre.tiles)
for r in range(9):
    t = np.zeros((8, 8), int); t[:r, :] = 9
    pre.add_raw(t)
assert len(pre.tiles) == wipe0 + 9
W.define("WIPE_TILE", wipe0)
W.arr("u16", "rain_map", rain_map, 16, "0x%x")
print("CB2 shared tiles", len(pre.tiles))

NST = len(ST.STAGES)
var_names = []
for si in range(NST):
    wall, floor, out, pw, pf, po = ST.stage_images(si)
    ts2, m2 = bg2_set(wall, floor)
    for vi, vimg in enumerate(ST.STAGE_VARIANTS.get(si, lambda: [])()):
        # a cut stalk / a fallen lamp: the map entries that change, as (index, entry) pairs
        mv = map_image(ts2, vimg, 0, 64)
        mw = map_image(TileSet(1024), wall, 0, 64) if False else None
        lay = sb_layout(mv, 64)
        pairs = []
        for idx in range(len(lay)):
            if idx < len(m2) and lay[idx] != m2[idx] and (idx % 1024) // 32 < 15:
                pairs += [idx, lay[idx]]
        W.arr("u16", f"st{si}_var{vi}", pairs if pairs else [0, 0], 16, "%d")
        var_names.append((si, vi, f"st{si}_var{vi}", len(pairs) // 2))
    W.arr("u32", f"st{si}_bg2_tiles", sum((tile_words(t) for t in ts2.tiles), []), 8, "0x%08x")
    W.arr("u16", f"st{si}_bg2_map", m2, 16, "0x%x")
    ts = copy.deepcopy(pre)
    om = sb_layout(map_image(ts, out, 3, 32), 32)
    W.arr("u32", f"st{si}_cb2_tiles", sum((tile_words(t) for t in ts.tiles), []), 8, "0x%08x")
    W.arr("u16", f"st{si}_out_map", om, 16, "0x%x")
    W.arr("u16", f"st{si}_pal_wall", [rgb15(c) for c in pw], 16, "0x%04x")
    W.arr("u16", f"st{si}_pal_floor", [rgb15(c) for c in pf], 16, "0x%04x")
    W.arr("u16", f"st{si}_pal_out", [rgb15(c) for c in po], 16, "0x%04x")
    print("stage", si, ST.STAGES[si]["name"], "BG2 tiles", len(ts2.tiles), "CB2 tiles", len(ts.tiles))
    assert len(ts.tiles) <= 860 and len(ts2.tiles) <= 832, "stage tile budget"
W.h.append("extern const u16 *const stage_var[%d];\nextern const u16 stage_var_n[%d];\n" % (NST * 2, NST * 2))
vt = ["0"] * (NST * 2); vn = ["0"] * (NST * 2)
for si, vi, nm_, n_ in var_names:
    vt[si * 2 + vi] = nm_; vn[si * 2 + vi] = str(n_)
W.c.append("const u16 *const stage_var[%d] = {%s};\nconst u16 stage_var_n[%d] = {%s};\n" % (NST * 2, ",".join(vt), NST * 2, ",".join(vn)))
W.arr("s16", "bamboo_stalks", list(ST.BAMBOO_STALKS), 8, "%d")
W.arr("s16", "hall_andon", list(ST.HALL_ANDON), 8, "%d")
# death poems, full screen on BG2
for pi in range(len(BZ.POEMS)):
    simg = np.zeros((160, 256), int); simg[:, 8:248] = BZ.still_poem(pi); simg[:, :8] = 1; simg[:, 248:] = 1
    tsp, mp = bg2_set(simg, None)
    W.arr("u32", f"poem{pi}_tiles", sum((tile_words(t) for t in tsp.tiles), []), 8, "0x%08x")
    W.arr("u16", f"poem{pi}_map", mp, 16, "0x%x")
    print("poem", pi, "tiles", len(tsp.tiles))
W.arr("u16", "poem_pal", [rgb15(c) for c in BZ.PAL_POEM], 16, "0x%04x")
W.h.append("extern const u32 *const poem_tiles[%d];\nextern const u32 poem_words[%d];\nextern const u16 *const poem_maps[%d];\n" % ((len(BZ.POEMS),) * 3))
W.c.append("const u32 *const poem_tiles[%d] = {%s};\nconst u32 poem_words[%d] = {%s};\nconst u16 *const poem_maps[%d] = {%s};\n" % (
    len(BZ.POEMS), ",".join(f"poem{i}_tiles" for i in range(len(BZ.POEMS))), len(BZ.POEMS),
    ",".join(f"sizeof(poem{i}_tiles) / 4" for i in range(len(BZ.POEMS))), len(BZ.POEMS), ",".join(f"poem{i}_map" for i in range(len(BZ.POEMS)))))
# dropped weapons (64x16, the fighter's palette), and the falling bamboo top
def weapon_image(S_):
    pz = F.P(F.M, hx=0, hy=-6, ba=0, behind=0)
    img = F.render(pz, S_, [190.0] * 3, only="weapon").idx
    ys_, xs_ = np.nonzero(img)
    crop = img[ys_.min():ys_.max() + 1, xs_.min():xs_.max() + 1]
    h, w = crop.shape
    k = min(1.0, 62.0 / w, 14.0 / h)
    if k < 1.0:
        nh, nw = max(1, int(h * k)), max(1, int(w * k))
        yy = (np.arange(nh) / k).astype(int); xx = (np.arange(nw) / k).astype(int)
        crop = crop[yy][:, xx]
    out = np.zeros((16, 64), int)
    h, w = crop.shape
    out[(16 - h) // 2:(16 - h) // 2 + h, (64 - w) // 2:(64 - w) // 2 + w] = crop
    return out
for S_ in F.load_chars():
    W.arr("u32", f"{S_['name']}_wpn", sum((tile_words(t) for t in block_tiles(weapon_image(S_), 0, 0, 64, 16)), []), 8, "0x%08x")
pole = np.zeros((16, 64), int)
for x in range(4, 62):
    pole[6:10, x] = 3 if x % 14 else 1
    pole[6, x] = 3; pole[9, x] = 2
pole[5:11, 4] = 1; pole[6:10, 61] = 11
for x in range(40, 62):
    for y in (2, 3, 12, 13):
        if (x + y) % 5 < 3: pole[y, x] = 2 if y in (3, 12) else 3
W.arr("u32", "pole_tiles", sum((tile_words(t) for t in block_tiles(pole, 0, 0, 64, 16)), []), 8, "0x%08x")
mark = t8(["00000000", "0000001f", "00001f30", "001f3000", "01f30000", "1f300000", "f3000000", "00000000"]) if False else None
# dialogue portraits (48x48, loaded at run time into CB2 tiles 860..895)
W.define("PORTRAIT_TILE", 860)
pchars = F.load_chars()
pw_all = []
for S_ in pchars:
    for ex_ in range(4):                 # v5.5: calm, angry, hurt, respectful
        pim = F.render_portrait(S_, expr=ex_)
        for ty in range(6):
            for tx in range(6):
                pw_all += tile_words(pim[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8])
W.arr("u32", "portrait_tiles", pw_all, 8, "0x%08x")
W.define("PORTRAIT_WORDS", 36 * 8)
# painted woodblock stills, full screen on BG2
for sn, fn, pal in (("still0", BZ.still_storm, BZ.PAL_STORM), ("still1", BZ.still_dawn, BZ.PAL_DAWN)):
    simg = np.zeros((160, 256), int); simg[:, 8:248] = fn(); simg[:, :8] = 1; simg[:, 248:] = 1
    ts2s, m2s = bg2_set(simg, None)
    W.arr("u32", f"{sn}_tiles", sum((tile_words(t) for t in ts2s.tiles), []), 8, "0x%08x")
    W.arr("u16", f"{sn}_map", m2s, 16, "0x%x")
    W.arr("u16", f"{sn}_pal", [rgb15(c) for c in pal], 16, "0x%04x")
    print(sn, "tiles", len(ts2s.tiles))
# the stairwell: a full-screen picture on BG2 (no floor)
stw = ST.stairwell()
ts2, m2 = bg2_set(stw, None)
W.arr("u32", "stair_tiles", sum((tile_words(t) for t in ts2.tiles), []), 8, "0x%08x")
W.arr("u16", "stair_map", m2, 16, "0x%x")
W.arr("u16", "stair_pal", [rgb15(c) for c in ST.PAL_STAIR_WALL], 16, "0x%04x")
print("stairwell tiles", len(ts2.tiles))
W.define("NSTAGES", NST)
lamps = []
for si in range(NST):
    L = ST.STAGE_LAMPS[si][:8]
    lamps += L + [0] * (8 - len(L))
W.arr("s16", "stage_lamps", lamps, 8, "%d")
W.h.append("typedef struct { const u32 *bg2; u32 bg2_words; const u16 *bg2_map; const u32 *cb2; u32 cb2_words; const u16 *out_map; const u16 *pw, *pf, *po; } StageArt;\n")
W.h.append(f"extern const StageArt stage_art[{NST}];\n")
W.c.append(f"const StageArt stage_art[{NST}] = {{\n" + "".join(
    f"  {{st{i}_bg2_tiles, sizeof(st{i}_bg2_tiles) / 4, st{i}_bg2_map, st{i}_cb2_tiles, sizeof(st{i}_cb2_tiles) / 4, st{i}_out_map, st{i}_pal_wall, st{i}_pal_floor, st{i}_pal_out}},\n"
    for i in range(NST)) + "};\n")

# ------------------------------------------------------------ palettes
bgpal = [0] * 256
for bank, pal in ((0, B.PAL_WALL), (1, B.PAL_FLOOR), (3, B.PAL_OUT), (4, B.PAL_RAIN)):   # stage 0 defaults
    for i, c in enumerate(pal):
        bgpal[bank * 16 + i] = rgb15(c)
for i, c in enumerate(B.PAL_TITLE):
    bgpal[8 * 16 + i] = rgb15(c)
for bank, pal in B.PAL_TEXT.items():
    for i, c in enumerate(pal):
        bgpal[bank * 16 + i] = rgb15(c)
bgpal[0] = rgb15((0, 0, 0))
W.arr("u16", "bg_pal", bgpal, 16, "0x%04x")
W.arr("u16", "fx_pal", [rgb15(c) for c in B.PAL_FX], 16, "0x%04x")
W.arr("u16", "flash_pal", [rgb15(c) for c in B.PAL_FLASH], 16, "0x%04x")
W.arr("u16", "out_pal", [rgb15(c) for c in B.PAL_OUT], 16, "0x%04x")
W.arr("u16", "wall_pal", [rgb15(c) for c in B.PAL_WALL], 16, "0x%04x")

# ------------------------------------------------------------ effects sprites
fx = []
fx += block_tiles(B.make_slash(), 0, 0, 64, 32)
sh = B.make_shadow()
fx += block_tiles(sh, 0, 0, 32, 16)
amb = []
def t8(rows): return np.array([[int(c, 16) for c in r] for r in rows])
amb.append(t8(["00000000", "00000330", "00003320", "00033200", "00332000", "03320000", "02200000", "00000000"]))   # leaf
amb.append(t8(["00000000", "00000000", "00000000", "03333320", "02222220", "00000000", "00000000", "00000000"]))   # leaf edge-on
amb.append(t8(["00000000", "00000000", "00066000", "00677600", "00677600", "00066000", "00000000", "00000000"]))   # ember
amb.append(t8(["00000000", "00000000", "00000000", "09888890", "00999900", "00000000", "00000000", "00000000"]))   # mist streak
amb.append(t8(["00000000", "00000000", "000ff000", "00fff100", "000f1000", "00000000", "00000000", "00000000"]))   # debris
amb.append(t8(["00000000", "00aaaa00", "0abbbba0", "0abbbba0", "0abbbba0", "0abbbba0", "00aaaa00", "00000000"]))   # dust puff
rip = np.zeros((8, 16), int); yy_, xx_ = np.mgrid[0:8, 0:16]
e_ = ((xx_ + 0.5 - 8) / 7.5) ** 2 + ((yy_ + 0.5 - 4) / 3.2) ** 2
rip[(e_ <= 1) & (e_ > 0.55)] = 12; rip[(e_ <= 1) & (e_ > 0.82) & (yy_ < 4)] = 13
fx += amb + block_tiles(rip, 0, 0, 16, 8)
fx += B.make_sparks()
fx += block_tiles(B.make_glint(), 0, 0, 16, 16)
# blood: a small drop, a wide stain (16x8) and a small stain
d2 = np.zeros((8, 8), int); d2[3:5, 3:5] = 5; d2[3, 3] = 7
fx.append(d2)
st = np.zeros((8, 16), int)
yy, xx = np.mgrid[0:8, 0:16]
e = ((xx + 0.5 - 8) / 7.5) ** 2 + ((yy + 0.5 - 4.5) / 2.6) ** 2
st[e <= 1] = 7; st[(e <= 0.45)] = 5; st[(e <= 1) & ((xx + yy * 3) % 7 == 0)] = 5
fx += block_tiles(st, 0, 0, 16, 8)
ss = np.zeros((8, 8), int); ss[4:6, 2:6] = 7; ss[4, 3:5] = 5; ss[3, 4] = 7
fx.append(ss)
fx.append(t8(["00000000", "000ee000", "00eeee00", "0eeeeee0", "00eeeee0", "000ee0e0", "0e000000", "00000000"]))   # ink splat (1021)
fx.append(t8(["00000000", "00000000", "000d0000", "00dcd000", "000c0000", "00000000", "00000000", "00000000"]))   # spray (1022)
fx.append(t8(["00000001", "0000001f", "000001f3", "00001f30", "0001f300", "001f3000", "01f30000", "11000000"]))   # cloth cut (1023), fighter palette
W.arr("u32", "fx_tiles", sum((tile_words(t) for t in fx), []), 8, "0x%08x")
# the iai target: a red reticule the falling leaf drops into (32x32, effects palette: 5 red, 7 dark red, 1 white)
ret = np.zeros((32, 32), int); yy_, xx_ = np.mgrid[0:32, 0:32]
rr_ = np.hypot(xx_ + 0.5 - 16, yy_ + 0.5 - 16)
ret[(rr_ <= 13.5) & (rr_ > 10.5)] = 7
ret[(rr_ <= 12.8) & (rr_ > 11.2)] = 5
for (a_, b_) in ((15, 16),):
    ret[1:9, a_:b_ + 1] = 0; ret[23:31, a_:b_ + 1] = 0; ret[a_:b_ + 1, 1:9] = 0; ret[a_:b_ + 1, 23:31] = 0
    ret[2:9, a_:b_ + 1] = 5; ret[23:30, a_:b_ + 1] = 5; ret[a_:b_ + 1, 2:9] = 5; ret[a_:b_ + 1, 23:30] = 5
ret[15:17, 15:17] = 1
W.arr("u32", "ret_tiles", sum((tile_words(t) for t in block_tiles(ret, 0, 0, 32, 32)), []), 8, "0x%08x")

W.define("FX_SLASH", 960)
W.define("FX_SHADOW", 992)
W.define("FX_SPARK", 1008)
W.define("FX_DROP", 1012)
W.define("FX_GLINT", 1013)
W.define("FX_DROP2", 1017)
W.define("FX_STAIN", 1018)
W.define("FX_LEAF", 1000)
W.define("FX_EMBER", 1002)
W.define("FX_MIST", 1003)
W.define("FX_DEBRIS", 1004)
W.define("FX_DUST", 1005)
W.define("FX_RIPPLE", 1006)
W.define("FX_INK", 1021)
W.define("FX_SPRAY", 1022)
W.define("FX_MARK", 1023)
W.define("FX_PROP", 960)          # the slash tiles double as the dropped weapon (960) and the falling bamboo (976)
PAL_AMB = [(0, 0, 0), (20, 14, 10), (180, 40, 24), (232, 110, 40), (40, 90, 40), (110, 170, 70), (230, 70, 20),
           (255, 220, 110), (210, 220, 235), (140, 160, 190), (150, 132, 108), (200, 186, 156), (150, 190, 230),
           (220, 240, 255), (16, 12, 14), (90, 80, 70)]
PAL_AMB2 = list(PAL_AMB); PAL_AMB2[2] = (40, 90, 40); PAL_AMB2[3] = (110, 170, 70)      # bamboo leaves
W.arr("u16", "amb_pal", [rgb15(c) for c in PAL_AMB], 16, "0x%04x")
W.arr("u16", "amb_pal2", [rgb15(c) for c in PAL_AMB2], 16, "0x%04x")
PAL_BREATH = list(PAL_AMB); PAL_BREATH[10] = (150, 160, 182); PAL_BREATH[11] = (214, 222, 238)   # the dust puff, as breath
W.arr("u16", "breath_pal", [rgb15(c) for c in PAL_BREATH], 16, "0x%04x")

# ------------------------------------------------------------ tables
kfix = [int(round((B.K_WALL if y < B.HORIZON else B.k_of(y)) * 256)) for y in range(160)]
W.arr("u16", "k_table", kfix, 16, "%d")
W.define("K_OUT_FIX", int(round(B.K_OUT * 256)))
W.arr("s16", "sin_table", [int(round(math.sin(i * 2 * math.pi / 256) * 4096)) for i in range(256)], 16, "%d")


# square-channel frequency register for MIDI notes 36..96
def sqreg(note):
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    return max(0, min(2047, int(round(2048 - 131072 / f))))


W.arr("u16", "note_reg", [sqreg(n) for n in range(36, 97)], 16, "%d")

# ------------------------------------------------------------ soundtrack (tools/music.py)
import music as MU
W.arr("u16", "wave_reg", [MU.wave_reg(n) for n in range(24, 97)], 16, "%d")
wb = []
for i in range(0, 32, 2):
    wb.append((MU.WAVE[i] << 4) | MU.WAVE[i + 1])
W.arr("u8", "wave_shape", wb, 16, "%d")
W.h.append("typedef struct { const u8 *lead, *bass, *drum; u8 tempo, duty, vol, step, bass_vol; } Song;\n")
for nm in MU.ORDER:
    sg = MU.SONGS[nm]
    for v in ("lead", "bass", "drum"):
        W.arr("u8", f"song_{nm}_{v}", MU.stream_bytes(sg[v], sg["loop"]), 16, "%d")
W.h.append("enum {" + ", ".join(f"SONG_{nm.upper()}" for nm in MU.ORDER) + ", NSONGS};\n")
W.h.append("extern const Song songs[NSONGS];\n")
W.c.append("const Song songs[NSONGS] = {\n" + "".join(
    f"  {{song_{nm}_lead, song_{nm}_bass, song_{nm}_drum, {MU.SONGS[nm]['tempo']}, {MU.SONGS[nm]['inst']['duty']}, {MU.SONGS[nm]['inst']['vol']}, {MU.SONGS[nm]['inst']['step']}, {MU.SONGS[nm]['bass_vol']}}},\n"
    for nm in MU.ORDER) + "};\n")

open(os.path.join(OUT, "assets.c"), "w").write("".join(W.c))
open(os.path.join(OUT, "assets.h"), "w").write("".join(W.h))
print("fighter data total", total // 1024, "KB")
