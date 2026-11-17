/* Desktop harness: runs the game code against a software model of the GBA
 * display (mode 0 text BGs, regular + affine sprites, blending, per-line
 * BG2 scroll) with a scripted bot player, and dumps frames as PPM. */
#define HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define main game_main
#include "../src/main.c"
#undef main

u8 host_io[0x400];
u16 host_pal[512];
u8 host_vram[0x18000];
u16 host_oam[512];
static const u16 *hdma_tab;
static long frame_no, frame_limit = 600;
static int dump_every = 0;
static const char *dump_dir = "out/frames";
static u8 screen[160][240][3];
static FILE *log_f;

void host_set_hdma(const u16 *t) { hdma_tab = t; }
static const u16 *hdma3_tab;
void host_set_hdma3(const u16 *t) { hdma3_tab = t; }
static const char *stname[] = {"IDLE","TELL","WINDUP","SWING","FOLLOW","RECOVER","GUARD","HURT","STAGGER","RECOIL","DODGE","DASH","SUPER","STRAFE","PICKUP","AIM","JUMP","CROUCH","LOCK","DYING","DEATH","DEAD","POSE","VICTORY"};
static int hsplit_y = -1; static const u16 *split_p;
void host_set_split(int y, const u16 *p) { hsplit_y = (y > 0 && y < 160) ? y : -1; split_p = p; }
static const u16 *hdma2_tab;
void host_set_hdma2(const u16 *t) { hdma2_tab = t; }

static u16 bg_pixel(int bg, int x, int y, int *ok) {
    u16 cnt = REG_BGCNT(bg);
    int cb = (cnt >> 2) & 3, sb = (cnt >> 8) & 31, size = cnt >> 14;
    int w = (size & 1) ? 512 : 256, h = (size & 2) ? 512 : 256;
    int hofs = REG_BGHOFS(bg), vofs = REG_BGVOFS(bg);
    if (bg == 2 && hdma_tab) hofs = hdma_tab[y];
    if (bg == 3 && hdma3_tab) hofs = hdma3_tab[y];
    int tx = (x + hofs) & (w - 1), ty = (y + vofs) & (h - 1);
    int sbi = sb + (tx >> 8) + ((ty >> 8) * (w >> 8));
    u16 e = ((u16 *)(host_vram + sbi * 0x800))[((ty & 255) >> 3) * 32 + ((tx & 255) >> 3)];
    int tile = e & 1023, px = tx & 7, py = ty & 7;
    if (e & 0x400) px = 7 - px;
    if (e & 0x800) py = 7 - py;
    u8 b = host_vram[cb * 0x4000 + tile * 32 + py * 4 + (px >> 1)];
    int c = (px & 1) ? b >> 4 : b & 15;
    *ok = c != 0;
    return (e >> 12) * 16 + c;
}

