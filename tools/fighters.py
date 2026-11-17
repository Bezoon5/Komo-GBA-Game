"""Pose-based fighter renderer for The Crimson Pagoda.

Each fighter is a 2D puppet (hip, torso, head, two legs, two arms, sword)
posed with a handful of parameters, solved with two-bone IK and drawn as
shaded capsules into a 192x128 indexed canvas (16-colour palette).
"""
import math
import numpy as np

CW, CH = 192, 160          # canvas (3 x 64x32 on top, then 3x2 x 64x64)
RX, GY = 70, 156           # root x and ground y inside the canvas

# palette index layout shared by every fighter
OUT = 1
SKIN, HAIR, TOP, BOT = 2, 5, 8, 11   # each has 3 tones: dark, mid, light
STEEL_L, STEEL_M = 14, 15

L3 = np.array([-0.45, 0.55, 0.70])
L3 = L3 / np.linalg.norm(L3)
OCCL_OFFS = ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2), (0, -2), (2, 0), (-2, 0), (1, 1), (-1, -1), (1, -1), (-1, 1))


def rad(a):
    return a * math.pi / 180.0


def v(x, y):
    return np.array([float(x), float(y)])


def ik(a, b, l1, l2, bend):
    d = b - a
    dist = float(np.hypot(*d))
    dist = max(min(dist, l1 + l2 - 1e-3), abs(l1 - l2) + 1e-3)
    base = math.atan2(d[1], d[0])
    ca = (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist)
    A = math.acos(max(-1.0, min(1.0, ca)))
    ang = base + bend * A
    j = a + l1 * v(math.cos(ang), math.sin(ang))
    dj = b - j
    n = float(np.hypot(*dj)) or 1.0
    e = j + l2 * dj / n
    return j, e


class Canvas:
    def __init__(self):
        self.idx = np.zeros((CH, CW), np.uint8)
        self.z = np.full((CH, CW), -1e9)
        self.I = np.full((CH, CW), np.nan)     # v5.4: the light on each shaded pixel (NaN: flat colour)

    def _grid(self, x0, y0, x1, y1):
        x0 = max(0, int(math.floor(x0)) - 1)
        y0 = max(0, int(math.floor(y0)) - 1)
        x1 = min(CW, int(math.ceil(x1)) + 2)
        y1 = min(CH, int(math.ceil(y1)) + 2)
        if x1 <= x0 or y1 <= y0:
            return None
        ys, xs = np.mgrid[y0:y1, x0:x1]
        return (slice(y0, y1), slice(x0, x1), xs + 0.5, ys + 0.5)

    def cap(self, p0, p1, r0, r1, mat, z, bias=0.0, flat=None, mask=None):
        """Shaded capsule from p0 to p1 (canvas coords, y down)."""
        g = self._grid(min(p0[0] - r0, p1[0] - r1), min(p0[1] - r0, p1[1] - r1),
                       max(p0[0] + r0, p1[0] + r1), max(p0[1] + r0, p1[1] + r1))
        if g is None:
            return
        sy, sx, xs, ys = g
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        l2 = dx * dx + dy * dy
        if l2 > 1e-9:
            t = np.clip(((xs - p0[0]) * dx + (ys - p0[1]) * dy) / l2, 0, 1)
        else:
            t = np.zeros_like(xs)
        cx = p0[0] + t * dx
        cy = p0[1] + t * dy
        r = r0 + (r1 - r0) * t
        ox, oy = xs - cx, ys - cy
        d = np.sqrt(ox * ox + oy * oy)
        inside = d <= r
        if mask is not None:
            inside &= mask(xs, ys)
        if not inside.any():
            return
        if flat is None:
            nx = ox / r
            ny = -oy / r
            nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
            I = nx * L3[0] + ny * L3[1] + nz * L3[2] + bias
            tone = np.where(I < 0.28, 0, np.where(I < 0.74, 1, 2))
            val = (mat + tone).astype(np.uint8)
        else:
            val = np.full(xs.shape, flat, np.uint8)
            I = np.full(xs.shape, np.nan)
        sub = self.idx[sy, sx]
        subz = self.z[sy, sx]
        subI = self.I[sy, sx]
        sub[inside] = val[inside]
        subz[inside] = z
        subI[inside] = I[inside]

    def blade(self, a, b, w, z, edge_n, lite=STEEL_L, mid=STEEL_M, taper=0.75):
        """Two-tone blade along segment a->b (edge side light)."""
        g = self._grid(min(a[0], b[0]) - w, min(a[1], b[1]) - w,
                       max(a[0], b[0]) + w, max(a[1], b[1]) + w)
        if g is None:
            return
        sy, sx, xs, ys = g
        dx, dy = b[0] - a[0], b[1] - a[1]
        l2 = dx * dx + dy * dy
        t = np.clip(((xs - a[0]) * dx + (ys - a[1]) * dy) / l2, 0, 1)
        cx, cy = a[0] + t * dx, a[1] + t * dy
        ox, oy = xs - cx, ys - cy
        d = np.sqrt(ox * ox + oy * oy)
        ww = w * np.where(t > 0.85, 1 - (t - 0.85) / 0.15 * taper, 1.0)
        inside = d <= ww
        side = ox * edge_n[0] + oy * edge_n[1]
        val = np.where(side >= -0.15, lite, mid).astype(np.uint8)
        sub = self.idx[sy, sx]
        subz = self.z[sy, sx]
        sub[inside] = val[inside]
        subz[inside] = z
        self.I[sy, sx][inside] = np.nan

    def finish(self):
        """Ink outlines: inner lines where a nearer part meets a farther one,
        then a 1px outline around the whole silhouette."""
        idx, z = self.idx, self.z
        filled = idx != 0
        inner = np.zeros_like(filled)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nz = np.roll(np.roll(z, dy, 0), dx, 1)
            nf = np.roll(np.roll(filled, dy, 0), dx, 1)
            inner |= filled & nf & (nz < z - 0.5)
        # v5.4 painted shading: the three tones blend through a checker where they meet; a part
        # lying under a nearer one darkens near its edge (contact shadow); a part's edge over a farther
        # one is drawn in its own darkest tone rather than black ink
        occl = np.zeros_like(filled)
        for dy, dx in OCCL_OFFS:
            nz = np.roll(np.roll(z, dy, 0), dx, 1)
            nf = np.roll(np.roll(filled, dy, 0), dx, 1)
            occl |= filled & nf & (nz > z + 0.5)
        shaded = filled & ~np.isnan(self.I) & (idx >= 2) & (idx <= 13)
        base = idx - (idx - 2) % 3
        Iv = np.where(shaded, self.I, 0.5) - 0.25 * occl
        p = np.where(Iv <= 0.05, 0.0, np.where(Iv <= 0.5, (Iv - 0.05) / 0.45, np.where(Iv <= 0.95, 1.0 + (Iv - 0.5) / 0.45, 2.0)))
        fl = np.floor(p)
        fr = p - fl
        yy, xx = np.mgrid[0:CH, 0:CW]
        chk = ((yy + xx) & 1) == 0
        k = np.where((fr > 0.3) & (fr < 0.7), fl + chk, np.floor(p + 0.5))
        k = np.clip(k, 0, 2).astype(np.uint8)
        idx[shaded] = (base + k)[shaded]
        ramp = (idx >= 2) & (idx <= 13)
        soft = inner & ramp
        idx[soft] = (idx - (idx - 2) % 3)[soft]
        idx[inner & ~ramp] = OUT
        grow = np.zeros_like(filled)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            grow |= np.roll(np.roll(filled, dy, 0), dx, 1)
        grow &= ~filled
        grow[0, :] = grow[-1, :] = False
        grow[:, 0] = grow[:, -1] = False
        idx[grow] = OUT


# ---------------------------------------------------------------- hair styles
# anchor (fraction of head radius: forward, up), radius root->tip, spring rest angle, segments
CHAIN_STYLES = {
    "ponytail": ((-0.85, 0.35), (3.1, 1.3), 238.0, 5),
    "long": ((-0.55, 0.2), (5.0, 2.8), 258.0, 5),
    "flowing": ((-0.7, 0.2), (5.8, 2.2), 214.0, 6),
    "highpony": ((-0.55, 0.85), (3.4, 1.2), 208.0, 6),
    "braid": ((-0.8, 0.0), (2.5, 1.6), 218.0, 7),
    "halfup": ((-0.7, 0.2), (4.6, 2.0), 216.0, 6),
    "lowpony": ((-0.85, -0.3), (3.8, 1.6), 228.0, 6),
}
BIG_HAIR = tuple(CHAIN_STYLES)
CLAMPED = ("flowing", "highpony", "braid", "halfup", "lowpony")   # long hair stays behind her


def clamp_tails(hair, tails):
    if hair not in CLAMPED:
        return tails
    return [min(292.0, max(150.0, a)) for a in tails]


# ---------------------------------------------------------------- characters

LIEVE = dict(name="lieve", scale=1.16, torso=30, neck=3.0, head_r=6.6,
               uarm=16, farm=15, thigh=24, shin=24, blade=42,
               hair="ponytail", build=1.0)
KENJI = dict(name="kenji", scale=1.22, torso=31, neck=3.2, head_r=6.6,
             uarm=16.5, farm=15.5, thigh=24.5, shin=24.5, blade=44,
             hair="topknot", build=1.12)

BASE = dict(crouch=45, hipx=0, lean=5, ff=13, bf=-15, ffl=0, bfl=0,
            hx=15, hy=-13, ba=14, head=0, behind=0, trail=0, aim=0)


def P(base=None, **kw):
    d = dict(BASE if base is None else base)
    d.update(kw)
    return d


def lerp_pose(a, b, t):
    out = {}
    for k in a:
        if k in ("behind", "trail"):
            out[k] = a[k] if t < 0.5 else b[k]
        else:
            out[k] = a[k] + (b[k] - a[k]) * t
    return out


def skeleton(p, S):
    """Return all joint positions in unscaled, y-up coordinates."""
    k = {}
    lean = rad(p["lean"])
    up = v(math.sin(lean), math.cos(lean))
    hip = v(p["hipx"], p["crouch"])
    k["hip"] = hip
    k["waist"] = hip + up * 11
    k["sh"] = hip + up * S["torso"]
    upr = S.get("upright", 0.0)
    hl = rad(p["lean"] * (0.6 - 0.45 * upr) + p["head"])
    hup = v(math.sin(hl), math.cos(hl))
    k["neck"] = k["sh"] + hup * S["neck"]
    k["head"] = k["neck"] + hup * (S["head_r"] * 0.95) + v(0.8 - 0.2 * upr, 0)
    k["hang"] = hl
    # legs (front leg = far side)
    for side, fx, fl in (("f", p["ff"], p["ffl"]), ("b", p["bf"], p["bfl"])):
        hp = hip + v(1.5 if side == "f" else -1.5, 0)
        ank = v(fx, 2.6 + fl)
        knee, ank2 = ik(hp, ank, S["thigh"], S["shin"], +1)
        k[side + "hip"], k[side + "knee"], k[side + "ank"] = hp, knee, ank2
    # sword and hands
    ba = rad(p["ba"])
    dirv = v(math.cos(ba), math.sin(ba))
    hand = k["sh"] + v(p["hx"], p["hy"])
    k["hand_f"] = hand
    k["tsuba"] = hand + dirv * 2.6
    k["hand_b"] = hand - dirv * 6.0
    k["pommel"] = hand - dirv * 9.5
    k["tip"] = k["tsuba"] + dirv * S["blade"]
    wpn = S.get("weapon", "katana")
    if wpn in ("dual", "knives"):
        # two weapons: the near hand lets go of the hilt and holds its own blade
        k["pommel"] = hand - dirv * 3.4
        lean_ = rad(p["lean"])
        k["hand_b"] = hand + (v(-7.0 - 2.0 * math.sin(lean_), -5.5) if wpn == "dual" else v(-3.0, -8.5))
        a1 = math.atan2(dirv[1], dirv[0])
        a2 = a1 + rad(-38.0) if wpn == "dual" else a1 + rad(-80.0)    # knives: the second held low
        d2 = v(math.cos(a2), math.sin(a2))
        k["bdir2"] = d2
        k["tsuba2"] = k["hand_b"] + d2 * 2.0
        k["pommel2"] = k["hand_b"] - d2 * 3.2
        k["tip2"] = k["tsuba2"] + d2 * (S["blade"] * (0.72 if wpn == "dual" else 0.9))
    k["bdir"] = dirv
    shf = k["sh"] + v(2.2, 0.4 - 2.0 * upr)
    shb = k["sh"] + v(-1.8, -0.6 - 2.0 * upr)
    if wpn == "rapier":
        # one-handed: the near hand rests by the hip holding a flintlock, or levels it (pose "aim")
        k["pommel"] = hand - dirv * 3.0
        rest = shb + v(4.0, -(S["uarm"] + S["farm"]) * 0.78)
        aimp = shb + v((S["uarm"] + S["farm"]) * 0.93, 1.5)
        t_ = p.get("aim", 0)
        k["hand_b"] = rest + (aimp - rest) * t_
        pa = rad(-62.0 + 62.0 * t_)
        k["pdir"] = v(math.cos(pa), math.sin(pa))
    k["shf"], k["shb"] = shf, shb
    k["elf"], k["hf"] = ik(shf, k["hand_f"], S["uarm"], S["farm"], -1)
    k["elb"], k["hb"] = ik(shb, k["hand_b"], S["uarm"], S["farm"], -1)
    return k


HANG = 262.0          # long hair at rest hangs almost straight down her back
MOTION = {"walk_low": 2.6, "walk_mid": 2.6, "walk_high": 2.6, "backstep": -3.0, "sidestep": 1.2}   # virtual forward speed per animation


class Springs:
    """Simple per-sequence spring for the ponytail / headband tails.
    hang=True (long hair): it lies against her back when she is still and
    streams out behind her, bouncing, when she moves."""

    def __init__(self, n, rest, hang=False):
        self.n = n
        self.rest = HANG if hang else rest
        self.hang = hang
        self.ang = [self.rest] * n
        self.vel = [0.0] * n
        self.prev = None

    def step(self, head, phase, motion=0.0):
        vx = 0.0 if self.prev is None else head[0] - self.prev[0]
        vy = 0.0 if self.prev is None else head[1] - self.prev[1]
        self.prev = head
        if self.hang:
            sp = vx + motion
            trail = max(-8.0, min(70.0, sp * 12.0))
            lift = max(-20.0, min(20.0, vy * 4.0))
            amp = 0.5 + 1.8 * min(1.0, abs(sp) / 2.0 + abs(vy) / 3.0)
            target = HANG - trail + lift + amp * math.sin(phase)
            prev_a = target
            for i in range(self.n):
                tgt = prev_a + (1.2 if i else 0.0) + amp * 0.6 * math.sin(phase + i * 0.9)
                self.vel[i] = self.vel[i] * 0.55 + (tgt - self.ang[i]) * 0.45
                self.ang[i] += self.vel[i]
                prev_a = self.ang[i]
            return list(self.ang)
        target = self.rest + vx * 11.0 - vy * 5.0 + 4.0 * math.sin(phase)
        prev_a = target
        for i in range(self.n):
            tgt = prev_a + (6.0 if i else 0.0) + 2.5 * math.sin(phase + i * 0.9)
            self.vel[i] = self.vel[i] * 0.55 + (tgt - self.ang[i]) * 0.45
            self.ang[i] += self.vel[i]
            prev_a = self.ang[i]
        return list(self.ang)


