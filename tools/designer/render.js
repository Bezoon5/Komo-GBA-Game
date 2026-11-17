// Kōmō fighter renderer: a line-for-line port of tools/fighters.py so the
// designer preview matches the game's sprites. Works in the browser and Node.
(function (root) {
  const CW = 192, CH = 160, RX = 70, GY = 156;
  const OUT = 1, SKIN = 2, HAIR = 5, TOP = 8, BOT = 11, STEEL_L = 14, STEEL_M = 15;
  const Ln = Math.hypot(-0.45, 0.55, 0.70);
  const L3 = [-0.45 / Ln, 0.55 / Ln, 0.70 / Ln];
  const rad = a => a * Math.PI / 180;
  const v = (x, y) => [x, y];
  const add = (a, b) => [a[0] + b[0], a[1] + b[1]];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1]];
  const mul = (a, s) => [a[0] * s, a[1] * s];

  // anchor (fraction of head radius: forward, up), radius root->tip, spring rest, segments
  const CHAIN_STYLES = {
    ponytail: [[-0.85, 0.35], [3.1, 1.3], 238.0, 5],
    long: [[-0.55, 0.2], [5.0, 2.8], 258.0, 5],
    flowing: [[-0.7, 0.2], [5.8, 2.2], 214.0, 6],
    highpony: [[-0.55, 0.85], [3.4, 1.2], 208.0, 6],
    braid: [[-0.8, 0.0], [2.5, 1.6], 218.0, 7],
    halfup: [[-0.7, 0.2], [4.6, 2.0], 216.0, 6],
    lowpony: [[-0.85, -0.3], [3.8, 1.6], 228.0, 6],
  };
  const CLAMPED = ['flowing', 'highpony', 'braid', 'halfup', 'lowpony'];

  function ik(a, b, l1, l2, bend) {
    const d = sub(b, a);
    let dist = Math.hypot(d[0], d[1]);
    dist = Math.max(Math.min(dist, l1 + l2 - 1e-3), Math.abs(l1 - l2) + 1e-3);
    const base = Math.atan2(d[1], d[0]);
    const ca = (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist);
    const A = Math.acos(Math.max(-1, Math.min(1, ca)));
    const ang = base + bend * A;
    const j = add(a, [l1 * Math.cos(ang), l1 * Math.sin(ang)]);
    const dj = sub(b, j);
    const n = Math.hypot(dj[0], dj[1]) || 1;
    return [j, add(j, mul(dj, l2 / n))];
  }

  class Canvas {
    constructor() { this.idx = new Uint8Array(CW * CH); this.z = new Float64Array(CW * CH).fill(-1e9); this.I = new Float64Array(CW * CH).fill(NaN); }
    grid(x0, y0, x1, y1) {
      x0 = Math.max(0, Math.floor(x0) - 1); y0 = Math.max(0, Math.floor(y0) - 1);
      x1 = Math.min(CW, Math.ceil(x1) + 2); y1 = Math.min(CH, Math.ceil(y1) + 2);
      if (x1 <= x0 || y1 <= y0) return null;
      return [x0, y0, x1, y1];
    }
    cap(p0, p1, r0, r1, mat, z, bias = 0, flat = null, mask = null) {
      const g = this.grid(Math.min(p0[0] - r0, p1[0] - r1), Math.min(p0[1] - r0, p1[1] - r1),
        Math.max(p0[0] + r0, p1[0] + r1), Math.max(p0[1] + r0, p1[1] + r1));
      if (!g) return;
      const dx = p1[0] - p0[0], dy = p1[1] - p0[1], l2 = dx * dx + dy * dy;
      for (let py = g[1]; py < g[3]; py++) for (let px = g[0]; px < g[2]; px++) {
        const xs = px + 0.5, ys = py + 0.5;
        let t = l2 > 1e-9 ? ((xs - p0[0]) * dx + (ys - p0[1]) * dy) / l2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const cx = p0[0] + t * dx, cy = p0[1] + t * dy, r = r0 + (r1 - r0) * t;
        const ox = xs - cx, oy = ys - cy;
        if (Math.sqrt(ox * ox + oy * oy) > r) continue;
        if (mask && !mask(xs, ys)) continue;
        let val, I = NaN;
        if (flat === null) {
          const nx = ox / r, ny = -oy / r;
          const nz = Math.sqrt(Math.min(1, Math.max(0, 1 - nx * nx - ny * ny)));
          I = nx * L3[0] + ny * L3[1] + nz * L3[2] + bias;
          val = mat + (I < 0.28 ? 0 : I < 0.74 ? 1 : 2);
        } else val = flat;
        const i = py * CW + px;
        this.idx[i] = val; this.z[i] = z; this.I[i] = I;
      }
    }
    blade(a, b, w, z, en, lite = STEEL_L, mid = STEEL_M, taper = 0.75) {
      const g = this.grid(Math.min(a[0], b[0]) - w, Math.min(a[1], b[1]) - w, Math.max(a[0], b[0]) + w, Math.max(a[1], b[1]) + w);
      if (!g) return;
      const dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
      for (let py = g[1]; py < g[3]; py++) for (let px = g[0]; px < g[2]; px++) {
        const xs = px + 0.5, ys = py + 0.5;
        let t = ((xs - a[0]) * dx + (ys - a[1]) * dy) / l2;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const ox = xs - (a[0] + t * dx), oy = ys - (a[1] + t * dy);
        const ww = w * (t > 0.85 ? 1 - (t - 0.85) / 0.15 * taper : 1);
        if (Math.sqrt(ox * ox + oy * oy) > ww) continue;
        const i = py * CW + px;
        this.idx[i] = (ox * en[0] + oy * en[1]) >= -0.15 ? lite : mid;
        this.z[i] = z; this.I[i] = NaN;
      }
    }
    finish() {
      const idx = this.idx, z = this.z;
      const filled = new Uint8Array(CW * CH);
      for (let i = 0; i < idx.length; i++) filled[i] = idx[i] !== 0;
      const at = (x, y) => [((x % CW) + CW) % CW, ((y % CH) + CH) % CH]; // wrap like np.roll
      const inner = [], grow = [];
      for (let y = 0; y < CH; y++) for (let x = 0; x < CW; x++) {
        const i = y * CW + x;
        let inn = false, gr = false;
        for (const [dy, dx] of [[0, 1], [0, -1], [1, 0], [-1, 0]]) {
          const [nx, ny] = at(x - dx, y - dy);
          const j = ny * CW + nx;
          if (filled[i] && filled[j] && z[j] < z[i] - 0.5) inn = true;
          if (filled[j]) gr = true;
        }
        if (inn) inner.push(i);
        if (gr && !filled[i] && y > 0 && y < CH - 1 && x > 0 && x < CW - 1) grow.push(i);
      }
      // v5.4 painted shading (see fighters.py Canvas.finish)
      const OCC = [[0, 1], [0, -1], [1, 0], [-1, 0], [0, 2], [0, -2], [2, 0], [-2, 0], [1, 1], [-1, -1], [1, -1], [-1, 1]];
      const nidx = new Uint8Array(idx);
      for (let y = 0; y < CH; y++) for (let x = 0; x < CW; x++) {
        const i = y * CW + x, v = idx[i];
        if (!filled[i] || Number.isNaN(this.I[i]) || v < 2 || v > 13) continue;
        let occl = false;
        for (const [dy, dx] of OCC) {
          const [nx, ny] = at(x - dx, y - dy);
          const j = ny * CW + nx;
          if (filled[j] && z[j] > z[i] + 0.5) { occl = true; break; }
        }
        const Iv = this.I[i] - (occl ? 0.25 : 0);
        const p = Iv <= 0.05 ? 0.0 : Iv <= 0.5 ? (Iv - 0.05) / 0.45 : Iv <= 0.95 ? 1.0 + (Iv - 0.5) / 0.45 : 2.0;
        const fl = Math.floor(p), fr = p - fl;
        let k = (fr > 0.3 && fr < 0.7) ? fl + (((y + x) & 1) === 0 ? 1 : 0) : Math.floor(p + 0.5);
        k = k < 0 ? 0 : k > 2 ? 2 : k;
        nidx[i] = v - (v - 2) % 3 + k;
      }
      idx.set(nidx);
      for (const i of inner) { const v = idx[i]; idx[i] = (v >= 2 && v <= 13) ? v - (v - 2) % 3 : OUT; }
      for (const i of grow) idx[i] = OUT;
    }
  }

  function lerpPose(a, b, t) {
    const o = {};
    for (const k in a) o[k] = (k === 'behind' || k === 'trail') ? (t < 0.5 ? a[k] : b[k]) : a[k] + (b[k] - a[k]) * t;
    return o;
  }

  function skeleton(p, S) {
    const k = {};
    const lean = rad(p.lean), up = v(Math.sin(lean), Math.cos(lean));
    const hip = v(p.hipx, p.crouch);
    k.hip = hip; k.waist = add(hip, mul(up, 11)); k.sh = add(hip, mul(up, S.torso));
    const upr = S.upright ?? 0;
    const hl = rad(p.lean * (0.6 - 0.45 * upr) + p.head), hup = v(Math.sin(hl), Math.cos(hl));
    k.neck = add(k.sh, mul(hup, S.neck));
    k.head = add(add(k.neck, mul(hup, S.head_r * 0.95)), v(0.8 - 0.2 * upr, 0));
    k.hang = hl;
    for (const [side, fx, fl] of [['f', p.ff, p.ffl], ['b', p.bf, p.bfl]]) {
      const hp = add(hip, v(side === 'f' ? 1.5 : -1.5, 0));
      const [knee, ank] = ik(hp, v(fx, 2.6 + fl), S.thigh, S.shin, +1);
      k[side + 'hip'] = hp; k[side + 'knee'] = knee; k[side + 'ank'] = ank;
    }
    const ba = rad(p.ba), dirv = v(Math.cos(ba), Math.sin(ba));
    const hand = add(k.sh, v(p.hx, p.hy));
    k.hand_f = hand; k.tsuba = add(hand, mul(dirv, 2.6));
    k.hand_b = sub(hand, mul(dirv, 6.0)); k.pommel = sub(hand, mul(dirv, 9.5));
    k.tip = add(k.tsuba, mul(dirv, S.blade));
    const wpn = S.weapon ?? 'katana';
    if (wpn === 'dual' || wpn === 'knives') {
      // two weapons: the near hand lets go of the hilt and holds its own blade
      k.pommel = sub(hand, mul(dirv, 3.4));
      const lean_ = rad(p.lean);
      k.hand_b = add(hand, wpn === 'dual' ? v(-7.0 - 2.0 * Math.sin(lean_), -5.5) : v(-3.0, -8.5));
      const a1 = Math.atan2(dirv[1], dirv[0]);
      const a2 = wpn === 'dual' ? a1 + rad(-38.0) : a1 + rad(-80.0);
      const d2 = v(Math.cos(a2), Math.sin(a2));
      k.bdir2 = d2;
      k.tsuba2 = add(k.hand_b, mul(d2, 2.0));
      k.pommel2 = sub(k.hand_b, mul(d2, 3.2));
      k.tip2 = add(k.tsuba2, mul(d2, S.blade * (wpn === 'dual' ? 0.72 : 0.9)));
    }
    k.bdir = dirv;
    const shf = add(k.sh, v(2.2, 0.4 - 2.0 * upr)), shb = add(k.sh, v(-1.8, -0.6 - 2.0 * upr));
    if (wpn === 'rapier') {   // one-handed: the near hand rests by the hip holding a flintlock, or levels it (pose aim)
      k.pommel = sub(hand, mul(dirv, 3.0));
      const rest = add(shb, v(4.0, -(S.uarm + S.farm) * 0.78));
      const aimp = add(shb, v((S.uarm + S.farm) * 0.93, 1.5));
      const t_ = p.aim ?? 0;
      k.hand_b = add(rest, mul(sub(aimp, rest), t_));
      const pa = rad(-62.0 + 62.0 * t_);
      k.pdir = v(Math.cos(pa), Math.sin(pa));
    }
    k.shf = shf; k.shb = shb;
    [k.elf, k.hf] = ik(shf, k.hand_f, S.uarm, S.farm, -1);
    [k.elb, k.hb] = ik(shb, k.hand_b, S.uarm, S.farm, -1);
    return k;
  }

  const HANG = 262.0;
  class Springs {
    constructor(n, rest, hang = false) { this.n = n; this.hang = hang; this.rest = hang ? HANG : rest; this.ang = Array(n).fill(this.rest); this.vel = Array(n).fill(0); this.prev = null; }
    step(head, phase, motion = 0) {
      const vx = this.prev ? head[0] - this.prev[0] : 0, vy = this.prev ? head[1] - this.prev[1] : 0;
      this.prev = head;
      if (this.hang) {
        const sp = vx + motion;
        const trail = Math.max(-8.0, Math.min(70.0, sp * 12.0));
        const lift = Math.max(-20.0, Math.min(20.0, vy * 4.0));
        const amp = 0.5 + 1.8 * Math.min(1.0, Math.abs(sp) / 2.0 + Math.abs(vy) / 3.0);
        let pa = HANG - trail + lift + amp * Math.sin(phase);
        for (let i = 0; i < this.n; i++) {
          const tgt = pa + (i ? 1.2 : 0.0) + amp * 0.6 * Math.sin(phase + i * 0.9);
          this.vel[i] = this.vel[i] * 0.55 + (tgt - this.ang[i]) * 0.45;
          this.ang[i] += this.vel[i];
          pa = this.ang[i];
        }
        return this.ang.slice();
      }
      const target = this.rest + vx * 11.0 - vy * 5.0 + 4.0 * Math.sin(phase);
      let pa = target;
      for (let i = 0; i < this.n; i++) {
        const tgt = pa + (i ? 6.0 : 0) + 2.5 * Math.sin(phase + i * 0.9);
        this.vel[i] = this.vel[i] * 0.55 + (tgt - this.ang[i]) * 0.45;
        this.ang[i] += this.vel[i];
        pa = this.ang[i];
      }
      return this.ang.slice();
    }
  }

  function fillPoly(arr, pts, val) {
    let y0 = Infinity, y1 = -Infinity;
    for (const p of pts) { y0 = Math.min(y0, p[1]); y1 = Math.max(y1, p[1]); }
    for (let y = Math.max(0, Math.floor(y0)); y <= Math.min(CH - 1, Math.ceil(y1)); y++) {
      const yc = y + 0.5, xsArr = [];
      for (let i = 0; i < pts.length; i++) {
        const a = pts[i], b = pts[(i + 1) % pts.length];
        if ((a[1] <= yc && b[1] > yc) || (b[1] <= yc && a[1] > yc))
          xsArr.push(a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
      }
      xsArr.sort((p, q) => p - q);
      for (let i = 0; i + 1 < xsArr.length; i += 2)
        for (let x = Math.max(0, Math.round(xsArr[i])); x <= Math.min(CW - 1, Math.round(xsArr[i + 1]) - 1); x++) arr[y * CW + x] = val;
    }
  }

  function drawTrail(c, pa, pb, S, W) {
    const steps = 6, blades = [];
    for (let i = 0; i <= steps; i++) {
      const K = skeleton(lerpPose(pa, pb, i / steps), S);
      blades.push([add(K.tsuba, mul(sub(K.tip, K.tsuba), 0.7)), K.tip]);
    }
    const m = new Uint8Array(CW * CH);
    for (let i = 0; i < steps; i++) {
      const [a0, a1] = blades[i], [b0, b1] = blades[i + 1];
      fillPoly(m, [W(a0), W(a1), W(b1), W(b0)], 1 + (i >= steps - 3 ? 1 : 0));
    }
    for (let y = 0; y < CH; y++) for (let x = 0; x < CW; x++) {
      const i = y * CW + x;
      if (c.idx[i] !== 0) continue;
      if (m[i] === 2 || (m[i] === 1 && ((x + y) & 1) === 0)) c.idx[i] = ['bokken', 'bo'].includes(S.weapon ?? 'katana') ? SKIN + 2 : STEEL_L;
    }
  }

  function render(p, S, tails, prevP, motion = 0) {
    const c = new Canvas();
    const sc = S.scale, bld = S.build;
    const K = skeleton(p, S);
    const W = q => [RX + q[0] * sc, GY - q[1] * sc];
    const R = r => r * sc;
    const hr = S.head_r, hc = K.head, hang = K.hang;
    const fwd = v(Math.cos(-hang), Math.sin(-hang)), hupv = v(Math.sin(hang), Math.cos(hang));
    const headLocal = (xs, ys) => {
      const X = (xs - RX) / sc - hc[0], Y = (GY - ys) / sc - hc[1];
      return [X * fwd[0] + Y * fwd[1], X * hupv[0] + Y * hupv[1]];
    };
    // the body the long hair rests on (torso and hakama capsules)
    const _fg = S.figure ?? 0, _upr = S.upright ?? 0, _cd = S.chest_depth ?? 1.0;
    const _rw = 9.2 * bld * (1 - 0.22 * _fg) * _cd, _rc = 10.4 * bld * (1 - 0.12 * _fg) * _cd;
    const _ln = rad(p.lean), _up = v(Math.sin(_ln), Math.cos(_ln));
    const _topOld = add(K.sh, mul(sub(K.hip, K.sh), 0.08));
    const _top = add(_topOld, mul(sub(sub(K.sh, mul(_up, _rc - 1.2)), _topOld), _upr));
    const bodyCaps = [[K.waist, _top, _rw, _rc], [K.hip, K.waist, 10.0 * bld * (1 + 0.06 * _fg), 9.4 * bld * (1 - 0.18 * _fg)]];
    const lieOnBack = (q, margin) => {
      for (const [a, b, ra, rb] of bodyCaps) {
        const d = sub(b, a), l2 = d[0] * d[0] + d[1] * d[1];
        const t = l2 < 1e-9 ? 0.0 : Math.max(0.0, Math.min(1.0, ((q[0] - a[0]) * d[0] + (q[1] - a[1]) * d[1]) / l2));
        const cpt = add(a, mul(d, t)), r = ra + (rb - ra) * t + margin;
        const off = sub(q, cpt), dist = Math.hypot(off[0], off[1]);
        if (dist < r && dist > 1e-6) q = add(cpt, mul(off, r / dist));
      }
      return q;
    };
    const lie = CLAMPED.includes(S.hair);
    const tailChain = (anchor, seglen, r0, r1, mat, zz, flat = null, push = false) => {
      let pt = anchor;
      const n = tails.length;
      tails.forEach((a, i) => {
        let nxt = add(pt, mul(v(Math.cos(rad(a)), Math.sin(rad(a))), seglen));
        if (push && i > 0) nxt = lieOnBack(nxt, r0 * 0.5);
        c.cap(W(pt), W(nxt), R(r0 + (r1 - r0) * i / n), R(r0 + (r1 - r0) * (i + 1) / n), mat, (push && i > 0) ? 3.36 : zz, 0, flat);
        pt = nxt;
      });
    };
    const hair = S.hair, band = !!S.headband, fg = S.figure ?? 0;
    if (CLAMPED.includes(hair)) tails = tails.map(a => Math.min(292.0, Math.max(150.0, a)));
    const anchor = add(add(hc, mul(fwd, -hr * 0.85)), mul(hupv, hr * 0.35));
    let tip = null, anchorC = anchor;
    if (CHAIN_STYLES[hair]) {
      const [[au, aw], [r0, r1]] = CHAIN_STYLES[hair];
      let ca = add(add(hc, mul(fwd, hr * au)), mul(hupv, hr * aw));
      if (hair === 'ponytail') ca = anchor;
      if (hair === 'braid') {
        let pt = ca, dv = null;
        const n = tails.length;
        tails.forEach((a, i) => {
          dv = v(Math.cos(rad(a)), Math.sin(rad(a)));
          let nxt = add(pt, mul(dv, S.tail_len));
          const rr = r0 + (r1 - r0) * i / n;
          if (i > 0) {
            nxt = lieOnBack(nxt, rr * 0.6);
            const dd = sub(nxt, pt), ln = Math.hypot(dd[0], dd[1]) || 1.0;
            dv = [dd[0] / ln, dd[1] / ln];
          }
          const pv = v(-dv[1], dv[0]);
          [[0, 0.3], [1, 0.75]].forEach(([j, off]) => {
            const sgn = (i * 2 + j) % 2 === 0 ? 1 : -1;
            const q = add(add(pt, mul(sub(nxt, pt), off)), mul(pv, 0.8 * sgn));
            c.cap(W(q), W(q), R(rr), R(rr), HAIR, i === 0 ? 0 : 3.36);
          });
          pt = nxt;
        });
        tip = pt;
        c.cap(W(pt), W(add(pt, mul(dv, 2.6))), R(1.5), R(0.8), HAIR, 3.36);
      } else tailChain(ca, S.tail_len, r0, r1, HAIR, 0, null, lie);
      anchorC = ca;
    }
    if (band) tailChain(add(add(hc, mul(fwd, -hr * 0.9)), mul(hupv, hr * 0.45)), 4.2, 1.3, 0.9, STEEL_L, 0, STEEL_L);
    if (S.helm) {   // shikoro: lacquered neck guard flaring behind the helmet
      const s0 = add(add(hc, mul(fwd, -hr * 0.5)), mul(hupv, hr * 0.2));
      const s1 = add(add(hc, mul(fwd, -hr * 1.25)), mul(hupv, -hr * 0.9));
      c.cap(W(s0), W(s1), R(hr * 0.95), R(hr * 0.75), TOP, 0.2);
      c.cap(W(add(s1, mul(fwd, 0.5))), W(add(add(s1, mul(fwd, 3.5)), mul(hupv, 1.2))), R(1.0), R(1.0), HAIR, 0.25, 0, HAIR + 1);
    }

    const wpn = S.weapon ?? 'katana';
    const drawSword = zz => {
      const en = K.bdir, edge = v(en[1], -en[0]), edgeC = [edge[0], -edge[1]];
      if (wpn === 'none') return;
      if (wpn === 'bo') {   // long oak staff held near the middle, iron ferrules at both ends
        const back = sub(K.pommel, mul(en, S.staff_back ?? 22));
        c.cap(W(back), W(K.tip), R(1.55), R(1.55), SKIN, zz, 0.1);
        for (const [e0, e1] of [[back, add(back, mul(en, 3.0))], [sub(K.tip, mul(en, 3.0)), K.tip]])
          c.cap(W(e0), W(e1), R(1.7), R(1.7), OUT, zz + 0.05, 0, OUT);
        return;
      }
      if (wpn === 'kusarigama') {   // sickle on a short haft, weighted chain from the butt
        const top = add(K.tsuba, mul(en, 4.0));
        c.cap(W(K.pommel), W(top), R(1.3), R(1.3), SKIN, zz, 0.1);
        const bt = add(add(top, mul(edge, S.blade * 0.85)), mul(en, 3.0));
        c.blade(W(top), W(bt), R(1.5), zz + 0.02, [en[0], -en[1]], STEEL_L, STEEL_M, 0.95);
        c.cap(W(top), W(top), R(1.4), R(1.4), OUT, zz + 0.03, 0, OUT);
        let q = K.pommel;
        const sw = Math.sin(rad(p.ba)) * 4.0;
        for (let i = 1; i < 9; i++) {
          const q2 = add(K.pommel, v(-1.2 * i + sw * (i / 8.0) ** 2, -2.2 * i + 0.09 * i * i));
          c.cap(W(q2), W(q2), R(0.75), R(0.75), STEEL_M, zz - 0.01, 0, (i & 1) ? OUT : STEEL_M);
          q = q2;
        }
        c.cap(W(q), W(q), R(1.9), R(1.9), OUT, zz - 0.02, 0, OUT);
        return;
      }
      if (wpn === 'rapier') {   // long thin blade, a cup hilt and swept quillons
        const tn = v(-en[1], en[0]);
        c.blade(W(K.tsuba), W(K.tip), R(0.85), zz, edgeC, STEEL_L, STEEL_M, 0.6);
        c.cap(W(K.pommel), W(K.tsuba), R(1.1), R(1.1), OUT, zz, 0, OUT);
        c.cap(W(sub(K.pommel, mul(en, 0.8))), W(sub(K.pommel, mul(en, 0.8))), R(1.3), R(1.3), STEEL_L, zz + 0.02, 0, STEEL_L);
        c.cap(W(K.tsuba), W(K.tsuba), R(2.3), R(2.3), STEEL_M, zz + 0.05, 0, STEEL_M);
        const hl_ = sub(add(K.tsuba, mul(tn, 0.8)), mul(en, 0.4));
        c.cap(W(hl_), W(hl_), R(0.8), R(0.8), STEEL_L, zz + 0.055, 0, STEEL_L);
        c.cap(W(sub(sub(K.tsuba, mul(tn, 3.6)), mul(en, 0.6))), W(add(add(K.tsuba, mul(tn, 3.6)), mul(en, 0.6))), R(0.55), R(0.55), OUT, zz + 0.06, 0, OUT);
        return;
      }
      if (wpn === 'cutlass') {  // short broad blade with a knuckle bow
        const tn = v(-en[1], en[0]);
        c.blade(W(K.tsuba), W(K.tip), R(1.8), zz, edgeC, STEEL_L, STEEL_M, 0.9);
        c.cap(W(K.pommel), W(K.tsuba), R(1.6), R(1.6), OUT, zz, 0, OUT);
        c.cap(W(add(K.tsuba, mul(tn, 2.4))), W(add(K.pommel, mul(tn, 2.2))), R(0.6), R(0.6), STEEL_M, zz + 0.05, 0, STEEL_M);
        c.cap(W(sub(K.tsuba, mul(tn, 2.6))), W(add(K.tsuba, mul(tn, 2.6))), R(1.0), R(1.0), OUT, zz + 0.06, 0, OUT);
        return;
      }
      if (wpn === 'dual' || wpn === 'knives') {
        const small = wpn === 'knives';
        for (const [tsu, tp, pom, en_] of [[K.tsuba, K.tip, K.pommel, K.bdir], [K.tsuba2, K.tip2, K.pommel2, K.bdir2]]) {
          const ed = v(en_[1], -en_[0]);
          c.blade(W(tsu), W(tp), R(small ? 1.15 : 1.3), zz, [ed[0], -ed[1]], STEEL_L, STEEL_M, small ? 0.9 : 0.75);
          c.cap(W(pom), W(tsu), R(1.5), R(1.5), OUT, zz, 0, OUT);
          const tn_ = v(-en_[1], en_[0]), g_ = small ? 1.4 : 2.3;
          c.cap(W(sub(tsu, mul(tn_, g_))), W(add(tsu, mul(tn_, g_))), R(0.9), R(0.9), OUT, zz + 0.1, 0, OUT);
          zz = 6.6;   // the near hand's blade sits in front of the body
        }
        return;
      }
      if (wpn === 'bokken') {
        const tn = v(-en[1], en[0]);
        c.blade(W(K.tsuba), W(K.tip), R(1.6), zz, edgeC, SKIN + 2, SKIN + 1, 0.35);
        c.cap(W(K.pommel), W(K.tsuba), R(1.6), R(1.6), SKIN, zz, 0, SKIN);
        c.cap(W(sub(K.tsuba, mul(tn, 1.8))), W(add(K.tsuba, mul(tn, 1.8))), R(0.8), R(0.8), OUT, zz + 0.1, 0, OUT);
        return;
      }
      const bigB = wpn === 'nodachi';
      c.blade(W(K.tsuba), W(K.tip), R(bigB ? 1.6 : 1.35), zz, edgeC);
      c.cap(W(K.pommel), W(K.tsuba), R(1.7), R(1.7), OUT, zz, 0, OUT);
      for (const t of [0.25, 0.55, 0.8]) {
        const q = add(K.pommel, mul(sub(K.tsuba, K.pommel), t));
        c.cap(W(q), W(q), R(0.7), R(0.7), BOT, zz, 0, BOT + 2);
      }
      const tn = v(-en[1], en[0]), gg = bigB ? 3.4 : 2.6;
      c.cap(W(sub(K.tsuba, mul(tn, gg))), W(add(K.tsuba, mul(tn, gg))), R(1.1), R(1.1), OUT, zz + 0.1, 0, OUT);
    };
    if (p.behind) drawSword(0.5);
    const fl = S.flare ?? 1.0;
    // cloth: how fast each limb is moving since the previous frame
    const KP = prevP ? skeleton(prevP, S) : null;
    const vel = key => KP ? sub(K[key], KP[key]) : [0.0, 0.0];
    const spd = q => Math.hypot(q[0], q[1]);
    const vfa = vel('fank'), vba = vel('bank'), vfe = vel('elf'), vbe = vel('elb'), vhp = vel('hip');
    const armMat = S.bare_arms ? SKIN : TOP, armB = S.bare_arms ? -0.2 : -0.25, foreMat = S.gauntlets ? TOP : SKIN;
    const mv = [motion, 0.0];
    const lag = va => { const q = add(mul(va, 0.5), mul(add(vhp, mv), 0.4)); const n = spd(q); return n > 4.5 ? mul(q, 4.5 / n) : q; };
    const lagF = lag(vfa), lagB = lag(vba);
    const hemF = 1.0 + 0.3 * Math.min(1.0, spd(add(vfa, mv)) / 4.0), hemB = 1.0 + 0.3 * Math.min(1.0, spd(add(vba, mv)) / 4.0);
    const slvF = 1.0 + 0.15 * Math.min(1.0, spd(vfe) / 5.0), slvB = 1.0 + 0.15 * Math.min(1.0, spd(vbe) / 5.0);
    c.cap(W(K.fhip), W(K.fknee), R(7.2 * bld), R(7.6 * bld), BOT, 1, -0.22);
    c.cap(W(K.fknee), W(sub(K.fank, lagF)), R(7.6 * bld), R(9.2 * bld * fl * hemF), BOT, 1, -0.22);
    const sf = 1 - 0.15 * fg;
    c.cap(W(add(K.fank, v(0, -0.6))), W(add(K.fank, v(7.5, -1.6))), R(2.3 * sf), R(1.9 * sf), SKIN, 1.05, -0.2);
    if (S.torn) {   // the sleeve torn away above the elbow
      const midF = add(K.shf, mul(sub(K.elf, K.shf), 0.5));
      c.cap(W(midF), W(K.elf), R(3.4 * bld), R(3.2 * bld), SKIN, 2, -0.2);
      c.cap(W(K.shf), W(midF), R(4.6 * bld * (1 - 0.12 * fg)), R(4.4 * bld * (1 - 0.12 * fg)), armMat, 2.01, armB);
    } else c.cap(W(K.shf), W(K.elf), R(4.6 * bld * (1 - 0.12 * fg)), R(4.1 * bld * (1 - 0.12 * fg) * slvF), armMat, 2, armB);
    c.cap(W(K.elf), W(K.hf), R(3.2 * bld), R(2.7 * bld), foreMat, 2, -0.2);
    c.cap(W(K.hf), W(K.hf), R(3.1 * sf), R(3.1 * sf), SKIN, 2, -0.15);
    const upr = S.upright ?? 0, cd = S.chest_depth ?? 1.0;
    const rw = 9.2 * bld * (1 - 0.22 * fg) * cd, rc = 10.4 * bld * (1 - 0.12 * fg) * cd;
    const lean_ = rad(p.lean), up_ = v(Math.sin(lean_), Math.cos(lean_));
    const lean = lean_, up = up_, side = v(up[1], -up[0]);
    if (S.sode) c.cap(W(add(K.shf, mul(up, 1.0))), W(add(sub(K.shf, mul(up, 7.5)), mul(side, 1.0))), R(4.6 * bld), R(5.2 * bld), TOP, 2.05, 0.05);
    c.cap(W(K.hip), W(K.waist), R(10.0 * bld * (1 + 0.06 * fg)), R(9.4 * bld * (1 - 0.18 * fg)), BOT, 3);
    const topOld = add(K.sh, mul(sub(K.hip, K.sh), 0.08));
    const top = add(topOld, mul(sub(sub(K.sh, mul(up_, rc - 1.2)), topOld), upr));
    c.cap(W(K.waist), W(top), R(rw), R(rc), TOP, 3);
    if (S.stripes) {   // a sailor's striped shirt
      for (let k = 0; k < 6; k++) {
        const a0 = add(K.waist, mul(sub(top, K.waist), 0.08 + 0.17 * k));
        c.cap(W(sub(a0, mul(side, rw * 0.9))), W(add(a0, mul(side, rw * 0.9))), R(0.6), R(0.6), TOP, 3.02, 0, TOP);
      }
    }
    if (S.torn) {   // cuts across the keikogi
      for (const [a_, b_] of [[0.25, 0.55], [0.6, 0.8]]) {
        const q0 = add(add(K.waist, mul(sub(top, K.waist), a_)), mul(side, rw * 0.1));
        const q1 = add(add(K.waist, mul(sub(top, K.waist), b_)), mul(side, rw * 0.75));
        c.cap(W(q0), W(q1), R(0.45), R(0.45), OUT, 3.03, 0, OUT);
        c.cap(W(add(q0, mul(up, 0.7))), W(add(q1, mul(up, 0.7))), R(0.4), R(0.4), SKIN, 3.031, 0, SKIN + 1);
      }
    }
    if (S.lames) {   // lacquered armour plates: dark seams and gold lacing
      for (let k = 0; k < 4; k++) {
        const a0 = add(K.waist, mul(sub(top, K.waist), 0.18 + 0.2 * k));
        c.cap(W(sub(a0, mul(side, rw * 0.82))), W(add(a0, mul(side, rw * 0.82))), R(0.45), R(0.45), OUT, 3.02, 0, OUT);
        for (const j of [-0.45, 0.0, 0.45]) {
          const q = add(add(a0, mul(side, rw * j)), mul(up, 1.6));
          c.cap(W(q), W(q), R(0.55), R(0.55), HAIR, 3.03, 0, HAIR + 1);
        }
      }
    }
    if (S.kusazuri) {   // armour skirt hanging from the waist
      const k0 = sub(K.waist, mul(up, 1.0)), k1 = sub(sub(K.hip, mul(up, 8.0)), mul(vhp, 0.5));
      c.cap(W(k0), W(k1), R(10.6 * bld), R(12.0 * bld), TOP, 3.15);
      for (let k = 1; k < 4; k++) {
        const q = add(k0, mul(sub(k1, k0), k / 3.6));
        c.cap(W(sub(q, mul(side, 11.0 * bld))), W(add(q, mul(side, 11.0 * bld))), R(0.45), R(0.45), OUT, 3.16, 0, OUT);
      }
    }
    const ob = K.waist;
    const obi = S.obi ?? 0, ow = 8.5 * bld * (1 - 0.2 * fg);
    if (obi === 0) c.cap(W(sub(ob, mul(side, ow))), W(add(ob, mul(side, ow))), R(1.5), R(1.5), BOT, 3.2, 0, BOT);
    else if (obi === 3) { /* no sash */ }
    else c.cap(W(sub(ob, mul(side, ow))), W(add(ob, mul(side, ow))), R(1.9), R(1.9), BOT, 3.2, 0, obi === 1 ? HAIR + 1 : TOP + 2);
    const ch = S.chest ?? 0;
    if (ch > 0.01) {
      const rb = 2.6 + 3.0 * ch, d = 10.1 * bld * cd + 3.2 * ch - rb;
      const bc = add(sub(K.sh, mul(up, 7.2)), mul(side, d));
      c.cap(W(add(bc, mul(up, 0.8))), W(sub(bc, mul(up, 0.6))), R(rb), R(rb), TOP, 3.05);
    }
    if (S.kesa) {   // monk's stole over one shoulder, with prayer beads
      const k0 = add(add(K.sh, mul(side, 2.0)), mul(up, 0.5)), k1 = add(add(K.waist, mul(side, rw * 0.9)), mul(up, 1.0));
      c.cap(W(k0), W(k1), R(2.6), R(2.6), HAIR, 3.25, 0.1);
      for (let n = 0; n < 7; n++) {
        const t_ = 0.08 + (0.92 - 0.08) * n / 6;
        const q = sub(add(K.sh, mul(side, 1.5 + 5.5 * t_)), mul(up, 2.0 + 11.0 * t_ * (1.15 - t_)));
        c.cap(W(q), W(q), R(0.9), R(0.9), OUT, 3.42, 0, OUT);
      }
    }
    const nb = sub(sub(K.sh, mul(up, 1.5 * upr)), mul(side, 1.2 * upr));
    c.cap(W(nb), W(add(K.neck, v(0, 0.5))), R(3.4 * (1 - 0.25 * fg) * (1 + 0.3 * upr)), R(3.0 * (1 - 0.25 * fg) * (1 - 0.1 * upr)), SKIN, 3.3);
    if (S.ruff) {   // a starched white ruff around the neck
      const neckTop = add(K.neck, v(0, 0.5)), rq = add(nb, mul(sub(neckTop, nb), 0.25));
      c.cap(W(sub(rq, mul(side, 1.2))), W(add(rq, mul(side, 3.2))), R(3.9), R(3.6), STEEL_L, 3.36, 0, STEEL_L);
      c.cap(W(sub(sub(rq, mul(side, 0.8)), mul(up, 1.6))), W(sub(add(rq, mul(side, 2.8)), mul(up, 1.6))), R(1.6), R(1.4), STEEL_L, 3.361, 0, STEEL_M);
      for (let k = -2; k < 3; k++) {
        const q = sub(add(rq, mul(side, 1.0 * k + 1.0)), mul(up, 2.6));
        c.cap(W(q), W(add(q, mul(up, 1.0))), R(0.35), R(0.35), STEEL_L, 3.37, 0, TOP + 2);
      }
    }
    if (S.vneck) c.cap(W(add(add(K.sh, mul(up, 1.0)), mul(side, 1.6))), W(add(sub(K.sh, mul(up, 4.0)), mul(side, rc * 0.68))), R(1.6), R(0.8), SKIN, 3.35);
    const lap0 = add(K.sh, v(2.5, 1.0)), lap1 = add(sub(K.sh, mul(up, 12)), mul(side, 4));
    c.cap(W(lap0), W(lap1), R(0.9), R(0.9), TOP, 3.4, 0, TOP);
    // head
    c.cap(W(hc), W(hc), R(hr), R(hr), SKIN, 4);
    const big = !!CHAIN_STYLES[hair];
    const hcap = add(add(hc, mul(fwd, big ? -1.2 : -0.8)), mul(hupv, big ? 1.0 : 0.9));
    const hairMask = (xs, ys) => { const [u, w] = headLocal(xs, ys); return !(u > -1.6 && w < 2.6); };
    if (hair !== 'bald') { const rr = R(hr + (big ? 0.9 : 0.5)); c.cap(W(hcap), W(hcap), rr, rr, HAIR, 4.1, 0, null, hairMask); }
    if (band) c.cap(W(hc), W(hc), R(hr + 0.6), R(hr + 0.6), STEEL_L, 4.2, 0, STEEL_L, (xs, ys) => { const w = headLocal(xs, ys)[1]; return w > 2.9 && w < 4.6; });
    if (hair === 'topknot') {
      const tk0 = add(add(hc, mul(hupv, hr + 0.2)), mul(fwd, -1.5)), tk1 = add(add(tk0, mul(fwd, 4.0)), mul(hupv, 1.2));
      c.cap(W(tk0), W(tk1), R(2.0), R(1.6), HAIR, 4.3);
    } else if (hair === 'bun') {
      const bc = add(add(hc, mul(fwd, -hr * 0.85)), mul(hupv, hr * 0.65));
      c.cap(W(bc), W(bc), R(3.0), R(3.0), HAIR, 4.3);
    } else if (hair === 'ponytail') c.cap(W(anchor), W(anchor), R(1.6), R(1.6), TOP, 4.2, 0, TOP);
    else if (hair === 'halfup') {
      const bc = add(add(hc, mul(fwd, -hr * 0.7)), mul(hupv, hr * 0.75));
      c.cap(W(bc), W(bc), R(2.6), R(2.6), HAIR, 4.3);
    }
    if (hair === 'highpony' || hair === 'lowpony') {
      for (const k of [-1, 1]) {
        const q = add(add(anchorC, mul(fwd, 1.7 * k)), mul(hupv, 0.9));
        c.cap(W(anchorC), W(q), R(1.0), R(1.3), TOP, 4.25, 0, TOP + 2);
      }
      c.cap(W(anchorC), W(anchorC), R(1.1), R(1.1), TOP, 4.26, 0, TOP);
    }
    if (hair === 'braid' && tip) c.cap(W(tip), W(tip), R(1.4), R(1.4), TOP, 4.25, 0, TOP + 2);
    if (S.bangs && hair !== 'bald') {
      const rb = R(hr + (big ? 0.9 : 0.5));
      c.cap(W(hcap), W(hcap), rb, rb, HAIR, 4.15, 0, null, (xs, ys) => {
        const [u, w] = headLocal(xs, ys);
        const zig = (Math.floor(u * 1.2) & 1) * 0.9;
        return u > -1.6 && w >= 1.3 + zig;
      });
    }
    if (S.sidelocks && hair !== 'bald') {
      const s0 = add(add(hc, mul(fwd, 0.6)), mul(hupv, 1.8)), s1 = add(add(hc, mul(fwd, 1.6)), mul(hupv, -7.5));
      c.cap(W(s0), W(s1), R(1.7), R(0.9), HAIR, 4.22);
    }
    if (S.ears && !S.helm && !S.hood) {   // the ear, just behind the middle of the head
      const ea = add(add(hc, mul(fwd, -hr * 0.12)), mul(hupv, -0.4));
      c.cap(W(add(ea, mul(hupv, 1.1))), W(sub(ea, mul(hupv, 1.0))), R(1.7), R(1.4), SKIN, 4.08, 0, SKIN);
      c.cap(W(add(ea, mul(hupv, 0.6))), W(sub(ea, mul(hupv, 0.5))), R(0.95), R(0.8), SKIN, 4.085, 0, SKIN + 1);
      const [ix, iy] = W(add(ea, mul(fwd, 0.35)));
      const xi = Math.trunc(ix), yi = Math.trunc(iy);
      if (xi >= 0 && xi < CW && yi >= 0 && yi < CH) { c.idx[yi * CW + xi] = OUT; c.z[yi * CW + xi] = 4.09; }
    }
    const nose = add(add(hc, mul(fwd, hr + 0.4)), mul(hupv, 0.2));
    c.cap(W(nose), W(nose), R(1.1), R(1.1), SKIN, 4.05, 0, SKIN + 1);
    const [ex, ey] = W(add(add(hc, mul(fwd, hr * 0.62)), mul(hupv, 1.3)));
    const exi = Math.trunc(ex), eyi = Math.trunc(ey);
    if (exi >= 0 && exi < CW && eyi >= 0 && eyi < CH) {
      c.idx[eyi * CW + exi] = OUT; c.z[eyi * CW + exi] = 4.06;
      if ((S.eyes ?? 0) === 1) for (const [qx, qy] of [[exi, eyi - 1], [exi + 1, eyi - 1]])
        if (qx >= 0 && qx < CW && qy >= 0 && qy < CH) { c.idx[qy * CW + qx] = OUT; c.z[qy * CW + qx] = 4.06; }
    }
    if (S.lips) {
      const [mx, my] = W(add(add(hc, mul(fwd, hr * 0.9)), mul(hupv, -2.4)));
      const mxi = Math.trunc(mx), myi = Math.trunc(my);
      if (mxi >= 0 && mxi < CW && myi >= 0 && myi < CH) { c.idx[myi * CW + mxi] = HAIR + 1; c.z[myi * CW + mxi] = 4.06; }
    }
    if (S.hood) {   // cloth hood over head and face, open at the eyes
      const hq = add(hc, mul(hupv, 0.4));
      c.cap(W(hq), W(hq), R(hr + 0.8), R(hr + 0.8), TOP, 4.4, 0, null, (xs, ys) => { const [u, w] = headLocal(xs, ys); return !(u > 1.2 && w > 0.0 && w < 2.8); });
      c.cap(W(sub(sub(hc, mul(fwd, 2.0)), mul(hupv, 3.5))), W(sub(sub(hc, mul(fwd, 5.5)), mul(hupv, 7.0))), R(2.6), R(1.6), TOP, 4.39);
    }
    if (S.menpo) {   // red lacquer face mask over the lower face
      const mq = add(hc, mul(fwd, 0.6));
      c.cap(W(mq), W(mq), R(hr + 0.4), R(hr + 0.4), BOT, 4.45, 0.15, null, (xs, ys) => { const [u, w] = headLocal(xs, ys); return u > -0.5 && w < 0.6; });
      const mt = add(add(hc, mul(fwd, hr * 0.85)), mul(hupv, -2.4));
      c.cap(W(sub(mt, mul(fwd, 1.6))), W(add(mt, mul(fwd, 0.6))), R(0.5), R(0.5), OUT, 4.46, 0, OUT);
      for (const k of [-1, 1]) {   // bristling moustache
        const q = add(add(hc, mul(fwd, hr * 0.95)), mul(hupv, -1.0));
        c.cap(W(q), W(add(add(q, mul(fwd, 1.8)), mul(hupv, 0.8 * k - 0.6))), R(0.5), R(0.4), STEEL_L, 4.47, 0, STEEL_L);
      }
    }
    if (S.beard) {   // moustache and pointed beard
      for (const k of [-1, 1]) {
        const q = add(add(hc, mul(fwd, hr * 0.92)), mul(hupv, -1.2));
        c.cap(W(q), W(sub(add(add(q, mul(fwd, 1.4)), mul(hupv, -0.6 + 0.9 * k)), mul(fwd, k < 0 ? 1.6 : 0))), R(0.6), R(0.45), HAIR, 4.46, 0, HAIR);
      }
      c.cap(W(add(add(hc, mul(fwd, hr * 0.7)), mul(hupv, -hr * 0.7))), W(add(add(hc, mul(fwd, hr * 0.85)), mul(hupv, -hr * 1.15))), R(1.4), R(0.5), HAIR, 4.46);
    }
    if (S.knitcap) {   // a sailor's wool cap
      const cq = sub(add(hc, mul(hupv, 1.2)), mul(fwd, 0.6));
      c.cap(W(cq), W(cq), R(hr + 1.0), R(hr + 1.0), BOT, 4.5, 0.1, null, (xs, ys) => headLocal(xs, ys)[1] > 3.1);
      c.cap(W(sub(add(hc, mul(hupv, 3.6)), mul(fwd, hr + 0.2))), W(add(add(hc, mul(hupv, 3.6)), mul(fwd, hr - 0.6))), R(1.1), R(1.1), BOT, 4.51, 0, BOT + 2);
      const q = sub(add(hc, mul(hupv, hr + 1.6)), mul(fwd, 1.5));
      c.cap(W(q), W(sub(sub(q, mul(fwd, 3.0)), mul(hupv, 0.6))), R(1.6), R(1.2), BOT, 4.52);
    }
    if (S.hat) {   // a Dutch capotain: tall black crown, wide brim, gilt buckle
      const br = add(hc, mul(hupv, hr * 0.78));
      c.cap(W(sub(br, mul(fwd, hr * 1.75))), W(add(br, mul(fwd, hr * 1.8))), R(1.2), R(1.0), TOP, 4.6, 0.1);
      c.cap(W(sub(add(br, mul(hupv, hr * 0.8)), mul(fwd, 0.3))), W(sub(add(br, mul(hupv, hr * 1.5)), mul(fwd, 0.9))), R(hr * 0.8), R(hr * 0.7), TOP, 4.61, 0.15);
      c.cap(W(sub(add(br, mul(hupv, 1.6)), mul(fwd, hr * 0.85))), W(add(add(br, mul(hupv, 1.6)), mul(fwd, hr * 0.8))), R(0.8), R(0.8), HAIR, 4.62, 0, HAIR + 2);
      const bk = add(add(br, mul(hupv, 1.6)), mul(fwd, hr * 0.5));
      c.cap(W(bk), W(bk), R(1.0), R(1.0), STEEL_L, 4.63, 0, STEEL_L);
    }
    if (S.helm) {   // kabuto bowl with gold kuwagata horns
      const hb_ = sub(add(hc, mul(hupv, 0.6)), mul(fwd, 0.4));
      c.cap(W(hb_), W(hb_), R(hr + 1.4), R(hr + 1.4), TOP, 4.5, 0.1, null, (xs, ys) => headLocal(xs, ys)[1] > 1.2);
      c.cap(W(sub(add(hc, mul(hupv, 1.6)), mul(fwd, hr + 1.6))), W(add(add(hc, mul(hupv, 1.6)), mul(fwd, hr + 1.2))), R(1.0), R(1.0), OUT, 4.52, 0, OUT);
      const base = add(add(hc, mul(fwd, hr * 0.75)), mul(hupv, hr * 0.55));
      for (const [k, lh] of [[1, 0.55], [-1, -0.2]]) {
        const h1 = add(add(base, mul(hupv, 5.0)), mul(fwd, 2.5 * lh));
        const h2 = add(add(h1, mul(hupv, 6.5)), mul(fwd, 4.0 * lh - 1.5));
        c.cap(W(base), W(h1), R(1.1), R(0.9), HAIR, 4.55 + 0.01 * k);
        c.cap(W(h1), W(h2), R(0.9), R(0.5), HAIR, 4.55 + 0.01 * k);
      }
      c.cap(W(base), W(base), R(1.6), R(1.6), HAIR, 4.57, 0, HAIR + 2);
    }
    c.cap(W(K.bhip), W(K.bknee), R(7.2 * bld), R(7.6 * bld), BOT, 5);
    c.cap(W(K.bknee), W(sub(K.bank, lagB)), R(7.6 * bld), R(9.2 * bld * fl * hemB), BOT, 5);
    c.cap(W(add(K.bank, v(0, -0.6))), W(add(K.bank, v(7.5, -1.6))), R(2.3 * sf), R(1.9 * sf), SKIN, 5.05);
    if (!p.behind) drawSword(6);
    if (S.torn) {
      const midB = add(K.shb, mul(sub(K.elb, K.shb), 0.55));
      c.cap(W(midB), W(K.elb), R(3.5 * bld), R(3.3 * bld), SKIN, 7);
      c.cap(W(K.shb), W(midB), R(4.8 * bld * (1 - 0.12 * fg)), R(4.6 * bld * (1 - 0.12 * fg)), armMat, 7.01);
      for (const j of [-1, 1]) {   // ragged hem
        const q = add(midB, v(j * 2.2, -0.6 * j));
        c.cap(W(q), W(add(q, v(0.3, -1.4))), R(0.6), R(0.4), armMat, 7.02, 0, TOP);
      }
    } else c.cap(W(K.shb), W(K.elb), R(4.8 * bld * (1 - 0.12 * fg)), R(4.3 * bld * (1 - 0.12 * fg) * slvB), armMat, 7);
    c.cap(W(K.elb), W(K.hb), R(3.3 * bld), R(2.8 * bld), foreMat, 7);
    c.cap(W(K.hb), W(K.hb), R(3.2 * sf), R(3.2 * sf), SKIN, 7.1);
    if (S.pistol) {   // the flintlock in the near hand
      const pd = K.pdir, pn = v(-pd[1], pd[0]), hb = K.hb;
      c.cap(W(sub(hb, mul(pd, 0.5))), W(sub(sub(hb, mul(pd, 2.6)), mul(pn, 5.0))), R(1.9), R(1.6), HAIR, 7.15);
      c.cap(W(sub(hb, mul(pd, 1.0))), W(add(hb, mul(pd, 12.5))), R(1.25), R(1.05), OUT, 7.16, 0, OUT);
      c.cap(W(add(add(hb, mul(pd, 1.5)), mul(pn, 0.6))), W(add(add(hb, mul(pd, 11.5)), mul(pn, 0.6))), R(0.45), R(0.4), STEEL_L, 7.17, 0, STEEL_M);
      c.cap(W(add(hb, mul(pd, 12.0))), W(add(hb, mul(pd, 12.0))), R(1.5), R(1.5), OUT, 7.165, 0, OUT);
      c.cap(W(add(add(hb, mul(pn, 2.2)), mul(pd, 0.8))), W(add(add(hb, mul(pn, 2.6)), mul(pd, 2.8))), R(0.8), R(0.6), STEEL_L, 7.18, 0, STEEL_L);
    }
    if (S.sode) {   // near shoulder plate over the arm
      c.cap(W(add(K.shb, mul(up, 1.2))), W(add(sub(K.shb, mul(up, 7.0)), mul(side, -1.0))), R(4.8 * bld), R(5.6 * bld), TOP, 7.3, 0.1);
      c.cap(W(sub(sub(K.shb, mul(up, 3.0)), mul(side, 4.8 * bld))), W(add(sub(K.shb, mul(up, 3.0)), mul(side, 4.8 * bld))), R(0.45), R(0.45), OUT, 7.31, 0, OUT);
    }
    c.finish();
    if (p.trail && prevP) drawTrail(c, prevP, p, S, W);
    return c.idx;
  }

  // Render one animation (springs restart per animation, exactly as the build does).
  function renderAnim(anim, S) {
    const st = CHAIN_STYLES[S.hair];
    const rest = st ? st[2] : 190.0;
    const nseg = st ? st[3] : 3;
    const spr = new Springs(nseg, rest, CLAMPED.includes(S.hair));
    const mot = anim.motion || 0;
    const poses = (anim.by && S.name && anim.by[S.name]) || anim.poses, frames = [];
    const passes = anim.loop ? 3 : 1;
    if (!anim.loop) for (let i = 0; i < 4; i++) spr.step(skeleton(poses[0], S).head, 0);
    for (let ps = 0; ps < passes; ps++) poses.forEach((pz, i) => {
      const tails = spr.step(skeleton(pz, S).head, i * 1.1, mot);
      if (ps === passes - 1) {
        let prevP = i > 0 ? poses[i - 1] : (anim.loop ? poses[poses.length - 1] : null);
        if (anim.name.startsWith('strike') && i === 3) prevP = poses[2];
        frames.push(render(pz, S, tails, prevP, mot));
      }
    });
    return frames;
  }

  const api = { CW, CH, RX, GY, renderAnim, render, skeleton };
  if (typeof module !== 'undefined') module.exports = api; else root.KomoRender = api;
})(this);