long host_cyc_drop;   /* scanlines on which the sprite budget ran out */
static void render(void) {
    static const int SW[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const int SH[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    u16 disp = REG_DISPCNT;
    u16 bld = REG_BLDCNT;
    int mode = (bld >> 6) & 3;
    int eva = REG_BLDALPHA & 31, evb = (REG_BLDALPHA >> 8) & 31, evy = REG_BLDY & 31;
    if (eva > 16) eva = 16; if (evb > 16) evb = 16; if (evy > 16) evy = 16;
    for (int y = 0; y < 160; y++) {
        /* sprite line buffer */
        int objc[240], objp[240], objsemi[240];
        for (int x = 0; x < 240; x++) { objc[x] = -1; objp[x] = 9; objsemi[x] = 0; }
        /* the hardware's per-line sprite budget: 1210 cycles (954 with H-blank free), spent in OAM order;
           a normal sprite costs its width, an affine one 10 + 2 x its box width. Sprites past it vanish. */
        static u8 cut[128];
        {   int budget = (disp & 0x20) ? 954 : 1210;
            for (int i = 0; i < 128; i++) {
                u16 a0 = host_oam[i * 4], a1 = host_oam[i * 4 + 1];
                int aff = a0 & 0x100; cut[i] = 0;
                if (!aff && (a0 & 0x200)) continue;
                int shape = a0 >> 14, sz = a1 >> 14; if (shape == 3) continue;
                int w = SW[shape][sz], h = SH[shape][sz], bw = w, bh = h;
                if (aff && (a0 & 0x200)) { bw *= 2; bh *= 2; }
                int sy = a0 & 255; if (sy >= 160) sy -= 256;
                if (y < sy || y >= sy + bh) continue;
                int c = aff ? 10 + 2 * bw : w;
                if (budget < c) { cut[i] = 1; budget = 0; continue; }
                budget -= c;
            }
            if (getenv("NOCYC")) for (int i = 0; i < 128; i++) cut[i] = 0;
            for (int i = 0; i < 128; i++) if (cut[i] && (host_oam[i * 4 + 2] & 1023) < 960) { host_cyc_drop++; break; }   /* fighters only */
            static long dbgf = -1; int dbg_now = 0;
            if (getenv("CYCFRAME") && frame_no == atol(getenv("CYCFRAME")) && dbgf != frame_no) { for (int i = 0; i < 128; i++) if (cut[i]) { dbg_now = 1; dbgf = frame_no; fprintf(stderr, "line %d\n", y); break; } }
            if (dbg_now || (getenv("CYCDBG") && y == atoi(getenv("CYCDBG")))) { for (int i = 0; i < 128; i++) { u16 a0 = host_oam[i*4], a1 = host_oam[i*4+1]; if ((a0 & 0x300) == 0x200) continue; fprintf(stderr, "%d%s a0 %04x a1 %04x a2 %04x\n", i, cut[i] ? " CUT" : "", a0, a1, host_oam[i*4+2]); } }
        }
        if (disp & 0x1000) {
            for (int i = 127; i >= 0; i--) {
                u16 a0 = host_oam[i * 4], a1 = host_oam[i * 4 + 1], a2 = host_oam[i * 4 + 2];
                int aff = a0 & 0x100;
                if (cut[i]) continue;
                if (!aff && (a0 & 0x200)) continue;
                int shape = a0 >> 14, sz = a1 >> 14;
                if (shape == 3) continue;
                int w = SW[shape][sz], h = SH[shape][sz];
                int bw = w, bh = h;
                if (aff && (a0 & 0x200)) { bw *= 2; bh *= 2; }
                int sy = a0 & 255, sx = a1 & 511;
                if (sy >= 160) sy -= 256;
                if (sx >= 240) sx -= 512;
                if (y < sy || y >= sy + bh) continue;
                int tile = a2 & 1023, prio = (a2 >> 10) & 3, pal = a2 >> 12;
                int semi = ((a0 >> 10) & 3) == 1;
                int mos = (a0 & 0x1000) != 0;
                int mosh = ((REG_MOSAIC >> 8) & 15) + 1, mosv = ((REG_MOSAIC >> 12) & 15) + 1;
                int yy_ = mos ? y - ((y - sy) % mosv) : y;
                int pa = 256, pb = 0, pc = 0, pd = 256;
                if (aff) {
                    int g = (a1 >> 9) & 31;
                    pa = (s16)host_oam[g * 16 + 3]; pb = (s16)host_oam[g * 16 + 7];
                    pc = (s16)host_oam[g * 16 + 11]; pd = (s16)host_oam[g * 16 + 15];
                }
                for (int bx = 0; bx < bw; bx++) {
                    int x = sx + bx;
                    if (x < 0 || x >= 240) continue;
                    int tx, ty;
                    int bxm = mos ? bx - (bx % mosh) : bx;
                    if (aff) {
                        int dx = bxm - bw / 2, dy = (yy_ - sy) - bh / 2;
                        tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                        ty = ((pc * dx + pd * dy) >> 8) + h / 2;
                        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
                    } else {
                        tx = bxm; ty = yy_ - sy;
                        if (a1 & 0x1000) tx = w - 1 - tx;
                        if (a1 & 0x2000) ty = h - 1 - ty;
                    }
                    int t = tile + (ty >> 3) * (w >> 3) + (tx >> 3);
                    u8 b = host_vram[0x10000 + (t & 1023) * 32 + (ty & 7) * 4 + ((tx & 7) >> 1)];
                    int c = (tx & 1) ? b >> 4 : b & 15;
                    if (!c) continue;
                    if (prio <= objp[x]) { objc[x] = 256 + pal * 16 + c; objp[x] = prio; objsemi[x] = semi; }
                }
            }
        }
        if (getenv("DBGLINE") && y == atoi(getenv("DBGLINE"))) for (int x = 50; x < 72; x += 2) fprintf(stderr, "x%d objc %d p %d s %d\n", x, objc[x], objp[x], objsemi[x]);
        for (int x = 0; x < 240; x++) {
            /* collect layers: (prio, kind, colour) kind 0-3 BG, 4 OBJ, 5 backdrop */
            int top = -1, topk = 5, topp = 9, sec = -1, seck = 5, secp = 9;
            int layers_c[6], layers_k[6], layers_p[6], n = 0;
            int wmask = 0x3F;                  /* windows: which layers and whether effects show here */
            if (disp & 0x6000) {
                u16 w0 = hdma2_tab ? hdma2_tab[y] : REG_WIN0H;
                int in0 = (disp & 0x2000) && x >= (w0 >> 8) && x < (w0 & 255) && y >= (REG_WIN0V >> 8) && y < (REG_WIN0V & 255);
                u16 w1 = REG_WIN1H;
                int in1 = (disp & 0x4000) && x >= (w1 >> 8) && x < (w1 & 255) && y >= (REG_WIN1V >> 8) && y < (REG_WIN1V & 255);
                wmask = in0 ? (REG_WININ & 0x3F) : in1 ? ((REG_WININ >> 8) & 0x3F) : (REG_WINOUT & 0x3F);
            }
            if (objc[x] >= 0 && (wmask & 0x10)) { layers_c[n] = objc[x]; layers_k[n] = 4; layers_p[n] = objp[x]; n++; }
            for (int bg = 0; bg < 4; bg++) {
                if (!(disp & (0x100 << bg)) || !(wmask & (1 << bg))) continue;
                int ok; u16 c = bg_pixel(bg, x, y, &ok);
                if (!ok) continue;
                layers_c[n] = c; layers_k[n] = bg; layers_p[n] = REG_BGCNT(bg) & 3; n++;
            }
            layers_c[n] = 0; layers_k[n] = 5; layers_p[n] = 4; n++;
            for (int pass = 0; pass < 2; pass++) {
                int best = -1;
                for (int i = 0; i < n; i++) {
                    if (pass == 1 && i == top) continue;
                    if (best < 0) { best = i; continue; }
                    int p = layers_p[i], bp = layers_p[best];
                    int k = layers_k[i], bk = layers_k[best];
                    int better = p < bp || (p == bp && ((k == 4 && bk != 4) || (k != 4 && bk != 4 && k < bk)));
                    if (better) best = i;
                }
                if (pass == 0) top = best; else sec = best;
            }
            if (sec < 0) sec = top;
            topk = layers_k[top]; seck = layers_k[sec];
            (void)topp; (void)secp;
            const u16 *hp = (hsplit_y > 0 && y >= hsplit_y && split_p) ? split_p : host_pal;
            u16 c1 = hp[layers_c[top]], c2 = hp[layers_c[sec]];
            int r = c1 & 31, g = (c1 >> 5) & 31, b = (c1 >> 10) & 31;
            int first = (bld >> topk) & 1, second = (bld >> (8 + seck)) & 1;
            if (!(wmask & 0x20)) first = second = 0;
            if (topk == 4 && objsemi[x] && second) {
                int r2 = c2 & 31, g2 = (c2 >> 5) & 31, b2 = (c2 >> 10) & 31;
                r = (r * eva + r2 * evb) >> 4; g = (g * eva + g2 * evb) >> 4; b = (b * eva + b2 * evb) >> 4;
            } else if (mode == 1 && first && second) {
                int r2 = c2 & 31, g2 = (c2 >> 5) & 31, b2 = (c2 >> 10) & 31;
                r = (r * eva + r2 * evb) >> 4; g = (g * eva + g2 * evb) >> 4; b = (b * eva + b2 * evb) >> 4;
            } else if (mode == 2 && first) {
                r += ((31 - r) * evy) >> 4; g += ((31 - g) * evy) >> 4; b += ((31 - b) * evy) >> 4;
            } else if (mode == 3 && first) {
                r -= (r * evy) >> 4; g -= (g * evy) >> 4; b -= (b * evy) >> 4;
            }
            if (getenv("DBGLINE") && y == atoi(getenv("DBGLINE")) && x == 62) fprintf(stderr, "top k%d c1 %04x sec k%d c2 %04x eva %d evb %d -> %d %d %d\n", topk, c1, seck, c2, eva, evb, r, g, b);
            if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
            screen[y][x][0] = r << 3 | r >> 2; screen[y][x][1] = g << 3 | g >> 2; screen[y][x][2] = b << 3 | b >> 2;
        }
    }
}

/* ------------------------------------------------------------ bot */
/* Bots play with human reaction times: they only see the opponent's state
 * after a delay, make mistakes, and press buttons as presses (not holds). */
static int bot_mode;     /* 0 decent, 1 passive, 3 masher, 4 novice, 5 tutorial follower, 9 idle */
static int bot_diff;     /* difficulty to choose in the menu */
static u16 bot_keys(void) {
    static int pulse, see_t, plan, plan_h, hold_b, mash_t, last_seen_st = -1;
    static u16 seq[4]; static int seq_n;
    pulse++;
    u16 k = 0;
    if (seq_n > 0) { u16 r = seq[0]; for (int i = 0; i < 3; i++) seq[i] = seq[i + 1]; seq_n--; return r; }
    if (bot_mode == 9) return 0;
    if (scene == S_DUEL && fp.st == ST_LOCK) { static int lk; if (getenv("LOCKLOG") && lk++ % 30 == 0) fprintf(stderr, "lock t%d pos %d\n", lock_t, lock_pos); return (pulse % 7) == 0 ? KEY_A : 0; }   /* a human tapping about 8 times a second */
    if (bot_mode == 11 && scene == S_DUEL && duel_on) {   /* the new pad, scripted */
        static int t; t++;
        if (t > 440) { fe.st = ST_IDLE; fe.t = 0; fe.x = fp.x + 150 * 256 * fp.dir; fe.hp = fe.maxhp; }   /* a passive dummy for the v5.3 checks */
        static const struct { int at; u16 k; } sc[] = {{30, KEY_L}, {36, KEY_L}, {42, KEY_L}, {120, 0}, {130, KEY_R}, {136, KEY_R}, {142, KEY_R},
            {148, KEY_R}, {154, KEY_R}, {160, KEY_R}, {260, 0}, {270, KEY_UP}, {330, 0}};
        for (unsigned i = 0; i < sizeof sc / sizeof sc[0]; i++) if (sc[i].at == t) return sc[i].k;
        if (t == 110 || t == 250 || t == 276 || t == 296 || t == 360 || t == 386) fprintf(stderr, "t%d z=%d st=%s hop=%d\n", t, fp.z, stname[fp.st], fp.hop);
        if (t >= 340 && t < 380) return KEY_DOWN;
        if (t == 381) return KEY_UP | KEY_A;
        if (t == 384) fprintf(stderr, "high cut? st=%s h=%d\n", stname[fp.st], fp.h);
        /* v5.3: cuts and guards mid-move */
        if (t == 460) return KEY_L;                                  /* a sidestep... */
        if (t == 463) return KEY_A;                                  /* ...cut short by a cut */
        if (t == 465) fprintf(stderr, "sidestep cut: st=%s z=%d z1=%d glide=%d\n", stname[fp.st], fp.z, fp.z1, fp.zslide);
        if (t == 490) fprintf(stderr, "  glide done: z=%d\n", fp.z);
        if (t == 520) return KEY_UP;                                 /* a leap... */
        if (t >= 521 && t <= 534 && (t & 3) == 0) fprintf(stderr, "  t%d st=%s t=%d hop=%d\n", t, stname[fp.st], fp.t, fp.hop);
        if (t == 535) return KEY_A;                                  /* ...and a cut in the air */
        if (t == 537) fprintf(stderr, "air cut: st=%s h=%d hop=%d air_t=%d\n", stname[fp.st], fp.h, fp.hop, fp.air_t);
        if (t == 580) fprintf(stderr, "  landed: hop=%d air_t=%d\n", fp.hop, fp.air_t);
        if (t == 604) fprintf(stderr, "guard x=%d\n", fp.x >> 8);
        if (t == 639) fprintf(stderr, "guard-walk back x=%d st=%s\n", fp.x >> 8, stname[fp.st]);
        if (t == 665) fprintf(stderr, "sidestep guard: st=%s glide=%d z=%d\n", stname[fp.st], fp.zslide, fp.z);
        if (t >= 600 && t < 640) { return KEY_B | (t > 605 ? KEY_LEFT : 0); }
        if (t == 660) return KEY_L;
        if (t >= 662 && t < 680) return KEY_B;                       /* a guard thrown mid-sidestep */
        return 0;
    }
    if (bot_mode == 6 && scene == S_DUEL && duel_on) {    /* demo: sidestep twice, dash back in, strike */
        static const u16 demo[] = {KEY_L,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, KEY_L,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                   KEY_RIGHT,0,KEY_RIGHT, 0,0,0, KEY_A,0,0,0,0,0,0,0,0, KEY_A};
        static int di = -1, gap = 60;
        if (di < 0) { if (--gap > 0) return dist() > 118 ? KEY_RIGHT : 0; di = 0; }
        if (di < (int)(sizeof(demo) / sizeof(demo[0]))) return demo[di++];
        di = -1; gap = 120; return 0;
    }
    extern int iai_state;
    if (bot_mode == 7) {                       /* scripted: Konami code, cheats, pause menu */
        static const u16 script[] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_LEFT, KEY_RIGHT, KEY_B, KEY_A,
                                     KEY_A /* GOD ON */, KEY_B /* back to title */};
        static int si, wait = 60, stage;
        if (wait > 0) { wait--; return 0; }
        if (stage == 0) { if (si < 12) { wait = 6; return script[si++]; } stage = 1; wait = 60; return 0; }
        if (stage == 1) {                          /* open menu, choose Samurai */
            if (scene == S_TITLE && !menu_on) { wait = 20; return KEY_START; }
            if (scene == S_TITLE && menu_level == 0 && menu_sel != (save_floor >= 1)) { wait = 8; return KEY_DOWN; }
            if (scene == S_TITLE && menu_level == 0) { wait = 20; return KEY_A; }
            if (scene == S_TITLE) { wait = 20; stage = 2; return KEY_A; }
        }
        if (stage == 2) {                          /* skip story / how-to, wait for the duel */
            if (scene != S_DUEL) { wait = 20; return KEY_A; }
            if (!duel_on) return 0;
            wait = 90; stage = 3; return 0;
        }
        if (stage == 3) { wait = 10; stage = 4; return KEY_START; }            /* pause */
        if (stage == 4) { wait = 10; stage = 5; return KEY_DOWN; }             /* -> CHEATS */
        if (stage == 5) { wait = 10; stage = 6; return KEY_A; }                /* open cheats */
        if (stage == 6) { wait = 10; stage = 7; return KEY_DOWN; }             /* -> ENDLESS KI */
        if (stage == 7) { wait = 10; stage = 8; return KEY_A; }                /* toggle */
        if (stage == 8) { wait = 10; stage = 9; return KEY_B; }                /* back */
        if (stage == 9) { wait = 10; stage = 10; return KEY_A; }               /* -> resume? (cursor on CHEATS) */
        if (stage == 10) { wait = 10; stage = 11; return KEY_B; }              /* back out of cheats page */
        if (stage == 11) { wait = 10; stage = 12; return KEY_UP; }             /* -> RESUME */
        if (stage == 12) { wait = 10; stage = 13; return KEY_A; }              /* resume */
        bot_mode = 0; return 0;
    }
    if (scene == S_TITLE) {
        if (!menu_on) return (pulse % 40) == 0 ? KEY_START : 0;
        int want = 0, cont = save_floor >= 1;
        int bm = getenv("BOTMODE") ? atoi(getenv("BOTMODE")) : 0;
        if (menu_level == 0) want = getenv("BOTCONT") ? 0 : cont + bm;
        else if (menu_level == 1) want = bot_diff;
        else { if (ta_floor != (getenv("TAFLOOR") ? atoi(getenv("TAFLOOR")) - 1 : 0)) return (pulse % 10) == 0 ? KEY_RIGHT : 0; }
        if (menu_level < 2 && menu_sel != want) return (pulse % 10) == 0 ? KEY_DOWN : 0;
        return (pulse % 30) == 0 ? KEY_A : 0;
    }
    extern int spare_t_pub;
    if (scene == S_DUEL && spare_t_pub > 20 && getenv("BOTSPARE")) return (pulse % 6) == 0 ? KEY_B : 0;
    if (scene == S_INTRO && iai_state) {         /* the draw-off: wait for the leaf, then draw */
        static int c, r;
        if (iai_state == 1) { c = 0; r = 9 + (pulse * 7) % 9; return 0; }
        return ++c >= r ? KEY_A : 0;
    }
    if (scene == S_INTRO && getenv("BOTBOW") && page && scene_t > 6 && scene_t < 50) return (pulse % 8) == 0 ? KEY_B : 0;
    if (scene == S_CONTINUE) return (pulse % 40) == 0 ? KEY_A : 0;
    if (scene == S_STAIRS && getenv("STAIRSHOT")) return 0;
    if (scene == S_CREDITS && getenv("CREDITSHOT")) return 0;
    if (scene != S_DUEL) return (pulse % 35) == 0 ? (scene == S_RESULT ? KEY_START : KEY_A) : 0;
    if (tut_step && bot_mode != 5) return (pulse % 20) == 0 ? KEY_START : 0;
    if (!duel_on || bot_mode == 1) return 0;
    int react = bot_mode == 4 ? 24 : 16, jitter = bot_mode == 4 ? 10 : 6;
    int d = dist();
    if (bot_mode == 3) {                     /* button masher: walk in, hammer A, sometimes B */
        if (d > 112) k |= KEY_RIGHT;
        if (++mash_t % 9 == 0) k |= KEY_A;
        if (mash_t % 47 == 0) k |= KEY_B;
        return k;
    }
    if (tut_step == TUT_CUT || tut_step == TUT_HIGHLOW) {
        if (d > 110) return KEY_RIGHT;
        return (pulse % 20) == 0 ? (KEY_A | (tut_step == TUT_HIGHLOW ? KEY_UP : 0)) : 0;
    }
    if (tut_step == TUT_COUNTER) return (pulse % 4) == 0 ? KEY_A : 0;
    int tut_defend = tut_step == TUT_GUARD || tut_step == TUT_PARRY;
    /* perception: notice Kenji's attack after a human delay */
    int threat = (fe.st == ST_TELL || fe.st == ST_WINDUP || fe.st == ST_SWING);
    if (threat && last_seen_st < 0) { last_seen_st = fe.st; see_t = react + (int)(rnd(jitter)); plan = rnd(100); plan_h = rnd(100) < 70 ? fe.h : rnd(3); }
    if (!threat) last_seen_st = -1;
    if (threat && see_t > 0) see_t--;
    if (threat && see_t == 0 && fe.h == HIGH && bot_mode != 4 && fp.st == ST_IDLE && plan % 100 >= 55) {
        u16 dir = fp.z >= 768 ? KEY_R : KEY_L;          /* step out of his plane */
        plan = 0; return dir;
    }
    if (threat && see_t == 0 && fe.h == HIGH && bot_mode != 4 && (fp.st == ST_IDLE || fp.st == ST_CROUCH) && plan % 100 >= 45 && plan % 100 < 55)
        return KEY_DOWN;                                 /* duck under it */
    if (threat && see_t == 0 && fe.h == LOW && bot_mode != 4 && fp.st == ST_IDLE && fe.st == ST_SWING && plan % 100 >= 45 && plan % 100 < 60) {
        plan = 0; return KEY_UP;                          /* jump it */
    }
    if (threat && see_t == 0) {
        int parry_try = (bot_mode == 0 ? 45 : 20) > plan % 100 || tut_step == TUT_PARRY;
        if (parry_try) {      /* a timed press: wait for the cut to come in */
            if (!hold_b && (fe.st == ST_SWING || (fe.st == ST_WINDUP && fe.t >= 1 + (int)rnd(3)))) { hold_b = 1; return KEY_B | (plan_h == HIGH ? KEY_UP : plan_h == LOW ? KEY_DOWN : 0); }
            if (!hold_b) return 0;
        }
        else if (plan % 100 < 80) hold_b = 1;
        else if ((pulse & 1) == 0) { seq[0] = 0; seq[1] = KEY_LEFT; seq_n = 2; return KEY_LEFT; }   /* < < dodge */
    }
    if (hold_b) {
        if (fe.st == ST_SWING || fe.st == ST_FOLLOW || fe.st == ST_WINDUP || fe.st == ST_TELL) return KEY_B | (plan_h == HIGH ? KEY_UP : plan_h == LOW ? KEY_DOWN : 0);
        hold_b = 0;
    }
    /* counter after a parry */
    if (rip_owner == &fp && rip_t > 0 && (pulse % 6) == 0) return KEY_A;
    if (fp.meter >= 100 && d < 140 && (pulse % 30) == 0) return KEY_A | KEY_B;
    if (fp.st != ST_IDLE && fp.st != ST_FOLLOW) return 0;
    if (tut_step == TUT_STEP && fp.st == ST_IDLE && (pulse % 20) == 0) return KEY_L;
    if (fp.st == ST_IDLE && iabs(fp.z - fe.z) >= 200 && fe.st != ST_STRAFE && (pulse % 20) == 0)
        return fe.z > fp.z ? KEY_L : KEY_R;
    if (tut_defend) return d > 120 ? KEY_RIGHT : 0;
    int opening = fe.st == ST_RECOVER || fe.st == ST_STAGGER || fe.st == ST_RECOIL || fe.st == ST_HURT;
    if (fp.st == ST_FOLLOW) return (fe.st == ST_HURT && (pulse % 3) == 0) ? KEY_A : 0;
    if (d <= 116 && (opening || (pulse % (bot_mode == 4 ? 25 : 18)) == 0)) {
        int r = rnd(10);
        if (floor_i == 4 && fe.hp * 2 > fe.maxhp) return KEY_A | (r < 5 ? KEY_UP : KEY_DOWN);   /* read the ARMOUR banner */
        return KEY_A | (r < 2 ? KEY_UP : r < 4 ? KEY_DOWN : 0);
    }
    if (d > 110) k |= KEY_RIGHT;
    if (d > 170 && (pulse % 30) == 0) k = 0;
    return k;
}

static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 240 160 255\n");
    fwrite(screen, 1, sizeof(screen), f);
    fclose(f);
}