BLOOD = STEEL_M      # in gore frames (and on a bloodied blade) palette index 15 is shown as blood


def render(p, S, tails, prev_p=None, omit=None, only=None, gore=False, motion=0.0):
    """Render one pose. tails = list of segment angles (deg, y-up).
    omit/only: 'head', 'arm' or 'leg' leave that part off the body, or draw just that part
    (dismemberment). gore: blades are drawn one-tone so index 15 can be blood."""
    def part(g):
        return (only == g) if only is not None else (g != omit)
    body = only is None
    c = Canvas()
    sc = S["scale"]
    bld = S["build"]
    K = skeleton(p, S)

    def W(q):
        return (RX + q[0] * sc, GY - q[1] * sc)

    def R(r):
        return r * sc

    hr = S["head_r"]
    hc = K["head"]
    hang = K["hang"]
    fwd = v(math.cos(-hang), math.sin(-hang))        # head-forward (y-up)
    hupv = v(math.sin(hang), math.cos(hang))

    def head_local(xs, ys):
        # canvas -> head-local (u forward, w up) in unscaled units
        X = (xs - RX) / sc - hc[0]
        Y = (GY - ys) / sc - hc[1]
        u = X * fwd[0] + Y * fwd[1]
        w = X * hupv[0] + Y * hupv[1]
        return u, w

    z = 0

    # the body the long hair rests on (torso and hakama capsules, unscaled y-up)
    _upr = S.get("upright", 0.0)
    _cd = S.get("chest_depth", 1.0)
    _fg = S.get("figure", 0.0)
    _rw = 9.2 * bld * (1 - 0.22 * _fg) * _cd
    _rc = 10.4 * bld * (1 - 0.12 * _fg) * _cd
    _ln = rad(p["lean"])
    _up = v(math.sin(_ln), math.cos(_ln))
    _top_old = K["sh"] + (K["hip"] - K["sh"]) * 0.08
    _top = _top_old + ((K["sh"] - _up * (_rc - 1.2)) - _top_old) * _upr
    body_caps = [(K["waist"], _top, _rw, _rc), (K["hip"], K["waist"], 10.0 * bld * (1 + 0.06 * _fg), 9.4 * bld * (1 - 0.18 * _fg))]

    def lie_on_back(q, margin):
        for a, b, ra, rb in body_caps:
            d = b - a
            l2 = float(d[0] * d[0] + d[1] * d[1])
            t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, float((q - a) @ d) / l2))
            cpt = a + d * t
            r = ra + (rb - ra) * t + margin
            off = q - cpt
            dist = math.hypot(off[0], off[1])
            if dist < r and dist > 1e-6:
                q = cpt + off * (r / dist)
        return q

    lie = S["hair"] in CLAMPED

    def tail_chain(anchor, seglen, r0, r1, mat, zz, flat=None, push=False):
        pt = anchor
        n = len(tails)
        for i, a in enumerate(tails):
            nxt = pt + v(math.cos(rad(a)), math.sin(rad(a))) * seglen
            if push and i > 0:
                nxt = lie_on_back(nxt, r0 * 0.5)
            rr0 = r0 + (r1 - r0) * i / n
            rr1 = r0 + (r1 - r0) * (i + 1) / n
            c.cap(W(pt), W(nxt), R(rr0), R(rr1), mat, zz if not (push and i > 0) else 3.36, flat=flat)
            pt = nxt

    hair = S["hair"]
    band = S.get("headband", False)
    fg = S.get("figure", 0.0)
    tails = clamp_tails(hair, tails)
    # hair chains / headband tails at the very back
    anchor = hc + fwd * (-hr * 0.85) + hupv * (hr * 0.35)
    tip = None
    anchor_c = anchor
    if hair in CHAIN_STYLES and part("head"):
        (au, aw), (r0, r1), _, _ = CHAIN_STYLES[hair]
        ca = hc + fwd * (hr * au) + hupv * (hr * aw)
        if hair == "ponytail":
            ca = anchor
        if hair == "braid":
            pt = ca
            n = len(tails)
            for i, a in enumerate(tails):
                dv = v(math.cos(rad(a)), math.sin(rad(a)))
                nxt = pt + dv * S["tail_len"]
                rr = r0 + (r1 - r0) * i / n
                if i > 0:                    # the braid lies along her back instead of through it
                    nxt = lie_on_back(nxt, rr * 0.6)
                    dd = nxt - pt
                    ln_ = math.hypot(dd[0], dd[1]) or 1.0
                    dv = dd / ln_
                pv = v(-dv[1], dv[0])
                for j, off in ((0, 0.3), (1, 0.75)):
                    sgn = 1 if (i * 2 + j) % 2 == 0 else -1
                    q = pt + (nxt - pt) * off + pv * (0.8 * sgn)
                    c.cap(W(q), W(q), R(rr), R(rr), HAIR, 0 if i == 0 else 3.36)
                pt = nxt
            tip = pt
            c.cap(W(pt), W(pt + dv * 2.6), R(1.5), R(0.8), HAIR, 3.36)
        else:
            tail_chain(ca, S["tail_len"], r0, r1, HAIR, 0, push=lie)
        anchor_c = ca
    if band and part("head"):
        ba_ = hc + fwd * (-hr * 0.9) + hupv * (hr * 0.45)
        tail_chain(ba_, 4.2, 1.3, 0.9, STEEL_L, 0, flat=STEEL_L)
    if S.get("helm") and part("head"):      # shikoro: lacquered neck guard flaring behind the helmet
        s0 = hc + fwd * (-hr * 0.5) + hupv * (hr * 0.2)
        s1 = hc + fwd * (-hr * 1.25) + hupv * (-hr * 0.9)
        c.cap(W(s0), W(s1), R(hr * 0.95), R(hr * 0.75), TOP, 0.2)
        c.cap(W(s1 + fwd * 0.5), W(s1 + fwd * 3.5 + hupv * 1.2), R(1.0), R(1.0), HAIR, 0.25, flat=HAIR + 1)

    wpn = S.get("weapon", "katana")
    lite_m = STEEL_L if gore else STEEL_M   # gore frames: one-tone steel, index 15 is blood
    if only == "weapon":
        body = False

    def draw_sword(zz):
        en = K["bdir"]
        edge = v(en[1], -en[0])             # y-up edge normal (rotate -90)
        edge_c = (edge[0], -edge[1])        # canvas coords
        tn = v(-en[1], en[0])
        if wpn == "none":
            return
        if wpn == "bo":
            # long oak staff held near the middle, iron ferrules at both ends
            back = K["pommel"] - en * S.get("staff_back", 22)
            c.cap(W(back), W(K["tip"]), R(1.55), R(1.55), SKIN, zz, bias=0.1)
            for e0, e1 in ((back, back + en * 3.0), (K["tip"] - en * 3.0, K["tip"])):
                c.cap(W(e0), W(e1), R(1.7), R(1.7), OUT, zz + 0.05, flat=OUT)
            return
        if wpn == "kusarigama":
            # sickle: short wooden haft, blade at right angles at the top; weighted chain from the butt
            top = K["tsuba"] + en * 4.0
            c.cap(W(K["pommel"]), W(top), R(1.3), R(1.3), SKIN, zz, bias=0.1)
            bt = top + edge * S["blade"] * 0.85 + en * 3.0
            ec = (en[0], -en[1])
            c.blade(W(top), W(bt), R(1.5), zz + 0.02, ec, STEEL_L, lite_m, 0.95)
            c.cap(W(top), W(top), R(1.4), R(1.4), OUT, zz + 0.03, flat=OUT)
            q = K["pommel"]
            sw = math.sin(rad(p["ba"])) * 4.0
            for i in range(1, 9):
                q2 = K["pommel"] + v(-1.2 * i + sw * (i / 8.0) ** 2, -2.2 * i + 0.09 * i * i)
                c.cap(W(q2), W(q2), R(0.75), R(0.75), STEEL_M if not gore else OUT, zz - 0.01, flat=OUT if (i & 1) else (STEEL_L if gore else STEEL_M))
                q = q2
            c.cap(W(q), W(q), R(1.9), R(1.9), OUT, zz - 0.02, flat=OUT)
            return
        if wpn == "rapier":
            # long thin blade, a cup hilt and swept quillons
            c.blade(W(K["tsuba"]), W(K["tip"]), R(0.85), zz, edge_c, STEEL_L, lite_m, 0.6)
            c.cap(W(K["pommel"]), W(K["tsuba"]), R(1.1), R(1.1), OUT, zz, flat=OUT)
            c.cap(W(K["pommel"] - en * 0.8), W(K["pommel"] - en * 0.8), R(1.3), R(1.3), STEEL_L, zz + 0.02, flat=STEEL_L)
            c.cap(W(K["tsuba"]), W(K["tsuba"]), R(2.3), R(2.3), STEEL_M, zz + 0.05, flat=STEEL_M if not gore else STEEL_L)
            c.cap(W(K["tsuba"] + tn * 0.8 - en * 0.4), W(K["tsuba"] + tn * 0.8 - en * 0.4), R(0.8), R(0.8), STEEL_L, zz + 0.055, flat=STEEL_L)
            c.cap(W(K["tsuba"] - tn * 3.6 - en * 0.6), W(K["tsuba"] + tn * 3.6 + en * 0.6), R(0.55), R(0.55), OUT, zz + 0.06, flat=OUT)
            return
        if wpn == "cutlass":
            # short broad blade with a knuckle bow
            c.blade(W(K["tsuba"]), W(K["tip"]), R(1.8), zz, edge_c, STEEL_L, lite_m, 0.9)
            c.cap(W(K["pommel"]), W(K["tsuba"]), R(1.6), R(1.6), OUT, zz, flat=OUT)
            c.cap(W(K["tsuba"] + tn * 2.4), W(K["pommel"] + tn * 2.2), R(0.6), R(0.6), STEEL_M if not gore else STEEL_L, zz + 0.05, flat=STEEL_M if not gore else STEEL_L)
            c.cap(W(K["tsuba"] - tn * 2.6), W(K["tsuba"] + tn * 2.6), R(1.0), R(1.0), OUT, zz + 0.06, flat=OUT)
            return
        if wpn in ("dual", "knives"):
            small = wpn == "knives"
            for tsu, tip_, pom, en_ in ((K["tsuba"], K["tip"], K["pommel"], K["bdir"]), (K["tsuba2"], K["tip2"], K["pommel2"], K["bdir2"])):
                ed = v(en_[1], -en_[0])
                c.blade(W(tsu), W(tip_), R(1.15 if small else 1.3), zz, (ed[0], -ed[1]), STEEL_L, lite_m, 0.9 if small else 0.75)
                c.cap(W(pom), W(tsu), R(1.5), R(1.5), OUT, zz, flat=OUT)
                tn_ = v(-en_[1], en_[0])
                g_ = 1.4 if small else 2.3
                c.cap(W(tsu - tn_ * g_), W(tsu + tn_ * g_), R(0.9), R(0.9), OUT, zz + 0.1, flat=OUT)
                zz = 6.6                       # the near hand's blade sits in front of the body
            return
        if wpn == "bokken":
            # wooden practice sword: oak tones from the skin ramp, rounded tip, no metal guard
            c.blade(W(K["tsuba"]), W(K["tip"]), R(1.6), zz, edge_c, SKIN + 2, SKIN + 1, 0.35)
            c.cap(W(K["pommel"]), W(K["tsuba"]), R(1.6), R(1.6), SKIN, zz, flat=SKIN)
            c.cap(W(K["tsuba"] - tn * 1.8), W(K["tsuba"] + tn * 1.8), R(0.8), R(0.8), OUT, zz + 0.1, flat=OUT)
            return
        big = wpn == "nodachi"
        c.blade(W(K["tsuba"]), W(K["tip"]), R(1.6 if big else 1.35), zz, edge_c, STEEL_L, lite_m)
        c.cap(W(K["pommel"]), W(K["tsuba"]), R(1.7), R(1.7), OUT, zz, flat=OUT)
        # handle wrap diamonds
        for t in (0.25, 0.55, 0.8):
            q = K["pommel"] + (K["tsuba"] - K["pommel"]) * t
            c.cap(W(q), W(q), R(0.7), R(0.7), BOT, zz, flat=BOT + 2)
        g_ = 3.4 if big else 2.6
        c.cap(W(K["tsuba"] - tn * g_), W(K["tsuba"] + tn * g_), R(1.1), R(1.1),
              OUT, zz + 0.1, flat=OUT)

    if only == "weapon":
        draw_sword(6)
        c.finish()
        return c
    if p["behind"] and body:
        draw_sword(0.5)
    # cloth: how fast each limb is moving since the previous frame
    if prev_p is not None:
        KP = skeleton(prev_p, S)
        def vel(key):
            return K[key] - KP[key]
    else:
        def vel(key):
            return v(0.0, 0.0)
    def spd(q):
        return math.hypot(q[0], q[1])
    vfa, vba, vfe, vbe, vhp = vel("fank"), vel("bank"), vel("elf"), vel("elb"), vel("hip")
    # v5.5 the hakama hem trails the way the body travels and swings after each leg
    mv = v(motion, 0.0)
    def lag(va):
        q = va * 0.5 + (vhp + mv) * 0.4
        n_ = spd(q)
        return q * (4.5 / n_) if n_ > 4.5 else q
    lag_f, lag_b = lag(vfa), lag(vba)
    hem_f = 1.0 + 0.3 * min(1.0, spd(vfa + mv) / 4.0)
    hem_b = 1.0 + 0.3 * min(1.0, spd(vba + mv) / 4.0)
    slv_f = 1.0 + 0.15 * min(1.0, spd(vfe) / 5.0)
    slv_b = 1.0 + 0.15 * min(1.0, spd(vbe) / 5.0)
    arm_mat = SKIN if S.get("bare_arms") else TOP
    arm_b = -0.2 if S.get("bare_arms") else -0.25
    fore_mat = TOP if S.get("gauntlets") else SKIN
    sf = 1 - 0.15 * fg                     # smaller hands and feet with a feminine figure

    def draw_head():
        # head
        c.cap(W(hc), W(hc), R(hr), R(hr), SKIN, 4)
        big = hair in BIG_HAIR
        hcap = hc + fwd * (-1.2 if big else -0.8) + hupv * (1.0 if big else 0.9)

        def hair_mask(xs, ys):
            u, w = head_local(xs, ys)
            return ~((u > -1.6) & (w < 2.6))
        if hair != "bald":
            c.cap(W(hcap), W(hcap), R(hr + (0.9 if big else 0.5)), R(hr + (0.9 if big else 0.5)), HAIR, 4.1, mask=hair_mask)
        if band:
            def band_mask(xs, ys):
                u, w = head_local(xs, ys)
                return (w > 2.9) & (w < 4.6)
            c.cap(W(hc), W(hc), R(hr + 0.6), R(hr + 0.6), STEEL_L, 4.2, flat=STEEL_L, mask=band_mask)
        if hair == "topknot":
            tk0 = hc + hupv * (hr + 0.2) + fwd * (-1.5)
            tk1 = tk0 + fwd * 4.0 + hupv * 1.2
            c.cap(W(tk0), W(tk1), R(2.0), R(1.6), HAIR, 4.3)
        elif hair == "bun":
            bc = hc + fwd * (-hr * 0.85) + hupv * (hr * 0.65)
            c.cap(W(bc), W(bc), R(3.0), R(3.0), HAIR, 4.3)
        elif hair == "ponytail":
            c.cap(W(anchor), W(anchor), R(1.6), R(1.6), TOP, 4.2, flat=TOP)
        elif hair == "halfup":
            bc = hc + fwd * (-hr * 0.7) + hupv * (hr * 0.75)
            c.cap(W(bc), W(bc), R(2.6), R(2.6), HAIR, 4.3)
        if hair in ("highpony", "lowpony"):     # ribbon bow at the tie
            for k in (-1, 1):
                q = anchor_c + fwd * (1.7 * k) + hupv * 0.9
                c.cap(W(anchor_c), W(q), R(1.0), R(1.3), TOP, 4.25, flat=TOP + 2)
            c.cap(W(anchor_c), W(anchor_c), R(1.1), R(1.1), TOP, 4.26, flat=TOP)
        if hair == "braid" and tip is not None:
            c.cap(W(tip), W(tip), R(1.4), R(1.4), TOP, 4.25, flat=TOP + 2)
        if S.get("bangs", False) and hair != "bald":
            def bang_mask(xs, ys):
                u, w = head_local(xs, ys)
                zig = (np.floor(u * 1.2).astype(int) & 1) * 0.9
                return (u > -1.6) & (w >= 1.3 + zig)
            rb = R(hr + (0.9 if big else 0.5))
            c.cap(W(hcap), W(hcap), rb, rb, HAIR, 4.15, mask=bang_mask)
        if S.get("sidelocks", False) and hair != "bald":
            s0 = hc + fwd * 0.6 + hupv * 1.8
            s1 = hc + fwd * 1.6 + hupv * (-7.5)
            c.cap(W(s0), W(s1), R(1.7), R(0.9), HAIR, 4.22)
        if S.get("ears") and not S.get("helm") and not S.get("hood"):   # the ear, just behind the middle of the head
            ea = hc + fwd * (-hr * 0.12) + hupv * (-0.4)
            c.cap(W(ea + hupv * 1.1), W(ea - hupv * 1.0), R(1.7), R(1.4), SKIN, 4.08, flat=SKIN)          # rim
            c.cap(W(ea + hupv * 0.6), W(ea - hupv * 0.5), R(0.95), R(0.8), SKIN, 4.085, flat=SKIN + 1)    # bowl
            ix, iy = W(ea + fwd * 0.35)
            ix, iy = int(ix), int(iy)
            if 0 <= ix < CW and 0 <= iy < CH:
                c.idx[iy, ix] = OUT
                c.z[iy, ix] = 4.09
        # nose and eye
        nose = hc + fwd * (hr + 0.4) + hupv * 0.2
        c.cap(W(nose), W(nose), R(1.1), R(1.1), SKIN, 4.05, flat=SKIN + 1)
        eye = hc + fwd * (hr * 0.62) + hupv * 1.3
        ex, ey = W(eye)
        ex, ey = int(ex), int(ey)
        if 0 <= ex < CW and 0 <= ey < CH:
            c.idx[ey, ex] = OUT
            c.z[ey, ex] = 4.06
            if S.get("_portrait"):               # close-up: a proper eye, brow and mouth
                ex_ = S.get("_expr", 0)          # v5.5: 0 calm, 1 angry, 2 hurt, 3 respectful
                sk = SKIN + 1
                if ex_ == 0:
                    pts = ((ex, ey, OUT), (ex - 1, ey, OUT), (ex, ey + 1, OUT), (ex - 1, ey + 1, STEEL_L),
                           (ex - 2, ey - 2, OUT), (ex - 1, ey - 3, OUT), (ex, ey - 3, OUT), (ex + 1, ey - 3, OUT))
                elif ex_ == 1:                   # the brow drawn down hard toward the nose, a glare
                    pts = ((ex, ey, OUT), (ex - 1, ey, OUT), (ex, ey + 1, OUT), (ex - 1, ey + 1, STEEL_L), (ex, ey - 1, sk),
                           (ex - 2, ey - 3, OUT), (ex - 1, ey - 3, OUT), (ex, ey - 2, OUT), (ex + 1, ey - 1, OUT), (ex + 2, ey - 1, OUT))
                elif ex_ == 2:                   # eye screwed shut, brow knotted, blood from the brow
                    pts = ((ex - 1, ey + 1, OUT), (ex, ey + 1, OUT), (ex + 1, ey, OUT), (ex, ey, sk), (ex - 1, ey, sk),
                           (ex - 2, ey - 2, OUT), (ex - 1, ey - 2, OUT), (ex, ey - 3, OUT), (ex + 1, ey - 3, OUT),
                           (ex + 2, ey - 2, BLOOD), (ex + 2, ey - 1, BLOOD), (ex + 2, ey, BLOOD), (ex + 1, ey + 2, BLOOD),
                           (ex + 1, ey + 3, BLOOD), (ex + 1, ey + 4, BLOOD), (ex + 2, ey + 6, BLOOD))
                else:                            # eyes lowered and closed, the brow at rest
                    pts = ((ex - 1, ey + 1, OUT), (ex, ey + 1, OUT), (ex, ey, sk), (ex - 1, ey, sk),
                           (ex - 2, ey - 2, OUT), (ex - 1, ey - 3, OUT), (ex, ey - 3, OUT), (ex + 1, ey - 3, OUT))
                for qx, qy, col in pts:
                    if 0 <= qx < CW and 0 <= qy < CH:
                        c.idx[qy, qx] = col
                mth = hc + fwd * (hr * 0.82) + hupv * (-2.6)
                mx_, my_ = W(mth)
                mx_, my_ = int(mx_), int(my_)
                mcol = OUT if not S.get("lips") else HAIR + 1
                if ex_ == 1:                     # set jaw, the mouth turned down
                    mpts = ((mx_ - 2, my_), (mx_ - 1, my_), (mx_, my_), (mx_ + 1, my_ + 1))
                elif ex_ == 2:                   # the mouth open in pain, blood at the lip
                    mpts = ((mx_ - 1, my_), (mx_, my_), (mx_ - 1, my_ + 1), (mx_, my_ + 1))
                else:
                    mpts = ((mx_ - 1, my_), (mx_, my_))
                for qx, qy in mpts:
                    if 0 <= qx < CW and 0 <= qy < CH:
                        c.idx[qy, qx] = mcol
                if ex_ == 2 and 0 <= mx_ + 1 < CW and 0 <= my_ + 2 < CH:
                    c.idx[my_ + 2, mx_ + 1] = BLOOD
            if S.get("eyes", 0) == 1:            # taller eye with a lash
                for qx, qy in ((ex, ey - 1), (ex + 1, ey - 1)):
                    if 0 <= qx < CW and 0 <= qy < CH:
                        c.idx[qy, qx] = OUT
                        c.z[qy, qx] = 4.06
        if S.get("lips", False):
            mx, my = W(hc + fwd * (hr * 0.9) + hupv * (-2.4))
            mx, my = int(mx), int(my)
            if 0 <= mx < CW and 0 <= my < CH:
                c.idx[my, mx] = HAIR + 1
                c.z[my, mx] = 4.06
        if S.get("hood"):                    # cloth hood over head and face, open at the eyes
            def hood_mask(xs, ys):
                u, w = head_local(xs, ys)
                return ~((u > 1.2) & (w > 0.0) & (w < 2.8))
            c.cap(W(hc + hupv * 0.4), W(hc + hupv * 0.4), R(hr + 0.8), R(hr + 0.8), TOP, 4.4, mask=hood_mask)
            c.cap(W(hc - fwd * 2.0 - hupv * 3.5), W(hc - fwd * 5.5 - hupv * 7.0), R(2.6), R(1.6), TOP, 4.39)
        if S.get("menpo"):                   # red lacquer face mask over the lower face
            def menpo_mask(xs, ys):
                u, w = head_local(xs, ys)
                return (u > -0.5) & (w < 0.6)
            c.cap(W(hc + fwd * 0.6), W(hc + fwd * 0.6), R(hr + 0.4), R(hr + 0.4), BOT, 4.45, mask=menpo_mask, bias=0.15)
            mt = hc + fwd * (hr * 0.85) + hupv * (-2.4)
            c.cap(W(mt - fwd * 1.6), W(mt + fwd * 0.6), R(0.5), R(0.5), OUT, 4.46, flat=OUT)
            for k in (-1, 1):                # bristling moustache
                q = hc + fwd * (hr * 0.95) + hupv * (-1.0)
                c.cap(W(q), W(q + fwd * 1.8 + hupv * (0.8 * k - 0.6)), R(0.5), R(0.4), STEEL_L, 4.47, flat=STEEL_L)
        if S.get("beard"):                   # moustache and pointed beard
            for k in (-1, 1):
                q = hc + fwd * (hr * 0.92) + hupv * (-1.2)
                c.cap(W(q), W(q + fwd * 1.4 + hupv * (-0.6 + 0.9 * k) - fwd * (1.6 if k < 0 else 0)), R(0.6), R(0.45), HAIR, 4.46, flat=HAIR)
            c.cap(W(hc + fwd * (hr * 0.7) + hupv * (-hr * 0.7)), W(hc + fwd * (hr * 0.85) + hupv * (-hr * 1.15)), R(1.4), R(0.5), HAIR, 4.46)
        if S.get("knitcap"):                 # a sailor's wool cap
            def cap_mask(xs, ys):
                u, w = head_local(xs, ys)
                return w > 3.1
            c.cap(W(hc + hupv * 1.2 - fwd * 0.6), W(hc + hupv * 1.2 - fwd * 0.6), R(hr + 1.0), R(hr + 1.0), BOT, 4.5, mask=cap_mask, bias=0.1)
            c.cap(W(hc + hupv * 3.6 - fwd * (hr + 0.2)), W(hc + hupv * 3.6 + fwd * (hr - 0.6)), R(1.1), R(1.1), BOT, 4.51, flat=BOT + 2)
            q = hc + hupv * (hr + 1.6) - fwd * 1.5
            c.cap(W(q), W(q - fwd * 3.0 - hupv * 0.6), R(1.6), R(1.2), BOT, 4.52)
        if S.get("hat"):                     # a Dutch capotain: tall black crown, wide brim, gilt buckle
            br = hc + hupv * (hr * 0.78)
            c.cap(W(br - fwd * (hr * 1.75)), W(br + fwd * (hr * 1.8)), R(1.2), R(1.0), TOP, 4.6, bias=0.1)
            c.cap(W(br + hupv * (hr * 0.8) - fwd * 0.3), W(br + hupv * (hr * 1.5) - fwd * 0.9), R(hr * 0.8), R(hr * 0.7), TOP, 4.61, bias=0.15)
            c.cap(W(br + hupv * 1.6 - fwd * (hr * 0.85)), W(br + hupv * 1.6 + fwd * (hr * 0.8)), R(0.8), R(0.8), HAIR, 4.62, flat=HAIR + 2)
            c.cap(W(br + hupv * 1.6 + fwd * (hr * 0.5)), W(br + hupv * 1.6 + fwd * (hr * 0.5)), R(1.0), R(1.0), STEEL_L, 4.63, flat=STEEL_L)
        if S.get("helm"):                    # kabuto bowl with gold kuwagata horns
            def helm_mask(xs, ys):
                u, w = head_local(xs, ys)
                return w > 1.2
            hb_ = hc + hupv * 0.6 - fwd * 0.4
            c.cap(W(hb_), W(hb_), R(hr + 1.4), R(hr + 1.4), TOP, 4.5, mask=helm_mask, bias=0.1)
            c.cap(W(hc + hupv * 1.6 - fwd * (hr + 1.6)), W(hc + hupv * 1.6 + fwd * (hr + 1.2)), R(1.0), R(1.0), OUT, 4.52, flat=OUT)
            base = hc + fwd * (hr * 0.75) + hupv * (hr * 0.55)
            for k, lean_h in ((1, 0.55), (-1, -0.2)):
                h1 = base + hupv * 5.0 + fwd * (2.5 * lean_h)
                h2 = h1 + hupv * 6.5 + fwd * (4.0 * lean_h - 1.5)
                c.cap(W(base), W(h1), R(1.1), R(0.9), HAIR, 4.55 + 0.01 * k)
                c.cap(W(h1), W(h2), R(0.9), R(0.5), HAIR, 4.55 + 0.01 * k)
            c.cap(W(base), W(base), R(1.6), R(1.6), HAIR, 4.57, flat=HAIR + 2)

    if body:
        # far (front) leg
        c.cap(W(K["fhip"]), W(K["fknee"]), R(7.2 * bld), R(7.6 * bld), BOT, 1, bias=-0.22)
        c.cap(W(K["fknee"]), W(K["fank"] - lag_f), R(7.6 * bld), R(9.2 * bld * S.get("flare", 1.0) * hem_f), BOT, 1, bias=-0.22)
        c.cap(W(K["fank"] + v(0, -0.6)), W(K["fank"] + v(7.5, -1.6)), R(2.3 * sf), R(1.9 * sf), SKIN, 1.05, bias=-0.2)
        # far arm
        if S.get("torn"):                  # the sleeve torn away above the elbow
            mid_f = K["shf"] + (K["elf"] - K["shf"]) * 0.5
            c.cap(W(mid_f), W(K["elf"]), R(3.4 * bld), R(3.2 * bld), SKIN, 2, bias=-0.2)
            c.cap(W(K["shf"]), W(mid_f), R(4.6 * bld * (1 - 0.12 * fg)), R(4.4 * bld * (1 - 0.12 * fg)), arm_mat, 2.01, bias=arm_b)
        else:
            c.cap(W(K["shf"]), W(K["elf"]), R(4.6 * bld * (1 - 0.12 * fg)), R(4.1 * bld * (1 - 0.12 * fg) * slv_f), arm_mat, 2, bias=arm_b)
        c.cap(W(K["elf"]), W(K["hf"]), R(3.2 * bld), R(2.7 * bld), fore_mat, 2, bias=-0.2)
        c.cap(W(K["hf"]), W(K["hf"]), R(3.1 * sf), R(3.1 * sf), SKIN, 2, bias=-0.15)
    upr = S.get("upright", 0.0)
    cd = S.get("chest_depth", 1.0)
    rw = 9.2 * bld * (1 - 0.22 * fg) * cd
    rc = 10.4 * bld * (1 - 0.12 * fg) * cd
    lean_ = rad(p["lean"])
    up_ = v(math.sin(lean_), math.cos(lean_))
    lean = lean_
    up = up_
    side = v(up[1], -up[0])
    if body:
        if S.get("sode"):                    # far shoulder plate, behind the torso
            c.cap(W(K["shf"] + up * 1.0), W(K["shf"] - up * 7.5 + side * 1.0), R(4.6 * bld), R(5.2 * bld), TOP, 2.05, bias=0.05)
        # torso: hakama lower, top upper (narrower waist and shoulders with a feminine figure)
        c.cap(W(K["hip"]), W(K["waist"]), R(10.0 * bld * (1 + 0.06 * fg)), R(9.4 * bld * (1 - 0.18 * fg)), BOT, 3)
        top_old = K["sh"] + (K["hip"] - K["sh"]) * 0.08
        top = top_old + ((K["sh"] - up_ * (rc - 1.2)) - top_old) * upr
        c.cap(W(K["waist"]), W(top), R(rw), R(rc), TOP, 3)
        if S.get("stripes"):                 # a sailor's striped shirt
            for k in range(6):
                a0 = K["waist"] + (top - K["waist"]) * (0.08 + 0.17 * k)
                c.cap(W(a0 - side * rw * 0.9), W(a0 + side * rw * 0.9), R(0.6), R(0.6), TOP, 3.02, flat=TOP)
        if S.get("torn"):                    # cuts across the keikogi
            for a_, b_ in ((0.25, 0.55), (0.6, 0.8)):
                q0 = K["waist"] + (top - K["waist"]) * a_ + side * (rw * 0.1)
                q1 = K["waist"] + (top - K["waist"]) * b_ + side * (rw * 0.75)
                c.cap(W(q0), W(q1), R(0.45), R(0.45), OUT, 3.03, flat=OUT)
                c.cap(W(q0 + up * 0.7), W(q1 + up * 0.7), R(0.4), R(0.4), SKIN, 3.031, flat=SKIN + 1)
        if S.get("lames"):                   # lacquered armour plates: dark seams and gold lacing
            for k in range(4):
                a0 = K["waist"] + (top - K["waist"]) * (0.18 + 0.2 * k)
                c.cap(W(a0 - side * rw * 0.82), W(a0 + side * rw * 0.82), R(0.45), R(0.45), OUT, 3.02, flat=OUT)
                for j in (-0.45, 0.0, 0.45):
                    q = a0 + side * rw * j + up * 1.6
                    c.cap(W(q), W(q), R(0.55), R(0.55), HAIR, 3.03, flat=HAIR + 1)
        if S.get("kusazuri"):               # armour skirt hanging from the waist
            k0 = K["waist"] - up * 1.0
            k1 = K["hip"] - up * 8.0 - vhp * 0.5
            c.cap(W(k0), W(k1), R(10.6 * bld), R(12.0 * bld), TOP, 3.15)
            for k in range(1, 4):
                q = k0 + (k1 - k0) * (k / 3.6)
                c.cap(W(q - side * 11.0 * bld), W(q + side * 11.0 * bld), R(0.45), R(0.45), OUT, 3.16, flat=OUT)
        # obi
        ob = K["waist"]
        obi = S.get("obi", 0)
        ow = 8.5 * bld * (1 - 0.2 * fg)
        if obi == 0:
            c.cap(W(ob - side * ow), W(ob + side * ow), R(1.5), R(1.5), BOT, 3.2, flat=BOT)
        elif obi == 3:
            pass                                # no sash
        else:
            c.cap(W(ob - side * ow), W(ob + side * ow), R(1.9), R(1.9), BOT, 3.2, flat=HAIR + 1 if obi == 1 else TOP + 2)
        # chest (drawn as part of the top, so it shades with the garment)
        ch = S.get("chest", 0.0)
        if ch > 0.01:
            rb = 2.6 + 3.0 * ch
            d = 10.1 * bld * cd + 3.2 * ch - rb
            bc = K["sh"] - up * 7.2 + side * d
            c.cap(W(bc + up * 0.8), W(bc - up * 0.6), R(rb), R(rb), TOP, 3.05)
        if S.get("kesa"):                    # monk's stole over one shoulder, with prayer beads
            k0 = K["sh"] + side * 2.0 + up * 0.5
            k1 = K["waist"] + side * (rw * 0.9) + up * 1.0
            c.cap(W(k0), W(k1), R(2.6), R(2.6), HAIR, 3.25, bias=0.1)
            for t_ in np.linspace(0.08, 0.92, 7):
                q = K["sh"] + side * (1.5 + 5.5 * t_) - up * (2.0 + 11.0 * t_ * (1.15 - t_))
                c.cap(W(q), W(q), R(0.9), R(0.9), OUT, 3.42, flat=OUT)
    # neck + lapel
    nb = K["sh"] - up * (1.5 * upr) - side * (1.2 * upr)          # upright: neck rises from the back of the shoulders
    nr0 = 3.4 * (1 - 0.25 * fg) * (1 + 0.3 * upr)
    nr1 = 3.0 * (1 - 0.25 * fg) * (1 - 0.1 * upr)
    neck_top = K["neck"] + v(0, 0.5)
    if body:
        if part("head"):
            c.cap(W(nb), W(neck_top), R(nr0), R(nr1), SKIN, 3.3)
        else:                               # decapitated: a short neck ending in a bloody stump
            nm = nb + (neck_top - nb) * 0.45
            c.cap(W(nb), W(nm), R(nr0), R(nr0 * 0.92), SKIN, 3.3)
            c.cap(W(nm), W(nm), R(nr0 * 0.95), R(nr0 * 0.95), BLOOD, 3.31, flat=BLOOD)
        if S.get("ruff"):                       # a starched white ruff around the neck
            rq = nb + (neck_top - nb) * 0.25
            c.cap(W(rq - side * 1.2), W(rq + side * 3.2), R(3.9), R(3.6), STEEL_L, 3.36, flat=STEEL_L)
            c.cap(W(rq - side * 0.8 - up * 1.6), W(rq + side * 2.8 - up * 1.6), R(1.6), R(1.4), STEEL_L, 3.361, flat=STEEL_M)
            for k in range(-2, 3):
                q = rq + side * (1.0 * k + 1.0) - up * 2.6
                c.cap(W(q), W(q + up * 1.0), R(0.35), R(0.35), STEEL_L, 3.37, flat=TOP + 2)
        if S.get("vneck", False):               # V collar: skin at the throat, down along the lapel
            c.cap(W(K["sh"] + up * 1.0 + side * 1.6), W(K["sh"] - up * 4.0 + side * (rc * 0.68)), R(1.6), R(0.8), SKIN, 3.35)
        lap0 = K["sh"] + v(2.5, 1.0)
        lap1 = K["sh"] - up * 12 + side * 4
        c.cap(W(lap0), W(lap1), R(0.9), R(0.9), TOP, 3.4, flat=TOP)
    elif only == "head":                     # the severed head keeps the top of its neck
        nm = nb + (neck_top - nb) * 0.45
        c.cap(W(nm), W(neck_top), R(nr0 * 0.92), R(nr1), SKIN, 3.3)
        c.cap(W(nm), W(nm), R(nr0 * 0.9), R(nr0 * 0.9), BLOOD, 3.31, flat=BLOOD)

    if part("head"):
        draw_head()

    # near (back) leg
    if body:
        c.cap(W(K["bhip"]), W(K["bknee"]), R(7.2 * bld), R(7.6 * bld), BOT, 5)
        if not part("leg"):
            c.cap(W(K["bknee"]), W(K["bknee"]), R(7.4 * bld), R(7.4 * bld), BLOOD, 5.01, flat=BLOOD)
    if part("leg"):
        c.cap(W(K["bknee"]), W(K["bank"] - lag_b), R(7.6 * bld), R(9.2 * bld * S.get("flare", 1.0) * hem_b), BOT, 5)
        c.cap(W(K["bank"] + v(0, -0.6)), W(K["bank"] + v(7.5, -1.6)), R(2.3 * sf), R(1.9 * sf), SKIN, 5.05)
        if only == "leg":
            c.cap(W(K["bknee"]), W(K["bknee"]), R(7.0 * bld), R(7.0 * bld), BLOOD, 5.02, flat=BLOOD)

    if not p["behind"] and body:
        draw_sword(6)
    # near arm (back hand on the pommel end)
    if part("arm"):
        if S.get("torn"):
            mid_b = K["shb"] + (K["elb"] - K["shb"]) * 0.55
            c.cap(W(mid_b), W(K["elb"]), R(3.5 * bld), R(3.3 * bld), SKIN, 7)
            c.cap(W(K["shb"]), W(mid_b), R(4.8 * bld * (1 - 0.12 * fg)), R(4.6 * bld * (1 - 0.12 * fg)), arm_mat, 7.01)
            for j in (-1, 1):              # ragged hem
                q = mid_b + v(j * 2.2, -0.6 * j)
                c.cap(W(q), W(q + v(0.3, -1.4)), R(0.6), R(0.4), arm_mat, 7.02, flat=TOP)
        else:
            c.cap(W(K["shb"]), W(K["elb"]), R(4.8 * bld * (1 - 0.12 * fg)), R(4.3 * bld * (1 - 0.12 * fg) * slv_b), arm_mat, 7)
        c.cap(W(K["elb"]), W(K["hb"]), R(3.3 * bld), R(2.8 * bld), fore_mat, 7)
        c.cap(W(K["hb"]), W(K["hb"]), R(3.2 * sf), R(3.2 * sf), SKIN, 7.1)
        if only == "arm":
            c.cap(W(K["shb"]), W(K["shb"]), R(4.4 * bld), R(4.4 * bld), BLOOD, 7.2, flat=BLOOD)
    elif body:
        c.cap(W(K["shb"]), W(K["shb"]), R(4.6 * bld), R(4.6 * bld), BLOOD, 7.0, flat=BLOOD)
    if S.get("pistol") and part("arm") and body:
        pd_ = K["pdir"]
        pn = v(-pd_[1], pd_[0])
        hb_ = K["hb"]
        c.cap(W(hb_ - pd_ * 0.5), W(hb_ - pd_ * 2.6 - pn * 5.0), R(1.9), R(1.6), HAIR, 7.15)                    # the butt
        c.cap(W(hb_ - pd_ * 1.0), W(hb_ + pd_ * 12.5), R(1.25), R(1.05), OUT, 7.16, flat=OUT)                   # the barrel
        c.cap(W(hb_ + pd_ * 1.5 + pn * 0.6), W(hb_ + pd_ * 11.5 + pn * 0.6), R(0.45), R(0.4), STEEL_L, 7.17, flat=STEEL_M)
        c.cap(W(hb_ + pd_ * 12.0), W(hb_ + pd_ * 12.0), R(1.5), R(1.5), OUT, 7.165, flat=OUT)                   # the muzzle
        c.cap(W(hb_ + pn * 2.2 + pd_ * 0.8), W(hb_ + pn * 2.6 + pd_ * 2.8), R(0.8), R(0.6), STEEL_L, 7.18, flat=STEEL_L)   # the lock
    if S.get("sode") and (part("arm") if only else body):   # near shoulder plate over the arm
        c.cap(W(K["shb"] + up * 1.2), W(K["shb"] - up * 7.0 + side * (-1.0)), R(4.8 * bld), R(5.6 * bld), TOP, 7.3, bias=0.1)
        c.cap(W(K["shb"] - up * 3.0 - side * 4.8 * bld), W(K["shb"] - up * 3.0 + side * 4.8 * bld), R(0.45), R(0.45), OUT, 7.31, flat=OUT)

    c.finish()

    # sword trail (motion arc) drawn only into empty pixels
    if S.get("saya") and body:              # the scabbard at the hip, for the sheathing
        mouth = K["waist"] + v(3.5, 0.5)
        sd = v(math.cos(rad(200.0)), math.sin(rad(200.0)))
        sc_m = Canvas()
        sc_m.cap(W(mouth), W(mouth + sd * (S.get("saya_len", S["blade"]) + 1.0)), R(1.7), R(1.5), OUT, 0, flat=OUT)
        sc_m.cap(W(mouth + sd * 1.0), W(mouth + sd * (S.get("saya_len", S["blade"]) - 0.5)), R(0.9), R(0.8), OUT, 0, flat=BOT)
        put = (sc_m.idx != 0) & ((c.idx == 0) | (c.z < 1.0))
        c.idx[put] = sc_m.idx[put]
    if p["trail"] and prev_p is not None and S.get("weapon", "katana") != "none":
        draw_trail(c, prev_p, p, S, W)
    return c