void host_frame(void) {
    if ((dump_every && frame_no + 1 >= (getenv("DUMPFROM") ? atol(getenv("DUMPFROM")) : 0)) || getenv("RENDER") || getenv("SCENESHOT")) render();
    frame_no++;
    if (dump_every && frame_no % dump_every == 0 && frame_no >= (getenv("DUMPFROM") ? atol(getenv("DUMPFROM")) : 0)) {
        char p[256]; snprintf(p, sizeof p, "%s/f%06ld.ppm", dump_dir, frame_no); dump_ppm(p);
    }
    if (log_f) {
        static int lp = -1, le = -1;
        if (fp.st != lp || fe.st != le)
            fprintf(log_f, "%ld d=%d P:%s h%d hp%d m%d z%d  E:%s h%d hp%d z%d ko=%d w%d-%d sc=%u tut=%d\n", frame_no, dist(),
                    stname[fp.st], fp.h, fp.hp, fp.meter, fp.z, stname[fe.st], fe.h, fe.hp, fe.z, ko_side, wins_p, wins_e, score, tut_step);
        lp = fp.st; le = fe.st;
        static int lk = 0;
        if (ko_side != lk && ko_side) fprintf(log_f, "RESULT %d at %ld round %d hp %d-%d kind %d\n", ko_side, frame_no, round_no, fp.hp, fe.hp, finish_kind);
        static int lsc = -1;
        if (lsc != scene) { if (scene == S_RESULT && !ending_done) fprintf(log_f, "MATCH WON floor %d score %u\n", floor_i + 1, score); if (scene == S_RESULT && ending_done) fprintf(log_f, "CAMPAIGN DONE score %u\n", score); if (scene == S_CONTINUE) fprintf(log_f, "MATCH LOST floor %d score %u\n", floor_i + 1, score); }
        lsc = scene;
        lk = ko_side;
        extern int iai_last; static int liai; if (iai_last && !liai) fprintf(log_f, "IAI %d floor %d\n", iai_last, floor_i + 1); liai = iai_last; if (scene == S_INTRO) iai_last = liai = 0;
        static Fighter *lw; if (wpn_owner != lw && wpn_owner) fprintf(log_f, "DISARM %s floor %d\n", wpn_owner == &fp ? "lieve" : "opp", floor_i + 1); lw = wpn_owner;
        static int lfall; if ((fp.fall || fe.fall) && !lfall) fprintf(log_f, "RINGOUT %s at %ld round %d\n", fp.fall ? "lieve" : "opp", frame_no, round_no); lfall = fp.fall || fe.fall;
    }
    if (getenv("SCENESHOT")) {
        static int ls = -1, cnt = 0; static long enter = 0;
        if (scene != ls) { ls = scene; enter = frame_no; }
        if (frame_no - enter == (getenv("SHOTAT") ? atoi(getenv("SHOTAT")) : 45) && cnt < 40) { char p[256]; snprintf(p, sizeof p, "%s/scene%02d_%d.ppm", dump_dir, cnt++, scene); dump_ppm(p); }
    }
    if (getenv("STAIRSHOT") && scene == S_STAIRS && (scene_t % 12) == 0) {
        static int sc = 0; char p[256]; snprintf(p, sizeof p, "%s/stairs%03d.ppm", dump_dir, sc++); render(); dump_ppm(p);
    }
    if (getenv("CUTSHOT") && scene == S_CUT && (scene_t % 40) == 39) {
        static int uc = 0; char p[256]; snprintf(p, sizeof p, "%s/cut%03d.ppm", dump_dir, uc++); render(); dump_ppm(p);
    }
    if (getenv("CREDITSHOT") && scene == S_CREDITS && (scene_t % 210) == 100) {
        static int cc = 0; char p[256]; snprintf(p, sizeof p, "%s/credits%02d.ppm", dump_dir, cc++); render(); dump_ppm(p);
    }
    if (getenv("KOSHOT")) {
        static int kc = 0, kt = -1, kn = 0;
        if (ko_side && kt < 0 && kn < 3) { kt = 0; kn++; }
        if (kt >= 0) { if ((kt % (getenv("KOEVERY") ? atoi(getenv("KOEVERY")) : 6)) == 0 && kt <= 96) { char p[256]; snprintf(p, sizeof p, "%s/ko%03d.ppm", dump_dir, kc++); render(); dump_ppm(p); } kt++; if (!ko_side) kt = -1; }
    }
    if (getenv("TROPHYSHOT") && trophy_on && fp.st == ST_VICTORY && (fp.t % 6) == 0) {
        static int tc = 0; char p[256]; snprintf(p, sizeof p, "%s/tr%03d.ppm", dump_dir, tc++); render(); dump_ppm(p);
    }
    if (getenv("SHOTSCENE") && scene == atoi(getenv("SHOTSCENE")) && (scene_t % atoi(getenv("SHOTEVERY"))) == 0) {
        static int qc = 0; if (qc < atoi(getenv("SHOTMAX"))) { char p[256]; snprintf(p, sizeof p, "%s/q%03d.ppm", dump_dir, qc++); render(); dump_ppm(p); }
    }
    if (getenv("BREATHSHOT")) {
        static int bc = 0; int hit = 0;
        for (int i = 0; i < NAMB; i++) if (amb[i].life && amb[i].kind == AK_BREATH && amb[i].life == 16) hit = 1;
        if (hit && bc < 6) { for (int i = 0; i < NAMB; i++) if (amb[i].life && amb[i].kind == AK_BREATH) fprintf(stderr, "breath %d,%d life %d age %d\n", (int)(amb[i].x >> 8), (int)(amb[i].y >> 8), amb[i].life, amb[i].age); { int nz = 0; for (int q = 0; q < 32; q++) nz += host_vram[0x10000 + 1005 * 32 + q] != 0; fprintf(stderr, "tile1005 nz %d bld %04x\n", nz, REG_BLDCNT); } for (int j = 0; j < 128; j++) if ((host_oam[j*4+2] & 1023) == 1005) fprintf(stderr, "oam %d: %04x %04x %04x\n", j, host_oam[j*4], host_oam[j*4+1], host_oam[j*4+2]); char p[256]; snprintf(p, sizeof p, "%s/br%02d.ppm", dump_dir, bc++); render(); dump_ppm(p); }
    }
    if (getenv("EVENTSHOT")) {
        static int cnt[40];
        const char *nm = 0; int k = -1;
        #define EV(id, cond, name) if (k < 0 && (cond) && cnt[id] < 2) { k = id; nm = name; }
        EV(0, iai_state == 2 && iai_t == iai_wait + 3, "iai_cue")
        EV(1, iai_state == 1 && iai_t == 40, "iai_wait")
        EV(2, spare_t_pub == 30, "spare")
        EV(3, scene == S_DUEL && fp.st == ST_POSE && fp.pose_anim == A_CHIBURI && fp.pose_fr == 2, "chiburi")
        EV(4, scene == S_DUEL && fp.st == ST_POSE && fp.pose_anim == A_NOTO && fp.pose_fr == 4, "noto")
        EV(5, spot_on && (frame_no % 90) == 0, "spotlight")
        EV(6, fire_t == 480 || fire_t == 300, "fire")
        EV(7, pole_t == 14 || pole_t == 30, "bamboo")
        EV(8, wpn_owner && wpn_t == 12, "disarm_fly")
        EV(9, wpn_owner && wpn_t == 0 && (frame_no % 40) == 0, "disarm_floor")
        EV(10, fe.st == ST_AIM && fe.t == 30, "aim")
        EV(11, fe.st == ST_AIM && fe.t == 45, "shot")
        EV(12, (fp.mosaic > 6 && fp.mosaic < 40) || (fe.mosaic > 6 && fe.mosaic < 40), "mosaic")
        EV(13, fe.mosaic >= 46 || fp.mosaic >= 46, "dissolve")
        EV(14, fe.nmarks >= 3 && scene == S_DUEL && duel_on && (frame_no % 50) == 0, "marks")
        EV(15, dawn_amt == 14 && (frame_no % 30) == 0, "dawn")
        EV(16, scene == S_DUEL && ko_side == 2 && wipe_y == 0 && grey_amt == 0 && show_fp == 0 && (frame_no % 40) == 0, "poem")
        EV(17, scene == S_DUEL && duel_on && wet_stage(stage_i) && fp.st == ST_IDLE && (frame_no % 200) == 0, "reflect")
        EV(18, scene == S_INTRO && fe.pose_anim == A_INTRO && fe.pose_fr == 3, "intro")
        EV(19, scene == S_DUEL && duel_on && floor_i == F_DOCK && (frame_no % 300) == 0, "dock")
        EV(20, scene == S_DUEL && duel_on && floor_i == F_SPIRE && (frame_no % 300) == 0, "spire")
        EV(21, scene == S_CUT && cut_id == CUT_SECRET && (frame_no % 90) == 0, "secret")
        EV(22, scene == S_RESULT && scene_t == 40, "result")
        EV(23, scene == S_DUEL && fp.fall && fp.t == 14 || fe.fall && fe.t == 14, "fall")
        EV(24, scene == S_DUEL && ko_side == 2 && fe.st == ST_VICTORY && (fe.t == 20 || fe.t == 45), "defeat_a")
        EV(25, scene == S_DUEL && ko_side == 2 && fe.st == ST_VICTORY && (fe.t == 75 || fe.t == 150), "defeat_b")
        EV(26, scene == S_CONTINUE && scene_t == 30, "continue")
        EV(27, spare_t_pub == 45 || spare_t_pub == 80, "spare2")
        EV(28, scene == S_DUEL && duel_on && (fp.st == ST_CROUCH || fp.st == ST_JUMP) && fp.t == 5, "crouchjump")
        EV(29, scene == S_DUEL && duel_on && fp.st == ST_STRAFE && fp.t == 8 && fp.z1 >= 512, "plane")
        EV(30, replay_t == 30 || replay_t == 80, "replay")
        EV(31, scene == S_CUT && cut_id >= CUT_RIVAL_HELP && cut_t == 40, "rival")
        EV(32, scene == S_DUEL && fp.st == ST_LOCK && (lock_t == 20 || lock_t == 60), "lock")
        EV(33, scene == S_INTRO && scene_t == 30 && page, "challenge")
        if (k >= 0) { char p[256]; snprintf(p, sizeof p, "%s/ev_%s_%d.ppm", dump_dir, nm, cnt[k]++); render(); dump_ppm(p); }
    }
    if (getenv("BGSHOT") && scene == S_DUEL && duel_on && floor_i == atoi(getenv("BGSHOT"))) {
        static int done = 0, wait_ = 0;
        if (++wait_ > 100 && !done++) { text_clear(); brush_clear(); stain_n = 0; for (int i = 0; i < NPART; i++) parts[i].life = 0; show_fp = show_fe = 0; hud_hide = 1; draw_hud(); build_sprites(); plat_copy32(OAM_MEM, oam, 256); render(); char p[256]; snprintf(p, sizeof p, "%s/bg.ppm", dump_dir); dump_ppm(p);
            fprintf(stderr, "feet %d fpx %d fex %d\n", feet_y(&fp), screen_x(&fp), screen_x(&fe)); exit(0); }
    }
    if (getenv("FORCEHON") && scene != S_RESULT) honour = atoi(getenv("FORCEHON"));
    if (getenv("STORYLOG")) { static int lsc = -1, lcut = -1; if (scene != lsc || (scene == S_CUT && cut_id != lcut)) { fprintf(stderr, "f%ld scene %d cut %d floor %d opp %s honour %d ambush %d help %d\n", frame_no, scene, cut_id, floor_i, OPP->art->name, honour, ambush_opp, rival_help); lsc = scene; lcut = cut_id; } }
    if (getenv("CHEATLOG")) {
        static int lc = -1, lp = -1, ls2 = -1;
        if (cheat_on != lc || paused != lp || scene != ls2) fprintf(stderr, "frame %ld scene %d paused %d page %d cheat_on %d unlocked %d meter %d\n", frame_no, scene, paused, pause_page, cheat_on, cheats_unlocked, fp.meter);
        lc = cheat_on; lp = paused; ls2 = scene;
    }
    if (getenv("CYCSCAN")) {          /* count frames that lose sprite lines to the hardware's per-line budget */
        static long fr[4], bad[4], worst[4];
        int c = iai_on ? 0 : spare_t_pub ? 1 : cam_k != 64 ? 2 : 3;
        if (c < 3 || (frame_no % 5) == 0) {
            host_cyc_drop = 0; render(); fr[c]++;
            if (host_cyc_drop) { bad[c]++; if (getenv("CYCWHO")) fprintf(stderr, "drop c%d scene %d st %d/%d k %d/%d kmul %d/%d cam %d lines %ld frame %ld\n", c, scene, fp.st, fe.st, fig_k(&fp), fig_k(&fe), fp.kmul, fe.kmul, cam_k, host_cyc_drop, frame_no); if (host_cyc_drop > worst[c]) worst[c] = host_cyc_drop;
                static int sv = 0; if (sv < 6 && getenv("CYCSHOT")) { char p[256]; snprintf(p, sizeof p, "%s/cyc_%d_%d.ppm", dump_dir, c, sv++); dump_ppm(p); } }
        }
        if (frame_no >= frame_limit) fprintf(stderr, "CYC iai %ld/%ld (worst %ld lines)  spare %ld/%ld (%ld)  zoom %ld/%ld (%ld)  play %ld/%ld (%ld)\n",
            bad[0], fr[0], worst[0], bad[1], fr[1], worst[1], bad[2], fr[2], worst[2], bad[3], fr[3], worst[3]);
    }
    if (frame_no >= frame_limit) { if (log_f) fclose(log_f); exit(0); }
    u16 k = bot_keys();
    *(u16 *)(host_io + 0x130) = (~k) & 0x3FF;
}

int main(int argc, char **argv) {
    if (argc > 1) frame_limit = atol(argv[1]);
    if (argc > 2) dump_every = atoi(argv[2]);
    if (argc > 3) bot_mode = atoi(argv[3]);
    if (argc > 4) rng_s = (u32)atol(argv[4]);
    if (argc > 5) bot_diff = atoi(argv[5]);
    if (getenv("DUMPDIR")) dump_dir = getenv("DUMPDIR");
    if (getenv("LOG")) log_f = fopen(getenv("LOG"), "w");
    if (getenv("FLOOR")) { start_floor = atoi(getenv("FLOOR")) - 1; if (start_floor) cheat_on |= CH_FLOOR; }
    *(u16 *)(host_io + 0x130) = 0x3FF;
    if (getenv("SAVEFLOOR")) { int f = atoi(getenv("SAVEFLOOR")), d = 1;
        host_sram[0] = 'K'; host_sram[1] = 'O'; host_sram[2] = 'M'; host_sram[3] = 'O'; host_sram[4] = 0x40; host_sram[5] = 0x9c;
        host_sram[8] = 'F'; host_sram[9] = f; host_sram[10] = d; host_sram[11] = f ^ d ^ 0x5A; }
    game_main();
    return 0;
}