def fill_poly(arr, pts, val):
    """Even-odd scanline fill at pixel centres (same as the designer page)."""
    y0 = min(p[1] for p in pts)
    y1 = max(p[1] for p in pts)
    for y in range(max(0, math.floor(y0)), min(CH - 1, math.ceil(y1)) + 1):
        yc = y + 0.5
        xs = []
        n = len(pts)
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            if (a[1] <= yc < b[1]) or (b[1] <= yc < a[1]):
                xs.append(a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            x0 = max(0, js_round(xs[i]))
            x1 = min(CW - 1, js_round(xs[i + 1]) - 1)
            if x1 >= x0:
                arr[y, x0:x1 + 1] = val


def js_round(x):
    return int(math.floor(x + 0.5))


def draw_trail(c, pa, pb, S, W):
    steps = 6
    blades = []
    for i in range(steps + 1):
        q = lerp_pose(pa, pb, i / steps)
        K = skeleton(q, S)
        blades.append((K["tsuba"] + (K["tip"] - K["tsuba"]) * 0.7, K["tip"]))
    arr = np.zeros((CH, CW), np.uint8)
    for i in range(steps):
        a0, a1 = blades[i]
        b0, b1 = blades[i + 1]
        fill_poly(arr, [W(a0), W(a1), W(b1), W(b0)], 1 + (i >= steps - 3))
    ys, xs = np.mgrid[0:CH, 0:CW]
    empty = c.idx == 0
    solid = (arr == 2)
    dith = (arr == 1) & (((xs + ys) & 1) == 0)
    sel = empty & (solid | dith)
    c.idx[sel] = SKIN + 2 if S.get("weapon", "katana") in ("bokken", "bo") else STEEL_L


# ---------------------------------------------------------------- poses

M = P()                                                    # chudan (mid)
H = P(crouch=46, lean=0, ff=12, bf=-15, hx=8, hy=6, ba=166, behind=1)   # jodan (high)
L = P(crouch=43, lean=8, ff=14, bf=-15, hx=14, hy=-22, ba=-22)          # gedan (low)
STANCE = [L, M, H]

GUARD = [P(L, hx=12, hy=-20, ba=-62),
         P(M, hx=12, hy=-12, ba=55),
         P(H, hx=9, hy=8, ba=8, behind=0)]

STRIKE = [
    # low: sweep at the legs
    [P(L, hx=2, hy=-20, ba=-160, lean=4, behind=1),
     P(L, hx=10, hy=-24, ba=-110, hipx=3, ff=17, crouch=40, lean=12, behind=1),
     P(L, hx=20, hy=-26, ba=-50, hipx=6, ff=21, crouch=38, lean=16, trail=1),
     P(L, hx=26, hy=-26, ba=-8, hipx=8, ff=23, crouch=37, lean=18, trail=1),
     P(L, hx=24, hy=-20, ba=12, hipx=8, ff=23, crouch=38, lean=16, trail=1),
     P(L, hx=21, hy=-19, ba=8, hipx=7, ff=22, crouch=39, lean=14)],
    # mid: diagonal cut to the body
    [P(M, hx=6, hy=4, ba=140, lean=-2, behind=1),
     P(M, hx=14, hy=2, ba=100, hipx=3, ff=17, behind=1),
     P(M, hx=24, hy=-6, ba=40, hipx=6, ff=21, lean=10, trail=1),
     P(M, hx=30, hy=-14, ba=-10, hipx=8, ff=23, lean=16, crouch=41, trail=1),
     P(M, hx=26, hy=-22, ba=-45, hipx=8, ff=23, lean=16, crouch=41, trail=1),
     P(M, hx=21, hy=-24, ba=-58, hipx=7, ff=22, lean=13, crouch=42)],
    # high: overhead cut to the head
    [P(H, hx=4, hy=9, ba=178, lean=-4, crouch=47),
     P(H, hx=12, hy=10, ba=120, lean=2, hipx=3, ff=18),
     P(H, hx=22, hy=4, ba=60, lean=8, hipx=6, ff=22, behind=0, trail=1),
     P(H, hx=28, hy=-4, ba=4, lean=14, hipx=8, ff=24, crouch=42, behind=0, trail=1),
     P(H, hx=26, hy=-14, ba=-20, lean=16, hipx=8, ff=24, crouch=42, behind=0, trail=1),
     P(H, hx=22, hy=-17, ba=-30, lean=12, hipx=7, ff=23, crouch=43, behind=0)],
]

STAGGER_K = P(M, hx=-2, hy=12, ba=150, lean=-14, hipx=-6, ff=8, bf=-20, crouch=46, behind=1)
FLINCH_K = P(M, hx=8, hy=-4, ba=40, lean=-8, hipx=-4)

DEATH = [
    P(M, hx=10, hy=-22, ba=-50, lean=4, crouch=45),
    P(M, hx=11, hy=-23, ba=-58, lean=10, crouch=42, head=-10),
    P(M, hx=14, hy=-26, ba=-80, lean=22, crouch=34, head=-14),
    P(M, hx=16, hy=-30, ba=-95, lean=35, crouch=26, ff=10, bf=-14, head=-18),
    P(M, hx=20, hy=-26, ba=-110, lean=55, crouch=20, ff=4, bf=-20, hipx=2, head=-10),
    P(M, hx=22, hy=-14, ba=-150, lean=75, crouch=14, ff=-10, bf=-30, hipx=4, head=-4),
    P(M, hx=16, hy=-4, ba=178, lean=88, crouch=10, ff=-38, bf=-46, hipx=2, head=0),
]

STAND = P(M, crouch=49, lean=2, ff=5, bf=-5, hx=7, hy=-29, ba=-140)
BOW = [STAND, P(STAND, lean=18, head=6), P(STAND, lean=34, head=10)]

VICTORY = [P(M, hx=10, hy=6, ba=110, behind=1),
           P(M, hx=22, hy=-18, ba=-35, lean=10, trail=1),
           P(M, crouch=48, lean=3, ff=8, bf=-8, hx=14, hy=-28, ba=-45),
           P(M, crouch=49, lean=2, ff=6, bf=-6, hx=12, hy=-30, ba=-50)]


def ease(t):
    return t * t * (3 - 2 * t)


IDLE_N, WALK_N = 8, 12

VICTORY_BY = {
    "kenji": P(M, crouch=49, lean=0, ff=6, bf=-6, hx=4, hy=-2, ba=162),          # bokken resting on the shoulder
    "hozen": P(M, crouch=49, lean=-1, ff=5, bf=-5, hx=12, hy=-12, ba=-90),       # staff planted upright
    "gonzo": P(M, crouch=46, lean=2, ff=9, bf=-9, hx=6, hy=-6, ba=55),           # blades crossed before him
    "kagero": P(M, crouch=36, lean=14, ff=16, bf=-14, hx=16, hy=-22, ba=-70),    # a low crouch, knives down
    "kurogane": P(M, crouch=48, lean=-3, ff=8, bf=-8, hx=3, hy=22, ba=96),       # the nodachi raised overhead
    "sailor": P(M, crouch=47, lean=-8, ff=9, bf=-9, hx=10, hy=16, ba=104, head=-6),  # arms up, swaying
    "morsel": P(M, crouch=49, lean=-1, ff=6, bf=-7, hx=14, hy=-26, ba=-88),         # point down, a gentleman again
}


# ---- v4.2: dedicated sidestep, backstep and hit reactions; a round-start move for each master
SS_G = P(M, crouch=41, ff=9, bf=-10, lean=8)                                   # gather
SS_A = P(M, crouch=45, ff=6, bf=-6, ffl=4, bfl=3, lean=3, hy=M["hy"] + 3)     # in the air, legs tucked
SS_L = P(M, crouch=40, ff=15, bf=-17, lean=9, hy=M["hy"] - 2)                  # land wide
SIDESTEP = [lerp_pose(M, SS_G, 0.5), SS_G, lerp_pose(SS_G, SS_A, 0.5), SS_A,
            P(SS_A, lean=1), lerp_pose(SS_A, SS_L, 0.6), SS_L, lerp_pose(SS_L, M, 0.5)]
BS_1 = P(M, lean=-6, hipx=-4, ff=4, bf=-20, hx=10, hy=-6, ba=60, crouch=44)
BS_2 = P(M, lean=-11, hipx=-6, ff=-3, bf=-18, ffl=3, hx=9, hy=-1, ba=78, crouch=46)
BS_3 = P(M, lean=2, crouch=41, ff=12, bf=-17, hx=13, hy=-10, ba=30)
BACKSTEP = [lerp_pose(M, BS_1, 0.5), BS_1, BS_2, lerp_pose(BS_2, BS_3, 0.5), BS_3, lerp_pose(BS_3, M, 0.5)]
HIT_K = {
    "high": P(M, head=-16, lean=-14, hipx=-5, hx=6, hy=4, ba=95, crouch=46, bf=-19),      # head snaps back
    "mid": P(M, lean=24, head=12, crouch=40, hipx=-6, hx=4, hy=-14, ba=-60, ff=10, bf=-18),  # doubles over
    "low": P(M, crouch=35, lean=13, ff=18, bf=-12, hipx=-3, head=5, hx=12, hy=-24, ba=-35),  # the leg buckles
}


def hit_frames(K):
    return [lerp_pose(M, K, 0.55), K, P(K, lean=K["lean"] * 1.12, head=K["head"] * 1.2),
            lerp_pose(M, K, 0.75), lerp_pose(M, K, 0.5), lerp_pose(M, K, 0.25)]


# ---- v5.0
CHIBURI = [P(M, hx=12, hy=4, ba=118, lean=2), P(M, hx=14, hy=10, ba=140, lean=0, crouch=47),
           P(M, hx=24, hy=-14, ba=-40, lean=10, trail=1), P(M, hx=24, hy=-24, ba=-62, lean=12, crouch=43),
           P(M, hx=23, hy=-25, ba=-64, lean=11, crouch=43), P(M, hx=22, hy=-24, ba=-60, lean=9, crouch=44)]
NOTO_D = (32.0, 24.0, 17.0, 11.0, 6.0, 2.5, 0.6, 0.6)          # blade still showing as it slides home
CROUCH = [P(M, crouch=36, lean=12, ff=15, bf=-17, hx=16, hy=-20, ba=-6, head=2),       # dropping under a high cut
          P(M, crouch=29, lean=15, ff=16, bf=-19, hx=17, hy=-24, ba=-10, head=4)]
IAI = [P(M, crouch=40, lean=11, ff=18, bf=-18, hx=3, hy=-25, ba=-166, behind=1, head=-2),
       P(M, crouch=39.6, lean=11.5, ff=18, bf=-18, hx=3, hy=-25.4, ba=-166, behind=1, head=-2)]
BARE_G = P(M, hx=13, hy=-6, ba=70, lean=7)
BARE_IDLE = [P(BARE_G, crouch=45 + 0.6 * math.sin(i * math.pi / 3), hy=-6 + 0.8 * math.sin(i * math.pi / 3)) for i in range(6)]
BARE_WALK = [P(BARE_G, ff=13 + 4.5 * math.sin(i / 8 * 2 * math.pi), bf=-15 + 4.5 * math.sin(i / 8 * 2 * math.pi - 1.7),
               hipx=2.2 * math.sin(i / 8 * 2 * math.pi - 0.85)) for i in range(8)]
PUNCH = [P(BARE_G, hx=6, hy=-4, lean=3), P(BARE_G, hx=3, hy=-3, lean=1, crouch=44),
         P(BARE_G, hx=22, hy=-1, ba=10, lean=14, ff=18, hipx=4), P(BARE_G, hx=27, hy=0, ba=0, lean=17, ff=20, hipx=6),
         P(BARE_G, hx=24, hy=-2, ba=8, lean=14, ff=19, hipx=5), P(BARE_G, hx=17, hy=-4, ba=40, lean=10),
         P(BARE_G, hx=14, hy=-5, ba=60, lean=8), BARE_G]
PICKUP = [P(BARE_G, crouch=38, lean=20, hx=14, hy=-20), P(M, crouch=28, lean=42, ff=16, bf=-18, hx=12, hy=-34, ba=-10),
          P(M, crouch=27, lean=44, ff=16, bf=-18, hx=14, hy=-35, ba=-6), P(M, crouch=34, lean=24, hx=14, hy=-22, ba=10),
          P(M, crouch=41, lean=10, hx=15, hy=-15, ba=14), M]
YIELD = [P(M, crouch=36, lean=12, head=6, hx=12, hy=-22, ba=-20), P(M, crouch=28, lean=16, ff=14, bf=-24, head=10, hx=12, hy=-30, ba=-8),
         P(M, crouch=25, lean=18, ff=13, bf=-26, head=14, hx=13, hy=-33, ba=-3), P(M, crouch=25, lean=19, ff=13, bf=-26, head=16, hx=13, hy=-33.5, ba=-3)]
AIM = [P(M, hx=6, hy=-12, ba=-110, aim=0.35), P(M, hx=5, hy=-13, ba=-118, aim=0.8, lean=-2), P(M, hx=5, hy=-13, ba=-120, aim=1.0, lean=-3),
       P(M, hx=5, hy=-13, ba=-120, aim=1.0, lean=-6, head=-3)]
STRIKE_BY = {
    "morsel": [   # rapier thrusts: low to the leg, mid to the body, high to the face
        [P(L, hx=4, hy=-12, ba=-20), P(L, hx=1, hy=-12, ba=-18, crouch=42),
         P(L, hx=18, hy=-16, ba=-22, ff=20, lean=12, hipx=3, crouch=39), P(L, hx=30, hy=-18, ba=-24, ff=27, lean=20, hipx=7, crouch=35),
         P(L, hx=29, hy=-18, ba=-24, ff=27, lean=20, hipx=7, crouch=35), P(L, hx=18, hy=-15, ba=-20, ff=20, lean=12, hipx=3, crouch=39)],
        [P(M, hx=3, hy=-6, ba=2), P(M, hx=0, hy=-5, ba=2, crouch=43),
         P(M, hx=17, hy=-7, ba=0, ff=20, lean=12, hipx=3, crouch=40), P(M, hx=31, hy=-8, ba=-2, ff=28, lean=20, hipx=7, crouch=36),
         P(M, hx=30, hy=-8, ba=-2, ff=28, lean=20, hipx=7, crouch=36), P(M, hx=17, hy=-7, ba=0, ff=20, lean=12, hipx=3, crouch=40)],
        [P(H, hx=4, hy=2, ba=14, behind=0), P(H, hx=1, hy=3, ba=14, crouch=44, behind=0),
         P(H, hx=17, hy=3, ba=10, ff=20, lean=10, hipx=3, crouch=41, behind=0), P(H, hx=30, hy=3, ba=8, ff=27, lean=18, hipx=7, crouch=37, behind=0),
         P(H, hx=29, hy=3, ba=8, ff=27, lean=18, hipx=7, crouch=37, behind=0), P(H, hx=17, hy=3, ba=10, ff=20, lean=10, hipx=3, crouch=41, behind=0)],
    ],
}
IDLE_AMP = {"sailor": 4.5}          # the drunk sways


def intro_frames(name):
    """Eight frames: each master's flourish before HAJIME."""
    if name == "kenji":                          # points the bokken at her, taps it twice
        PT = P(M, crouch=47, lean=2, hx=24, hy=2, ba=8, ff=10, bf=-12)
        return [STAND, lerp_pose(STAND, PT, 0.5), PT, P(PT, ba=-10, hx=25), PT, P(PT, ba=-10, hx=25),
                lerp_pose(PT, M, 0.5), M]
    if name == "hozen":                          # spins the staff overhead
        return [P(M, crouch=46, lean=0, hx=5, hy=18, ba=30 + 60 * k) for k in range(7)] + [M]
    if name == "gonzo":                          # crosses the blades, then flings them wide and stamps
        X = VICTORY_BY["gonzo"]
        Wd = P(M, crouch=41, lean=-4, hx=26, hy=-2, ba=-30, ff=18, bf=-18)
        return [lerp_pose(M, X, 0.5), X, P(X, crouch=X["crouch"] - 1), lerp_pose(X, Wd, 0.5), Wd,
                P(Wd, crouch=38, lean=-2), lerp_pose(Wd, M, 0.5), M]
    if name == "kagero":                         # twirls a knife low in a crouch
        return [P(M, crouch=34, lean=16, ff=17, bf=-14, hx=17, hy=-14, ba=90 * k) for k in range(7)] + [lerp_pose(M, P(M, crouch=36, lean=12), 0.5)]
    if name == "sailor":                         # staggers, belches, waves the cutlass at her
        return [P(M, lean=5 + 9 * math.sin(k * 0.9), hipx=3 * math.sin(k * 0.9 + 1), hx=12 + 8 * math.sin(k * 1.3),
                  hy=-6 + 10 * math.sin(k * 1.3), ba=40 + 50 * math.sin(k * 1.3), head=-6 * math.sin(k * 0.9)) for k in range(7)] + [M]
    if name == "morsel":                         # the fencer's salute, then en garde
        SA = P(M, crouch=48, lean=0, hx=8, hy=8, ba=88, ff=8, bf=-10)
        return [STAND, lerp_pose(STAND, SA, 0.5), SA, P(SA, head=4), lerp_pose(SA, P(M, hx=22, hy=-26, ba=-50), 0.5),
                P(M, hx=22, hy=-26, ba=-50), lerp_pose(P(M, hx=22, hy=-26, ba=-50), M, 0.5), M]
    if name == "kurogane":                       # the slow draw of the nodachi, overhead, then down
        O = VICTORY_BY["kurogane"]
        return [STAND, lerp_pose(STAND, O, 0.3), lerp_pose(STAND, O, 0.65), O, P(O, lean=O["lean"] - 1),
                lerp_pose(O, M, 0.4), lerp_pose(O, M, 0.75), M]
    return [lerp_pose(STAND, M, ease(t / 7.0)) for t in range(8)]


def build_anims(S=None):
    """Return ordered list of (anim_name, [poses]) and phase hints."""
    A = []
    names = ["low", "mid", "high"]
    nm_ = (S or {}).get("name")
    amp = IDLE_AMP.get(nm_, 1.0)
    for s in range(3):
        base = STANCE[s]
        A.append((f"idle_{names[s]}", [
            P(base, crouch=base["crouch"] + 0.5 * math.sin(i * math.pi / 4),
              hy=base["hy"] + 0.7 * math.sin(i * math.pi / 4),
              lean=base["lean"] + 0.6 * amp * math.sin(i * math.pi / 4),
              hipx=base["hipx"] + (amp - 1) * 0.5 * math.sin(i * math.pi / 4 + 1.0))
            for i in range(IDLE_N)], True))
    for s in range(3):
        base = STANCE[s]
        fr = []
        for i in range(WALK_N):
            ph = i / WALK_N * 2 * math.pi
            # v5.4 a stride with weight in it: the body drops onto the landing foot and rises over the
            # passing leg, leans into each step, the head stays level and the hands lag the body
            sn = math.sin(ph)
            fr.append(P(base, ff=base["ff"] + 6.0 * sn, bf=base["bf"] - 6.0 * sn,
                        ffl=max(0.0, 3.0 * math.cos(ph)), bfl=max(0.0, -3.0 * math.cos(ph)),
                        crouch=base["crouch"] - 0.4 - 1.6 * sn * sn,
                        hipx=base["hipx"] + 1.2 * math.sin(2 * ph - 0.6),
                        lean=base["lean"] + 1.5 + 1.2 * math.sin(2 * ph - 1.0),
                        head=base["head"] - 1.0 * math.sin(2 * ph - 1.0),
                        hy=base["hy"] + 1.0 * math.sin(2 * ph - 1.9),
                        hx=base["hx"] + 0.6 * math.sin(2 * ph - 1.9)))
        A.append((f"walk_{names[s]}", fr, True))
    for s in range(3):
        A.append((f"guard_{names[s]}", [lerp_pose(STANCE[s], GUARD[s], ease(t)) for t in (0.3, 0.6, 0.85, 1.0)], False))
    for a, b, nm in ((0, 1, "trans_lm"), (1, 2, "trans_mh"), (0, 2, "trans_lh")):
        A.append((nm, [lerp_pose(STANCE[a], STANCE[b], ease(t)) for t in (0.33, 0.66)], False))
    for s in range(3):
        ks = STRIKE_BY.get(nm_, STRIKE)[s]
        def mid(a, b, t=0.5):
            q = lerp_pose(a, b, t)
            q["trail"] = b["trail"]
            return q
        fr = [lerp_pose(STANCE[s], ks[0], ease(t)) for t in (0.2, 0.4, 0.6, 0.75, 0.9, 1.0)]  # windup 0-5
        fr += [mid(ks[0], ks[1]), ks[1], mid(ks[1], ks[2]), ks[2], mid(ks[2], ks[3]), ks[3]]   # swing 6-11
        fr += [mid(ks[3], ks[4]), ks[4], mid(ks[4], ks[5]), ks[5]]                             # follow 12-15
        fr += [lerp_pose(ks[5], STANCE[s], ease(t)) for t in (0.15, 0.3, 0.45, 0.6, 0.75, 0.9)]  # recover 16-21
        A.append((f"strike_{names[s]}", fr, False))
    A.append(("stagger", [lerp_pose(M, STAGGER_K, t) for t in (0.25, 0.5, 0.75)] + [STAGGER_K] +
              [lerp_pose(M, STAGGER_K, t) for t in (0.8, 0.6, 0.4, 0.25)], False))
    A.append(("flinch", [lerp_pose(M, FLINCH_K, t) for t in (0.35, 0.7, 1.0, 0.8, 0.6, 0.35)], False))
    dfr = []
    for i in range(len(DEATH) - 1):
        n = 1 if i == 0 else 2
        for j in range(n):
            dfr.append(lerp_pose(DEATH[i], DEATH[i + 1], j / n))
    dfr.append(DEATH[-1])
    A.append(("death", dfr, False))
    A.append(("bow", [BOW[0], lerp_pose(BOW[0], BOW[1], 0.5), BOW[1],
                      lerp_pose(BOW[1], BOW[2], 0.5), BOW[2]], False))
    A.append(("rise", [lerp_pose(STAND, M, ease(t)) for t in (0.3, 0.6, 0.85)], False))
    J0 = P(M, crouch=M["crouch"] - 7, lean=M["lean"] + 6)
    JT = P(M, ffl=13, bfl=11, ff=M["ff"] - 3, bf=M["bf"] + 3, lean=M["lean"] + 9, hy=M["hy"] + 3)
    A.append(("jump", [J0, lerp_pose(M, JT, 0.5), JT, P(JT, lean=JT["lean"] + 3), lerp_pose(M, JT, 0.4), J0], False))
    A.append(("crouch", CROUCH, False))
    A.append(("chiburi", CHIBURI, False))
    A.append(("noto", [M] * len(NOTO_D), False))               # rendered with a shrinking blade and the scabbard
    A.append(("iai", IAI, True))
    A.append(("bare_idle", BARE_IDLE, True))
    A.append(("bare_walk", BARE_WALK, True))
    A.append(("punch", PUNCH, False))
    A.append(("pickup", PICKUP, False))
    A.append(("yield", YIELD, False))
    A.append(("aim", AIM, False))
    A.append(("sidestep", SIDESTEP, False))
    A.append(("backstep", BACKSTEP, False))
    for hn in ("low", "mid", "high"):
        A.append((f"hit_{hn}", hit_frames(HIT_K[hn]), False))
    A.append(("intro", intro_frames((S or {}).get("name")), False))
    A.append(("turn", [M] * len(TURN_YAWS), False))       # rendered by render_turn
    A.append(("trophy", [M] * 3, False))                     # Lieve lifts the giant's head
    A.append(("headless", [dfr[-1]], False))                # the body alone, once the head is taken
    A.append(("bowf", [M] * len(BOW_ANGLES), False))
    vfinal = VICTORY_BY.get((S or {}).get("name"))
    if vfinal is None:
        A.append(("victory", [lerp_pose(M, VICTORY[0], 0.5), VICTORY[0], VICTORY[1],
                              lerp_pose(VICTORY[1], VICTORY[2], 0.5), VICTORY[2], VICTORY[3]], False))
    else:
        A.append(("victory", [lerp_pose(M, VICTORY[0], 0.5), VICTORY[0], lerp_pose(VICTORY[0], vfinal, 0.4),
                              lerp_pose(VICTORY[0], vfinal, 0.8), vfinal, P(vfinal, lean=vfinal["lean"] + 1, head=2)], False))
    # dismemberment deaths: GORE_FLIGHT frames while the severed part flies, then the collapse
    for nm, part_ in GORE:
        if part_ == "arm":
            fly = [lerp_pose(DEATH[0], FLINCH_K, min(1.0, 0.35 + i * 0.2)) for i in range(GORE_FLIGHT)]
        elif part_ == "leg":
            fly = [lerp_pose(DEATH[0], DEATH[2], i / (GORE_FLIGHT - 1) * 0.6) for i in range(GORE_FLIGHT)]
        else:
            fly = [P(DEATH[0], head=0, lean=DEATH[0]["lean"] - 3 * math.sin(i * 0.9)) for i in range(GORE_FLIGHT)]
        A.append((nm, fly + dfr, False))
    # v5.4 weight in the cuts: a deeper sink and draw-back before the swing, the hips and the front
    # foot driving through it, the back foot dragged after, the head held steady
    def _bump(i, a, pk, b):
        if i <= a or i >= b:
            return 0.0
        return (i - a) / (pk - a) if i <= pk else (b - i) / (b - pk)
    for n_, (nm, poses, loop) in enumerate(A):
        if not nm.startswith("strike_"):
            continue
        tr = [i for i, p_ in enumerate(poses) if p_.get("trail")]
        if not tr:
            continue
        ts, te = tr[0], tr[-1]
        amt = {"strike_high": 1.0, "strike_mid": 0.8, "strike_low": 0.7}.get(nm, 0.8)
        out = []
        for i, p_ in enumerate(poses):
            wa = _bump(i, 0, max(1, ts - 1), ts + 1) * amt
            wd = _bump(i, ts - 1, te, te + 6) * amt
            out.append(P(p_, crouch=p_["crouch"] - 3.0 * wa - 3.5 * wd, lean=p_["lean"] - 6.0 * wa + 7.0 * wd,
                         hipx=p_["hipx"] - 2.5 * wa + 4.5 * wd, ff=p_["ff"] + 4.0 * wd, bf=p_["bf"] - 2.0 * wa + 6.0 * wd,
                         bfl=p_.get("bfl", 0.0) + 1.5 * wd, head=p_["head"] + 3.0 * wa - 4.0 * wd))
        A[n_] = (nm, out, loop)
    # v5.5 weight in the remaining moves
    def _w(nm, p_, i, n):
        t = i / max(1, n - 1)
        hump = math.sin(math.pi * t)
        if nm.startswith("guard_"):            # sinking into the guard, a settle at the end
            d = (0.0, 1.2, 2.0, 1.4)[min(i, 3)]
            return P(p_, crouch=p_["crouch"] - d, lean=p_["lean"] - 0.8 * d, head=p_["head"] + 0.6 * d)
        if nm == "sidestep":                    # push off, float, absorb the landing
            ends = 1.0 - hump
            return P(p_, crouch=p_["crouch"] - 1.6 * ends, lean=p_["lean"] + 1.5 * hump, hy=p_["hy"] + 1.2 * hump)
        if nm == "backstep":                    # the head and shoulders go first, the legs absorb the landing
            return P(p_, lean=p_["lean"] - 4.0 * hump, hipx=p_["hipx"] - 2.0 * hump, crouch=p_["crouch"] - 2.5 * t * t,
                     head=p_["head"] + 2.5 * hump)
        if nm.startswith("hit_"):               # the head snaps, the body follows, a stumble back
            snap = math.exp(-((i - 1) ** 2) / 1.5)
            follow = math.exp(-((i - 3) ** 2) / 3.0)
            return P(p_, head=p_["head"] + 6.0 * snap, lean=p_["lean"] - 5.0 * follow, hipx=p_["hipx"] - 2.0 * follow,
                     crouch=p_["crouch"] - 2.0 * follow, bf=p_["bf"] - 3.0 * follow)
        if nm in ("stagger", "flinch"):
            return P(p_, crouch=p_["crouch"] - 2.0 * hump, bf=p_["bf"] - 4.0 * hump, lean=p_["lean"] - 2.0 * hump)
        if nm == "death":                       # the knees buckle before the body falls
            k = math.exp(-((i - 2) ** 2) / 2.0)
            return P(p_, crouch=p_["crouch"] - 4.0 * k, head=p_["head"] - 5.0 * k)
        return p_
    for n_, (nm, poses, loop) in enumerate(A):
        if nm.startswith(("guard_", "hit_")) or nm in ("sidestep", "backstep", "stagger", "flinch", "death"):
            A[n_] = (nm, [_w(nm, p_, i, len(poses)) for i, p_ in enumerate(poses)], loop)
    return A


GORE = (("death_head", "head"), ("death_arm", "arm"), ("death_leg", "leg"))
GORE_FLIGHT = 7
# arc of the severed part: (distance back, apex height, total spin in degrees)
GORE_ARC = {"head": (44, 40, -330), "arm": (50, 30, 400), "leg": (34, 18, -200)}


def _stump(K, part_, S, p):
    """Canvas-space (unscaled y-up) stump point for a gore part."""
    if part_ == "head":
        upr = S.get("upright", 0.0)
        lean = rad(p["lean"])
        up = v(math.sin(lean), math.cos(lean))
        side = v(up[1], -up[0])
        nb = K["sh"] - up * (1.5 * upr) - side * (1.2 * upr)
        return nb + (K["neck"] + v(0, 0.5) - nb) * 0.45
    if part_ == "arm":
        return K["shb"]
    return K["bknee"]


def _paste_rot(dst, patch, cx, cy, ang):
    """Paste an indexed patch rotated by ang degrees with its centre at (cx, cy)."""
    ph, pw = patch.shape
    r = int(math.ceil(math.hypot(ph, pw) / 2)) + 1
    ca, sa = math.cos(rad(ang)), math.sin(rad(ang))
    for y in range(int(cy) - r, int(cy) + r + 1):
        if y < 0 or y >= CH:
            continue
        for x in range(int(cx) - r, int(cx) + r + 1):
            if x < 0 or x >= CW:
                continue
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            u = ca * dx + sa * dy + pw / 2
            w = -sa * dx + ca * dy + ph / 2
            iu, iw = int(math.floor(u)), int(math.floor(w))
            if 0 <= iu < pw and 0 <= iw < ph and patch[iw, iu]:
                dst[y, x] = patch[iw, iu]


def _dot(dst, x, y, r=1):
    x, y = int(round(x)), int(round(y))
    for yy in range(y - r + 1, y + 1):
        for xx in range(x - r + 1, x + 1):
            if 0 <= xx < CW and 0 <= yy < CH:
                dst[yy, xx] = BLOOD


def _pool(dst, cx, rx, y=GY):
    ry = 1.0 + rx * 0.16
    for yy in range(int(y - ry) - 1, int(y + ry) + 2):
        for xx in range(int(cx - rx) - 1, int(cx + rx) + 2):
            if 0 <= xx < CW and 0 <= yy < CH and dst[yy, xx] == 0:
                if ((xx + 0.5 - cx) / rx) ** 2 + ((yy + 0.5 - y) / ry) ** 2 <= 1.0:
                    dst[yy, xx] = BLOOD


def render_char(S):
    """Render every frame for a character. Returns (frames, anim_table)."""
    f, t, _ = render_char_full(S)
    return f, t


def render_gore(S, name, part_, poses):
    """Frames for one dismemberment death, plus the stump point of every frame."""
    sc = S["scale"]

    def W(q):
        return (RX + q[0] * sc, GY - q[1] * sc)
    st = CHAIN_STYLES.get(S["hair"])
    tails = [(HANG if S["hair"] in CLAMPED else st[2]) if st else 190.0] * (st[3] if st else 3)
    p0 = poses[0]
    piece = render(p0, S, tails, only=part_, gore=True).idx
    ys, xs = np.nonzero(piece)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    patch = piece[y0:y1, x0:x1]
    sx0, sy0 = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    back, apex, spin = GORE_ARC[part_]
    rest_y = GY - min(patch.shape) / 2.0 + 1
    ex = max(min(patch.shape) / 2.0 + 2, sx0 - back * sc / 1.2)
    rnd = np.random.default_rng(__import__("zlib").crc32(name.encode()) & 0xFFFF)
    jets = [(rnd.uniform(-1.6, 0.6), rnd.uniform(-5.5, -3.0), rnd.uniform(0, 1)) for _ in range(14)]
    frames, pts = [], []
    nf = GORE_FLIGHT
    last_piece = None
    pool_x = None
    for i, pz in enumerate(poses):
        cnv = render(pz, S, tails, omit=part_, gore=True)
        img = cnv.idx
        K = skeleton(pz, S)
        sx, sy = W(_stump(K, part_, S, pz))
        pts.append((sx, sy))
        if i < nf:
            t = i / (nf - 1)
            px = sx0 + (ex - sx0) * t
            py = sy0 + (rest_y - sy0) * t - apex * sc / 1.2 * 4 * t * (1 - t)
            ang = spin * t
            last_piece = (px, py, ang)
            # blood trailing the flying part
            for k in range(3):
                tt = max(0.0, t - 0.06 * (k + 1))
                qx = sx0 + (ex - sx0) * tt
                qy = sy0 + (rest_y - sy0) * tt - apex * sc / 1.2 * 4 * tt * (1 - tt)
                _dot(img, qx, qy, 2 if k == 0 else 1)
        else:
            px, py, ang = last_piece
        # the fountain from the stump: a thick arcing stream while the part flies, then weakening spurts
        strength = 1.0 if i < nf else max(0.0, 1.0 - (i - nf) / 8.0) * (1.0 if (i & 1) else 0.45)
        if strength > 0.05:
            k_ = sc / 1.2
            bvx = {"head": -0.9, "arm": -1.4, "leg": -1.1}[part_]
            pulse = 0.85 + 0.15 * math.sin(i * 1.7)
            for jx, jy, ph in jets[:6]:
                vx_ = (bvx + jx * 0.35) * k_
                vy_ = (-8.0 + jy * 0.3) * strength * pulse * k_
                tt = 0.0
                while tt < 2.6:
                    gx = sx + vx_ * tt * 3.0
                    gy = sy + vy_ * tt * 3.0 + 3.2 * (tt * 3.0) ** 2 * 0.13 * k_
                    if gy >= GY - 1:
                        break
                    _dot(img, gx, gy, 2 if tt < 1.6 else 1)
                    tt += 0.09
            for vx, vy, ph in jets:               # spray droplets
                tau = ((i * 0.37 + ph) % 1.0) * 2.4
                gx = sx + (vx * 1.4 + bvx) * tau * 3.0 * k_
                gy = sy + vy * strength * tau * 3.0 * k_ + 0.7 * (tau * 3.0) ** 2 * k_
                if gy < GY - 1:
                    _dot(img, gx, gy, 2)
        # pools: under the stump once the body is down, and under the severed part
        if i >= nf - 2:
            grow = min(1.0, (i - nf + 3) / 9.0)
            if pool_x is None or i >= nf:
                pool_x = sx if i < nf else pts[-1][0]
            _pool(img, pool_x, 4 + 18 * grow * sc / 1.2)
            _pool(img, px, 3 + 8 * grow)
        _paste_rot(img, patch, px, py, ang)
        frames.append(img.copy())
    return frames, pts


def render_char_full(S):
    """Frames, anim table and per-frame stump points (0,0 outside the gore deaths)."""
    anims = build_anims(S)
    frames, table, gore_pts = [], [], []
    gmap = dict(GORE)
    st = CHAIN_STYLES.get(S["hair"])
    rest = st[2] if st else 190.0
    nseg = st[3] if st else 3
    nm_ = S.get("name")
    stub = None
    for name, poses, loop in anims:
        if name in ONLY and nm_ not in ONLY[name] and frames:
            start = len(frames)
            if stub is None:
                stub = frames[0].copy()
            for _ in poses:
                frames.append(stub)
                gore_pts.append((0, 0))
            table.append((name, start, len(poses)))
            continue
        if name == "noto":
            start = len(frames)
            K0 = skeleton(M, S)
            mouth = K0["waist"] + v(3.5, 0.5)
            sd = v(math.cos(rad(200.0)), math.sin(rad(200.0)))
            for i, d_ in enumerate(NOTO_D):
                t0 = min(1.0, i / 2.0)
                hand = mouth - sd * (d_ + 2.6) + v(0, 1.5)
                tgt = P(M, crouch=46, lean=4, hx=float(hand[0] - K0["sh"][0]), hy=float(hand[1] - K0["sh"][1]), ba=200.0, behind=1)
                pz = lerp_pose(CHIBURI[-1], tgt, ease(t0)) if i < 2 else tgt
                S2 = dict(S, blade=max(0.6, d_ if i >= 2 else S["blade"]), saya=True, saya_len=S["blade"])
                frames.append(render(pz, S2, [HANG if S["hair"] in CLAMPED else 190.0] * (CHAIN_STYLES[S["hair"]][3] if S["hair"] in CHAIN_STYLES else 3)).idx.copy())
                gore_pts.append((0, 0))
            table.append((name, start, len(NOTO_D)))
            continue
        if name == "trophy":
            start = len(frames)
            tp = trophy_patch(S) if str(S.get("name")).startswith("lieve") else None
            for amt in (0.45, 0.85, 1.0):
                frames.append(render_turn(S, 90, 0.0, amt, tp).copy() if tp is not None else render_turn(S, 90).copy())
                gore_pts.append((0, 0))
            table.append((name, start, 3))
            continue
        if name == "headless":
            start = len(frames)
            pz = poses[0]
            st_ = CHAIN_STYLES.get(S["hair"])
            tl = [190.0] * (st_[3] if st_ else 3)
            img = render(pz, S, tl, omit="head", gore=True).idx.copy()
            sc_ = S["scale"]
            Kk = skeleton(pz, S)
            sx_, sy_ = RX + _stump(Kk, "head", S, pz)[0] * sc_, GY - _stump(Kk, "head", S, pz)[1] * sc_
            _pool(img, sx_, 4 + 18 * sc_ / 1.2)
            frames.append(img)
            gore_pts.append((0, 0))
            table.append((name, start, 1))
            continue
        if name in ("turn", "bowf"):
            start = len(frames)
            views = [(y, 0.0) for y in TURN_YAWS] if name == "turn" else [(90, b) for b in BOW_ANGLES]
            for y, b in views:
                frames.append(render_turn(S, y, b).copy())
                gore_pts.append((0, 0))
            table.append((name, start, len(frames) - start))
            continue
        if name in gmap:
            start = len(frames)
            fr_, pts = render_gore(S, name, gmap[name], poses)
            frames += fr_
            gore_pts += [(max(1, min(255, int(round(x)))), max(1, min(255, int(round(y))))) for x, y in pts]
            table.append((name, start, len(frames) - start))
            continue
        S_ = dict(S, weapon="none", pistol=False) if name in BARE else S
        spr = Springs(nseg, rest, hang=S["hair"] in CLAMPED)
        mot = MOTION.get(name, 0.0)
        passes = 3 if loop else 1
        start = len(frames)
        # pre-roll from the previous pose for non-loops so tails settle
        if not loop:
            for _ in range(4):
                k = skeleton(poses[0], S)
                spr.step(k["head"], 0)
        for ps in range(passes):
            prev = None
            for i, pz in enumerate(poses):
                k = skeleton(pz, S)
                tails = spr.step(k["head"], i * 1.1, mot)
                if ps == passes - 1:
                    prevp = poses[i - 1] if i > 0 else (poses[-1] if loop else None)
                    if name.startswith("strike") and i == 3:
                        prevp = poses[2]
                    cnv = render(pz, S_, tails, prevp, motion=mot)
                    frames.append(cnv.idx.copy())
                    gore_pts.append((0, 0))
        table.append((name, start, len(frames) - start))
    return frames, table, gore_pts


PALETTES = {
    "lieve": [(0, 0, 0), (24, 14, 20),
                (150, 84, 64), (212, 144, 108), (248, 202, 162),       # skin
                (126, 26, 14), (204, 58, 24), (248, 118, 46),          # red hair
                (126, 122, 136), (196, 192, 196), (244, 240, 232),     # white keikogi
                (26, 30, 62), (46, 56, 104), (82, 98, 156),            # navy hakama
                (240, 244, 252), (150, 160, 180)],
    "kenji": [(0, 0, 0), (16, 10, 14),
              (122, 72, 48), (190, 126, 86), (230, 176, 130),
              (14, 12, 18), (44, 38, 48), (84, 76, 92),               # black hair
              (40, 30, 56), (74, 60, 104), (122, 104, 158),            # dark violet top
              (62, 60, 66), (108, 104, 112), (158, 154, 162),          # grey hakama
              (240, 240, 236), (150, 156, 170)],
}


# ======================================================================= the turning, bowing hero
TURN_YAWS = (0, 45, 90, 270, 315)          # views rendered; the rest are mirror images
BOW_ANGLES = (22, 44, 62)


def render_turn(S, yaw_deg, bow_deg=0.0, raise_amt=0.0, trophy=None):
    """Lieve standing upright, seen from any angle around her (yaw 0 = facing right,
    90 = facing the camera), optionally bowing. A small 3D puppet: every part is a
    capsule placed in 3D, projected, and drawn back to front."""
    sc = S["scale"]
    bld = S.get("build", 1.0)
    fg = S.get("figure", 0.0)
    cd = S.get("chest_depth", 1.0)
    hr = S["head_r"]
    c = Canvas()
    yaw, bw = rad(yaw_deg), rad(bow_deg)
    cyw, syw = math.cos(yaw), math.sin(yaw)
    cb, sb = math.cos(bw), math.sin(bw)
    leg = S["thigh"] + S["shin"]
    hipY = leg * 0.96 + 2.5

    def T(r, u, f, upper=False):
        """local (right, up, forward) -> (screen X, Y, depth toward the camera)"""
        if upper:                            # the bow pivots at the hips
            du = u - hipY
            f, u = f * cb + du * sb, hipY + du * cb - f * sb
        X = f * cyw - r * syw
        D = f * syw + r * cyw
        return X, u, D

    def W2(X, Y):
        return (RX + 2 + X * sc, GY - Y * sc)

    ops = []

    def cap(a, b, r0, r1, mat, upper=False, flat=None, bias=0.0, mask=None, dz=0.0):
        A, B = T(*a, upper=upper), T(*b, upper=upper)
        ops.append(((A[2] + B[2]) / 2 + dz, lambda: c.cap(W2(A[0], A[1]), W2(B[0], B[1]), R(r0), R(r1), mat, 0, flat=flat, bias=bias, mask=mask)))

    def R(x):
        return x * sc

    rw = 9.2 * bld * (1 - 0.22 * fg)
    rc = 10.4 * bld * (1 - 0.12 * fg)
    sf = 1 - 0.15 * fg
    flare = S.get("flare", 1.0)
    shY = hipY + S["torso"]
    waistY = hipY + S["torso"] * 0.32
    # legs in the hakama, feet
    for i in (1, -1):
        hw = 4.4 * bld * (1 + 0.1 * fg)
        hp, kn, an = (i * hw * 0.8, hipY, 0.0), (i * hw * 0.9, hipY - S["thigh"], 0.6), (i * hw, 2.5, 0.0)
        cap(hp, kn, 7.0 * bld, 7.4 * bld, BOT, bias=-0.1)
        cap(kn, an, 7.4 * bld, 9.0 * bld * flare, BOT, bias=-0.1)
        cap((i * hw, 1.4, 0.5), (i * hw, 1.2, 6.5), 2.3 * sf, 1.9 * sf, SKIN, dz=0.5)
    # hakama top and the keikogi, two capsules side by side so she is wider than deep
    for i in (1, -1):
        cap((i * 3.0, hipY, 0), (i * 3.0, waistY, 0), 8.0 * bld, 7.6 * bld * (1 - 0.18 * fg), BOT, upper=True)
        cap((i * 3.2, waistY, 0), (i * 3.0, shY - 1.2, 0), rw * 0.78 * cd, rc * 0.78 * cd, TOP, upper=True)
    ch = S.get("chest", 0.0)
    if ch > 0.01:
        rb = 2.6 + 3.0 * ch
        for i in (1, -1):
            cap((i * 3.6, shY - 7.4, rc * cd * 0.55), (i * 3.6, shY - 8.4, rc * cd * 0.55), rb, rb, TOP, upper=True, dz=0.1)
    if S.get("vneck"):
        for i in (1, -1):
            cap((i * 2.2, shY + 0.5, rc * cd * 0.62), (i * 0.3, shY - 5.5, rc * cd * 0.7), 1.2, 0.7, SKIN, upper=True, dz=0.3)
    obi = S.get("obi", 0)
    if obi != 3:
        col = BOT if obi == 0 else (HAIR + 1 if obi == 1 else TOP + 2)
        for i in (1, -1):
            cap((i * 6.5, waistY, 2.0), (0, waistY, rw * 0.9), 1.6, 1.6, col, upper=True, flat=col, dz=0.2)
    # arms hanging at her sides; the sword lowered in her right hand
    upr = S.get("upright", 0.0)
    hand_up = None
    for i in (1, -1):
        sh_ = (i * rc * 0.92, shY - 1.6 - 1.8 * upr, 0.0)
        el = (i * rc * 1.02, shY - 1.6 - S["uarm"], -0.6)
        hd = (i * rc * 0.9, shY - S["uarm"] - S["farm"] + 1.0, 2.2)
        if i == -1 and raise_amt > 0:          # the left arm lifts the trophy high
            t_ = raise_amt
            el_up = (-(rc * 0.92 + S["uarm"] * 0.45), shY + S["uarm"] * 0.8, 1.5)
            hd_up = (-(rc * 0.92 + (S["uarm"] + S["farm"]) * 0.42), shY + (S["uarm"] + S["farm"]) * 0.88, 2.0)
            el = tuple(a + (b - a) * t_ for a, b in zip(el, el_up))
            hd = tuple(a + (b - a) * t_ for a, b in zip(hd, hd_up))
            hand_up = hd
        cap(sh_, el, 4.6 * bld * (1 - 0.12 * fg), 4.1 * bld * (1 - 0.12 * fg), TOP, upper=True, bias=-0.15)
        cap(el, hd, 3.2 * bld, 2.7 * bld, SKIN, upper=True, bias=-0.1)
        cap(hd, hd, 3.1 * sf, 3.1 * sf, SKIN, upper=True, dz=0.2)
        if i == 1:
            tip = (hd[0] + 1.0, 8.0, hd[2] + 9.0)
            grip = (hd[0], hd[1] + 6.0, hd[2] - 2.0)
            A, B = T(*hd, upper=True), (T(*tip))
            cap(grip, hd, 1.5, 1.5, OUT, upper=True, flat=OUT, dz=0.25)
            ops.append(((A[2] + B[2]) / 2 + 0.3, lambda A=A, B=B: c.blade(W2(A[0], A[1]), W2(B[0], B[1]), R(1.2), 0, (1.0, 0.0), STEEL_L, STEEL_M)))
    # neck and head
    neckTop = shY + S["neck"] * 0.9 + 0.8
    cap((0, shY - 1.5, -1.0), (0, neckTop, -0.3), 3.4 * (1 - 0.25 * fg) * (1 + 0.3 * upr), 3.0 * (1 - 0.25 * fg), SKIN, upper=True)
    hc_l = (0.0, neckTop + hr * 0.9, 0.6)
    HC = T(*hc_l, upper=True)
    hcx, hcy = W2(HC[0], HC[1])
    ops.append((HC[2], lambda: c.cap((hcx, hcy), (hcx, hcy), R(hr), R(hr), SKIN, 0)))
    hair = S.get("hair", "braid")
    if hair != "bald":
        Rh = hr * 1.07

        def hair_mask(xs, ys):
            dx = (xs - hcx) / sc
            dy = -(ys - hcy) / sc
            dd = np.sqrt(np.clip(Rh * Rh - dx * dx - dy * dy, 0, None))
            f = dx * cyw + dd * syw            # back to her local frame
            r = -dx * syw + dd * cyw
            u = dy
            u, f = u * cb + f * sb, f * cb - u * sb
            limit = 0.22 if S.get("bangs") else 0.5
            face = (f > 0.25 * Rh) & (u < limit * Rh) & (np.abs(r) < 0.86 * Rh) & (u > -1.2 * Rh)
            return ~face & (u > -0.55 * Rh)
        hx2, hy2 = W2(*T(hc_l[0], hc_l[1] + 0.5, hc_l[2] - 0.6, upper=True)[:2])
        ops.append((HC[2] + 0.05, lambda: c.cap((hx2, hy2), (hx2, hy2), R(Rh), R(Rh), HAIR, 0, mask=hair_mask)))
        if hair in CHAIN_STYLES or hair in CLAMPED:
            # the braid hangs down her back
            top = (0.0, hc_l[1] - 1.5, -hr * 0.85)
            prev = T(*top, upper=True)
            n = 7
            for k in range(1, n + 1):
                yk = prev[1] - S.get("tail_len", 8) * 0.95
                q = (prev[0], yk, prev[2])
                if k == 1:
                    q = T(0.0, hc_l[1] - 1.5 - S.get("tail_len", 8), -(rc * cd * 0.78 + 2.4), upper=True)
                else:
                    # straight down along her back
                    base = T(0.0, 0.0, -(rc * cd * 0.78 + 2.4), upper=True)
                    q = (base[0] + (prev[0] - base[0]) * 0.3, yk, base[2])
                rr = 2.3 - 0.9 * k / n
                for off in (0.3, 0.75):
                    px = prev[0] + (q[0] - prev[0]) * off
                    py = prev[1] + (q[1] - prev[1]) * off
                    pd = prev[2] + (q[2] - prev[2]) * off
                    sx_, sy_ = W2(px, py)
                    ops.append((pd, lambda sx_=sx_, sy_=sy_, rr=rr: c.cap((sx_, sy_), (sx_, sy_), R(rr), R(rr), HAIR, 0)))
                prev = q
            sx_, sy_ = W2(prev[0], prev[1])
            ops.append((prev[2], lambda sx_=sx_, sy_=sy_: c.cap((sx_, sy_), (sx_, sy_ + R(2.2)), R(1.4), R(0.8), STEEL_L, 0, flat=STEEL_L)))
    if S.get("headband"):                    # Kenji's headband, a white band around her brow
        Rb = hr * 1.1

        def band_mask(xs, ys):
            dy = -(ys - hcy) / sc
            return (dy > hr * 0.25) & (dy < hr * 0.52)
        ops.append((HC[2] + 0.08, lambda: c.cap((hcx, hcy), (hcx, hcy), R(Rb), R(Rb), STEEL_L, 0, flat=STEEL_L, mask=band_mask)))
    # face: eyes, lips (only on the side of the head turned toward the camera)
    face_pts = []
    for i in (1, -1):
        e = (i * 2.4, hc_l[1] + 0.4, hc_l[2] + math.sqrt(max(0.0, hr * hr - 2.4 ** 2 - 0.16)) * 0.97)
        face_pts.append(("eye", T(*e, upper=True)))
    lp = (0.0, hc_l[1] - 2.7, hc_l[2] + math.sqrt(max(0.0, hr * hr - 2.7 ** 2)) * 0.97)
    face_pts.append(("lips", T(*lp, upper=True)))

    def draw_face():
        for kind, (X, Y, D) in face_pts:
            if D - HC[2] < hr * 0.35:
                continue
            x, y = W2(X, Y)
            x, y = int(round(x)), int(round(y))
            if not (0 <= x < CW - 1 and 1 <= y < CH):
                continue
            if kind == "eye":
                c.idx[y, x] = OUT
                if S.get("eyes", 0):
                    c.idx[y - 1, x] = OUT
                    c.idx[y - 1, x + 1] = OUT
            elif S.get("lips"):
                c.idx[y, x] = HAIR + 1
    ops.append((HC[2] + hr + 1.0, draw_face))
    for _, fn in sorted(ops, key=lambda o: o[0]):
        fn()
    c.finish()
    if trophy is not None and hand_up is not None:
        X, Y, _ = T(*hand_up, upper=True)
        hx_, hy_ = W2(X, Y)
        ph, pw = trophy.shape
        x0, y0 = int(hx_ - pw * 0.45), int(hy_ - 3)          # held from above, hanging below the fist
        for yy in range(ph):
            for xx in range(pw):
                v_ = trophy[yy, xx]
                if v_ and 0 <= y0 + yy < CH and 0 <= x0 + xx < CW:
                    c.idx[y0 + yy, x0 + xx] = v_
        # her fist over the hair she holds it by
        fx_, fy_ = int(hx_), int(hy_)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if 0 <= fy_ + dy < CH and 0 <= fx_ + dx < CW:
                    c.idx[fy_ + dy, fx_ + dx] = SKIN + 1 if (dx or dy) else SKIN + 2
        # blood dripping from the stump
        for k in range(3):
            yy = y0 + ph + 2 + k * 4
            xx = x0 + pw // 2 + (k & 1)
            if 0 <= yy < CH and 0 <= xx < CW:
                c.idx[yy, xx] = BLOOD
    return c.idx


_TROPHY = {}


def trophy_patch(S_hero):
    """Kurogane's severed, helmeted head, recoloured into the hero's palette."""
    if "p" in _TROPHY:
        return _TROPHY["p"]
    K = {c["name"]: c for c in load_chars()}.get("kurogane")
    if K is None:
        return None
    K2 = dict(K)
    K2["scale"] = S_hero["scale"] * 1.05
    piece = render(DEATH[0], K2, [190.0] * 3, only="head", gore=True).idx
    ys, xs = np.nonzero(piece)
    patch = piece[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    remap = np.arange(16)
    for k in range(3):
        remap[TOP + k] = BOT + k        # black lacquer -> the hero's darkest blues
        remap[BOT + k] = HAIR + k       # the red mask -> her red ramp
        remap[HAIR + k] = SKIN + min(2, k + 1)   # gold horns -> pale gold
    patch = remap[patch]
    patch = patch[::-1, :] if False else patch
    _TROPHY["p"] = patch
    return patch


CHAR_ORDER = ("lieve", "kenji", "hozen", "gonzo", "kagero", "kurogane", "sailor", "morsel")
HEROES = ("lieve", "lieve2", "lieve3")
# animations only some fighters use; the rest get copies of one frame, which the build stores once
ONLY = {"turn": HEROES, "bowf": HEROES, "trophy": HEROES, "chiburi": HEROES, "noto": HEROES,
        "headless": ("kurogane",), "aim": ("morsel",),
        "intro": ("kenji", "hozen", "gonzo", "kagero", "kurogane", "sailor", "morsel"),
        "yield": ("kenji", "hozen", "gonzo", "kagero", "kurogane", "sailor", "morsel")}
BARE = ("bare_idle", "bare_walk", "punch", "pickup")


def load_chars(path=None, with_variants=False):
    """Character specs from tools/characters.json (edited with the designer)."""
    import json, os
    path = path or os.path.join(os.path.dirname(__file__), "characters.json")
    txt = open(path).read().strip()
    if txt.startswith("KOMO CHARACTERS"):
        txt = txt.split("\n", 1)[1]
    d = json.loads(txt)
    out = []
    for name in [n for n in CHAR_ORDER if n in d]:
        S = dict(d[name])
        S["name"] = name
        S["palette"] = [tuple(int(v) for v in c) for c in S["palette"]]
        out.append(S)
        if name == "lieve" and with_variants:     # her look as she climbs: Kenji's headband, then the braid loose and the sleeves torn
            L2 = dict(S, name="lieve2", headband=True)
            L3 = dict(L2, name="lieve3", hair="flowing", tail_len=7.0, torn=True, bangs=True)
            out += [L2, L3]
    return out


def frame_tips(S):
    """Canvas position of the sword tip for every frame (for telegraph glints)."""
    sc = S["scale"]
    out = []
    for name, poses, loop in build_anims(S):
        for pz in poses:
            k = skeleton(pz, S)
            wpn = S.get("weapon", "katana")
            if wpn == "none":
                q = k["hand_f"] + k["bdir"] * 3.0
            elif wpn == "kusarigama":
                en = k["bdir"]
                q = k["tsuba"] + en * 4.0 + v(en[1], -en[0]) * S["blade"] * 0.7 + en * 2.5
            else:
                q = k["tsuba"] + (k["tip"] - k["tsuba"]) * 0.8
            x = int(round(RX + q[0] * sc))
            y = int(round(GY - q[1] * sc))
            out.append((max(0, min(255, x)), max(0, min(255, y))))
    return out


def frame_anchors(S):
    """Canvas chest and thigh points per frame, for cuts in the clothing (0,0 where they don't apply)."""
    sc = S["scale"]
    out = []
    nm_ = S.get("name")
    for name, poses, loop in build_anims(S):
        skip = name in ("turn", "bowf", "trophy", "headless", "noto") or name.startswith("death") or (name in ONLY and nm_ not in ONLY[name])
        for pz in poses:
            if skip:
                out.append((0, 0, 0, 0)); continue
            k = skeleton(pz, S)
            ch = k["waist"] + (k["sh"] - k["waist"]) * 0.55
            th = k["bhip"] + (k["bknee"] - k["bhip"]) * 0.5
            f = lambda q: (max(1, min(255, int(round(RX + q[0] * sc)))), max(1, min(255, int(round(GY - q[1] * sc)))))
            out.append(f(ch) + f(th))
    return out


def render_portrait(S, size=48, zoom=1.4, expr=0):
    """A head-and-shoulders close-up for the dialogue box, facing right, on a dark ground.
    expr: 0 calm, 1 angry, 2 hurt, 3 respectful (the head bowed, eyes closed)."""
    global RX, GY
    old = (RX, GY)
    S2 = dict(S)
    S2["scale"] = S["scale"] * zoom
    S2["_portrait"] = True
    S2["_expr"] = expr
    pose = P(M, head=M["head"] - 9) if expr == 3 else (P(M, head=M["head"] + 4, lean=M["lean"] - 2) if expr == 2 else M)
    k = skeleton(M, S2)
    hx, hy = k["head"][0], k["head"][1]
    sc = S2["scale"]
    # place the head at (size*0.48, size*0.42) inside a crop taken from the canvas origin
    RX = int(size * 0.44 - hx * sc) + 40
    GY = int(size * 0.40 + hy * sc) + 40
    try:
        st = CHAIN_STYLES.get(S2["hair"])
        tails = [(HANG if S2["hair"] in CLAMPED else st[2]) if st else 190.0] * (st[3] if st else 3)
        img = render(pose, S2, tails).idx
    finally:
        RX, GY = old
    crop = img[40:40 + size, 40:40 + size].copy()
    crop[crop == 0] = OUT
    crop[0, :] = STEEL_L; crop[-1, :] = STEEL_L; crop[:, 0] = STEEL_L; crop[:, -1] = STEEL_L
    return crop
