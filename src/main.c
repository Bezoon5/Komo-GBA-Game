/* KOMO (Kōmō, "red hair") - Floor One prototype.
 * A Bushido Blade style duel on the GBA: big streamed fighters, one-hit kills,
 * stances, parries and wounds, in a lantern-lit dojo on a rainy night. */
#include "hw.h"
#include "gen/assets.h"

/* ------------------------------------------------------------------ platform */
#ifdef HOST
void host_frame(void);
void host_set_hdma(const u16 *table);
void host_set_hdma3(const u16 *table);
static void plat_copy32(volatile void *dst, const void *src, u32 words) {
    volatile u32 *d = dst; const u32 *s = src;
    while (words--) *d++ = *s++;
}
static void plat_fill32(volatile void *dst, u32 words) {
    volatile u32 *d = dst;
    while (words--) *d++ = 0;
}
static void plat_vsync(void) { host_frame(); }
static void plat_hdma(const u16 *t) { REG_BGHOFS(2) = t[0]; host_set_hdma(t); }
static void plat_hdma3(const u16 *t) { REG_BGHOFS(3) = t[0]; host_set_hdma3(t); }
void host_set_hdma2(const u16 *table);
static void plat_hdma2(const u16 *t) { if (t) REG_WIN0H = t[0]; host_set_hdma2(t); }
void host_set_split(int y, const u16 *pal);
static void plat_split(int y, const u16 *grey_pal, const u16 *col_pal) {
    if (y >= 160 && grey_pal) { for (int i = 0; i < 512; i++) host_pal[i] = grey_pal[i]; host_set_split(-1, 0); return; }
    if (y <= 0) { host_set_split(-1, 0); return; }
    for (int i = 0; i < 512; i++) host_pal[i] = grey_pal[i];
    host_set_split(y, col_pal);
}
#else
static void plat_copy32(volatile void *dst, const void *src, u32 words) {
    REG_DMA_SAD(3) = (u32)src;
    REG_DMA_DAD(3) = (u32)dst;
    REG_DMA_CNT(3) = words | 0x84000000u;      /* enable, 32-bit, immediate */
}
/* the zero for DMA fills must be in RAM: a DMA reading the cartridge can't hold its source address
   fixed (the hardware, and accurate emulators such as mGBA, step through the ROM instead) */
static volatile u32 zero_word;
static void plat_fill32(volatile void *dst, u32 words) {
    zero_word = 0;
    REG_DMA_SAD(3) = (u32)&zero_word;
    REG_DMA_DAD(3) = (u32)dst;
    REG_DMA_CNT(3) = words | 0x85000000u;      /* enable, 32-bit, fixed source, immediate */
}
static void plat_vsync(void) { __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory"); }
static void plat_hdma(const u16 *t) {
    REG_DMA_CNT(0) = 0;
    REG_BGHOFS(2) = t[0];
    REG_DMA_SAD(0) = (u32)(t + 1);
    REG_DMA_DAD(0) = (u32)&REG_BGHOFS(2);
    /* 1 halfword, dest fixed, repeat, hblank timing, enable */
    REG_DMA_CNT(0) = 1 | ((0x0040 | 0x0200 | 0x2000 | 0x8000u) << 16);
}
u32 *split_pal;                            /* read by the IRQ handler: copied into palette RAM at the VCount line */
static void plat_split(int y, const u16 *grey_pal, const u16 *col_pal) {
    if (y <= 0 || y >= 160) {              /* no split: the whole frame one palette */
        split_pal = 0;
        REG_DISPSTAT = 0x0008; REG_IE = 0x0001;
        if (y >= 160 && grey_pal) plat_copy32(PAL_BG, grey_pal, 256);
        else if (y > -1000 && y <= 0 && col_pal) plat_copy32(PAL_BG, col_pal, 256);
        return;
    }
    plat_copy32(PAL_BG, grey_pal, 256);     /* black and white above the line (already wiped)... */
    split_pal = (u32 *)col_pal;             /* ...colour still below it */
    REG_DISPSTAT = 0x0008 | 0x0020 | (y << 8);
    REG_IE = 0x0001 | 0x0004;
}
static void plat_hdma2(const u16 *t) {     /* per-line shape of the spotlight window */
    REG_DMA_CNT(2) = 0;
    if (!t) return;
    REG_WIN0H = t[0];
    REG_DMA_SAD(2) = (u32)(t + 1);
    REG_DMA_DAD(2) = (u32)&REG_WIN0H;
    REG_DMA_CNT(2) = 1 | ((0x0040 | 0x0200 | 0x2000 | 0x8000u) << 16);
}
static void plat_hdma3(const u16 *t) {     /* per-line scroll of the view outside (shimmer, sway) */
    REG_DMA_CNT(1) = 0;
    REG_BGHOFS(3) = t[0];
    REG_DMA_SAD(1) = (u32)(t + 1);
    REG_DMA_DAD(1) = (u32)&REG_BGHOFS(3);
    REG_DMA_CNT(1) = 1 | ((0x0040 | 0x0200 | 0x2000 | 0x8000u) << 16);
}
#endif

/* ------------------------------------------------------------------ helpers */
static u32 rng_s = 0x2545F491;
static u32 rnd(u32 n) {
    rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5;
    return ((rng_s >> 16) * n) >> 16;      /* n < 65536; multiply instead of a slow division */
}
static int iabs(int v) { return v < 0 ? -v : v; }

static u16 keys, keys_old, keys_hit;
static void read_keys(void) {
    keys_old = keys;
    keys = (~REG_KEYINPUT) & 0x3FF;
    keys_hit = keys & ~keys_old;
}

/* ------------------------------------------------------------------ video memory map */
#define CB2 0x8000
#define SB_TEXT 13          /* maps live above the stage tiles in charblocks 0-1, freeing CB2 for art */
#define SB_RAIN 14
#define SB_OUT 15
#define SB_DOJO 30
#define OBJ_VRAM (VRAM + 0x10000)
#define TEXT_MAP ((volatile u16 *)(VRAM + SB_TEXT * 0x800))

static u16 oam[512] __attribute__((aligned(4)));
static u16 hdma_buf[2][160] __attribute__((aligned(4)));
static int hdma_cur;

/* ------------------------------------------------------------------ text */
static u8 font_lut[128];
static void text_init(void) {
    const char *f = FONT_CHARS;
    for (int i = 0; i < 128; i++) font_lut[i] = 0;
    for (int i = 0; f[i]; i++) font_lut[(u8)f[i]] = FONT_FIRST + i;
}
static u16 hud_buf[3 * 32] __attribute__((aligned(4)));
static u16 hud_vram[3 * 32] __attribute__((aligned(4)));
static int hud_to_buf, hud_dirty;
static void text_put(int col, int row, const char *s, int bank) {
    volatile u16 *m = (hud_to_buf && row < 3) ? (volatile u16 *)hud_buf + row * 32 + col : TEXT_MAP + row * 32 + col;
    while (*s && col < 32) {
        u8 c = (u8)*s++;
        if (c >= 'a' && c <= 'z') c -= 32;
        *m++ = (c == ' ') ? 0 : (font_lut[c & 127] | (bank << 12));
        col++;
    }
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void text_center(int row, const char *s, int bank) { text_put((30 - slen(s)) / 2, row, s, bank); }
static void text_clear_row(int row) {
    if (hud_to_buf && row < 3) { for (int i = 0; i < 32; i++) hud_buf[row * 32 + i] = 0; return; }
    for (int i = 0; i < 32; i++) TEXT_MAP[row * 32 + i] = 0;
}
static void text_clear(void) {
    for (int r = 0; r < 20; r++) text_clear_row(r);
    for (int i = 0; i < 96; i++) hud_vram[i] = 0xFFFF;   /* force the HUD to be rewritten */
}
#define T_WHITE 5
#define T_RED 6
#define T_GOLD 7

/* ------------------------------------------------------------------ sound (PSG) */
static int noise_t;
static int storm_vol;
static void rain_noise(void) {
    REG16(0x78) = ((2 + storm_vol * 2) << 12);
    REG16(0x7C) = 0x8000 | (1 << 4) | 0;
}
static int sfx_noise_hold;
static void drum_fx(int vol, int step, int s, int r, int dur);
static void wave_init(void);
static void noise_fx(int vol, int step, int s, int r, int dur) {
    sfx_noise_hold = dur;
    REG16(0x78) = (vol << 12) | (step << 8);
    REG16(0x7C) = 0x8000 | (s << 4) | r;
    noise_t = dur;
}
static void sq1(int freg, int duty, int vol, int step, int sweep) {
    REG16(0x60) = sweep;
    REG16(0x62) = (duty << 6) | (step << 8) | (vol << 12);
    REG16(0x64) = 0x8000 | freg;
}
static void sq2(int freg, int duty, int vol, int step) {
    REG16(0x68) = (duty << 6) | (step << 8) | (vol << 12);
    REG16(0x6C) = 0x8000 | freg;
}
static int note(int midi) { if (midi < 36) midi = 36; if (midi > 96) midi = 96; return note_reg[midi - 36]; }
static void snd_init(void) {
    REG16(0x84) = 0x80;
    REG16(0x80) = 0xFF77;
    REG16(0x82) = 0x0002;
    wave_init();
    rain_noise();
}
static void sfx_clang(void) { sq1(1986, 0, 12, 1, 0); sq2(1998, 2, 9, 1); noise_fx(10, 1, 2, 1, 10); }
static void sfx_block(void) { sq1(1970, 1, 10, 2, 0); noise_fx(8, 1, 3, 1, 8); }
static void sfx_whoosh(void) { noise_fx(7, 1, 4, 3, 14); }
static void sfx_cut(void) { noise_fx(15, 2, 3, 0, 30); sq1(note(52), 2, 13, 3, (3 << 4) | 8 | 2); }
static void sfx_taiko(void) { sq1(175, 2, 15, 3, (2 << 4) | 8 | 3); noise_fx(9, 2, 9, 4, 18); }
static void sfx_thunder(void) { noise_fx(15, 7, 10, 7, 110); }
static void sfx_shot(void) { noise_fx(15, 2, 0, 3, 26); sq1(note(43), 1, 15, 1, (3 << 4) | 8 | 4); }
static void sfx_menu(void) { sq2(note(81), 2, 8, 1); }
static void sfx_wound(void) { noise_fx(12, 1, 3, 2, 14); sq1(note(60), 2, 9, 2, (2 << 4) | 8 | 2); }
static void sfx_stance(void) { sq2(note(86), 1, 4, 1); }

/* ---- the soundtrack: three voices (koto/flute lead on square 2, bass on the wave
 * channel, taiko on noise), each a stream of (note, ticks) pairs; 255 loops, 254 ends */
static const Song *song;
static const u8 *song_p[3];
static int song_left[3], song_t, song_id = -1, sfx_noise_t;
static void wave_init(void) {
    REG16(0x70) = 0x40;                             /* play bank 1 while bank 0 is written */
    for (int i = 0; i < 16; i += 2) REG16(0x90 + i) = wave_shape[i] | (wave_shape[i + 1] << 8);
    REG16(0x70) = 0x80;                             /* enable, one 32-sample bank */
    REG16(0x72) = 0;
}
static void drum_fx(int vol, int step, int s, int r, int dur) {   /* like noise_fx, but music */
    REG16(0x78) = (vol << 12) | (step << 8);
    REG16(0x7C) = 0x8000 | (s << 4) | r;
    noise_t = dur;
}
static void music_play_id(int id) {
    if (id == song_id && song) return;              /* already playing: don't restart */
    song = &songs[id]; song_id = id;
    song_p[0] = song->lead; song_p[1] = song->bass; song_p[2] = song->drum;
    song_left[0] = song_left[1] = song_left[2] = 0;
    song_t = 0;
}
#define music_play(id) music_play_id(id)
static void music_stop(void) {
    song = 0; song_id = -1;
    REG16(0x72) = 0;                                /* silence the bass */
    REG16(0x68) = 0; REG16(0x6C) = 0x8000;          /* and the lead */
}
static void music_tick(void) {
    if (sfx_noise_t > 0) sfx_noise_t--;
    if (!song) return;
    if (song_t > 0) { song_t--; return; }
    song_t = song->tempo - 1;
    for (int ch = 0; ch < 3; ch++) {
        if (song_left[ch] > 0) { song_left[ch]--; continue; }
        const u8 *p = song_p[ch];
        if (*p == 255) p = ch == 0 ? song->lead : ch == 1 ? song->bass : song->drum;
        if (*p == 254) { if (ch == 0) { music_stop(); return; } continue; }
        int n = p[0], d = p[1];
        song_p[ch] = p + 2;
        song_left[ch] = d - 1;
        if (ch == 0) {
            if (n) sq2(note(n), song->duty, song->vol, song->step);
            else { REG16(0x68) = 0; REG16(0x6C) = 0x8000; }
        } else if (ch == 1) {
            if (n) { REG16(0x72) = song->bass_vol << 13; REG16(0x74) = 0x8000 | wave_reg[n < 24 ? 0 : n - 24]; }
            else REG16(0x72) = 0;
        } else if (n && sfx_noise_t == 0) {
            if (n & 1) drum_fx(12, 3, 7, 5, 12);       /* taiko */
            else drum_fx(7, 1, 2, 1, 5);               /* rim */
        }
    }
}
static void sound_tick(void) {
    if (sfx_noise_hold > 0) { sfx_noise_t = sfx_noise_hold; sfx_noise_hold = 0; }
    if (noise_t > 0 && --noise_t == 0) rain_noise();
    music_tick();
}

/* ------------------------------------------------------------------ extra sound */
static void sfx_kiai(void) { sq1(note(64), 2, 12, 2, (2 << 4) | 0 | 3); noise_fx(6, 1, 4, 2, 8); }
static void sfx_hit(void) { sfx_wound(); }
static void sfx_super(void) { sq1(note(50), 2, 15, 4, (1 << 4) | 0 | 2); sq2(note(74), 1, 12, 4); noise_fx(12, 3, 5, 1, 40); }
static void sfx_dash(void) { noise_fx(5, 1, 3, 4, 8); }

/* ------------------------------------------------------------------ scenes (declared early) */
enum { S_TITLE, S_STORY, S_HOWTO, S_INTRO, S_DUEL, S_RESULT, S_CONTINUE, S_CHEATS, S_CUT, S_STAIRS, S_CREDITS };
static int scene, scene_t, page;
int g_scene_dbg;
static void load_stage(int si);
static void load_stairs(int next_stage);
static void load_still(int id);

/* ------------------------------------------------------------------ difficulty */
typedef struct {
    u8 hp_p, hp_e;
    u8 react_min, react_rng;
    u8 guard_pct, parry_pct, right_h_pct, dodge_pct;
    u8 tell, chain_pct, rip_pct;
    u8 atk_min, atk_rng;
    u8 parry_win, parry_any, block_any;
    u8 stagger, relock;
    u8 anticipate_pct, wary_bonus, aggro_mult;
    const char *name, *desc1, *desc2;
} Diff;
static const Diff DIFFS[3] = {
    {5, 3, 20, 12, 30, 0, 50, 10, 24, 0, 0, 80, 70, 16, 1, 0, 58, 4, 10, 20, 20, "ROOKIE", "KENJI GOES EASY.", "TUTORIAL FIRST."},
    {4, 5, 13, 10, 55, 18, 70, 14, 15, 45, 65, 24, 28, 11, 1, 0, 50, 6, 55, 35, 70, "SAMURAI", "A REAL DUEL.", "KENJI FIGHTS BACK."},
    {1, 1, 9, 7, 62, 40, 72, 18, 5, 50, 85, 12, 16, 7, 0, 0, 36, 12, 72, 40, 110, "BUSHIDO", "NO MERCY.", "MATCH HIS HEIGHT."},
};
static int diff_i;
#define DF (&DIFFS[diff_i])

/* ------------------------------------------------------------------ fighters */
enum { ST_IDLE, ST_TELL, ST_WINDUP, ST_SWING, ST_FOLLOW, ST_RECOVER, ST_GUARD, ST_HURT,
       ST_STAGGER, ST_RECOIL, ST_DODGE, ST_DASH, ST_SUPER, ST_STRAFE, ST_PICKUP, ST_AIM, ST_JUMP, ST_CROUCH, ST_LOCK,
       ST_DYING, ST_DEATH, ST_DEAD, ST_POSE, ST_VICTORY };
enum { LOW, MID, HIGH };

static const u8 T_WINDUP[3] = {5, 6, 9};
static const u8 T_SWING[3] = {3, 3, 4};
static const u8 T_FOLLOW[3] = {5, 5, 7};
static const u8 T_RECOVER[3] = {6, 7, 9};
static const u8 REACH[3] = {100, 104, 108};
static const u8 LUNGE[3] = {12, 14, 16};
#define MIN_DIST 56
#define GORE_FLIGHT 7
#define STAGE_HALF 126
#define CAM_RANGE 56

typedef struct {
    const u32 *data, *off;
    const u16 *mask, *runs;
    const u32 *roff;
    const u8 *tipx, *tipy, *gorex, *gorey, *mouth, *anc;
    const u32 *wpn;
    const u16 *pal16;
    int base_tile, pal;
    const char *name;
    int sharp;          /* a real blade: its killing cuts sever */
} Art;

#define ART(n, base, slot, nm, sharp) {n##_data, n##_off, n##_mask, n##_runs, n##_roff, n##_tipx, n##_tipy, n##_gorex, n##_gorey, n##_mouth, n##_anc, n##_wpn, n##_pal, base, slot, nm, sharp}
#define lieve2_wpn lieve_wpn
#define lieve3_wpn lieve_wpn
static const Art art_lieve = ART(lieve, 0, 0, "LIEVE", 1);
static const Art art_kenji = ART(kenji, 480, 1, "KENJI", 0);
static const Art art_hozen = ART(hozen, 480, 1, "HOZEN", 0);
static const Art art_gonzo = ART(gonzo, 480, 1, "GONZO", 1);
static const Art art_kagero = ART(kagero, 480, 1, "KAGERO", 1);
static const Art art_kurogane = ART(kurogane, 480, 1, "KUROGANE", 1);
static const Art art_lieve2 = ART(lieve2, 0, 0, "LIEVE", 1);       /* Kenji's headband */
static const Art art_lieve3 = ART(lieve3, 0, 0, "LIEVE", 1);       /* hair loose, sleeves torn */
static const Art art_sailor = ART(sailor, 480, 1, "SAILOR", 1);
static const Art art_morsel = ART(morsel, 480, 1, "MORSEL", 1);

/* the five masters of the pagoda, one per floor */
enum { SP_NONE, SP_SWEEP, SP_THROW, SP_HEAVY, SP_PISTOL };
typedef struct {
    const Art *art;
    const char *floor, *title;
    s8 reach[3];        /* added to the low/mid/high reach */
    s8 windup;          /* frames added to (or taken from) each wind-up */
    s8 hp_add;
    u8 h_low, h_mid;    /* attack mix in percent; the rest are high cuts */
    u8 special, sp_pct[3];
    u8 feint_pct[3];    /* glints that are not followed by a cut */
    u8 catch_pct[3];    /* chance to catch a mid cut barehanded */
    u8 armour;          /* mid cuts glance off until the armour is broken */
    u8 zoom;            /* Lieve's size on this floor, in 64ths (the giant needs room) */
    u8 skill;           /* sharper reactions, guards and parries as she climbs */
    u8 style;           /* ST_NORMAL, STY_JUGGERNAUT (walks through cuts), STY_NIMBLE (dodges, darts) */
} Opp;
enum { STY_NORMAL, STY_JUGGERNAUT, STY_NIMBLE, STY_DRUNK, STY_FENCER };
/* floors 0-4 are the pagoda; 5 is the pier on Dejima (the prologue), 6 the secret spire */
#define F_DOCK 5
#define F_SPIRE 6
static const Opp OPPS[7] = {
    {&art_kenji, "THE FIRST FLOOR", "MASTER OF THE GATE", {0, 0, 0}, 0, 0, 24, 40, SP_NONE, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 64, 1, STY_NORMAL},
    {&art_hozen, "THE SECOND FLOOR", "MONK OF THE STAFF", {13, 16, 15}, 1, 0, 35, 45, SP_SWEEP, {18, 28, 34}, {0, 0, 0}, {0, 0, 0}, 0, 64, 2, STY_NORMAL},
    {&art_gonzo, "THE THIRD FLOOR", "THE TWIN-BLADED WALL", {-8, -6, -8}, 0, 1, 25, 55, SP_THROW, {14, 24, 30}, {0, 0, 0}, {20, 42, 58}, 0, 64, 2, STY_JUGGERNAUT},
    {&art_kagero, "THE FOURTH FLOOR", "THE KNIVES IN THE RAIN", {2, 6, 2}, -2, 0, 20, 55, SP_NONE, {0, 0, 0}, {0, 30, 35}, {0, 0, 0}, 0, 64, 3, STY_NIMBLE},
    {&art_kurogane, "THE ROOF", "THE GIANT IN BLACK", {18, 20, 22}, 1, 1, 30, 30, SP_HEAVY, {0, 35, 45}, {0, 0, 0}, {0, 0, 0}, 1, 48, 0, STY_NORMAL},
    {&art_sailor, "DEJIMA", "A DRUNK ON THE PIER", {-12, -10, -12}, 2, -1, 30, 50, SP_NONE, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 64, 0, STY_DRUNK},
    {&art_morsel, "THE SPIRE", "ADRIAAN MORSEL", {14, 18, 16}, -1, 1, 25, 50, SP_PISTOL, {30, 40, 50}, {0, 10, 20}, {0, 0, 0}, 0, 64, 3, STY_FENCER},
};
static int floor_i, start_floor;
static int stage_i = -1, stairs_on, grey_scene;
static int dark_level, last_dark = -1;      /* the lantern hall going dark: 0..14 */
static int ending_done;
enum { CUT_PRE = 0, CUT_POST = 5, CUT_PROLOGUE = 10, CUT_DOCK_PRE = 11, CUT_DOCK_POST = 12, CUT_SPIRE_PRE = 13,
       CUT_SPIRE_POST = 14, CUT_SPIRE_SPARED = 15, CUT_SPARED = 16, CUT_SECRET = 21,
       CUT_RIVAL_HELP = 22, CUT_RIVAL_AMBUSH = 26, CUT_DARK_END = 30, NCUTS = 31 };
/* v5.5: a spared master who returns for revenge fights on the roof in the giant's place (ambush_opp) */
static int ambush_opp = -1, rival_done, rival_help;
#define OPP (&OPPS[ambush_opp >= 0 ? ambush_opp : floor_i])
static const u8 FLOOR_STAGE[7] = {0, 1, 2, 3, 4, 6, 7};
#define OPP_STAGE() (FLOOR_STAGE[floor_i])
enum { MODE_STORY, MODE_GAUNTLET, MODE_TIME };
static int game_mode;
#define WINS_NEED() ((floor_i == F_DOCK || game_mode == MODE_GAUNTLET || ambush_opp >= 0) ? 1 : 2)
static const Art *hero_art(void) {          /* her look as she climbs */
    if (floor_i == F_DOCK || floor_i == 0) return &art_lieve;
    if (floor_i <= 3) return &art_lieve2;
    return &art_lieve3;
}
static int song_for_floor(int f) { return f == F_DOCK ? SONG_DOCK : f == F_SPIRE ? SONG_SPIRE : SONG_FLOOR1 + f; }
/* the stages with an edge to fall from: the gallery's railing, the roof, the pier, the spire */
static int edge_stage(int si) { return si == 3 || si == 4 || si == 6 || si == 7; }
static int wet_stage(int si) { return si == 3 || si == 4 || si == 6; }

typedef struct {
    const Art *art;
    int cpu;
    s32 x;              /* world x, 8.8 */
    int dir;            /* +1 faces right */
    int st, t;
    int h;              /* height of the current cut */
    int stance;         /* held stance (visual) */
    int chain, riposte, unblock, dmg_bonus;
    int hp, maxhp;
    int guard_t, guard_fresh, last_release;
    int buf_a, buf_h;   /* buffered attack */
    int iframes, dodge_cool;
    int walkph;
    int anim, afr, frame, shown;
    int flash;
    int knock;          /* px/frame * 256 */
    int lunge_left;
    int ghost_t;
    s32 hist_x[8];
    int pose_anim, pose_fr;
    int hitdone;
    int meter;          /* 0..100 (player only) */
    int combo_taken, calm_t;
    int z, z0, z1;      /* depth plane, 256 per plane, -768..+768 */
    int hop, vx;        /* sidestep hop (px) and walking velocity (px*256/frame) */
    int yoff, kmul;     /* cutscene height offset; size in 64ths */
    int last_fr;        /* walking: the last stride frame (for footfalls) */
    int bloody, death_anim, sweep, feint, armour_broke;
    int hurt_h, fall, fvy;   /* height of the cut that hurt; falling off the roof */
    int mosaic;              /* a blow to the head blurs; the fallen dissolve */
    int disarmed;
    u8 nmarks, marks[5][3];  /* cuts in the clothing: anchor (0 chest, 1 thigh), dx, dy */
    /* inputs */
    int i_fwd, i_back, i_up, i_down, i_a, i_b, i_bpress, i_dodge, i_dash, i_super, i_strafe, i_jump, i_pickup, i_crouch;
    int strafe_q;            /* sidesteps queued with L / R, one plane each */
    int air_t;               /* frames into a jump that was turned into a cut or a guard: the arc carries on */
    int zslide;              /* a sidestep cut short by a cut or a guard: still gliding to its plane (z1) */
    int carry;               /* walking momentum carried into a cut (px*256/frame) */
} Fighter;
static void canvas_to_screen(const Fighter *f, int tx, int ty, int *ox, int *oy);

static Fighter fp, fe;
static int freeze, shake, flash_white, darken_t, super_dark;
static int duel_on;
static int ko_side, ko_t, finish_kind;     /* finish_kind: 1 riposte, 2 super */
/* v5.5 the replay of the final cut: the last 64 frames of both fighters are kept */
typedef struct { s32 x[2]; s16 fr[2], z[2], hop[2], yoff[2], kmul[2], camC, slx, sly; s8 dir[2], slt; u8 sla, bl[2], mos[2]; } Snap;
static Snap rec[64];
static u32 rec_head, replay_mark;
static int replay_t, replay_ok;
static int banner_t;
static int camC;
static Fighter *rip_owner;
static int rip_t;
static u32 score, best_score;
static int combo, combo_t, round_no, wins_p, wins_e, round_hp_lost;
static int tut_step, tut_t, tut_done;      /* tutorial */
static int frame_counter;
static int cpu_meter, cpu_peak, cpu_peak_t;
/* cheats (Konami code on the title, as in Greywatch) */
enum { CH_GOD = 1, CH_KI = 2, CH_MIGHTY = 4, CH_TURBO = 8, CH_BULLET = 16, CH_GHOST = 32, CH_STORM = 64, CH_ONECUT = 128, CH_FLOOR = 256 };
#define NCHEATS 8
static const char *const cheat_names[NCHEATS + 1] = {"GOD MODE", "ENDLESS KI", "MIGHTY BLOWS", "TURBO", "BULLET TIME", "GHOST VISION", "ENDLESS STORM", "ONE-CUT MASTERS", "START FLOOR"};
static int cheat_on, cheats_unlocked, cheat_sel;
static int paused, pause_sel, pause_page;
static int ai_punish;
static int oam_n;
static void load_palettes(void);
static int pistol_used;                     /* Morsel fires once a round */
static int honour;                          /* bowing, sparing, not striking the fallen */
/* v5.5 duel challenges: one optional condition per floor, kept in the save once met */
static u8 ch_done;
static int ch_guarded, ch_hit, ch_iai_ok, ch_sweep_jumped, ch_pistol_dodged, ch_final_h = -1;
static const char *const CH_TXT[7] = {"WIN WITHOUT GUARDING", "LEAP OVER HIS SWEEP", "WIN WITHOUT BEING HIT",
                                      "WIN THE IAI DRAW", "WIN WITHOUT LOSING A ROUND", "FINISH HIM WITH A LOW CUT",
                                      "EVADE HIS PISTOL SHOT"};
static u16 best_time[5], best_gaunt;        /* in frames; 0 = none */
static u32 run_frames;
static int gaunt_hp;
static void put_time(int col, int row, u32 fr, int bank);
static void restore_colour(void);
static u8 spared[7];
static int spare_t, spare_h, spare_lock, spared_now, dishonour_done, bowed;
int iai_state, iai_last;                              /* 0 none, 1 waiting for the leaf, 2 the cue has come (the harness bot reads it) */
static int iai_on, iai_wait, iai_cpu, iai_t, iai_seen, iai_res, iai_free;
static int trophy_on;
#define TITLE_STAGE 5
static int cam_k = 64, cam_px, cam_py;   /* the killing-blow zoom, about the point of impact */
static int storm_lvl;                    /* the roof: the storm grows each round */
static int shock_t;                      /* big hits ripple the stage */
static int split_y = -1;                   /* the Kurosawa wipe: colour above this line, black and white below */
static u16 split_col[512] __attribute__((aligned(4))), split_grey[512] __attribute__((aligned(4)));   /* word-aligned: copied with ldm/stm and 32-bit DMA */

static int sil_on, cine_flash;
static int chib_extra, marks_keep;                   /* more time between rounds while she cleans and sheathes the blade */
static int dawn_amt;                     /* the sun rising after the giant falls, 0..16 */
static int spot_on, spot_x, spot_w;      /* the Lantern Hall blackout: a pool of light */
static u16 spot_buf[2][161];
static int spot_cur;
static int fire_t, fire_x;               /* a fallen paper lamp burning (wall x) */
static int edge_warned;                   /* the last blow takes the giant's head, and Lieve lifts it */
static int lightning_now, cpu_req;      /* force a lightning flash; the AI's special move request */

static int reach(const Fighter *f, int h) { return f->disarmed ? 66 : REACH[h] + (f->cpu ? OPP->reach[h] : 0); }
#define SHARP(f) ((f)->art->sharp && !(f)->disarmed)
static int dist(void) { return iabs((fe.x - fp.x) >> 8); }
static int windup_len(const Fighter *f) {
    if (f->riposte) return 1;
    if (f->chain) return 2;
    int w = T_WINDUP[f->h] + (f->cpu ? OPP->windup : 0) + (f->sweep ? 3 : 0);
    if (f->cpu && OPP->armour && fe.hp * 2 <= fe.maxhp) w -= 2;      /* the giant, desperate, is faster */
    return w < 2 ? 2 : w;
}
static int is_attacking(const Fighter *f) { return f->st == ST_TELL || f->st == ST_WINDUP || f->st == ST_SWING || f->st == ST_SUPER; }

static void set_frame(Fighter *f, int anim, int fr) {
    if (f->disarmed) {                       /* no weapon: bare-handed frames */
        if (anim >= A_IDLE_LOW && anim <= A_IDLE_HIGH) { anim = A_BARE_IDLE; fr %= 6; }
        else if ((anim >= A_WALK_LOW && anim <= A_WALK_HIGH) || anim == A_BACKSTEP || anim == A_SIDESTEP) { anim = A_BARE_WALK; fr = fr * 8 / 12; }
        else if (anim >= A_STRIKE_LOW && anim <= A_STRIKE_HIGH) { anim = A_PUNCH; fr = fr * 8 / 22; }
        else if ((anim >= A_HIT_LOW && anim <= A_HIT_HIGH) || anim == A_FLINCH || anim == A_STAGGER || (anim >= A_GUARD_LOW && anim <= A_GUARD_HIGH)) { anim = A_PICKUP; fr = 0; }
    }
    if (fr >= anim_len[anim]) fr = anim_len[anim] - 1;
    if (fr < 0) fr = 0;
    if (fr >= anim_len[anim]) fr = anim_len[anim] - 1;
    if (fr < 0) fr = 0;
    f->anim = anim; f->afr = fr;
    f->frame = anim_start[anim] + fr;
}

static void banner(const char *s, int bank, int time) {
    text_clear_row(4);
    text_center(4, s, bank);
    banner_t = time;
}
static void add_score(int n) { score += n; if (score > 999999) score = 999999; }

/* ---- effects */
typedef struct { s32 x, y, vx, vy; int life, kind, gy; } Part;
#define NPART 40
static Part parts[NPART];
static int spawn_dir, spawn_gy = 150, spawn_up;
static void parry_burst(int x, int y);
static void cine_start(int ippon);
static int cine_t, cine_ippon, cine_bar;
static void brush_clear(void);
static void brush_start(const u16 *map, int w, int h, int row, int reveal, int life);
static void draw_bars(int h);
static void affine_star(int x, int y, int mtx, int inv, int ang, int pal);   /* blood: sideways bias, ground line, extra lift */
static void spawn(int kind, int x, int y, int n) {
    for (int i = 0; i < NPART && n; i++) {
        if (parts[i].life > 0) continue;
        Part *p = &parts[i];
        p->kind = kind;
        p->x = x << 8; p->y = y << 8;
        p->vx = ((int)rnd(512) - 256) * (kind ? 2 : 3) + spawn_dir * (int)rnd(500);
        p->vy = -(int)rnd(600 + spawn_up) - 150;
        p->life = kind ? 40 + rnd(30) : 10 + rnd(8);
        p->gy = spawn_gy;
        n--;
    }
}
/* blood on the floor stays until the round ends */
#define NSTAIN 22
static s16 stain_x[NSTAIN], stain_y[NSTAIN];
static u8 stain_big[NSTAIN];
static int stain_n;
/* ---- the living stage: drifting leaves, embers, mist, debris; dust and splashes underfoot */
enum { AK_LEAF, AK_EMBER, AK_MIST, AK_DEBRIS, AK_DUST, AK_RIPPLE, AK_SPRAY, AK_BREATH, AK_FLAME };
typedef struct { s32 x, y; s16 vx, vy; u8 kind, life, near, age; } Amb;
#define NAMB 26
static Amb amb[NAMB];
static int amb_lastcam;
static Amb *amb_add(int kind, int x, int y, int vx, int vy, int life, int near) {
    for (int i = 0; i < NAMB; i++) {
        Amb *p = &amb[i];
        if (p->life) continue;
        p->kind = kind; p->x = x * 256; p->y = y * 256; p->vx = vx; p->vy = vy;
        p->life = life; p->near = near; p->age = (u8)rnd(64);
        return p;
    }
    return 0;
}
static void foot_fx(int x, int y, int big) {     /* water on the wet floors, dust on the dry ones */
    if (wet_stage(stage_i)) {
        amb_add(AK_RIPPLE, x - 8, y - 4, 0, 0, 16, 0);
        for (int k = 0; k < (big ? 4 : 2); k++) amb_add(AK_SPRAY, x - 4 + (int)rnd(9), y - 6, (int)rnd(300) - 150, -200 - (int)rnd(250), 14, 0);
    } else {
        for (int k = 0; k < (big ? 3 : 1); k++) amb_add(AK_DUST, x - 8 + (int)rnd(9) - 4, y - 10, (int)rnd(160) - 80, -40, 22, 0);
    }
}
static void update_amb(void) {
    int dc = camC - amb_lastcam; amb_lastcam = camC;
    int live = !stairs_on && stage_i >= 0 && scene != S_CONTINUE;
    int n = 0;
    for (int i = 0; i < NAMB; i++) if (amb[i].life && amb[i].kind <= AK_DEBRIS) n++;
    static const u8 want[8] = {7, 9, 9, 9, 10, 5, 5, 4};
    if (live && !iai_on && n < want[stage_i] + storm_lvl * 4 && rnd(6 - storm_lvl * 2) == 0) {
        int near = rnd(5) == 0;
        int sp = (near ? 2 : 1) * (2 + storm_lvl);
        sp = stage_i == 4 ? sp : sp / 2;
        switch (stage_i) {
        case 0: case 1:
            amb_add(AK_LEAF, (int)rnd(280) - 20, -8, (int)rnd(120) - 50, (90 + (int)rnd(110)) * sp, 255, near); break;
        case 2:
            amb_add(AK_EMBER, (int)rnd(240), 150 - (int)rnd(20), (int)rnd(60) - 30, -(100 + (int)rnd(130)) * sp, 120 + rnd(100), near); break;
        case 3:
            amb_add(AK_MIST, 248, 20 + (int)rnd(130), -(768 + (int)rnd(512)) * sp, 0, 255, near); break;
        case 4:
            amb_add(rnd(2) ? AK_DEBRIS : AK_LEAF, 248, (int)rnd(120), -(640 + (int)rnd(300)) * sp / 2, (60 + (int)rnd(200)) * sp / 2, 255, near); break;
        case 6: case 7:                          /* sea fog on the pier; cloud wisps past the spire */
            amb_add(AK_MIST, 248, (stage_i == 6 ? 70 : 20) + (int)rnd(stage_i == 6 ? 80 : 120), -(380 + (int)rnd(300)) * sp, 0, 255, near); break;
        default:                                 /* the title: a few leaves on a gentle wind */
            amb_add(AK_LEAF, 248, (int)rnd(110), -(260 + (int)rnd(200)) * sp, (40 + (int)rnd(70)) * sp, 255, near); break;
        }
    }
    for (int i = 0; i < NAMB; i++) {
        Amb *p = &amb[i];
        if (!p->life) continue;
        if (!live && p->kind <= AK_DEBRIS) { p->life = 0; continue; }
        p->age++;
        int sway = 0;
        if (p->kind == AK_LEAF) sway = sin_table[(p->age * 4) & 255] >> 4;      /* +-256: a lazy flutter */
        else if (p->kind == AK_EMBER) sway = sin_table[(p->age * 6) & 255] >> 6;
        p->x += p->vx + sway - dc * (p->near ? 360 : 180);
        p->y += p->vy;
        if (p->kind == AK_SPRAY) p->vy += 40;
        if (p->kind == AK_DUST) p->vy += 4;
        if (p->kind == AK_FLAME) p->vx = (p->vx * 7) / 8 + (int)rnd(60) - 30;
        if (p->kind == AK_BREATH) { p->vx = p->vx * 15 / 16 - (stage_i == 4 ? 14 + storm_lvl * 10 : 0); p->vy -= 2; }
        if (p->life != 255) p->life--;
        int x = p->x >> 8, y = p->y >> 8;
        if (x < -40 || x > 280 || y > 170 || y < -30) p->life = 0;
    }
}
static int fig_k(const Fighter *f);
static int screen_x(const Fighter *f);
static int feet_y(const Fighter *f);
static void banner(const char *s, int bank, int t);
static void breathe(Fighter *f, int phase) {   /* the cold up high: breath clouds on the gallery and the roof */
    if (!(stage_i == 3 || stage_i == 4 || stage_i == 6 || stage_i == 7) || stairs_on) return;
    if (f->st != ST_IDLE && f->st != ST_GUARD && f->st != ST_STRAFE && f->st != ST_POSE) return;
    int c = (frame_counter + phase) % 96;
    if (c != 0 && c != 5) return;
    int k = fig_k(f), flip = f->dir < 0;
    int mx = f->art->mouth[0] - FIG_RX, my = FIG_GY - f->art->mouth[1];
    int sx = screen_x(f) + (flip ? -mx : mx) * k / 64, sy = feet_y(f) - my * k / 64;
    Amb *p = amb_add(AK_BREATH, sx + f->dir * 3, sy, f->dir * (c ? 110 : 170), -40, 28, 0);
    if (p) p->age = c ? 4 : 0;
}
static int edge_room(const Fighter *f) {      /* world px between a fighter and the roof's edge behind them */
    return STAGE_HALF - ((f->x >> 8) * -f->dir);
}
static void edge_fx(Fighter *f) {
    if (!edge_stage(stage_i) || stairs_on || !duel_on || f->fall || tut_step) return;
    int moving = f->st == ST_DODGE || f->st == ST_DASH || f->st == ST_HURT || f->st == ST_RECOIL || f->st == ST_STAGGER || f->vx;
    if (edge_room(f) < 22 && moving && rnd(3) == 0)        /* loose tiles skitter off the edge */
        amb_add(AK_DEBRIS, screen_x(f) - f->dir * 14, feet_y(f) - 3, -f->dir * (300 + (int)rnd(300)), -(100 + (int)rnd(200)), 60, 0);
    if (f == &fp && edge_room(f) < 18 && !edge_warned) { edge_warned = 1; banner("THE EDGE!", T_RED, 45); }
}
static void add_stain(int wx, int y, int big) {
    int i = stain_n < NSTAIN ? stain_n++ : (int)rnd(NSTAIN);
    stain_x[i] = wx; stain_y[i] = y; stain_big[i] = big;
}
static int slash_t, slash_x, slash_y, slash_ang;
static int zoom = 64;            /* world-to-screen scale in 64ths: 48 on the roof */
static int wx_to_screen(int wx) { return 120 + ((wx - camC) * zoom >> 6); }
static int screen_x(const Fighter *f) { return wx_to_screen(f->x >> 8); }
/* seven planes of play, z = -768 (nearest the camera) .. +768 (deepest), 256 per plane */
#define ZMAX 768
static const u8 PLANE_FEET[7] = {170, 164, 158, 152, 143, 134, 125};
static const u8 PLANE_K[7] = {64, 64, 64, 64, 56, 48, 40};       /* sprite scale in 64ths */
static int feet_y(const Fighter *f) {
    int zz = f->z + ZMAX;
    if (zz < 0) zz = 0;
    if (zz > 2 * ZMAX) zz = 2 * ZMAX;
    int i = zz >> 8, fr = zz & 255;
    int y = i >= 6 ? PLANE_FEET[6] : PLANE_FEET[i] + ((PLANE_FEET[i + 1] - PLANE_FEET[i]) * fr) / 256;
    return y - f->hop + f->yoff;
}
static int fig_k(const Fighter *f) {
    int i = (f->z + ZMAX + 128) >> 8;
    if (i < 0) i = 0;
    if (i > 6) i = 6;
    int k = PLANE_K[i];
    if (f->kmul != 64) k = ((k * f->kmul >> 6) + 2) & ~3;    /* multiples of 4 keep the blocks seamless */
    return k;
}
static int base_cut_y(int h) { return h == HIGH ? 58 : (h == MID ? 86 : 122); }
static int fcut_y(const Fighter *f, int h) { return feet_y(f) - (152 - base_cut_y(h)) * fig_k(f) / 64; }

static int prop_kind, prop_want;           /* 0 the slash; 1 a dropped weapon; 2 the bamboo top */
static const u32 *prop_src;
static Fighter *wpn_owner;                 /* whose weapon lies on the floor */
static int wpn_t, wpn_wx, wpn_x0, wpn_z;
static int pole_t, pole_x;                 /* a cut bamboo top toppling */
static u8 hz_done[2];                      /* stalks cut / lamps fallen this floor */
static void prop_load(int kind) {
    if (kind == prop_kind) return;
    volatile u8 *dst = OBJ_VRAM + FX_PROP * 32;
    if (kind == 0) plat_copy32(dst, fx_tiles, 32 * 8);
    else if (kind == 3) { plat_fill32(dst, 32 * 8); plat_copy32(dst, ret_tiles, 16 * 8); }   /* the iai reticule */
    else {
        plat_fill32(dst, 8 * 8);
        plat_copy32(dst + 8 * 32, kind == 1 ? prop_src : pole_tiles, 16 * 8);
        plat_fill32(dst + 24 * 32, 8 * 8);
    }
    prop_kind = kind;
}
static void start_slash(Fighter *victim, int h, int dirsign) {
    slash_t = 1;
    prop_load(0);
    slash_x = screen_x(victim);
    slash_y = fcut_y(victim, h);
    int a = h == HIGH ? 40 : (h == MID ? 18 : 6);
    slash_ang = dirsign > 0 ? (256 - a) & 255 : (128 + a) & 255;
}

static void gain_meter(Fighter *f, int n) {
    if (f->cpu) return;
    int was = f->meter;
    f->meter += n;
    if (f->meter > 100) f->meter = 100;
    if (was < 100 && f->meter == 100) { sq2(note(88), 1, 10, 2); }
}

/* ---- tutorial hooks */
enum { TUT_OFF, TUT_CUT, TUT_HIGHLOW, TUT_GUARD, TUT_PARRY, TUT_COUNTER, TUT_STEP, TUT_MOVES, TUT_FIGHT };
static void tut_text(const char *a, const char *b) {
    text_clear_row(17); text_clear_row(18);
    text_center(17, a, T_GOLD);
    if (b) text_center(18, b, T_WHITE);
}
static void tut_set(int s) {
    tut_step = s; tut_t = 0;
    switch (s) {
    case TUT_CUT: tut_text("PRESS A TO CUT", "GET CLOSE FIRST"); break;
    case TUT_HIGHLOW: tut_text("UP,A HIGH CUT  DOWN+A LOW", "TAP A AGAIN TO CHAIN CUTS"); break;
    case TUT_GUARD: tut_text("HOLD B TO GUARD. UP+B HIGH,", "B MID, DOWN+B LOW"); break;
    case TUT_PARRY: tut_text("PRESS B JUST AS HIS CUT", "LANDS TO PARRY IT"); break;
    case TUT_COUNTER: tut_text("NOW PRESS A!", "COUNTER-CUT"); break;
    case TUT_STEP: tut_text("L STEPS BACK, R STEPS FORWARD", "THROUGH SEVEN PLANES"); break;
    case TUT_MOVES: tut_text("UP JUMPS  DOWN CROUCHES", "< < DODGE  > > DASH  A+B DRAW"); break;
    case TUT_FIGHT: tut_text("", 0); break;
    }
}

/* ---- hit resolution */
static void kill(Fighter *att, Fighter *def, int h) {
    if (def == &fe) { ch_final_h = h; replay_mark = rec_head; replay_ok = wins_p + 1 >= WINS_NEED() && !tut_step; }
    if (def == &fp) ch_hit = 1;
    if (att == &fp && def == &fe && game_mode == MODE_STORY && floor_i != F_DOCK && !tut_step && ambush_opp < 0 &&
        wins_p == WINS_NEED() - 1 && !spare_lock && !spare_t && !def->fall) {
        spare_t = 1; spare_h = h;                /* the master is beaten: finish him, or spare him */
        def->hp = 1; def->st = ST_POSE; def->pose_anim = A_YIELD; def->pose_fr = 0; def->knock = 0;
        att->st = ST_POSE; att->pose_anim = A_IDLE_HIGH; att->pose_fr = 0; att->knock = 0;
        duel_on = 0; freeze = 10; shock_t = 12; flash_white = 2;
        music_stop(); sfx_taiko();
        return;
    }
    def->hp = 0;
    def->st = ST_DYING; def->t = 0;
    freeze = 30; flash_white = 0; darken_t = 30;
    start_slash(def, h, att->dir);
    if (SHARP(att) && att->unblock != 3) {
        def->death_anim = att->unblock == 2 || h == HIGH ? A_DEATH_HEAD : h == MID ? A_DEATH_ARM : A_DEATH_LEG;
        att->bloody = 1;
        for (int i = 1; i <= 6; i++) add_stain((def->x >> 8) + att->dir * i * 9 - 4, feet_y(def) - 3 + (i & 1), i < 4);   /* a streak */
    } else def->death_anim = A_DEATH;
    /* the final blow of the whole climb, with a blade: always the head */
    if (def == &fe && att == &fp && floor_i == 4 && ambush_opp < 0 && game_mode == MODE_STORY && wins_p >= WINS_NEED() - 1 && SHARP(att)) { def->death_anim = A_DEATH_HEAD; trophy_on = 1; }
    cam_px = (screen_x(att) + screen_x(def)) / 2; cam_py = fcut_y(def, h);
    cine_start(def == &fe);
    spawn_dir = att->dir; spawn_gy = feet_y(def) - 2; spawn_up = 500;
    spawn(1, screen_x(def), fcut_y(def, h) + 2, def->death_anim == A_DEATH ? 12 : 22);
    spawn_dir = 0; spawn_up = 0;
    sfx_cut();
    finish_kind = att->riposte ? 1 : (att->st == ST_SUPER || att->unblock == 2) ? 2 : 0;
    ko_side = (def == &fe) ? 1 : 2;
    ko_t = 0;
    music_stop();                               /* silence after the killing cut */
    duel_on = 0;
    rip_owner = 0;
}

static void sfx_shot(void);
static void disarm(Fighter *v) {
    if (wpn_owner || v->disarmed) return;
#ifdef HOST
    if (getenv("NODISARM")) return;
#endif
    v->disarmed = 1;
    wpn_owner = v; wpn_t = 1; wpn_z = v->z;
    wpn_x0 = v->x >> 8;
    int lx = wpn_x0 - v->dir * (46 + (int)rnd(30));
    if (lx > STAGE_HALF - 10) lx = STAGE_HALF - 10;
    if (lx < -STAGE_HALF + 10) lx = -STAGE_HALF + 10;
    wpn_wx = lx;
    prop_src = v->art->wpn; prop_want = 1;
    banner(v == &fp ? "DISARMED! PICK IT UP: DOWN" : "DISARMED!", v == &fp ? T_RED : T_GOLD, 60);
    sfx_clang();
}
static int wall_x(int sx) { return sx + 136 + ((camC * (int)k_table[100]) >> 8); }
static void apply_var(int vi) {               /* swap in the map tiles of a cut stalk / a fallen lamp */
    const u16 *v = stage_var[stage_i * 2 + vi];
    volatile u16 *m = (volatile u16 *)(VRAM + SB_DOJO * 0x800);
    for (int i = 0; i < stage_var_n[stage_i * 2 + vi]; i++) m[v[i * 2]] = v[i * 2 + 1];
}
static void sfx_cut(void);
static void cut_bamboo(int sx) {               /* a cut that misses may take a bamboo stalk instead */
    if (stage_i != 1 || stairs_on) return;
    int wx = wall_x(sx);
    for (int i = 0; i < 2; i++) {
        if (hz_done[i] || iabs(wx - bamboo_stalks[i]) > 16) continue;
        hz_done[i] = 1; apply_var(i);
        pole_t = 1; pole_x = bamboo_stalks[i]; prop_want = 2;
        int psx = bamboo_stalks[i] - (wx - sx);
        for (int k = 0; k < 10; k++) amb_add(AK_LEAF, psx - 10 + (int)rnd(20), 20 + (int)rnd(40), (int)rnd(400) - 200, (int)rnd(200), 255, 0);
        sfx_cut(); banner("THE BAMBOO FALLS", T_WHITE, 40);
        return;
    }
}
static void knock_lamp(Fighter *f) {           /* knocked into a paper lamp: it falls and catches fire */
    if (stage_i != 2 || stairs_on) return;
    int wx = wall_x(screen_x(f));
    for (int i = 0; i < 2; i++) {
        if (hz_done[i] || iabs(wx - hall_andon[i]) > 26) continue;
        hz_done[i] = 1; apply_var(i);
        fire_t = 540; fire_x = hall_andon[i];
        drum_fx(10, 2, 0, 0, 8); noise_fx(10, 2, 4, 3, 20);
        banner("THE LAMP FALLS! FIRE!", T_RED, 50);
        return;
    }
}
static void update_hazards(void) {
    if (fire_t > 0) {
        fire_t--;
        int fx = fire_x - (wall_x(0));
        if (frame_counter & 1) amb_add(AK_FLAME, fx - 10 + (int)rnd(20), 116, 0, -(260 + (int)rnd(200)), 18 + rnd(8), 0);
        if ((frame_counter & 7) == 0) amb_add(AK_EMBER, fx - 6 + (int)rnd(12), 104, (int)rnd(80) - 40, -(150 + (int)rnd(120)), 90, 0);
        Fighter *fs[2] = {&fp, &fe};
        for (int k = 0; k < 2; k++) {        /* the flames burn whoever stands in them */
            Fighter *f = fs[k];
            if (f->st >= ST_DYING || !duel_on || iabs(screen_x(f) - fx) > 14 || f->st == ST_HURT) continue;
            if (f->hp > 1) f->hp--;
            f->st = ST_HURT; f->t = 0; f->hurt_h = MID; f->flash = 8;
            f->knock = (screen_x(f) < fx ? -900 : 900);
            banner(f->cpu ? "HE IS BURNED!" : "BURNED!", f->cpu ? T_GOLD : T_RED, 40);
            sfx_hit();
        }
    }
    if (wpn_owner && wpn_t) { if (++wpn_t > 28) { wpn_t = 0; noise_fx(8, 1, 3, 2, 6); } }
    if (pole_t) { if (++pole_t > 44) { pole_t = 0; if (prop_want == 2) prop_want = wpn_owner ? 1 : 0; } }
    if (!slash_t && prop_want != prop_kind) prop_load(prop_want);
}
static void draw_props(void) {
    if (slash_t || prop_kind == 0 || prop_kind == 3) return;
    if (prop_kind == 1 && wpn_owner && oam_n < 124) {       /* the dropped weapon, spinning, then lying flat */
        int zz = wpn_z + ZMAX; if (zz < 0) zz = 0; if (zz > 2 * ZMAX) zz = 2 * ZMAX;
        int gy = PLANE_FEET[zz >> 8 > 6 ? 6 : zz >> 8] - 4;
        int x, y, ang = 0;
        if (wpn_t) {
            int t = wpn_t;
            x = wx_to_screen(wpn_x0 + (wpn_wx - wpn_x0) * t / 28);
            y = gy - 60 - (t * (28 - t) * 60 / 196) + t * 60 / 28;
            ang = t * 22;
        } else { x = wx_to_screen(wpn_wx); y = gy; }
        int c = sin_table[(ang + 64) & 255], sn = sin_table[ang & 255];
        oam[12 * 16 + 3] = (u16)((c * 256) >> 12); oam[12 * 16 + 7] = (u16)((sn * 256) >> 12);
        oam[12 * 16 + 11] = (u16)((-sn * 256) >> 12); oam[12 * 16 + 15] = (u16)((c * 256) >> 12);
        u16 *o = &oam[oam_n * 4];
        o[0] = ((y - 32) & 255) | 0x0300 | (1 << 14);
        o[1] = ((x - 64) & 511) | (12 << 9) | (3 << 14);
        o[2] = FX_PROP | (1 << 10) | (wpn_owner->art->pal << 12);
        oam_n++;
    }
    if (prop_kind == 2 && pole_t && oam_n < 124) {          /* the cut bamboo top topples */
        int t = pole_t;
        int ang = 64 - (t * t * 64 / 900 > 64 ? 64 : t * t * 64 / 900);   /* from upright to lying */
        int px = pole_x - wall_x(0), py = 84 + (t > 30 ? (t - 30) * 2 : 0);
        int c = sin_table[(ang + 64) & 255], sn = sin_table[ang & 255];
        int cx = px + ((c * 30) >> 12), cy = py - ((sn * 30) >> 12);
        oam[13 * 16 + 3] = (u16)((c * 256) >> 12); oam[13 * 16 + 7] = (u16)((sn * 256) >> 12);
        oam[13 * 16 + 11] = (u16)((-sn * 256) >> 12); oam[13 * 16 + 15] = (u16)((c * 256) >> 12);
        u16 *o = &oam[oam_n * 4];
        o[0] = ((cy - 32) & 255) | 0x0300 | (1 << 14);
        o[1] = ((cx - 64) & 511) | (13 << 9) | (3 << 14);
        o[2] = FX_PROP | (3 << 10) | (7 << 12);
        oam_n++;
    }
}
static void chiburi_flick(Fighter *f) {      /* the blood leaves the blade in a line on the floor */
    if (!f->bloody) return;
    int tx, ty;
    canvas_to_screen(f, f->art->tipx[f->frame], f->art->tipy[f->frame], &tx, &ty);
    spawn_dir = f->dir; spawn_gy = feet_y(f) - 2; spawn_up = 0;
    spawn(1, tx, ty, 8);
    spawn_dir = 0;
    for (int i = 0; i < 4; i++) add_stain((f->x >> 8) + f->dir * (30 + i * 7), feet_y(f) - 2 + (i & 1), 0);
    f->bloody = 0; load_palettes();
    sfx_whoosh();
}
static void sfx_fall(void) { sq1(note(86), 2, 13, 0, (7 << 4) | (1 << 3) | 2); sq2(note(79), 1, 9, 3); }
static void ring_out(Fighter *att, Fighter *def) {
    def->hp = 0;
    def->st = ST_DYING; def->t = 0; def->fall = 1; def->fvy = -560; def->knock = 0;
    def->death_anim = A_DEATH;
    freeze = 8; shake = 12; shock_t = 16;
    spawn_dir = att->dir; spawn_gy = feet_y(def) - 2;
    spawn(1, screen_x(def), feet_y(def) - 60, 6);
    spawn_dir = 0;
    sfx_hit(); sfx_fall();
    banner(stage_i == 3 ? "THROUGH THE RAILING!" : stage_i == 6 ? "INTO THE HARBOUR!" : stage_i == 7 ? "INTO THE CLOUDS!" : "OVER THE EDGE!", def == &fp ? T_RED : T_GOLD, 90);
    if (stage_i == 3) for (int k = 0; k < 8; k++)          /* splinters of the balustrade */
        amb_add(AK_DEBRIS, screen_x(def), feet_y(def) - 30 - (int)rnd(30), -def->dir * (200 + (int)rnd(500)), -(int)rnd(400), 70, 0);
    if (att == &fp) add_score(1500);
    finish_kind = 0;
    ko_side = (def == &fe) ? 1 : 2;
    ko_t = 0;
    music_stop();
    duel_on = 0;
    rip_owner = 0;
}
/* ---- v5.5 the weight of each weapon: hit-stop frames and screen shake on a hit, and on a block */
typedef struct { u8 hit_f, hit_s, blk_f, blk_s; } Heft;
static Heft heft(const Fighter *a) {
    const Art *r = a->art;
#ifdef HOST
    if (getenv("NOHEFT")) return (Heft){5, 6, 4, 4};
#endif
    if (r == &art_kurogane) return (Heft){7, 12, 3, 9};      /* the nodachi: a long stop, the screen jolts */
    if (r == &art_hozen)    return (Heft){4, 9, 3, 7};       /* the staff: blunt, it shakes more than it stops */
    if (r == &art_kenji)    return (Heft){4, 6, 3, 5};       /* the bokken: a wooden knock */
    if (r == &art_gonzo)    return (Heft){4, 5, 2, 3};       /* two swords: quick and light, one after another */
    if (r == &art_kagero)   return (Heft){2, 3, 1, 2};       /* knives: a flick */
    if (r == &art_morsel)   return (Heft){3, 3, 2, 2};       /* the rapier: a sharp, still point */
    if (r == &art_sailor)   return (Heft){5, 7, 4, 5};       /* the cutlass: heavy and graceless */
    return (Heft){5, 6, 4, 4};                                /* her katana */
}
static void land_hit(Fighter *att, Fighter *def, int h, int dmg) {
    if (def == &fp) ch_hit = 1;
    if (att == &fp && (cheat_on & CH_MIGHTY)) dmg *= 4;
    if (def == &fp && (cheat_on & CH_GOD)) dmg = 0;
    if (tut_step && tut_step != TUT_FIGHT) {
        if (def == &fe && def->hp - dmg < 1) dmg = def->hp - 1;
        if (def == &fp && def->hp - dmg < 1) dmg = def->hp - 1;
    }
    if (def->hp - dmg <= 0) { kill(att, def, h); return; }
    if (def->cpu && OPP->style == STY_JUGGERNAUT && !att->riposte && att->unblock != 2 &&
        (def->st == ST_IDLE || def->st == ST_DASH) && dist() > reach(def, MID) - 10) {
        def->hp -= dmg;                          /* he takes the cut and keeps coming */
        def->flash = 8; freeze = 4; shake = 5;
        spawn_dir = att->dir; spawn_gy = feet_y(def) - 2;
        spawn(1, screen_x(def), fcut_y(def, h) + 4, SHARP(att) ? 8 : 4);
        spawn_dir = 0;
        if (SHARP(att)) att->bloody = 1;
        sfx_hit();
        banner("HE SHRUGS OFF THE CUT!", T_RED, 40);
        if (att == &fp) { combo++; combo_t = 90; add_score(100 * combo); gain_meter(att, 18); }
        return;
    }
    if (edge_stage(stage_i) && !stairs_on && scene == S_DUEL && !tut_step &&
        edge_room(def) < 18 &&
        (def == &fp ? (att->unblock || att->chain || att->riposte)                       /* his heavy blows */
                    : ((att->riposte || att->st == ST_SUPER) && def->hp - dmg <= 2))) {    /* the giant only falls when worn down */
        ring_out(att, def);                      /* a heavy blow at the edge: over it */
        return;
    }
    def->hp -= dmg;
    def->combo_taken++; def->calm_t = 0;
    def->st = ST_HURT; def->t = 0; def->hurt_h = h;
    if (h == HIGH) def->mosaic = 12;                 /* the head rings */
    if (SHARP(att) && def->nmarks < 5) {        /* the cut stays in the cloth */
        u8 *m = def->marks[def->nmarks++];
        m[0] = h == LOW; m[1] = (u8)(rnd(9) + 124); m[2] = (u8)(rnd(9) + 124);
    }
    def->sweep = 0; def->unblock = 0; def->feint = 0;
    def->flash = 8;
    def->knock = -def->dir * 720;
    def->buf_a = 0;
    knock_lamp(def);
    { Heft hf = heft(att); freeze = att->riposte ? 14 : hf.hit_f; shake = hf.hit_s; }
    if (att->riposte) { flash_white = 2; start_slash(def, h, att->dir); }
    else if (att->chain == 2 || att->unblock == 2) start_slash(def, h, att->dir);   /* finishers leave a long trail */
    spawn_dir = att->dir; spawn_gy = feet_y(def) - 2;
    spawn(1, screen_x(def), fcut_y(def, h) + 4, SHARP(att) ? 10 : 5);
    spawn_dir = 0;
    if (SHARP(att)) att->bloody = 1;
    sfx_hit();
    if (att == &fp) {
        combo++; combo_t = 90;
        add_score(100 * combo + (att->riposte ? 500 : 0));
        gain_meter(att, 22);
        if (combo >= 2) { static char b[] = "0 HITS!"; b[0] = '0' + (combo > 9 ? 9 : combo); banner(b, T_GOLD, 40); }
        else if (att->riposte) banner("COUNTER!", T_GOLD, 40);
        if (tut_step == TUT_CUT) tut_set(TUT_HIGHLOW);
        else if (tut_step == TUT_HIGHLOW && (h != MID || att->chain)) tut_set(TUT_GUARD);
        else if (tut_step == TUT_COUNTER && att->riposte) tut_set(TUT_STEP);
    } else {
        combo = 0; round_hp_lost = 1;
        gain_meter(def, 12);
    }
}

/* ---- v5.5 sword locks: two cuts meet and bind; tap A to push. The loser is thrown open. */
static int lock_t, lock_pos, lock_cx, lock_cy;
static void start_lock(Fighter *a, Fighter *b, int cx, int cy) {
    for (Fighter *f = a; f; f = (f == a ? b : 0)) {
        f->st = ST_LOCK; f->t = 0; f->buf_a = 0; f->hitdone = 1; f->knock = 0; f->chain = 0; f->carry = 0;
    }
    lock_t = 1; lock_pos = 0; lock_cx = cx; lock_cy = cy;
    {   int mid = (fp.x + fe.x) / 2;                 /* blade to blade, close */
        fp.x = mid - fp.dir * (MIN_DIST + 6) * 128; fe.x = mid + fp.dir * (MIN_DIST + 6) * 128; }
    freeze = 6; shake = 8; spawn(0, cx, cy, 12); sfx_clang(); shock_t = 12; parry_burst(cx, cy);
    banner("BIND!  TAP A TO PUSH", T_GOLD, 70);
}
static void lock_end(int who) {                     /* 1 she wins, -1 he wins, 0 neither */
    lock_t = 0;
    text_clear_row(6);
    if (who == 0) {
        fp.st = fe.st = ST_RECOIL; fp.t = fe.t = 0;
        fp.knock = -fp.dir * 520; fe.knock = -fe.dir * 520;
        sfx_clang(); banner("CLASH!", T_WHITE, 40);
        return;
    }
    Fighter *w = who > 0 ? &fp : &fe, *l = who > 0 ? &fe : &fp;
    w->st = ST_IDLE; w->t = 0;
    l->st = ST_STAGGER; l->t = 0; l->knock = -l->dir * 700; l->buf_a = 0;
    rip_owner = w; rip_t = DF->stagger;
    freeze = 6; shake = 10; flash_white = 1; shock_t = 14; sfx_clang();
    spawn(0, lock_cx, lock_cy, 14);
    banner(who > 0 ? "THROWN OPEN!  STRIKE!" : "SHE IS THROWN OFF BALANCE!", who > 0 ? T_GOLD : T_RED, 50);
    if (who > 0) add_score(250);
}
static void lock_update(void) {
    lock_t++;
    if (fp.i_a) lock_pos += 80;                     /* her pushes */
    int strong = OPP->style == STY_JUGGERNAUT || floor_i == 4;
    lock_pos -= 5 + 2 * diff_i + 3 * strong;        /* his steady weight */
    if ((lock_t % 24) == 12 && (int)rnd(100) < 30 + diff_i * 15) lock_pos -= 40;   /* and his surges */
    {   int sh = lock_pos / 8;                       /* the pair slides toward whoever is losing */
        fp.x += fp.dir * sh; fe.x += fp.dir * sh; }
    if ((lock_t & 7) == 0) { spawn(0, lock_cx + lock_pos / 60, lock_cy, 2); shake = 2; }
    if ((lock_t % 16) == 0) noise_fx(2, 2, 1, 3, 4);
    {   static char bar[22];
        int m = 10 + lock_pos / 60; if (m < 0) m = 0; if (m > 20) m = 20;
        for (int i = 0; i < 21; i++) bar[i] = i == m ? '|' : i < m ? '>' : '<';
        bar[21] = 0;
        text_center(6, bar, lock_pos >= 0 ? T_GOLD : T_RED);
    }
    if (lock_pos >= 600) lock_end(1);
    else if (lock_pos <= -600) lock_end(-1);
    else if (lock_t >= 150) lock_end(lock_pos > 150 ? 1 : lock_pos < -150 ? -1 : 0);
}
static void resolve(Fighter *att, Fighter *def) {
    int h = att->h;
    int d = dist();
    if (def->st >= ST_DYING) {
        if (att == &fp && game_mode == MODE_STORY && !dishonour_done && d <= reach(att, h) && def->st <= ST_POSE && ko_side == 1) {
            dishonour_done = 1; honour -= spared_now ? 4 : 2;       /* a cut at the fallen, or at a man who yielded */
            banner("DISHONOUR!", T_RED, 70); sfx_hit(); shake = 6;
            spawn_dir = att->dir; spawn_gy = feet_y(def) - 2; spawn(1, screen_x(def), feet_y(def) - 20, 8); spawn_dir = 0;
        }
        return;
    }
    int dmg = att->unblock == 2 ? 3 : att->riposte ? 2 : att->chain == 2 ? 2 : 1;
    if (att->unblock == 3 || (att->unblock == 1 && att->cpu)) dmg = 2;   /* throws and the giant's overhead */
    int dz = iabs(att->z - def->z);
    int track = att->unblock == 2 || att->riposte || att->sweep ? 999 : (h == HIGH || att->unblock == 3 ? 100 : 300);
    if (dz > track && d <= reach(att, h)) {
        sfx_whoosh();
        banner(def == &fp ? "SIDESTEPPED!" : "HE STEPPED ASIDE", def == &fp ? T_GOLD : T_WHITE, 30);
        if (att == &fp) combo = 0;
        if (tut_step == TUT_STEP && def == &fp) tut_set(TUT_MOVES);
        return;
    }
    if (d > reach(att, h) || def->iframes > 0) {
        sfx_whoosh();
        if (stage_i == 1 && h != LOW && !att->disarmed) {
            int tx, ty;
            canvas_to_screen(att, att->art->tipx[att->frame], att->art->tipy[att->frame], &tx, &ty);
            cut_bamboo(tx);
        }
        if (def->iframes > 0 && d <= reach(att, h)) banner(def == &fp ? "DODGED!" : "HE DODGED", def == &fp ? T_GOLD : T_WHITE, 30);
        if (att == &fp) combo = 0;
        return;
    }
    int cx = (screen_x(att) + screen_x(def)) / 2;
    int cy = (fcut_y(att, h) + fcut_y(def, h)) / 2 + 4;
    /* both cuts landing on the same frame: the blades clash */
    if (att->unblock == 3) {                    /* a throw: guard and parry are no use */
        banner(def == &fp ? "BARGED! DON'T SIT IN GUARD" : "SHE BARGES HIM", T_RED, 40);
        land_hit(att, def, h, dmg);
        if (def->st == ST_HURT) def->knock = -def->dir * 1500;
        shake = 12;
        return;
    }
    if (((def->st == ST_SWING && !def->hitdone) || (def->st == ST_WINDUP && def->t >= windup_len(def) - 3)) && d <= reach(def, def->h) && !att->unblock && !att->riposte && !att->disarmed && !def->disarmed && !tut_step) {
#ifdef HOST
        if (getenv("NOLOCK")) goto no_lock;
#endif
        start_lock(att, def, cx, cy);               /* v5.5: the blades meet and bind */
        return;
    }
#ifdef HOST
    no_lock:
#endif
    if (def->st == ST_SWING && !def->hitdone && def->t == T_SWING[def->h] - 1 && d <= reach(def, def->h) && !att->unblock && !att->riposte) {
        att->st = ST_RECOIL; att->t = 0; att->buf_a = 0; att->knock = -att->dir * 520;
        def->st = ST_RECOIL; def->t = 0; def->buf_a = 0; def->knock = -def->dir * 520;
        freeze = 8; shake = 8; spawn(0, cx, cy, 10); sfx_clang(); shock_t = 14;
        parry_burst(cx, cy);                     /* a flare where the blades meet */
        banner("CLASH!", T_WHITE, 40);
        if (att == &fp) combo = 0;
        return;
    }
    if (def == &fe && (tut_step == TUT_GUARD || tut_step == TUT_PARRY)) {
        att->st = ST_RECOIL; att->t = 0; att->buf_a = 0; att->knock = -att->dir * 420;
        spawn(0, cx, cy, 4); sfx_block();
        banner("DEFEND FIRST!", T_WHITE, 40);
        return;
    }
    if (def->cpu && diff_i == 1 && def->st == ST_HURT && def->combo_taken >= 2 && !att->unblock && !att->riposte && (int)rnd(100) < 70) {
        def->st = ST_GUARD; def->t = 2; def->guard_t = 99; def->combo_taken = 0;
        att->st = ST_RECOIL; att->t = 0; att->buf_a = 0; att->knock = -att->dir * 420;
        freeze = 5; shake = 4; spawn(0, cx, cy, 6); sfx_block();
        banner("BREAKS FREE!", T_WHITE, 40);
        if (att == &fp) combo = 0;
        return;
    }
    /* the giant's lacquered armour turns mid cuts until it is broken */
    if (def->cpu && OPP->armour && !rival_help && h == MID && att->unblock != 2 && def->hp * 2 > def->maxhp) {
        att->knock = -att->dir * 380; att->buf_a = 0;
        freeze = 6; shake = 6; spawn(0, cx, cy, 8); sfx_clang();
        banner("ARMOUR! CUT HIGH OR LOW", T_WHITE, 45);
        if (att == &fp) combo = 0;
        return;
    }
    if (def->st == ST_CROUCH && h == HIGH && !att->riposte && !att->unblock && d <= reach(att, h)) {   /* not under an overhead */
        sfx_whoosh();
        banner(def == &fp ? "DUCKED!" : "HE DUCKS", def == &fp ? T_GOLD : T_WHITE, 30);
        if (def == &fp) add_score(100);
        return;
    }
    if (def->hop > 10 && (def->st == ST_JUMP ? h != HIGH : (def->air_t && h == LOW)) && !att->riposte && att->unblock != 2) {   /* cutting or guarding in the air, only a low cut passes under */
        if (def == &fp && att->sweep) ch_sweep_jumped = 1;
        banner("SHE LEAPS OVER THE BLADE!", T_RED, 40);      /* only a high cut catches her in the air */
        return;
    }
    if (def->st == ST_GUARD && !att->unblock && !att->riposte) {
        int right_h = def->stance == h;
        int cpu_parry = def->cpu && (DF->parry_any || right_h) && (int)rnd(100) < DF->parry_pct + OPP->skill * 5;
        int caught = def->cpu && h == MID && !att->sweep && (int)rnd(100) < OPP->catch_pct[diff_i];
        if (caught) cpu_parry = 1;
        if (cpu_parry || (!def->cpu && def->guard_fresh && def->guard_t <= DF->parry_win && (DF->parry_any || right_h))) {
            att->st = ST_STAGGER; att->t = 0; att->knock = -att->dir * 300; att->buf_a = 0;
            freeze = 10; shake = 9; flash_white = 2; shock_t = 16;
            spawn(0, cx, cy, 16);
            parry_burst(cx, cy);                       /* a big star where the blades meet */
            sfx_clang();
            rip_owner = def; rip_t = DF->stagger;
            /* a perfect parry of a heavy cut knocks the weapon away */
            if (!tut_step && h == HIGH && !wpn_owner && !att->disarmed &&
                ((def == &fp && def->guard_t <= 3) || (def->cpu && diff_i > 0 && (int)rnd(100) < 22)))
                disarm(att);
            if (def == &fp) { banner("PARRY!  PRESS A!", T_GOLD, 50); add_score(300); gain_meter(def, 30);
                if (tut_step == TUT_PARRY || tut_step == TUT_GUARD) tut_set(TUT_COUNTER); }
            else banner(caught ? "CROSSED BLADES!" : "PARRIED!", T_RED, 50);
            return;
        }
        if (att->sweep) {                           /* the staff sweeps under a guard */
            banner(def == &fp ? "SWEPT! JUMP IT (UP)" : "SWEPT!", T_RED, 45);
            land_hit(att, def, h, dmg);
            return;
        }
        if (!right_h && diff_i == 0) {            /* Rookie: a guard at the wrong height still holds, barely */
            att->st = ST_RECOIL; att->t = 0; att->buf_a = 0;
            att->knock = -att->dir * 160; def->knock = -def->dir * 420;
            freeze = 3; shake = 5; spawn(0, cx, cy, 3); sfx_block();
            if (def == &fp) { banner(h == HIGH ? "GUARD HIGH: HOLD UP+B" : h == LOW ? "GUARD LOW: HOLD DOWN+B" : "GUARD MID: HOLD B", T_WHITE, 45);
                if (tut_step == TUT_GUARD) tut_set(TUT_PARRY); }
            return;
        }
        if (!right_h && def == &fp) banner(h == HIGH ? "WRONG GUARD - IT WAS HIGH" : h == LOW ? "WRONG GUARD - IT WAS LOW" : "WRONG GUARD - IT WAS MID", T_RED, 45);
        if (right_h) {
            att->st = ST_RECOIL; att->t = 0; att->buf_a = 0;
            att->knock = -att->dir * 200; def->knock = 0;
            { Heft hf = heft(att); freeze = hf.blk_f; shake = hf.blk_s; }
            spawn(0, cx, cy, 5);
            sfx_block();
            if (def == &fp) { add_score(20); gain_meter(def, 10); if (tut_step == TUT_GUARD) tut_set(TUT_PARRY); }
            if (att == &fp) combo = 0;
            return;
        }
    }
    if (def->cpu && OPP->special == SP_SWEEP && d < 72 && !att->unblock) {
        dmg++;                                       /* inside the staff's reach */
        banner("INSIDE HIS REACH!", T_GOLD, 40);
    }
    int was_armoured = def->cpu && OPP->armour && def->hp * 2 > def->maxhp;
    land_hit(att, def, h, dmg);
    if (was_armoured && def->hp > 0 && def->hp * 2 <= def->maxhp && !def->armour_broke) {
        def->armour_broke = 1;
        banner("HIS ARMOUR BREAKS!", T_GOLD, 60);
        lightning_now = 1;
    }
}

/* ---- per-fighter update */
static void ai_wary(void);
static void cut_short(Fighter *f) {       /* leaving a move early for a cut or a guard */
    if (f->st == ST_STRAFE) { if (f->z != f->z1) f->zslide = 1; }
    else if (f->st == ST_JUMP) { f->air_t = f->t < 1 ? 1 : f->t; }
}
static void begin_attack(Fighter *f, int h) {
    f->h = h; f->hitdone = 0; f->t = 0;
    f->buf_a = 0;
    f->stance = h;
    f->sweep = 0; f->feint = 0;
    if (f->cpu && !f->chain && !f->riposte) {
        if (cpu_req == SP_SWEEP) { f->sweep = 1; f->h = h = LOW; }
        else if (cpu_req == SP_THROW) { f->unblock = 3; f->h = h = MID; }
        else if (cpu_req == SP_HEAVY) { f->unblock = 1; f->h = h = HIGH; }
        else f->feint = (int)rnd(100) < OPP->feint_pct[diff_i];
        cpu_req = 0;
    }
    if (f->cpu && !f->chain && !f->riposte && diff_i == 0 && !f->feint)
        banner(h == HIGH ? "HIGH CUT!" : h == LOW ? "LOW CUT!" : "MID CUT!", T_GOLD, 22);
    if (f->cpu && !f->chain && !f->riposte && (DF->tell || f->unblock || f->sweep || f->feint) && !(tut_step && tut_step < TUT_GUARD) && !(ai_punish && DF->tell < 20 && !f->unblock && !f->sweep && !f->feint)) {
        f->st = ST_TELL;
        sfx_kiai();
    } else f->st = ST_WINDUP;
}

/* canvas point of a fighter's current frame -> screen */
static void canvas_to_screen(const Fighter *f, int tx, int ty, int *ox, int *oy) {
    int flip = f->dir < 0;
    int left = flip ? screen_x(f) - (FIG_CW - FIG_RX) : screen_x(f) - FIG_RX;
    int k = fig_k(f), top = feet_y(f) - FIG_GY;
    *ox = flip ? left + 128 - (tx - 64) * k / 64 : left + 64 + (tx - 64) * k / 64;
    *oy = top + 160 + (ty - 160) * k / 64;
}
static void gore_spurt(Fighter *f, int n) {
    int gx = f->art->gorex[f->frame], gy = f->art->gorey[f->frame];
    if (!gx) return;
    int x, y;
    canvas_to_screen(f, gx, gy, &x, &y);
    spawn_dir = -f->dir; spawn_gy = feet_y(f) - 2; spawn_up = 700;
    spawn(1, x, y, n);
    spawn_dir = 0; spawn_up = 0;
}

static void push_hist(Fighter *f) {
    for (int i = 7; i > 0; i--) f->hist_x[i] = f->hist_x[i - 1];
    f->hist_x[0] = f->x;
}

static void update_fighter(Fighter *f, Fighter *o) {
    push_hist(f);
    if (f->st != ST_IDLE) f->vx = 0;
    if (f->st != ST_STRAFE) f->hop = 0;
    if (f->air_t && f->st != ST_JUMP) {          /* a cut or a guard thrown in the air finishes the leap */
        const int L = 30;
        f->air_t++;
        if (f->air_t >= L || f->st == ST_DEAD) { f->air_t = 0; f->hop = 0; foot_fx(screen_x(f), feet_y(f), 1); }
        else f->hop = 60 * 4 * f->air_t * (L - f->air_t) / (L * L);
    }
    if (f->zslide && f->st != ST_STRAFE) {      /* the sidestep's glide goes on under the cut or the guard */
        int dz = f->z1 - f->z;
        if (dz > 40) dz = 40; if (dz < -40) dz = -40;
        f->z += dz;
        if (f->z == f->z1) f->zslide = 0;
    }
    if (f->carry) {                              /* a walking cut steps in */
        if ((f->st == ST_TELL || f->st == ST_WINDUP || f->st == ST_SWING) && dist() - (f->carry >> 8) >= MIN_DIST) {
            f->x += f->carry * f->dir; f->carry = f->carry * 3 / 4; if (f->carry < 24 && f->carry > -24) f->carry = 0;
        } else f->carry = 0;
    }
    if (f->st != ST_HURT && ++f->calm_t > 20) f->combo_taken = 0;
    if (f->flash > 0) f->flash--;
    if (f->mosaic > 0 && f->mosaic < 40) f->mosaic--;       /* 40+ is the dissolve of the fallen */
    if (f->iframes > 0) f->iframes--;
    if (f->dodge_cool > 0) f->dodge_cool--;
    if (f->ghost_t > 0) f->ghost_t--;
    if (f->buf_a > 0) f->buf_a--;
    if (f->i_a) { f->buf_a = 8; f->buf_h = f->i_up ? HIGH : f->i_down ? LOW : MID; }
    if (f->knock) {
        f->x += f->knock;
        f->knock = f->knock * 13 / 16;
        if (iabs(f->knock) < 40) f->knock = 0;
    }
    if (f->st == ST_LOCK) {                      /* blade against blade */
        set_frame(f, A_GUARD_HIGH, 3);
        if (f == &fp && lock_t) lock_update();
        if (!lock_t && f->st == ST_LOCK) { f->st = ST_IDLE; f->t = 0; }
        return;
    }
    int held = f->i_up ? HIGH : f->i_down ? LOW : MID;
    int can_act = (f->st == ST_IDLE || f->st == ST_GUARD || f->st == ST_CROUCH || (f->st == ST_RECOVER && f->t >= 3) || (f->st == ST_DASH && f->t >= 3) || (f->st == ST_STRAFE && f->t >= 13));
    /* cuts and guards may also be thrown mid-move: out of a sidestep (the glide carries on), in the air
       (the leap carries on), at the end of a dodge, or out of a dash */
    int can_cut = can_act || (!f->cpu && ((f->st == ST_STRAFE && f->t >= 2) || (f->st == ST_JUMP && f->t >= 3 && f->t <= 25)
               || (f->st == ST_DODGE && f->t >= 6) || f->st == ST_DASH));     /* (the masters keep their own timing) */
    if (f->i_strafe) {                       /* L / R presses queue, so three presses cross three planes */
        f->strafe_q += f->i_strafe;
        if (f->strafe_q > 3) f->strafe_q = 3;
        if (f->strafe_q < -3) f->strafe_q = -3;
        f->i_strafe = 0;
    }
    if (f->st == ST_STRAFE && f->t < 13) { /* keep the queue */ }
    else if (f->strafe_q && can_act) { f->i_strafe = f->strafe_q > 0 ? 1 : -1; f->strafe_q -= f->i_strafe; }
    if (can_act && f->i_jump && (!f->cpu || OPP->style == STY_NIMBLE)) {
        f->st = ST_JUMP; f->t = 0; f->ghost_t = 10; f->strafe_q = 0; sfx_dash();
        return;
    }

    /* riposte after our parry */
    if (rip_owner == f && rip_t > 0 && f->buf_a && can_cut) {
        cut_short(f);
        f->riposte = 1; f->chain = 0; f->unblock = 0;
        begin_attack(f, HIGH);
        rip_owner = 0;
    }
    if (can_cut && f->i_super && f->meter >= 100 && !f->disarmed) {
        cut_short(f);
        f->meter = 0; f->st = ST_SUPER; f->t = 0; f->h = MID; f->unblock = 2; f->chain = 0; f->riposte = 0;
        f->hitdone = 0;
        super_dark = 18; sfx_super(); shock_t = 16;
        banner("QUICK DRAW!", T_GOLD, 50);
        add_score(200);
    }
    if (can_act && f->st != ST_SUPER && f->i_strafe) {
        int target = ((f->z + (f->z >= 0 ? 128 : -128)) / 256) * 256 + f->i_strafe * 256;
        if (target >= -ZMAX && target <= ZMAX) {
            f->st = ST_STRAFE; f->t = 0; f->z0 = f->z; f->z1 = target;
            if (fig_k(f) == 64 && target <= 0) f->ghost_t = 12;
            sfx_dash();
            if (tut_step == TUT_STEP && !f->cpu) tut_t = 0;
        }
    }
    if (can_cut && !can_act && f->st != ST_SUPER && !f->disarmed && (f->buf_a || (f->i_b && f->st != ST_GUARD))) {
        int moving_vx = f->st == ST_DASH ? 300 : 0;
        cut_short(f);                         /* leave the move; its glide or leap carries on */
        if (f->buf_a) {
            f->chain = 0; f->riposte = 0; f->unblock = 0;
            f->dmg_bonus = 0; f->carry = moving_vx;
            begin_attack(f, f->air_t ? (f->buf_h == LOW ? MID : HIGH) : f->buf_h);   /* from the air: a leaping cut */
        } else {
            f->st = ST_GUARD; f->t = 0; f->guard_t = 0;
            f->guard_fresh = f->i_bpress && (frame_counter - f->last_release >= DF->relock);
        }
    }
    if (can_act && f->st != ST_SUPER && f->st != ST_STRAFE) {
        if (f->i_dodge && f->dodge_cool == 0) {
            f->st = ST_DODGE; f->t = 0; f->dodge_cool = (f->cpu && OPP->style == STY_NIMBLE) ? 12 : 26; f->iframes = (f->cpu && OPP->style == STY_NIMBLE) ? 13 : 10; f->ghost_t = 14; sfx_dash();
        } else if (f->buf_a && f->st != ST_GUARD + 100) {
            f->chain = 0; f->riposte = 0; f->unblock = 0;
            f->dmg_bonus = (f->st == ST_DASH);
            f->carry = f->st == ST_IDLE && f->vx > 0 && !f->cpu ? f->vx : 0;   /* a cut thrown while walking in steps in */
            begin_attack(f, f->buf_h);
        } else if (f->i_dash && f->st == ST_IDLE) {
            f->st = ST_DASH; f->t = 0; f->ghost_t = 12; sfx_dash();
        } else if (f->disarmed && wpn_owner == f && wpn_t == 0 && f->st == ST_IDLE && f->i_pickup && iabs((f->x >> 8) - wpn_wx) < 16) {
            f->st = ST_PICKUP; f->t = 0;
        } else if (f->i_b && f->st != ST_GUARD && !f->disarmed) {
            f->st = ST_GUARD; f->t = 0;
            f->guard_t = 0;
            f->guard_fresh = f->i_bpress && (frame_counter - f->last_release >= DF->relock);
        } else if (f->i_crouch && (f->st == ST_IDLE || f->st == ST_RECOVER)) {
            f->st = ST_CROUCH; f->t = 0;
        } else if (f->st == ST_RECOVER && (f->i_fwd || f->i_back)) {
            f->st = ST_IDLE; f->t = 0;
        }
    }

    switch (f->st) {
    case ST_IDLE: {
        f->stance = held;
        int target = f->i_fwd ? 470 : f->i_back ? -400 : 0;
        if (f->cpu && OPP->style == STY_NIMBLE) target = target * 9 / 5;
        if (f->cpu && OPP->style == STY_JUGGERNAUT && f->i_fwd) target = 560;
        if (f->vx < target) { f->vx += f->vx < 0 ? 95 : 60; if (f->vx > target) f->vx = target; }
        else if (f->vx > target) { f->vx -= f->vx > 0 ? 95 : 60; if (f->vx < target) f->vx = target; }
        if (f->vx) {
            f->x += f->vx * f->dir;
            f->walkph += f->vx;
            int fr = ((f->walkph >> 9) % 12 + 12) % 12;   /* one 12-frame stride cycle per 24 px */
            if (fr != f->last_fr && (fr == 3 || fr == 9)) foot_fx(screen_x(f) + f->dir * (fr == 3 ? 6 : -6), feet_y(f), 0);   /* each foot lands at the bottom of the stride */
            f->last_fr = fr;
            set_frame(f, A_WALK_LOW + f->stance, fr);
        } else {
            f->t++;
            set_frame(f, A_IDLE_LOW + f->stance, (f->t / 6) & 7);
        }
        break;
    }
    case ST_TELL:
        set_frame(f, A_IDLE_LOW + f->h, 0);
        {
            int tl = DF->tell + (tut_step == TUT_GUARD ? 14 : 0) + (f->sweep ? 6 : 0) + (f->unblock == 3 ? 12 : 0) + (f->unblock == 1 && f->cpu ? 14 : 0);
            if (f->unblock && f->cpu && tl < 14) tl = 14;          /* specials always get a clear warning */
            if (++f->t >= tl) {
                if (f->feint) { f->st = ST_IDLE; f->t = 0; f->feint = 0; }    /* only a glint */
                else { f->st = ST_WINDUP; f->t = 0; }
            }
        }
        break;
    case ST_GUARD:
        if (f->guard_t < 255) f->guard_t++;
        if (held != f->stance) { f->stance = held; }
        set_frame(f, A_GUARD_LOW + f->stance, f->t < 3 ? f->t : 3);
        f->t++;
        if (!f->cpu && !f->air_t && (f->i_fwd || f->i_back)) {      /* edging in or out behind the guard */
            int sp = f->i_fwd ? 190 : -170;
            if (sp < 0 || dist() - 1 >= MIN_DIST) f->x += sp * f->dir;
            f->walkph += sp;
            if (((f->walkph >> 9) & 7) == 0 && (frame_counter & 7) == 0) foot_fx(screen_x(f), feet_y(f), 0);
        }
        if (!f->i_b) { f->st = ST_IDLE; f->t = 0; f->last_release = frame_counter; }
        break;
    case ST_WINDUP: {
        int w = windup_len(f);
        {   /* cuts drift toward the opponent's plane; a cut out of a dash keeps homing */
            int dz = o->z - f->z;
            if (f->dmg_bonus) { if (dz > 64) dz = 64; if (dz < -64) dz = -64; f->z += dz; }
        }
        set_frame(f, A_STRIKE_LOW + f->h, f->chain || f->riposte ? 5 : f->t * 6 / w);
        if (++f->t >= w) {
            f->st = ST_SWING; f->t = 0; sfx_whoosh();
            {
                int need = dist() - (reach(f, f->h) - 6);
                int cap = LUNGE[f->h] + (f->dmg_bonus ? 8 : 0);
                if (need < 0) need = 0;
                if (need > cap) need = (dist() - reach(f, f->h) <= cap) ? cap : 0;
                f->lunge_left = need << 8;
            }
        }
        break;
    }
    case ST_SWING:
    case ST_SUPER: {
        if (f->st == ST_SUPER && f->t < 18) {
            /* the dash: hold the draw pose, then streak forward */
            set_frame(f, A_STRIKE_MID, 5);
            f->ghost_t = 10;
            if (super_dark == 0) {
                if (dist() > 64) f->x += 8 * 256 * f->dir;
                else f->t = 17;
                f->t++;
            }
            break;
        }
        int sw = T_SWING[f->h];
        int tt = f->st == ST_SUPER ? f->t - 18 : f->t;
        set_frame(f, A_STRIKE_LOW + f->h, 6 + (tt * 6) / sw);
        if (f->lunge_left > 0) {
            int step = (f->lunge_left + sw - 1) / sw + 64;
            if (step > f->lunge_left) step = f->lunge_left;
            if (dist() - (step >> 8) >= MIN_DIST) f->x += step * f->dir;
            f->lunge_left -= step;
        }
        if (tt == sw - 1 && !f->hitdone) {
            set_frame(f, A_STRIKE_LOW + f->h, 11);
            f->hitdone = 1;
            resolve(f, o);
            if (f->st != ST_SWING && f->st != ST_SUPER) break;
        }
        if (++tt >= sw) { f->st = ST_FOLLOW; f->t = 0; }
        else f->t++;
        break;
    }
    case ST_FOLLOW: {
        int fl = T_FOLLOW[f->h];
        set_frame(f, A_STRIKE_LOW + f->h, 12 + (f->t * 4) / fl);
        int max_chain = f->cpu ? 1 : 2;
        if (f->buf_a && f->chain < max_chain && !f->riposte && f->unblock != 2 && f->t >= 1) {
            f->chain++;
            int nh = f->chain == 2 ? HIGH : f->buf_h;
            begin_attack(f, nh);
            f->st = ST_WINDUP;
            break;
        }
        if (++f->t >= fl) { f->st = ST_RECOVER; f->t = 0; }
        break;
    }
    case ST_RECOVER: {
        int rc = T_RECOVER[f->h] + (f->chain == 2 ? 8 : 0) + (f->unblock == 2 ? 10 : 0) + (f->sweep || f->unblock == 3 ? 8 : 0);
        if (f->cpu && OPP->style == STY_NIMBLE) rc = rc * 2 / 3;      /* knives recover fast */
        set_frame(f, A_STRIKE_LOW + f->h, 16 + (f->t * 6) / rc);
        if (++f->t >= rc) { f->st = ST_IDLE; f->t = 0; f->chain = 0; f->riposte = 0; f->unblock = 0; f->dmg_bonus = 0; f->sweep = 0; }
        break;
    }
    case ST_HURT:
        set_frame(f, A_HIT_LOW + f->hurt_h, f->t * 6 / 14);
        if (++f->t >= 14) { f->st = ST_IDLE; f->t = 0; if (f->cpu) ai_wary(); }
        break;
    case ST_STAGGER: {
        int len = DF->stagger;
        set_frame(f, A_STAGGER, f->t < 6 ? f->t / 2 : f->t < len - 14 ? 3 : 4 + (f->t - (len - 14)) * 4 / 14);
        if (++f->t >= len) { f->st = ST_IDLE; f->t = 0; f->chain = 0; }
        break;
    }
    case ST_RECOIL:
        set_frame(f, A_FLINCH, f->t * 6 / 12);
        if (++f->t >= 12) { f->st = ST_IDLE; f->t = 0; f->chain = 0; }
        break;
    case ST_DODGE: {
        int r = 14 - f->t;
        int sp = 7 * 256 * r * r / 196;
        f->x -= sp * f->dir;
        set_frame(f, A_BACKSTEP, f->t * 6 / 14);
        if (++f->t >= 14) { f->st = ST_IDLE; f->t = 0; }
        break;
    }
    case ST_STRAFE: {
        static const u8 ease16[17] = {0, 3, 10, 22, 37, 55, 75, 97, 120, 143, 165, 186, 204, 219, 232, 241, 245};
        static const u8 hop16[17] = {0, 1, 2, 3, 4, 4, 5, 5, 5, 5, 5, 4, 4, 3, 2, 1, 0};
        f->t += (f->cpu && OPP->style == STY_NIMBLE && f->t < 14) ? 2 : 1;
        if (f->t > 16) f->t = 16;
        int e = f->t >= 16 ? 256 : ease16[f->t] * 256 / 245;
        f->z = f->z0 + (f->z1 - f->z0) * e / 256;
        f->hop = hop16[f->t > 16 ? 16 : f->t];
        set_frame(f, A_SIDESTEP, f->t * 8 / 17);
        if (f->t >= 16) { f->z = f->z1; f->hop = 0; f->st = ST_IDLE; f->t = 0;
            if (tut_step == TUT_STEP && !f->cpu) tut_set(TUT_MOVES); }
        break;
    }
    case ST_DASH: {
        if (f->t == 0) foot_fx(screen_x(f) - f->dir * 10, feet_y(f), 1);
        if (f->t == 4 && stage_i == 3 && !tut_step && (int)rnd(100) < (f->cpu && OPP->style == STY_NIMBLE ? 4 : 8)) {
            f->st = ST_HURT; f->t = 4; f->hurt_h = LOW; f->knock = f->dir * 500;     /* the wet boards */
            foot_fx(screen_x(f), feet_y(f), 1); foot_fx(screen_x(f) + f->dir * 8, feet_y(f), 1);
            banner(f->cpu ? "HE SLIPS!" : "SLIPPED!", f->cpu ? T_GOLD : T_RED, 40);
            sfx_whoosh();
            break;
        }
        int r = 12 - f->t;
        int sp = 7 * 256 * r * (r + 4) / 192;
        if (f->cpu && OPP->style == STY_NIMBLE) sp = sp * 5 / 4;
        if (dist() - (sp >> 8) >= MIN_DIST) f->x += sp * f->dir;
        if (f->t == 0) { f->z0 = f->z; }
        {   /* home in on the opponent's plane, at most two planes per dash */
            int tz = o->z;
            if (tz > f->z0 + 512) tz = f->z0 + 512;
            if (tz < f->z0 - 512) tz = f->z0 - 512;
            int dz = tz - f->z;
            if (dz > 104) dz = 104;
            if (dz < -104) dz = -104;
            f->z += dz;
        }
        set_frame(f, A_WALK_MID, f->t % 12);
        if (++f->t >= 12) { f->st = ST_IDLE; f->t = 0; }
        break;
    }
    case ST_AIM: {                          /* Morsel levels the flintlock: sidestep or dodge before he fires */
        set_frame(f, A_AIM, f->t < 6 ? 0 : f->t < 12 ? 1 : f->t < 46 ? 2 : 3);
        if (f->t == 6) { sfx_kiai(); banner(o == &fp ? "PISTOL! SIDESTEP!" : "PISTOL!", T_RED, 40); }
        if (f->t == 44) {
            int mx = screen_x(f) + f->dir * 44, my = feet_y(f) - 96 * fig_k(f) / 64;
            sfx_shot(); flash_white = 1; shake = 6; shock_t = 10;
            for (int k = 0; k < 6; k++) amb_add(AK_DUST, mx + f->dir * (int)rnd(14), my - 4 + (int)rnd(8), f->dir * (60 + (int)rnd(120)), -(int)rnd(60), 30, 0);
            spawn(0, mx, my, 6);
            int dz = iabs(o->z - f->z);
            if (dz < 200 && o->iframes == 0 && o->st < ST_DYING && o->st != ST_STRAFE && o->st != ST_DODGE) {
                if (o == &fp && (cheat_on & CH_GOD)) banner("THE SHOT GLANCES OFF", T_GOLD, 40);
                else {
                    int dmg = 2;
                    if (o->hp - dmg <= 0) { kill(f, o, MID); }
                    else { o->hp -= dmg; o->st = ST_HURT; o->t = 0; o->hurt_h = MID; o->flash = 8; o->knock = -o->dir * 900;
                           spawn_dir = f->dir; spawn_gy = feet_y(o) - 2; spawn(1, screen_x(o), fcut_y(o, MID), 10); spawn_dir = 0;
                           banner(o == &fp ? "SHOT!" : "HIT!", T_RED, 40); sfx_hit(); if (o == &fp) { combo = 0; round_hp_lost = 1; } }
                }
            } else { if (o == &fp) ch_pistol_dodged = 1; banner(o == &fp ? "THE SHOT MISSES!" : "MISSED", T_GOLD, 40); }
        }
        if (++f->t >= 66) { f->st = ST_IDLE; f->t = 0; }
        break;
    }
    case ST_PICKUP:
        set_frame(f, A_PICKUP, f->t * 6 / 28);
        if (f->t == 12 && wpn_owner == f) { wpn_owner = 0; prop_want = 0; f->disarmed = 0; sfx_stance(); }
        if (++f->t >= 28) { f->st = ST_IDLE; f->t = 0; }
        break;
    case ST_CROUCH:                         /* under high cuts; A from here cuts low, B guards low */
        set_frame(f, A_CROUCH, f->t < 3 ? 0 : 1);
        f->t++;
        if (!f->cpu && (f->i_fwd || f->i_back)) {                    /* a low shuffle */
            int sp = f->i_fwd ? 130 : -120;
            if (sp < 0 || dist() - 1 >= MIN_DIST) f->x += sp * f->dir;
        }
        if (!f->i_crouch) { f->st = ST_IDLE; f->t = 0; }
        break;
    case ST_JUMP: {
        const int L = 30;
        f->t++;
        f->hop = f->t >= L ? 0 : 60 * 4 * f->t * (L - f->t) / (L * L);
        set_frame(f, A_JUMP, f->t < 4 ? 0 : f->t < 8 ? 1 : f->t < 16 ? 2 : f->t < 22 ? 3 : f->t < 26 ? 4 : 5);
        if (f->cpu) f->x -= f->dir * 120;          /* Kagero drifts back a little */
        else f->x += f->dir * (f->i_fwd ? 160 : f->i_back ? -160 : 0);
        if (f->i_a) { f->buf_a = L - f->t + 3; f->buf_h = HIGH; }   /* a cut from the air lands as a high cut */
        if (f->t == L - 1) foot_fx(screen_x(f), feet_y(f) + f->hop, 1);
        if (f->t >= L) { f->st = ST_IDLE; f->t = 0; f->hop = 0; }
        break;
    }
    case ST_DYING:
        if (f->fall) {                           /* over the edge of the roof, tumbling */
            f->x -= f->dir * 3 * 256;
            f->yoff += f->fvy >> 8; f->fvy += 70;
            if (f->t < 10) set_frame(f, A_HIT_MID, f->t < 4 ? 1 : 2);
            else { int fr = (f->t - 10) / 3; set_frame(f, A_DEATH, fr > 6 ? 6 : fr); }
            f->t++;
            if (feet_y(f) > 340) { f->st = ST_DEAD; drum_fx(8, 2, 0, 0, 10); }
            break;
        }
        if (f->death_anim != A_DEATH) {           /* the severed part flies while the body stands */
            set_frame(f, f->death_anim, f->t * GORE_FLIGHT / 40);
            if ((f->t % 3) == 0) gore_spurt(f, 3);
        } else set_frame(f, A_DEATH, 0);
        if (++f->t >= 40) { f->st = ST_DEATH; f->t = 0; }
        break;
    case ST_DEATH: {
        int a = f->death_anim, off = a == A_DEATH ? 0 : GORE_FLIGHT;
        set_frame(f, a, off + f->t / 5);
        if (a != A_DEATH && (f->t % 6) == 0 && f->t < 50) gore_spurt(f, 2);
        if (f->t == (anim_len[a] - off) * 5 - 12) foot_fx(screen_x(f) + f->dir * 12, feet_y(f), 1);   /* the body hits the floor */
        if (++f->t >= (anim_len[a] - off) * 5) f->st = ST_DEAD;
        break;
    }
    case ST_DEAD:
        if (f->fall) break;
        set_frame(f, f->death_anim, anim_len[f->death_anim] - 1);
        break;
    case ST_VICTORY:
        if (f == &fp && f->t == 9 && !f->disarmed) chiburi_flick(f);
        if (f == &fp && trophy_on) {
            int t = f->t;
            if (t < 24) set_frame(f, f->disarmed ? A_BARE_IDLE : A_CHIBURI, t / 4);
            else if (t < 34) set_frame(f, A_TURN, 0);
            else if (t < 44) set_frame(f, A_TURN, 1);
            else if (t < 70) set_frame(f, A_TURN, 2);            /* faces the screen... */
            else {
                if (t >= 96 && (t & 3) == 0 && dawn_amt < 16 && honour > -3) dawn_amt++;    /* and then the sun comes up (not for a demon) */
                if (t == 130) REG_DISPCNT &= ~0x0200;            /* the rain stops */
                if (t == 70) {                                   /* ...lightning, and the head is in her hand */
                    lightning_now = 1;
                    fe.st = ST_POSE; fe.pose_anim = A_HEADLESS; fe.pose_fr = 0;
                }
                set_frame(f, A_TROPHY, t < 78 ? 0 : t < 86 ? 1 : 2);
            }
        } else if (f == &fp) {
            int t = f->t;
            if (t < 24) set_frame(f, f->disarmed ? A_BARE_IDLE : A_CHIBURI, t / 4);
            else if (t < 32) set_frame(f, A_TURN, 0);            /* stands, still in profile */
            else if (t < 40) set_frame(f, A_TURN, 1);            /* turning... */
            else if (t < 50) set_frame(f, A_TURN, 2);            /* ...to face the screen */
            else if (t < 58) set_frame(f, A_BOWF, 0);
            else if (t < 66) set_frame(f, A_BOWF, 1);
            else if (t < 110) set_frame(f, A_BOWF, 2);           /* the bow, held */
            else if (t < 118) set_frame(f, A_BOWF, 1);
            else if (t < 126) set_frame(f, A_BOWF, 0);
            else set_frame(f, A_TURN, 2);
        } else set_frame(f, A_VICTORY, f->t / 8);
        f->t++;
        break;
    case ST_POSE:
        set_frame(f, f->pose_anim, f->pose_fr);
        break;
    }
}

static void separate(void) {
    if (fp.fall || fe.fall) return;
    int d = (fe.x - fp.x) >> 8;
    if (d < MIN_DIST) {
        int push = (MIN_DIST - d) << 7;
        fp.x -= push; fe.x += push;
    }
    int lim = STAGE_HALF << 8;
    if (fp.x < -lim) fp.x = -lim;
    if (fe.x > lim) fe.x = lim;
    if (fp.x > fe.x - (MIN_DIST << 8)) fp.x = fe.x - (MIN_DIST << 8);
}

/* ------------------------------------------------------------------ Kenji's AI */
typedef struct { int react, react_t, plan, gh, parry, atk_wait, want, rip_t, chain_ok, retreat_t, wary, hold_t, hold_h, think, aggro, punish, dash_atk, align_t; } AI;
static AI ai;
enum { P_NONE, P_GUARD, P_DODGE, P_JUMP };

static void ai_wary(void) { ai.wary = 45; }

static void clear_inputs(Fighter *f) {
    f->i_fwd = f->i_back = f->i_up = f->i_down = f->i_a = f->i_b = f->i_bpress = f->i_dodge = f->i_dash = f->i_super = f->i_strafe = f->i_jump = f->i_pickup = f->i_crouch = 0;
}

static void ai_attack(Fighter *me, int h) {
    me->i_a = 1;
    me->i_up = h == HIGH; me->i_down = h == LOW;
}

static int ai_crouch_t, gap_a;
static void ai_update(Fighter *me, Fighter *op) {
    const int RMIN = DF->react_min - OPP->skill > 4 ? DF->react_min - OPP->skill : 4;
    const int GPCT = DF->guard_pct + OPP->skill * 5, PPCT = DF->parry_pct + OPP->skill * 5;
    int was_b = me->i_b;
    clear_inputs(me);
    if (ai_crouch_t > 0) { ai_crouch_t--; if (me->st == ST_IDLE || me->st == ST_CROUCH) { me->i_crouch = 1; return; } }
    if (op->st == ST_WINDUP && op->t == 0) ai.aggro += 34;
    if (ai.aggro > 0) ai.aggro--;
    if (ai.aggro > 160) ai.aggro = 160;
    if (me->st >= ST_DYING || op->st >= ST_DYING) return;
    int d = dist();
    int busy = !(me->st == ST_IDLE || me->st == ST_GUARD);
    if (me->disarmed && wpn_owner == me) {           /* go and get the weapon back */
        if (busy || wpn_t) return;
        int wd = wpn_wx - (me->x >> 8);
        if (iabs(wd) < 12) { me->i_pickup = 1; return; }
        if (d < 70 && !(op->st == ST_RECOVER || op->st == ST_STAGGER) && rnd(4) == 0) { ai_attack(me, MID); return; }   /* a desperate punch */
        if ((wd > 0) == (me->dir > 0)) me->i_fwd = 1; else me->i_back = 1;
        return;
    }
    if (OPP->special == SP_PISTOL && !pistol_used && me->st == ST_IDLE && !tut_step && d > 100 &&
        (int)rnd(1000) < OPP->sp_pct[diff_i] && op->st != ST_SUPER) {
        pistol_used = 1; me->st = ST_AIM; me->t = 0; return;
    }
    if (OPP->style == STY_DRUNK && !tut_step) {      /* he lurches, stumbles and swings wild */
        if (!busy && rnd(260) == 0) { if (rnd(2)) me->i_dodge = 1; else me->i_dash = 1; return; }
        if (!busy && rnd(5) == 0) { if (rnd(2)) me->i_fwd = 1; else me->i_back = 1; }
    }

    /* tutorial scripting */
    if (tut_step == TUT_CUT || tut_step == TUT_HIGHLOW || tut_step == TUT_COUNTER || tut_step == TUT_STEP) {
        if (d > 150) me->i_fwd = 1;
        return;
    }
    if (tut_step == TUT_GUARD || tut_step == TUT_PARRY) {
        if (busy) return;
        if (d > 104) { me->i_fwd = 1; return; }
        if (++ai.atk_wait > 70) { ai.atk_wait = 0; ai_attack(me, MID); }
        return;
    }

    /* riposte after we parried */
    if (rip_owner == me && rip_t > 0) {
        if (ai.rip_t == 0) ai.rip_t = RMIN / 2 + 1 + rnd(DF->react_rng);
        if (--ai.rip_t == 0 && (int)rnd(100) < DF->rip_pct) me->i_a = 1;
        return;
    }
    ai.rip_t = 0;

    /* react to an incoming cut */
    if (is_attacking(op) && op->st != ST_TELL) {
        if (!ai.react) {
            ai.react = 1;
            ai.react_t = RMIN + rnd(DF->react_rng);
            int gp = GPCT;
            if (ai.wary) { ai.react_t /= 2; gp += DF->wary_bonus; }
            if (OPP->style == STY_NIMBLE) ai.react_t = ai.react_t * 3 / 5;   /* a ninja's reflexes */
            if (OPP->style == STY_DRUNK) { ai.react_t = ai.react_t * 2 + 4; gp -= 25; }
            if (OPP->style == STY_FENCER) { gp += 10; }
            if (me->st == ST_GUARD) ai.react_t = 0;
            int r = rnd(100);
            int dp = DF->dodge_pct;
            if (OPP->style == STY_NIMBLE) { dp += 30 + 5 * diff_i; gp -= 20; }
            if (OPP->style == STY_JUGGERNAUT) gp -= 25;
            int jp = OPP->style == STY_NIMBLE && op->h != HIGH ? 22 + 12 * diff_i : 0;
            if (d > reach(op, op->h) + 24) ai.plan = P_NONE;
            else if ((int)rnd(100) < jp) ai.plan = P_JUMP;
            else if (r < dp && OPP->style == STY_NIMBLE) ai.plan = P_DODGE;
            else if (r < gp) ai.plan = P_GUARD;
            else if (r < gp + dp) ai.plan = P_DODGE;
            else ai.plan = P_NONE;
            ai.gh = (int)rnd(100) < DF->right_h_pct ? op->h : (int)rnd(3);
            if (op->h == HIGH && diff_i > 0 && (int)rnd(100) < 35 && d <= reach(op, op->h) + 24) ai.plan = P_DODGE;
            ai.parry = (int)rnd(100) < PPCT;
        }
        if (ai.react_t > 0) { ai.react_t--; me->i_b = was_b && me->st == ST_GUARD; }
        else if (!busy) {
            int left = 99;
            if (op->st == ST_WINDUP) left = windup_len(op) - op->t + T_SWING[op->h];
            else if (op->st == ST_SWING) left = T_SWING[op->h] - op->t;
            if (ai.plan == P_GUARD) {
                if (me->st == ST_GUARD || !ai.parry || left <= 6) {
                    me->i_b = 1; me->i_bpress = !was_b;
                    me->i_up = ai.gh == HIGH; me->i_down = ai.gh == LOW;
                }
            } else if (ai.plan == P_JUMP) {
                me->i_jump = 1; ai.dash_atk = 1;
                ai.plan = P_NONE;
            } else if (ai.plan == P_DODGE) {
                if (op->h == HIGH && diff_i > 0 && OPP->style != STY_JUGGERNAUT && (int)rnd(100) < 25) ai_crouch_t = 26;   /* duck under it */
                else if (OPP->style == STY_NIMBLE && op->h == HIGH && (int)rnd(100) < 75) { me->i_strafe = me->z >= ZMAX ? -1 : me->z <= -ZMAX ? 1 : (rnd(2) ? 1 : -1); ai.dash_atk = 1; }
                else if (op->h == HIGH && diff_i > 0 && (int)rnd(100) < 70) me->i_strafe = me->z >= ZMAX ? -1 : me->z <= -ZMAX ? 1 : (rnd(2) ? 1 : -1);
                else { me->i_dodge = 1; if (OPP->style == STY_NIMBLE) ai.dash_atk = 1; }
                ai.plan = P_NONE;
            }
        }
        if (ai.plan == P_NONE && !busy && diff_i > 0 && ai.react_t == 0 && op->st == ST_WINDUP) {
            int left = windup_len(op) - op->t + T_SWING[op->h];
            if (left > T_WINDUP[LOW] + T_SWING[LOW] + 1 && d <= reach(me, LOW) + LUNGE[LOW] && (int)rnd(100) < 50) { ai_punish = 1; ai_attack(me, LOW); return; }
        }
        if (OPP->style == STY_JUGGERNAUT && ai.plan == P_NONE && !busy && d > reach(me, MID)) { me->i_fwd = 1; return; }   /* through the cut */
        if (ai.plan != P_NONE || d <= reach(op, op->h) + 8) return;
    }
    if (ai.react && ai.plan == P_GUARD && me->st == ST_GUARD && op->st == ST_FOLLOW) { me->i_b = 1; me->i_up = ai.gh == HIGH; me->i_down = ai.gh == LOW; return; }
    ai.react = 0;

    /* chain a second cut */
    if (me->st == ST_FOLLOW && op->st == ST_HURT && ai.chain_ok) { ai_attack(me, rnd(2) ? MID : LOW); ai.chain_ok = 0; return; }
    /* step back into line with her */
    if (!busy && iabs(me->z - op->z) >= 200 && op->st != ST_STRAFE) {
        if (++ai.align_t > RMIN * (diff_i == 0 ? 3 : 1) + 6) {
            ai.align_t = 0;
            if (diff_i > 0 && iabs(me->z - op->z) >= 400 && (int)rnd(100) < 50) { me->i_dash = 1; return; }
            me->i_strafe = op->z > me->z ? 1 : -1;
            return;
        }
    } else ai.align_t = 0;
    if (me->st == ST_DASH && ai.dash_atk && me->t >= 2 && d <= reach(me, MID) + LUNGE[MID]) { ai.dash_atk = 0; ai_punish = 1; ai_attack(me, MID); return; }
    if (busy) return;
    ai.dash_atk = 0;
    ai.chain_ok = (int)rnd(100) < DF->chain_pct;

    if (ai.wary > 0) ai.wary--;
    int opening = op->st == ST_RECOVER || op->st == ST_STAGGER || op->st == ST_RECOIL || (op->st == ST_FOLLOW && op->chain == 2);
    int flurry = ai.aggro >= 30 && (op->st == ST_WINDUP || op->st == ST_SWING || op->st == ST_FOLLOW);
    int zone = d <= reach(me, HIGH) + LUNGE[HIGH] + 12;
    int ant = DF->anticipate_pct + (ai.wary ? DF->wary_bonus : 0) + ai.aggro * DF->aggro_mult / 200;
    if (ant > 90) ant = 90;
    ai_punish = opening;
    if (opening && (diff_i < 2 || d <= reach(me, MID) + LUNGE[MID] - 6 || ai.aggro < 60)) {
        ai.hold_t = 0;
        if (ai.atk_wait > 2) ai.atk_wait = 2;
        if (diff_i > 0 && me->st == ST_IDLE && ai.aggro < 60 && d > reach(me, MID) + LUNGE[MID] - 4 && d < reach(me, MID) + LUNGE[MID] + 34 && (int)rnd(100) < 70) {
            me->i_dash = 1; ai.dash_atk = 1; return;
        }
    }

    /* strike out of a held guard when she pauses */
    if (ai.hold_t > 0 && diff_i > 0 && (op->st == ST_IDLE || op->st == ST_FOLLOW) && op->t > 4 && d <= reach(me, MID) + LUNGE[MID] - 4 && (int)rnd(100) < 12) {
        ai.hold_t = 0; ai_punish = 1; ai_attack(me, rnd(2) ? MID : LOW); return;
    }
    if (ai.hold_t > 0) {
        ai.hold_t--;
        if (flurry && ai.hold_t < 8) ai.hold_t = 8;
        me->i_b = 1; me->i_bpress = !was_b;
        me->i_up = ai.hold_h == HIGH; me->i_down = ai.hold_h == LOW;
        return;
    }
    if (--ai.think <= 0 || (flurry && ai.think > 6)) {
        ai.think = flurry ? 4 + rnd(6) : 12 + rnd(12);
        if (!opening && zone && (flurry || op->st == ST_IDLE) && (int)rnd(100) < ant) {
            if (OPP->style == STY_NIMBLE && me->st == ST_IDLE && (int)rnd(100) < 40) {   /* slip away rather than turtle */
                if (rnd(2)) me->i_strafe = me->z >= ZMAX ? -1 : me->z <= -ZMAX ? 1 : (rnd(2) ? 1 : -1);
                else me->i_dodge = 1;
                ai.dash_atk = 1;
                return;
            }
            ai.hold_t = 20 + rnd(30);
            ai.hold_h = (int)rnd(100) < DF->right_h_pct ? (flurry ? op->h : op->stance) : (int)rnd(3);
            return;
        }
    }
    /* don't walk into a flurry, but close in while a whiffing attacker recovers */
    if (flurry && !zone) {
        if (diff_i == 2 && (op->st == ST_FOLLOW || op->st == ST_RECOVER)) me->i_fwd = 1;
        return;
    }
    if (diff_i == 2 && !zone && (op->st == ST_FOLLOW || op->st == ST_RECOVER)) { me->i_fwd = 1; return; }

    if (OPP->style == STY_JUGGERNAUT && !tut_step) {
        if (me->st == ST_IDLE && d > reach(me, MID) + LUNGE[MID] + 16 && (int)rnd(100) < 6) { me->i_dash = 1; ai.atk_wait = 0; return; }
        if (d > reach(me, MID) + LUNGE[MID] - 6) { me->i_fwd = 1; if (ai.atk_wait > 4) ai.atk_wait = 4; return; }
    }
    if (OPP->style == STY_NIMBLE && !tut_step) {
        if (me->st == ST_IDLE && me->t < 2 && ai.dash_atk && d <= reach(me, MID) + LUNGE[MID] - 6) { ai.dash_atk = 0; ai_punish = 1; ai_attack(me, rnd(2) ? MID : HIGH); return; }
        /* after a sidestep, dart straight back in with a cut */
        if (ai.dash_atk && me->st == ST_IDLE && d > reach(me, MID) + LUNGE[MID] - 4 && d < reach(me, MID) + LUNGE[MID] + 50) { me->i_dash = 1; return; }
        if (me->st == ST_IDLE && ai.aggro < 30 && rnd(100) < 1) { me->i_strafe = me->z >= ZMAX ? -1 : me->z <= -ZMAX ? 1 : (rnd(2) ? 1 : -1); return; }
        if (me->st == ST_IDLE && ai.atk_wait > 10 && d < reach(me, MID) - 10 && rnd(100) < 3) { me->i_dodge = 1; return; }   /* hit and away */
    }
    if (ai.retreat_t > 0) { ai.retreat_t--; me->i_back = 1; return; }
    if (ai.atk_wait > 0) ai.atk_wait--;
    int h;
    if (floor_i == 0) h = opening ? (rnd(100) < 60 ? MID : LOW) : rnd(100) < 40 ? MID : rnd(100) < 60 ? HIGH : LOW;
    else { int r = rnd(100); h = r < OPP->h_low ? LOW : r < OPP->h_low + OPP->h_mid ? MID : HIGH; }
    if (ai.atk_wait == 0 && !tut_step) {           /* each master's special move */
        int pct = OPP->sp_pct[diff_i];
        int desperate = OPP->armour && me->hp * 2 <= me->maxhp;
        if (OPP->special == SP_SWEEP && d <= reach(me, LOW) + LUNGE[LOW] - 6 && (int)rnd(100) < pct) { cpu_req = SP_SWEEP; h = LOW; }
        else if (OPP->special == SP_THROW && d <= reach(me, MID) + LUNGE[MID] - 4 && (op->st == ST_GUARD || op->st == ST_IDLE) && (int)rnd(100) < pct + (op->st == ST_GUARD ? 30 : 0)) { cpu_req = SP_THROW; h = MID; }
        else if (OPP->special == SP_HEAVY && desperate && (int)rnd(100) < pct) { cpu_req = SP_HEAVY; h = HIGH; }
    }
    if (ai.atk_wait == 0 && d <= reach(me, h) + LUNGE[h] - 6) {
        ai_attack(me, h);
        ai.atk_wait = DF->atk_min + rnd(DF->atk_rng);
        if (OPP->style == STY_NIMBLE && diff_i < 2) ai.atk_wait = ai.atk_wait * 3 / 5;   /* quick, relentless */
        return;
    }
    if (rnd(400) == 0) { ai.retreat_t = 20 + rnd(20); return; }
    /* circle: step into another lane now and then */
    if (tut_step == TUT_OFF && ai.aggro < 30 && rnd(diff_i == 0 ? 600 : 260) == 0) { me->i_strafe = me->z >= ZMAX ? -1 : me->z <= -ZMAX ? 1 : (rnd(2) ? 1 : -1); return; }
    ai.want = reach(me, MID) + LUNGE[MID] - 10;
    if (d > ai.want + 6) me->i_fwd = 1;
    else if (d < ai.want - 18) me->i_back = 1;
    if (op->meter >= 100 && rnd(100) < 2) me->i_b = 1;
}

/* ------------------------------------------------------------------ player input */
static int tap_r, tap_l, tap_u, tap_d, a_hold_t;
static void player_input(Fighter *f) {
    clear_inputs(f);
    if (!duel_on) {             /* after the killing cut she could still strike the fallen... */
        /* a deliberate cut: A after letting go for a while, not a mashed one */
        if (ko_side == 1 && ko_t >= 16 && ko_t < 44 && game_mode == MODE_STORY && scene == S_DUEL) f->i_a = (keys_hit & KEY_A) && gap_a >= 14;
        gap_a = (keys & KEY_A) ? 0 : gap_a + 1;
        return;
    }
    f->i_fwd = (keys & KEY_RIGHT) != 0;
    f->i_back = (keys & KEY_LEFT) != 0;
    f->i_b = (keys & KEY_B) != 0;
    f->i_bpress = (keys_hit & KEY_B) != 0;
    f->i_down = (keys & KEY_DOWN) != 0;
    f->i_pickup = f->i_down;
    /* L steps into the background, R toward the screen: one plane per press, seven planes */
    if (keys_hit & KEY_L) f->i_strafe = 1;
    if (keys_hit & KEY_R) f->i_strafe = -1;
    /* Up: a short take-off; A or B in it makes a high cut or a high guard instead of a jump */
    if ((keys_hit & KEY_UP) && f->st != ST_JUMP) tap_u = 5;
    f->i_up = tap_u > 0 || ((keys & KEY_UP) && (keys & KEY_B)) || (f->st == ST_JUMP);
    if (tap_u > 0 && ((keys_hit & KEY_A) || (keys & KEY_B))) tap_u = 0;
    else if (tap_u > 0 && --tap_u == 0) f->i_jump = 1;
    /* Down held alone crouches under high cuts */
    f->i_crouch = f->i_down && !(keys & KEY_B) && !(keys_hit & KEY_A);
    if (keys_hit & KEY_RIGHT) { if (tap_r > 0) { f->i_dash = 1; tap_r = 0; } else tap_r = 12; }
    if (keys_hit & KEY_LEFT) { if (tap_l > 0) { f->i_dodge = 1; tap_l = 0; } else tap_l = 12; }
    if (tap_r > 0) tap_r--;
    if (tap_l > 0) tap_l--;
    /* A+B together = Quick Draw when the meter is full; otherwise A cuts at once */
    if (f->meter >= 100) {
        if ((keys & KEY_A) && (keys & KEY_B) && ((keys_hit & KEY_A) || (keys_hit & KEY_B))) { f->i_super = 1; a_hold_t = 0; return; }
        if (keys_hit & KEY_A) a_hold_t = 4;
        if (a_hold_t > 0 && --a_hold_t == 0) f->i_a = 1;
        if (a_hold_t > 0) f->i_b = 0;
    } else {
        f->i_a = (keys_hit & KEY_A) != 0;
    }
}

/* ------------------------------------------------------------------ rendering */
static const u16 slot_tile[9] = {0, 32, 64, 96, 160, 224, 288, 352, 416};
static const u16 slot_words[9] = {256, 256, 256, 512, 512, 512, 512, 512, 512};
static const s16 blk_x[9] = {0, 64, 128, 0, 64, 128, 0, 64, 128};
static const s16 blk_y[9] = {0, 0, 0, 32, 32, 32, 96, 96, 96};

static int show_fp = 1, show_fe = 1;
static int lightning_t = 600, lightning_seq = -1, thunder_t;
static int rain_y, lantern_f;
static int scene_dark;
static int fade;

static void stream_frame(Fighter *f) {
    if (f->frame == f->shown) return;
    const Art *a = f->art;
    const u32 *src = a->data + a->off[f->frame];
    const u16 *r = a->runs + a->roff[f->frame];
    int n = *r++;
    volatile u8 *dst = OBJ_VRAM + a->base_tile * 32;
    while (n--) {                       /* runs of stored tiles, cleared tiles and undrawn blocks */
        int v = *r++, len = v & 0x3FFF, kind = v >> 14;
        if (kind == 1) { plat_copy32(dst, src, len * 8); src += len * 8; }
        else if (kind == 0) plat_fill32(dst, len * 8);
        dst += len * 32;
    }
    f->shown = f->frame;
}

/* the hardware draws at most 1210 cycles of sprites per scanline (a normal sprite costs its width, an
   affine one 10 + 2 x its box width); sprites past that vanish for the line. Optional sprites check here. */
static s16 line_cost[160];
static void cost_build(void) {
    static const u8 SW_[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const u8 SH_[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    for (int y = 0; y < 160; y++) line_cost[y] = 0;
    for (int i = 0; i < oam_n; i++) {
        u16 a0 = oam[i * 4], a1 = oam[i * 4 + 1];
        int aff = a0 & 0x100;
        if (!aff && (a0 & 0x200)) continue;
        int sh = a0 >> 14, sz = a1 >> 14; if (sh == 3) continue;
        int w = SW_[sh][sz], h = SH_[sh][sz], dbl = aff && (a0 & 0x200);
        int y0 = a0 & 255; if (y0 >= 160) y0 -= 256;
        int y1 = y0 + (dbl ? 2 * h : h), c = aff ? 10 + 2 * (dbl ? 2 * w : w) : w;
        if (y0 < 0) y0 = 0; if (y1 > 160) y1 = 160;
        for (int y = y0; y < y1; y++) line_cost[y] += c;
    }
}
static int cost_try(int y0, int h, int c) {
    int y1 = y0 + h; if (y0 < 0) y0 = 0; if (y1 > 160) y1 = 160;
    for (int y = y0; y < y1; y++) if (line_cost[y] + c > 1210) return 0;
    for (int y = y0; y < y1; y++) line_cost[y] += c;
    return 1;
}
static void oam_hide_rest(void) {
    for (int i = oam_n; i < 128; i++) { oam[i * 4] = 0x0200; oam[i * 4 + 1] = 0; oam[i * 4 + 2] = 0; }
}
static void oam_put(int x, int y, int shape, int size, int tile, int pal, int hflip, int semi) {
    if (oam_n >= 128) return;
    u16 *o = &oam[oam_n * 4];
    o[0] = (y & 255) | (shape << 14) | (semi ? 0x0400 : 0);
    o[1] = (x & 511) | (size << 14) | (hflip ? 0x1000 : 0);
    o[2] = tile | (1 << 10) | (pal << 12);
    oam_n++;
}

static void draw_fighter_at(Fighter *f, int sx, int semi) {
    const Art *a = f->art;
    int flip = f->dir < 0;
    int left = flip ? sx - (FIG_CW - FIG_RX) : sx - FIG_RX;
    int fy = feet_y(f);
    int top = fy - FIG_GY;
    u16 m = a->mask[f->frame];
    int pal = f->flash && (f->flash & 2) ? 3 : a->pal + ((f->bloody || f->frame >= anim_start[A_DEATH_HEAD]) ? 4 : 0);
    int k = fig_k(f);
    int zk = cam_k;
    if (k == 64 && zk == 64) {
        for (int b = 0; b < 9; b++) {
            if (!(m & (1 << b))) continue;
            int bx = flip ? FIG_CW - blk_x[b] - 64 : blk_x[b];
            int x = left + bx, y = top + blk_y[b];
            if (x <= -64 || x >= 240 || y >= 160 || y <= -64) continue;
            oam_put(x, y, b < 3 ? 1 : 0, 3, a->base_tile + slot_tile[b], pal, flip, semi);
            if (f->mosaic) oam[(oam_n - 1) * 4] |= 0x1000;
        }
        return;
    }
    /* back lane: every block shares one affine matrix and is placed around a pivot at
     * canvas (64,160), so 64ths in steps of 4 keep the blocks seamless */
    int mtx = (f == &fp) ? 1 : 2;
    int kt = k * zk / 64;         /* the killing-blow zoom scales the fighters about the impact */
    if (zk != 64) kt = 128;       /* close-ups are always exactly 2x: the enlarged boxes then tile without overlap */
    int dbl = kt > 64;            /* enlarged: double-size boxes so nothing is clipped */
    int inv = 16384 / kt;         /* rounded down so each block covers its full scaled width (no seams) */
    oam[mtx * 16 + 3] = (u16)(flip ? -inv : inv);
    oam[mtx * 16 + 7] = 0;
    oam[mtx * 16 + 11] = 0;
    oam[mtx * 16 + 15] = (u16)inv;
    int px = flip ? left + 128 : left + 64, py = top + 160;
    if (zk != 64) { px = cam_px + (px - cam_px) * zk / 64; py = cam_py + (py - cam_py) * zk / 64; }
    for (int b = 0; b < 9; b++) {
        if (!(m & (1 << b))) continue;
        int h = b < 3 ? 32 : 64;
        int cx = blk_x[b] + 32 - 64;
        if (flip) cx = -cx;
        int cy = blk_y[b] + h / 2 - 160;
        int x = px + cx * kt / 64 - (dbl ? 64 : 32), y = py + cy * kt / 64 - (dbl ? h : h / 2);
        if (x <= (dbl ? -128 : -64) || x >= 240 || y >= 160 || y <= (dbl ? -2 * h : -h) || oam_n >= 128) continue;
        if (dbl) {   /* an enlarged block costs the hardware 266 cycles a line however little of it shows: drop slivers */
            int vis = (x + 128 < 240 ? x + 128 : 240) - (x > 0 ? x : 0);
            if (vis < (cine_flash ? 44 : 24)) continue;
        }
        if (zk != 64 && !cost_try(y, dbl ? 2 * h : h, dbl ? 266 : 138)) continue;   /* past the line budget: leave it out whole */
        u16 *o = &oam[oam_n * 4];
        o[0] = (y & 255) | (dbl ? 0x0300 : 0x0100) | ((b < 3 ? 1 : 0) << 14) | (semi ? 0x0400 : 0) | (f->mosaic ? 0x1000 : 0);
        o[1] = (x & 511) | (mtx << 9) | (3 << 14);
        o[2] = (a->base_tile + slot_tile[b]) | (1 << 10) | (pal << 12);
        oam_n++;
    }
}

static void draw_marks(Fighter *f, int visible) {    /* cuts in the clothing, on top of the fighter */
    if (!visible || !f->nmarks || f->fall || cine_flash || sil_on || (f->flash & 2) || cam_k != 64) return;
    const u8 *an = f->art->anc + f->frame * 4;
    if (!an[0]) return;
    int pal = f->art->pal + 4;
    for (int i = 0; i < f->nmarks && oam_n < 120; i++) {
        const u8 *m = f->marks[i];
        int ax = an[m[0] * 2], ay = an[m[0] * 2 + 1], x, y;
        canvas_to_screen(f, ax + (m[1] - 128), ay + (m[2] - 128), &x, &y);
        oam_put(x - 4, y - 4, 0, 0, FX_MARK, pal, (f->dir < 0) ^ (i & 1), 0);
    }
}
static void draw_reflection(Fighter *f, int visible) {   /* the wet boards mirror the legs */
    if (!visible || !wet_stage(stage_i) || stairs_on || f->fall || cam_k != 64) return;
    const Art *a = f->art;
    u16 m = a->mask[f->frame];
    int k = fig_k(f), flip = f->dir < 0, mtx = f == &fp ? 10 : 11;
    int inv = 16384 / k;
    oam[mtx * 16 + 3] = (u16)(flip ? -inv : inv); oam[mtx * 16 + 7] = 0;
    oam[mtx * 16 + 11] = 0; oam[mtx * 16 + 15] = (u16)(-inv * 2);
    int sx = screen_x(f) + (sin_table[(frame_counter * 5 + (f == &fp ? 0 : 90)) & 255] >> 11);
    int left = flip ? sx - (FIG_CW - FIG_RX) : sx - FIG_RX;
    int fy = feet_y(f) + f->hop;                       /* the reflection stays on the floor */
    int px = flip ? left + 128 : left + 64;
    for (int b = 6; b < 9; b++) {
        if (!(m & (1 << b)) || oam_n >= 124) continue;
        int cx = blk_x[b] + 32 - 64;
        if (flip) cx = -cx;
        int cyc = (FIG_GY - (blk_y[b] + 32)) * k / 128;    /* mirrored about the feet, squashed to half */
        int x = px + cx * k / 64 - 32, y = fy + cyc - 32;
        if (x <= -64 || x >= 240 || y >= 160) continue;
        if (!cost_try(y, 64, 138)) continue;            /* never at the cost of the fighters themselves */
        u16 *o = &oam[oam_n * 4];
        o[0] = (y & 255) | 0x0100 | 0x0400;
        o[1] = (x & 511) | (mtx << 9) | (3 << 14);
        o[2] = (a->base_tile + slot_tile[b]) | (2 << 10) | (((f->bloody ? 4 : 0) + a->pal) << 12);
        oam_n++;
    }
}
static void draw_fighter(Fighter *f, int visible) {
    if (!visible) return;
    int sx = screen_x(f) + (shake ? ((shake & 2) ? 2 : -2) : 0);
    draw_fighter_at(f, sx, 0);
}

static void draw_ghosts(Fighter *f, int visible) {
    if (!visible || f->ghost_t <= 0 || fig_k(f) != 64) return;
    for (int k = 4; k <= 7; k += 3) {
        int gx = wx_to_screen(f->hist_x[k] >> 8);
        if (iabs(gx - screen_x(f)) < 6) continue;
        draw_fighter_at(f, gx, 1);
    }
}

static void draw_shadow(Fighter *f, int visible) {
    if (!visible || f->fall) return;
    int sx = screen_x(f);
    int cx = sx + (f->st >= ST_DEATH && f->st <= ST_DEAD ? f->dir * 10 : 0);
    int sy = feet_y(f) + f->hop - 8;
    oam_put(cx - 32, sy, 1, 2, FX_SHADOW, 2, 0, 1);
    oam_put(cx, sy, 1, 2, FX_SHADOW, 2, 1, 1);          /* the right half is the left, mirrored */
}

static void draw_glint(Fighter *f) {
    if (!(f->st == ST_TELL || (f->st == ST_WINDUP && f->cpu && f->t < 3 && !f->chain))) return;
    if (!(frame_counter & 2) && f->st == ST_WINDUP) return;
    if (floor_i == 3 && (frame_counter & 4)) return;       /* the rain hides the sickle's glint */
    const Art *a = f->art;
    int x, y;
    canvas_to_screen(f, a->tipx[f->frame], a->tipy[f->frame], &x, &y);
    int special = f->unblock || f->sweep;
    /* a big, turning star: twice the old size, pulsing */
    int inv = 128 + ((frame_counter & 8) ? 0 : 20);
    affine_star(x, y, 3, inv, frame_counter * 3, special && (frame_counter & 4) ? 3 : 2);
}

static const u16 slash_inv[8] = {1536, 768, 512, 384, 300, 256, 256, 256};
static int burst_x, burst_y, burst_t;
static void parry_burst(int x, int y) { burst_x = x; burst_y = y; burst_t = 16; }
/* an affine star: a 16x16 glint drawn double-size, scaled by 256/inv and turned by ang */
static void affine_star(int x, int y, int mtx, int inv, int ang, int pal) {
    if (oam_n >= 128) return;
    int c = sin_table[(ang + 64) & 255], s = sin_table[ang & 255];
    u16 *o = &oam[oam_n * 4];
    o[0] = ((y - 16) & 255) | 0x0300;
    o[1] = ((x - 16) & 511) | (mtx << 9) | (1 << 14);
    o[2] = FX_GLINT | (0 << 10) | (pal << 12);
    oam[mtx * 16 + 3] = (u16)((c * inv) >> 12); oam[mtx * 16 + 7] = (u16)((s * inv) >> 12);
    oam[mtx * 16 + 11] = (u16)((-s * inv) >> 12); oam[mtx * 16 + 15] = (u16)((c * inv) >> 12);
    oam_n++;
}

static void draw_fx(void) {
    if (slash_t > 0) {
        int st = slash_t - 1;
        int inv = slash_inv[st < 8 ? st : 7];
        int c = sin_table[(slash_ang + 64) & 255], s = sin_table[slash_ang];
        int pa = (c * inv) >> 12, pb = (s * inv) >> 12;
        int pc = (-s * 256) >> 12, pd = (c * 256) >> 12;
        u16 *o = &oam[oam_n * 4];
        o[0] = ((slash_y - 32) & 255) | 0x0300 | (1 << 14);
        o[1] = ((slash_x - 64) & 511) | (0 << 9) | (3 << 14);
        o[2] = FX_SLASH | (0 << 10) | (2 << 12);
        oam[3] = pa; oam[7] = pb; oam[11] = pc; oam[15] = pd;
        oam_n++;
    }
    draw_props();
    draw_glint(&fe);
    if (burst_t > 0) {                            /* the parry: two stars turning against each other */
        int k = 16 - burst_t;
        int inv = 128 + k * 6;                    /* full size (2x) at the clash, shrinking away */
        affine_star(burst_x, burst_y, 4, inv, k * 9, 2);
        affine_star(burst_x, burst_y, 5, inv + 24, 32 - k * 7, burst_t & 2 ? 3 : 2);
    }
    for (int i = 0; i < NPART && cam_k == 64; i++) {      /* (not in the enlarged close-ups: they'd sit at 1x anyway) */
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        int tile = p->kind ? ((i & 1) ? FX_DROP : FX_DROP2) : FX_SPARK + (p->life > 8 ? 0 : p->life > 5 ? 1 : p->life > 2 ? 2 : 3);
        oam_put((p->x >> 8) - 4, (p->y >> 8) - 4, 0, 0, tile, 2, 0, 0);
    }
}
static void draw_stains(void) {
    for (int i = 0; i < stain_n; i++) {
        int x = wx_to_screen(stain_x[i]);
        if (x < -16 || x > 248) continue;
        oam_put(x - (stain_big[i] ? 8 : 4), stain_y[i] - 4, stain_big[i] ? 1 : 0, 0, stain_big[i] ? FX_STAIN : FX_STAIN + 2, 2, i & 1, 0);
    }
}

static void update_fx(void) {
    if (burst_t > 0) burst_t--;
    if (slash_t > 0) { slash_t++; if (slash_t > 34) slash_t = 0; }
    for (int i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->x += p->vx; p->y += p->vy;
        p->vy += p->kind ? 60 : 30;
        if (p->kind && (p->y >> 8) > p->gy && p->vy > 0) {     /* a drop lands: leave a stain */
            int wx = camC + (((p->x >> 8) - 120) * 64) / zoom;
            if (rnd(3) == 0) add_stain(wx, p->gy, rnd(3) == 0);
            p->life = 0;
            continue;
        }
        p->life--;
    }
}

static void lerp_pal(volatile u16 *dst, const u16 *src, int n, int f) {
    for (int i = 0; i < n; i++) {
        int c = src[i];
        int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        r += ((31 - r) * f) >> 4; g += ((31 - g) * f) >> 4; b += ((31 - b) * f) >> 4;
        dst[i] = r | (g << 5) | (b << 10);
    }
}

static int light_level;
static int dark_t = 900;
static void update_weather(void) {
    int heavy = stage_i == 3 || stage_i == 4;
    storm_lvl = (stage_i == 4 && (scene == S_DUEL || scene == S_INTRO) && round_no > 1) ? (round_no > 3 ? 2 : round_no - 1) : 0;
    rain_y += heavy ? 9 + storm_lvl * 3 : 5;
    if (lightning_now) { lightning_now = 0; lightning_seq = 0; thunder_t = 8; }
    if (lightning_seq < 0) {
        if ((cheat_on & CH_STORM) && lightning_t > 140) lightning_t = 60 + rnd(80);
        if (stage_i == 4 && lightning_t > 360 - storm_lvl * 110) lightning_t = 150 - storm_lvl * 40 + rnd(200 - storm_lvl * 60);
        if (stage_i == 1 || stage_i == 2 || stage_i >= TITLE_STAGE || dawn_amt) lightning_t = 600;     /* indoors, sheltered: no lightning */
        if (--lightning_t <= 0) { lightning_seq = 0; lightning_t = 420 + rnd(700); thunder_t = 30 + rnd(40); }
        light_level = 0;
    } else {
        static const u8 seq[] = {12, 14, 6, 0, 0, 9, 11, 7, 4, 2, 1};
        light_level = seq[lightning_seq];
        if (++lightning_seq >= (int)sizeof(seq)) lightning_seq = -1;
    }
    if (thunder_t > 0 && --thunder_t == 0) sfx_thunder();
    if ((rng_s & 3) == 0) lantern_f = (int)rnd(5) - 2;
    /* the lantern hall: a gust blows the lanterns out now and then, for a few seconds */
    if (stage_i == 2 && scene == S_DUEL && duel_on) {
        dark_t--;
        if (dark_t > 300) { if (dark_level > 0 && (frame_counter & 3) == 0) dark_level--; }
        else if (dark_t == 300) { banner("THE LANTERNS GO OUT", T_WHITE, 50); sfx_whoosh(); }
        else if (dark_t > 0) { if (dark_level < 13 && (frame_counter & 1) == 0) dark_level++; }
        else dark_t = 1100 + rnd(600);
    } else if (dark_level > 0) dark_level--;
}

static u16 grey(u16 c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    int l = (r * 77 + g * 150 + b * 29) >> 8;
    return l | (l << 5) | (l << 10);
}
/* ---- stages */
static const u16 *cur_wall_pal = wall_pal, *cur_floor_pal, *cur_out_pal = out_pal;
static const u16 *opp_pal16 = kenji_pal;
#define BLOOD_RGB (19 | (2 << 5) | (3 << 10))
static u16 shade_c(u16 c, int k) {            /* k = 16 keeps the colour, 0 is black */
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return (r * k >> 4) | ((g * k >> 4) << 5) | ((b * k >> 4) << 10);
}
static int ghost_applied;
static int sil_on, cine_flash, warm_p, warm_e;
static const u16 *portrait_pal;            /* BG bank 9 while a dialogue portrait is up */
static u16 warm(u16 c, int w) {
    if (!w || !c) return c;
    int r = (c & 31) + w * 2, g = ((c >> 5) & 31) + w, b = (c >> 10) & 31;
    if (r > 31) r = 31; if (g > 31) g = 31;
    return r | (g << 5) | (b << 10);
}
static int grey_amt;                     /* 0..16: how far the colour has drained (the Kurosawa defeat) */
static u16 drain(u16 c) {
    if (!grey_amt) return c;
    u16 g = grey(c);
    int r = c & 31, gg = (c >> 5) & 31, b = (c >> 10) & 31, y = g & 31;
    r += (y - r) * grey_amt >> 4; gg += (y - gg) * grey_amt >> 4; b += (y - b) * grey_amt >> 4;
    return r | (gg << 5) | (b << 10);
}
static u16 dawn_c(u16 c, int amt) {        /* toward a sunrise ramp by brightness: violet, rose, gold */
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    int l = (r * 77 + g * 150 + b * 29) >> 8;
    int tr, tg, tb;
    if (l < 10) { tr = 6 + l; tg = 3 + l / 2; tb = 9 + l / 2; }
    else if (l < 20) { tr = 16 + (l - 10); tg = 8 + (l - 10) / 2; tb = 13 - (l - 10) / 3; }
    else { tr = 31; tg = 14 + (l - 20) * 3 / 2; tb = 10 + (l - 20) / 2; }
    if (tg > 30) tg = 30;
    r += (tr - r) * amt / 22; g += (tg - g) * amt / 22; b += (tb - b) * amt / 22;
    return r | (g << 5) | (b << 10);
}
/* v5.5 stage light on the fighters: the lit tones take the colour of the stage's light (moonlight,
   lantern glow, storm), and on the roof the lightning whitens them before the silhouette */
static const u16 KEY_LIGHT[8] = {25 | 26 << 5 | 31 << 10, 26 | 29 << 5 | 25 << 10, 31 | 21 << 5 | 11 << 10,
                                 21 | 24 << 5 | 30 << 10, 23 | 25 << 5 | 29 << 10, 0, 25 | 27 << 5 | 29 << 10, 27 | 28 << 5 | 31 << 10};
static int key_flash;
static u16 mix15(u16 a, u16 b, int t) {    /* t in 32ths */
    int r = a & 31, g = (a >> 5) & 31, bl = (a >> 10) & 31;
    r += (((b & 31) - r) * t) >> 5; g += ((((b >> 5) & 31) - g) * t) >> 5; bl += ((((b >> 10) & 31) - bl) * t) >> 5;
    return r | (g << 5) | (bl << 10);
}
static u16 key_light(u16 c, int i) {
    if (stage_i < 0 || stage_i > 7 || stairs_on || i < 2 || i > 13 || !c) return c;
    u16 k = KEY_LIGHT[stage_i];
    if (!k) return c;
    int tone = (i - 2) % 3;                   /* 0 dark, 1 mid, 2 lit */
    int t = tone == 2 ? 9 : tone == 1 ? 3 : 0;
    if (key_flash && tone) { k = 0x7FFF; t += key_flash * (tone == 2 ? 3 : 2); if (t > 30) t = 30; }
    return t ? mix15(c, k, t) : c;
}
static void load_palettes(void) {
    int gh = (cheat_on & CH_GHOST) != 0 || grey_scene;
    int dark_level_save = dark_level;
    if (spot_on) dark_level = 0;                 /* the spotlight darkens with the blend instead */
    int dk = 16 - dark_level;
    for (int i = 0; i < 256; i++) PAL_BG[i] = gh ? grey(bg_pal[i]) : bg_pal[i];
    for (int i = 0; i < 16; i++) {
        u16 w = cur_wall_pal[i], f = cur_floor_pal ? cur_floor_pal[i] : bg_pal[16 + i];
        if (stairs_on) f = 0;
        if (dark_level) w = shade_c(w, dk);
        if (dark_level) f = shade_c(f, dk);
        PAL_BG[i] = gh ? grey(w) : w;
        PAL_BG[16 + i] = gh ? grey(f) : f;
        u16 lv = key_light(warm(lieve_pal[i], warm_p), i), op = key_light(warm(opp_pal16[i], warm_e), i);
        if (dark_level && i) { lv = shade_c(lv, dk > 4 ? dk : 4); op = shade_c(op, dk > 4 ? dk : 4); }
        u16 lb = i == 15 ? BLOOD_RGB : lv, ob = i == 15 ? BLOOD_RGB : op;
        if (i && cine_flash) { lv = op = lb = ob = 0x7FFF; }            /* the killing cut: white figures */
        else if (i && sil_on) { lv = op = lb = ob = 0x0842; }           /* lightning: black silhouettes */
        PAL_OBJ[i] = gh ? grey(lv) : lv;
        PAL_OBJ[16 + i] = gh ? grey(op) : op;
        PAL_OBJ[64 + i] = gh && !(i == 15 && !sil_on && !cine_flash) ? grey(lb) : lb;     /* in black and white, */
        PAL_OBJ[80 + i] = gh && !(i == 15 && !sil_on && !cine_flash) ? grey(ob) : ob;     /* the blood stays red */
        PAL_OBJ[32 + i] = gh && i != 5 && i != 7 ? grey(fx_pal[i]) : fx_pal[i];
        PAL_OBJ[48 + i] = flash_pal[i];
        PAL_OBJ[96 + i] = gh ? grey(amb_pal[i]) : amb_pal[i];
        PAL_OBJ[112 + i] = gh ? grey(amb_pal2[i]) : amb_pal2[i];
        PAL_OBJ[128 + i] = gh ? grey(breath_pal[i]) : breath_pal[i];
    }
    if (dawn_amt && !gh) {
        for (int i = 0; i < 80; i++) PAL_BG[i] = dawn_c(PAL_BG[i], dawn_amt);
        for (int i = 1; i < 32; i++) PAL_OBJ[i] = dawn_c(PAL_OBJ[i], dawn_amt / 4);
        for (int i = 65; i < 96; i++) PAL_OBJ[i] = dawn_c(PAL_OBJ[i], dawn_amt / 4);
    }
    dark_level = dark_level_save;
    if (cine_flash) for (int i = 0; i < 80; i++) PAL_BG[i] = 0;   /* ...against black */
    if (portrait_pal) for (int i = 0; i < 16; i++) PAL_BG[144 + i] = gh ? grey(portrait_pal[i]) : portrait_pal[i];
    if (grey_amt) for (int i = 0; i < 256; i++) { PAL_BG[i] = drain(PAL_BG[i]); PAL_OBJ[i] = drain(PAL_OBJ[i]); }
    ghost_applied = gh;
    last_dark = dark_level;
}
static int last_light = -1, last_lantern = 99;
static int last_sil, last_warm = -1, last_cflash;
static void lantern_warmth(void) {
    int wp = 0, we = 0;
    if (!stairs_on && stage_i >= 0 && stage_i < 3 && (scene == S_DUEL || scene == S_INTRO || scene == S_CUT || scene == S_TITLE)) {
        int off = 136 + ((camC * (int)k_table[60]) >> 8);
        for (int k = 0; k < 8; k++) {
            int lx = stage_lamps[stage_i * 8 + k];
            if (!lx) break;
            int sx = lx - off;
            int dp = iabs(screen_x(&fp) - sx), de = iabs(screen_x(&fe) - sx);
            int a = 5 - dp / 16, b = 5 - de / 16;
            if (a > wp) wp = a;
            if (b > we) we = b;
        }
    }
    warm_p = wp; warm_e = we;
}
static void commit_palettes(void) {
    /* lantern warmth is switched off: it tinted the indoor floors orange */
    sil_on = (stage_i == 0 || stage_i == 3 || stage_i == 4) && !stairs_on && light_level >= 11 &&
             (scene == S_DUEL || scene == S_INTRO || scene == S_CUT);
    {   /* the Lantern Hall blackout: a pool of light around the fighters */
        static int last_spot = -1, last_dawn;
        spot_on = stage_i == 2 && !stairs_on && dark_level >= 3 && (scene == S_DUEL || scene == S_INTRO) &&
                  !fade && !flash_white && !cine_flash && !grey_amt && !paused;
        if (spot_on) {
            int a = screen_x(&fp), b = screen_x(&fe);
            int cx = (a + b) / 2, half = iabs(a - b) / 2 + 46 + (shock_t * 3) + ((frame_counter >> 2) & 1);
            spot_x = cx; spot_w = half;
            static u8 circ[78];                       /* 64 * sqrt(1 - (dy/78)^2) */
            if (!circ[0]) for (int d = 0; d < 78; d++) { int q = 4096 - d * d * 4096 / (78 * 78), r_ = 0; while ((r_ + 1) * (r_ + 1) <= q) r_++; circ[d] = (u8)r_; }
            u16 *tb = spot_buf[spot_cur];
            for (int y = 0; y <= 160; y++) {
                int dy = iabs(y - 104);
                int hw = dy < 78 ? (half * circ[dy]) >> 6 : 0;
                int l = cx - hw, r = cx + hw;
                if (l < 0) l = 0;
                if (r > 240) r = 240;
                if (r < l) r = l;
                tb[y] = (u16)((l << 8) | r);
            }
        }
        if (spot_on != last_spot || dawn_amt != last_dawn) { last_spot = spot_on; last_dawn = dawn_amt; last_sil = -1; }
    }
    {   /* the lightning's afterglow on the fighters (the flash itself is the black silhouette) */
        static int last_kf = -1;
        key_flash = (stage_i == 4 || stage_i == 3) && !sil_on && light_level >= 4 ? light_level - 3 : 0;
        if (key_flash > 7) key_flash = 7;
        if (key_flash != last_kf) { last_kf = key_flash; last_sil = -1; }
    }
    if (sil_on != last_sil || cine_flash != last_cflash || (warm_p * 8 + warm_e) != last_warm) {
        last_sil = sil_on; last_cflash = cine_flash; last_warm = warm_p * 8 + warm_e;
        load_palettes(); last_light = -1; last_lantern = 99;
    }
    if (ghost_applied != ((cheat_on & CH_GHOST) != 0 || grey_scene) || last_dark != dark_level) { load_palettes(); last_light = -1; last_lantern = 99; }
    if (light_level != last_light) {
        last_light = light_level;
        lerp_pal(PAL_BG + 3 * 16, cur_out_pal, 16, light_level);
        if (ghost_applied) for (int i = 0; i < 16; i++) PAL_BG[48 + i] = grey(PAL_BG[48 + i]);
        if (grey_amt) for (int i = 0; i < 16; i++) PAL_BG[48 + i] = drain(PAL_BG[48 + i]);
    }
    if (lantern_f == last_lantern || dark_level || stairs_on || grey_amt) return;
    last_lantern = lantern_f;
    for (int i = 11; i <= 12; i++) {
        int c = cur_wall_pal[i];
        int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        int k = 16 + lantern_f;
        r = r * k >> 4; g = g * k >> 4; b = b * k >> 4;
        if (r > 31) r = 31;
        if (g > 31) g = 31;
        if (b > 31) b = 31;
        PAL_BG[i] = ghost_applied ? grey(r | (g << 5) | (b << 10)) : (r | (g << 5) | (b << 10));
    }
}

static int hdma_key[2] = {0x7FFFFFFF, 0x7FFFFFFF};
static u16 bg3_buf[2][160] __attribute__((aligned(4)));
static int bg3_cur;
static void build_bg3(void) {
    u16 *t = bg3_buf[bg3_cur];
    int base = stairs_on ? 0 : 20 + ((camC * K_OUT_FIX) >> 8);
    int f = frame_counter;
    for (int y = 0; y < 160; y++) {
        int o = 0;
        if (!stairs_on) {
            if (stage_i == 1 && y >= 78) o = sin_table[(f * 2 + y * 5) & 255] * 2 >> 12;               /* bamboo tops sway */
            else if (stage_i == 0 && y >= 60) o = sin_table[(f + y * 4) & 255] >> 12;                    /* garden bamboo */
            else if (stage_i == 2 && y >= 64) o = sin_table[(f * 5 + y * 23) & 255] >> 12;               /* heat over the lamps */
            else if (stage_i == TITLE_STAGE) {                       /* the title: banded clouds drift past a still moon */
                if (y < 14) o = f / 14;
                else if (y >= 50 && y < 66) o = f / 9;
                else if (y >= 66 && y < 82) o = f / 6;
                else if (y >= 82 && y < 100) o = f / 4;
                else if (y >= 100) o = sin_table[(f * 3 + y * 21) & 255] >> 13;
            }
            else if (stage_i == 7) {                                 /* the spire: cloud wisps drift, the cloud sea below rolls */
                if (y < 80) o = f / (6 + (y >> 4));
                else if (y >= 94) o = (f >> 3) + (sin_table[(f * 2 + y * 9) & 255] >> 13);
            }
            else if (stage_i == 6 && y >= 84)                         /* the harbour at night */
                o = sin_table[(f * 4 + y * 19) & 255] * (1 + ((y - 84) >> 4)) >> 12;
            else if ((stage_i == 3 || stage_i == 4) && y >= (stage_i == 3 ? 72 : 96))                     /* the sea shimmers */
                o = sin_table[(f * 6 + y * 21) & 255] * (1 + (((y - 72) * 13) >> 8)) * (1 + storm_lvl) >> 12;
        }
        if (shock_t) o += (sin_table[(y * 7 + shock_t * 24) & 255] * shock_t) >> 14;
        t[y] = (u16)(base + o);
    }
}
static void build_hdma(void) {
    u16 *t = hdma_buf[hdma_cur];
    int sh = shake ? ((shake & 2) ? 2 : -2) : 0;
    int key = camC * 8 + sh + (shock_t << 20);
    if (stairs_on) key = 0x40000000;
    if (hdma_key[hdma_cur] == key) return;   /* this buffer already holds the right table */
    hdma_key[hdma_cur] = key;
    if (stairs_on) { for (int y = 0; y < 160; y++) t[y] = 8; return; }
    for (int y = 0; y < 160; y++) t[y] = (u16)(136 + ((camC * (int)k_table[y]) >> 8) - sh + (shock_t ? (sin_table[(y * 9 + shock_t * 28) & 255] * shock_t) >> 14 : 0));
}

static void update_camera(void) {
    int c = ((fp.x + fe.x) >> 9);
    if (scene == S_TITLE || scene == S_STORY || scene == S_HOWTO) c = 0;
    if (c < -CAM_RANGE) c = -CAM_RANGE;
    if (c > CAM_RANGE) c = CAM_RANGE;
    camC += (c - camC + (c > camC ? 5 : 0)) / 6;
}

static void set_blend(void) {
    if (flash_white > 0) {
        REG_BLDCNT = 0x3F | (2 << 6);
        REG_BLDY = 12;
    } else if (fade > 0) {
        REG_BLDCNT = 0x3F | (3 << 6);
        REG_BLDY = fade;
    } else if (spot_on && !darken_t && !super_dark) {
        REG_BLDCNT = 0x3F | (3 << 6);
        REG_BLDY = dark_level > 13 ? 13 : dark_level;
    } else if (darken_t > 0 || super_dark > 0 || scene_dark > 0) {
        REG_BLDCNT = 0x0E | 0x20 | (3 << 6) | (0x0C << 8);
        REG_BLDY = (darken_t > 0 || super_dark > 0) ? 12 : scene_dark;
        REG_BLDALPHA = 8 | (8 << 8);
    } else {
        REG_BLDCNT = 0x02 | (1 << 6) | (0x0C << 8);
        REG_BLDALPHA = (7 + storm_lvl * 3) | ((11 - storm_lvl * 2) << 8);
    }
}

static void vblank_commit(void) {
    stream_frame(&fp);
    stream_frame(&fe);
    plat_copy32(OAM_MEM, oam, 256);
    plat_hdma(hdma_buf[hdma_cur]);
    hdma_cur ^= 1;
    plat_hdma3(bg3_buf[bg3_cur]);
    bg3_cur ^= 1;
    {   int mz = fp.mosaic > fe.mosaic ? fp.mosaic : fe.mosaic;
        mz = mz >= 40 ? mz - 40 : mz / 3 + (mz ? 1 : 0);
        if (mz > 15) mz = 15;
        REG_MOSAIC = (u16)((mz << 8) | (mz << 12)); }
    if (split_y >= 0) plat_split(split_y, split_grey, split_col);
    if (spot_on) {
        REG_DISPCNT |= 0x2000 | (fire_t ? 0x4000 : 0);
        if (!fire_t) REG_DISPCNT &= ~0x4000;
        REG_WIN0V = 160; REG_WININ = 0x1F | (0x1F << 8); REG_WINOUT = 0x3F;
        if (fire_t) {
            int fx = fire_x - (136 + ((camC * (int)k_table[110]) >> 8));
            int l = fx - 30 < 0 ? 0 : fx - 30, r = fx + 30 > 240 ? 240 : fx + 30;
            if (r < l) r = l;
            REG_WIN1H = (u16)((l << 8) | r); REG_WIN1V = (u16)((70 << 8) | 160);
        }
        plat_hdma2(spot_buf[spot_cur]);
        spot_cur ^= 1;
    } else {
        REG_DISPCNT &= ~0x6000;
        plat_hdma2(0);
    }
    REG_BGVOFS(3) = 0;
    REG_BGHOFS(1) = (u16)(((camC * 100) >> 8) + (rain_y >> (stage_i >= 3 ? 1 : 2)) + ((rain_y * storm_lvl) >> 2));
    REG_BGVOFS(1) = (u16)(-rain_y);
    REG_BGVOFS(2) = 0;
    commit_palettes();
    set_blend();
    if (hud_dirty) {
        plat_copy32(TEXT_MAP, hud_buf, 48);
        for (int i = 0; i < 96; i++) hud_vram[i] = hud_buf[i];
        hud_dirty = 0;
    }
}

static void draw_amb(int front) {
    static const s16 ainv[4] = {128, 256, 200, 160};  /* near ones at 2x; dust grows from 1x to 1.6x */
    for (int m = 0; m < 4; m++) {
        int mtx = 6 + m;
        oam[mtx * 16 + 3] = ainv[m]; oam[mtx * 16 + 7] = 0; oam[mtx * 16 + 11] = 0; oam[mtx * 16 + 15] = ainv[m];
    }
    for (int i = 0; i < NAMB && oam_n < 120; i++) {
        Amb *p = &amb[i];
        if (!p->life || (front != p->near)) continue;
        if (cam_k != 64 && p->kind != AK_LEAF && p->kind != AK_MIST && p->kind != AK_EMBER) continue;   /* close-ups: keep the sprite budget */
        int x = p->x >> 8, y = p->y >> 8;
        int tile, pal = 6, shape = 0, aff = -1, semi = 0, hflip = 0;
        switch (p->kind) {
        case AK_LEAF: tile = FX_LEAF + ((p->age >> 3) & 1); pal = stage_i == 1 ? 7 : 6; hflip = (p->age >> 4) & 1; break;
        case AK_EMBER: if (p->life < 24 && (p->age & 2)) continue; tile = FX_EMBER; break;
        case AK_MIST: tile = FX_MIST; semi = 1; break;
        case AK_DEBRIS: tile = FX_DEBRIS; hflip = (p->age >> 2) & 1; break;
        case AK_DUST: tile = FX_DUST; semi = 1; aff = p->age < 8 ? 7 : p->age < 15 ? 8 : 9; break;
        case AK_RIPPLE: tile = FX_RIPPLE; shape = 1; semi = 1; break;
        case AK_FLAME: tile = FX_EMBER; aff = p->life > 10 ? 6 : -1; break;
        case AK_BREATH: if (p->life < 8 && (p->age & 1)) continue; tile = FX_DUST; semi = 1; pal = 8; aff = p->age < 7 ? 7 : p->age < 14 ? 8 : 9; break;
        default: tile = FX_SPRAY; break;
        }
        if (p->near) aff = 6;
        u16 *o = &oam[oam_n * 4];
        if (aff >= 0) {
            o[0] = ((y - 8) & 255) | 0x0300 | (semi ? 0x0400 : 0) | (shape << 14);
            o[1] = ((x - 8) & 511) | (aff << 9);
        } else {
            o[0] = (y & 255) | (semi ? 0x0400 : 0) | (shape << 14);
            o[1] = (x & 511) | (hflip ? 0x1000 : 0);
        }
        o[2] = tile | ((p->near ? 0 : 1) << 10) | (pal << 12);
        oam_n++;
    }
}
static void draw_reticule(void) {      /* the iai target: closes in on the landing point as the leaf falls */
    if (!iai_on || prop_kind != 3 || oam_n >= 124) return;
    int land = iai_state == 2;
    int prog = iai_wait ? iai_t * 256 / iai_wait : 0;
    if (prog > 256) prog = 256;
    int inv = land ? 236 + ((iai_t & 4) ? 0 : 40) : 90 + prog * 166 / 256;     /* 2.8x wide, down to life size */
    oam[4 * 16 + 3] = (u16)inv; oam[4 * 16 + 7] = 0; oam[4 * 16 + 11] = 0; oam[4 * 16 + 15] = (u16)inv;
    int pal = land ? ((iai_t & 2) ? 3 : 2) : 2;
    u16 *o = &oam[oam_n * 4];
    o[0] = ((112 - 32) & 255) | 0x0300;
    o[1] = ((120 - 32) & 511) | (4 << 9) | (2 << 14);
    o[2] = FX_PROP | (1 << 10) | (pal << 12);
    oam_n++;
}
static void build_sprites(void) {
    oam_n = 0;
    int zoomed = cam_k != 64;      /* close-ups: the fighters go first in OAM so the per-line budget is spent on them */
    if (!zoomed) { draw_amb(1); draw_fx(); }
    else for (int y = 0; y < 160; y++) line_cost[y] = 0;
    draw_reticule();
    Fighter *front = is_attacking(&fe) || fe.st == ST_FOLLOW ? &fe : &fp;
    if (iabs(fp.z - fe.z) > 100) front = fp.z < fe.z ? &fp : &fe;
    if (zoomed) front = (fp.st >= ST_DYING && fp.st <= ST_DEAD) ? &fp : &fe;     /* the one struck down is drawn whole */
    Fighter *back = front == &fp ? &fe : &fp;
    int sp_ = spare_t ? 0 : 1;                       /* the spare close-up shows only the kneeling master */
    draw_marks(front, front == &fp ? show_fp && sp_ : show_fe);
    draw_fighter(front, front == &fp ? show_fp && sp_ : show_fe);
    draw_marks(back, back == &fp ? show_fp && sp_ : show_fe);
    draw_fighter(back, back == &fp ? show_fp && sp_ : show_fe);
    if (zoomed) { draw_fx(); draw_amb(1); }     /* slash and near leaves are priority 0: still drawn on top */
    if (!darken_t && !flash_white && !super_dark && !cine_flash) {
        cost_build();
        draw_reflection(&fp, show_fp);
        draw_reflection(&fe, show_fe);
    }
    if (cam_k == 64) { draw_ghosts(&fp, show_fp); draw_ghosts(&fe, show_fe); }
    if (!darken_t && !flash_white && !scene_dark && !super_dark && cam_k == 64) {
        draw_shadow(&fp, show_fp);
        draw_shadow(&fe, show_fe);
    }
    draw_amb(0);
    if (!stairs_on && cam_k == 64) draw_stains();
    oam_hide_rest();
}

/* ------------------------------------------------------------------ HUD */
static void put_num(int col, int row, u32 v, int digits, int bank) {
    char b[8];
    for (int i = digits - 1; i >= 0; i--) { b[i] = '0' + v % 10; v /= 10; }
    b[digits] = 0;
    text_put(col, row, b, bank);
}
static int poem_on;
static const char *const POEM_TXT[7][3] = {
    {"RED LEAF ON THE FLOOR", "THE WOODEN BLADE TAUGHT NOTHING", "I LEARNED IT TOO LATE"},
    {"BAMBOO BENDS AND STANDS", "I WAS THE REED THAT WOULD NOT", "THE STAFF SWEPT ME DOWN"},
    {"TWO BLADES IN THE LIGHT", "ONE LANTERN GUTTERS AND DIES", "THE SECOND IS MINE"},
    {"RAIN ON THE LONG BOARDS", "I NEVER SAW THE KNIFE FALL", "ONLY THE RAINDROPS"},
    {"FATHER, THE STORM WINS", "THE GIANT'S SHADOW COVERS", "THE SEA WHERE YOU SLEEP"},
    {"SALT WIND ON THE PIER", "A LANTERN DROWNS IN BLACK TIDE", "THE SHIP LEAVES ALONE"},
    {"GOLD COIN, POWDER SMOKE", "THE COMPANY KEEPS ITS BOOKS", "MY NAME CROSSED OUT, RED"}};
static int hud_hide;
static void draw_hud(void) {
    if (poem_on || hud_hide) { if (hud_hide) for (int r = 0; r < 3; r++) for (int c = 0; c < 30; c++) TEXT_MAP[r * 32 + c] = 0; return; }
    hud_to_buf = 1;
    text_clear_row(0); text_clear_row(1); text_clear_row(2);
    text_put(1, 0, "LIEVE", T_WHITE);
    for (int i = 0; i < wins_p; i++) text_put(9 + i, 0, "*", T_GOLD);
    text_put(29 - slen(fe.art->name), 0, fe.art->name, T_WHITE);
    for (int i = 0; i < wins_e; i++) text_put(27 - slen(fe.art->name) - i, 0, "*", T_GOLD);
    if (game_mode == MODE_STORY) put_num(12, 0, score, 6, T_GOLD);
    else put_time(12, 0, run_frames, T_GOLD);
    for (int i = 0; i < fp.maxhp; i++) text_put(1 + i, 1, i < fp.hp ? "*" : "+", i < fp.hp ? T_RED : T_WHITE);
    for (int i = 0; i < fe.maxhp; i++) text_put(28 - i, 1, i < fe.hp ? "*" : "+", i < fe.hp ? T_RED : T_WHITE);
    text_put(1, 2, "KI", T_WHITE);
    for (int i = 0; i < 8; i++) text_put(4 + i, 2, fp.meter >= (i + 1) * 100 / 8 ? "#" : "~", fp.meter >= 100 ? T_GOLD : T_WHITE);
    if (fp.meter >= 100 && (frame_counter & 16)) text_put(13, 2, "A+B!", T_GOLD);
    if (cheat_on) text_put(23, 2, "CHEAT", T_RED);
    if (cpu_meter) put_num(26, 2, cpu_peak, 3, T_GOLD);
    hud_to_buf = 0;
    for (int i = 0; i < 96; i++) if (hud_buf[i] != hud_vram[i]) { hud_dirty = 1; break; }
}

/* ------------------------------------------------------------------ save (SRAM) */
#ifdef HOST
static u8 host_sram[64];
#define SRAM ((volatile u8 *)host_sram)
#else
#define SRAM ((volatile u8 *)0x0E000000)
#endif
__attribute__((used)) static const char save_tag[] = "SRAM_V113";
static void load_best(void) {
    if (SRAM[0] == 'K' && SRAM[1] == 'O' && SRAM[2] == 'M' && SRAM[3] == 'O')
        best_score = SRAM[4] | (SRAM[5] << 8) | (SRAM[6] << 16) | ((u32)SRAM[7] << 24);
    else best_score = 0;
    if (best_score > 999999) best_score = 0;
}
static int save_floor = -1, save_diff;     /* the highest floor reached (0-4), and on which difficulty */
static int save_honour, save_spared, save_rival;   /* v5.5: the story so far, for CONTINUE */
static void save_best(void) {
    SRAM[0] = 'K'; SRAM[1] = 'O'; SRAM[2] = 'M'; SRAM[3] = 'O';
    SRAM[4] = best_score; SRAM[5] = best_score >> 8; SRAM[6] = best_score >> 16; SRAM[7] = best_score >> 24;
    SRAM[8] = 'F'; SRAM[9] = save_floor + 1; SRAM[10] = save_diff; SRAM[11] = (save_floor + 1) ^ save_diff ^ 0x5A;
    {   /* v5.5: honour, who was spared, the rival, and the challenges met */
        int hv = save_honour + 64; if (hv < 0) hv = 0; if (hv > 255) hv = 255;
        SRAM[26] = 'H'; SRAM[27] = (u8)hv; SRAM[28] = (u8)save_spared; SRAM[29] = ch_done; SRAM[30] = (u8)save_rival;
        SRAM[31] = (u8)(SRAM[27] ^ SRAM[28] ^ SRAM[29] ^ SRAM[30] ^ 0x3C);
    }
}
static void save_times(void) {
    u8 sum = 'T';
    SRAM[12] = 'T';
    for (int i = 0; i < 5; i++) { SRAM[13 + i * 2] = best_time[i]; SRAM[14 + i * 2] = best_time[i] >> 8; }
    SRAM[23] = best_gaunt; SRAM[24] = best_gaunt >> 8;
    for (int i = 13; i < 25; i++) sum += SRAM[i];
    SRAM[25] = sum ^ 0xA5;
}
static void load_times(void) {
    u8 sum = 'T';
    for (int i = 13; i < 25; i++) sum += SRAM[i];
    if (SRAM[12] != 'T' || SRAM[25] != (u8)(sum ^ 0xA5)) return;
    for (int i = 0; i < 5; i++) best_time[i] = SRAM[13 + i * 2] | (SRAM[14 + i * 2] << 8);
    best_gaunt = SRAM[23] | (SRAM[24] << 8);
}
static void load_progress(void) {
    save_floor = -1; save_diff = 0;
    if (SRAM[0] == 'K' && SRAM[8] == 'F' && SRAM[9] >= 1 && SRAM[9] <= 5 && SRAM[10] <= 2 && SRAM[11] == (SRAM[9] ^ SRAM[10] ^ 0x5A)) {
        save_floor = SRAM[9] - 1; save_diff = SRAM[10];
    }
    save_honour = 0; save_spared = 0; save_rival = 0;
    if (SRAM[26] == 'H' && SRAM[31] == (u8)(SRAM[27] ^ SRAM[28] ^ SRAM[29] ^ SRAM[30] ^ 0x3C)) {
        save_honour = SRAM[27] - 64; save_spared = SRAM[28]; ch_done = SRAM[29] & 0x7F; save_rival = SRAM[30] & 3;
    }
}

/* ------------------------------------------------------------------ scenes */
static const char *const story[4][6] = {
    {"NAGASAKI, 1651.", "", "SYBRAND OOSTERLING WAS THE", "FINEST TRADER THE DUTCH", "COMPANY EVER SENT EAST.", ""},
    {"ON THE NIGHT OF THE STORM,", "A GIANT IN BLACK ARMOUR", "CAME ABOARD HIS SHIP.", "", "BY MORNING, SYBRAND", "WAS DEAD."},
    {"NO DUTCH WOMAN WAS ALLOWED", "ASHORE. HIS DAUGHTER", "LIEVE CAME ANYWAY.", "", "SHE HAD NEVER HELD", "A SWORD."},
    {"THE GIANT WAITS AT THE TOP", "OF THE PAGODA OF FIVE", "FLOORS. EACH FLOOR HAS", "ITS MASTER.", "", "SHE BEGINS AT THE FIRST."},
};
static int story_seen, menu_on, menu_sel;

static void set_opponent(const Art *a);
static void reset_fighter(Fighter *f, const Art *a, int x, int dir, int cpu) {
    int meter = f->meter;
    Fighter z = {0};
    *f = z;
    f->art = a; f->x = x * 256; f->dir = dir; f->stance = MID; f->cpu = cpu;
    for (int i = 0; i < 8; i++) f->hist_x[i] = f->x;
    f->shown = -1;
    f->st = ST_POSE; f->pose_anim = A_BOW; f->pose_fr = 0;
    f->maxhp = f->hp = cpu ? ((cheat_on & CH_ONECUT) ? 1 : DF->hp_e + (diff_i == 2 ? (OPP->style == STY_JUGGERNAUT ? 0 : (OPP->hp_add + 1) / 2) : OPP->hp_add)) : DF->hp_p;
    f->z = 0;
    f->meter = cpu ? 0 : meter;
    f->last_release = -100;
    f->kmul = 64;
    f->death_anim = A_DEATH;
    set_frame(f, A_BOW, 0);
}

static void goto_scene(int s) {
    scene = s; scene_t = -1;          /* the main loop increments before the next call */
    text_clear();
    banner_t = 0;
}

static void start_round(int bow) {
    wpn_owner = 0; wpn_t = 0; prop_want = 0; pole_t = 0; prop_load(0); pistol_used = 0;
    spare_t = 0; spared_now = 0; dishonour_done = 0; iai_on = 0; iai_state = 0; lock_t = 0; replay_t = 0; replay_ok = 0;
    trophy_on = 0; edge_warned = 0; cam_k = 64; shock_t = 0; dawn_amt = 0; fire_t = 0; chib_extra = 0;
    storm_vol = (floor_i == 4 && round_no > 1) ? (round_no > 3 ? 2 : round_no - 1) : 0;
    rain_noise();
    if (cine_t) { cine_t = 0; cine_flash = 0; draw_bars(0); for (int i = 0; i < 96; i++) hud_vram[i] = 0xFFFF; }
    brush_clear();
    if (stage_i != OPP_STAGE() || stairs_on) load_stage(OPP_STAGE());
    u8 pm[16], em[16];                       /* the cuts in the clothing last the whole match */
    pm[0] = fp.nmarks; em[0] = fe.nmarks;
    for (int i = 0; i < 15; i++) { pm[1 + i] = fp.marks[i / 3][i % 3]; em[1 + i] = fe.marks[i / 3][i % 3]; }
    reset_fighter(&fp, hero_art(), -96, 1, 0);
    reset_fighter(&fe, OPP->art, 96, -1, 1);
    if (marks_keep) {
        fp.nmarks = pm[0]; fe.nmarks = em[0];
        for (int i = 0; i < 15; i++) { fp.marks[i / 3][i % 3] = pm[1 + i]; fe.marks[i / 3][i % 3] = em[1 + i]; }
    }
    marks_keep = 1;
    if (game_mode == MODE_GAUNTLET) { fp.maxhp = DF->hp_p + 2; fp.hp = gaunt_hp; }
    set_opponent(OPP->art);
    fp.kmul = OPP->zoom; zoom = OPP->zoom;
    stain_n = 0; cpu_req = 0;
    show_fp = show_fe = 1;
    ko_side = 0; freeze = 0; darken_t = 0; flash_white = 0; slash_t = 0; super_dark = 0;
    rip_owner = 0; combo = 0; round_hp_lost = 0; finish_kind = 0;
    for (int i = 0; i < NPART; i++) parts[i].life = 0;
    ai.react = 0; ai.atk_wait = 60; ai.rip_t = 0; ai.retreat_t = 0;
    camC = 0; scene_dark = 0;
    fade = 16;
    music_stop();
    goto_scene(S_INTRO);
    page = bow;
}

static u32 floor_score;                  /* score when this floor began (a continue goes back to it) */
static void begin_cutscene(int id);
static int pre_cut(void);
static void begin_floor(void) {
    fp.meter = (rival_help && floor_i == 4 && ambush_opp < 0) ? 100 : 0;     /* the rival's advice: she starts the roof ready */
    ch_guarded = ch_hit = ch_iai_ok = ch_sweep_jumped = ch_pistol_dodged = 0; ch_final_h = -1;
    if (game_mode == MODE_STORY && floor_i <= 4 && (floor_i > save_floor || (floor_i == save_floor && diff_i > save_diff))) {   /* remember the climb */
        save_floor = floor_i; save_diff = diff_i; save_best();
    }
    if (game_mode == MODE_STORY && floor_i <= 4 && floor_i == save_floor && !cheat_on) {   /* and the story so far */
        save_honour = honour; save_spared = 0;
        for (int i = 0; i < 7; i++) if (spared[i]) save_spared |= 1 << i;
        save_rival = rival_done | (rival_help << 1);
        save_best();
    }
    floor_score = score;
    wins_p = wins_e = 0; round_no = 1;
    tut_step = (diff_i == 0 && !tut_done && floor_i == F_DOCK && game_mode == MODE_STORY) ? TUT_CUT : TUT_OFF;
    marks_keep = 0;
    start_round(1);
}
static void start_match(void) {          /* a new campaign from the chosen floor */
    score = 0; honour = 0; game_mode = MODE_STORY;
    for (int i = 0; i < 7; i++) spared[i] = 0;
    ambush_opp = -1; rival_done = 0; rival_help = 0;
    floor_i = start_floor ? start_floor : F_DOCK;
    begin_cutscene(pre_cut());
}

static void put_tilemap(int col, int row, const u16 *m, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) TEXT_MAP[(row + y) * 32 + col + x] = m[y * w + x];
}
static void title_text(int row, const char *s, int bank) { text_put(10 + (20 - slen(s)) / 2, row, s, bank); }

static int menu_level, ta_floor;         /* 0 the modes, 1 the difficulty, 2 the floor (time attack) */
static void put_time(int col, int row, u32 fr, int bank) {   /* m:ss.cc */
    u32 cs = fr * 100 / 60; char b[8];
    u32 m = cs / 6000; if (m > 9) m = 9;
    b[0] = '0' + m; b[1] = ':'; b[2] = '0' + (cs / 1000) % 6; b[3] = '0' + (cs / 100) % 10; b[4] = '.';
    b[5] = '0' + (cs / 10) % 10; b[6] = '0' + cs % 10; b[7] = 0;
    text_put(col, row, b, bank);
}
static int menu_items(void) { return menu_level == 0 ? 3 + (save_floor >= 1) : menu_level == 1 ? 3 : 1; }
static void draw_menu(void) {
    for (int r = 14; r <= 19; r++) for (int c = 9; c < 30; c++) TEXT_MAP[r * 32 + c] = 0;
    int cont = save_floor >= 1;
    if (menu_level == 0) {
        static const char *const modes[3] = {"NEW CLIMB", "GAUNTLET", "TIME ATTACK"};
        for (int i = 0; i < menu_items(); i++) {
            text_put(13, 15 + i, i == menu_sel ? ">" : " ", T_GOLD);
            if (cont && i == 0) {
                static char cs[] = "CONTINUE 0F";
                cs[9] = '1' + save_floor;
                text_put(15, 15, cs, menu_sel == 0 ? T_GOLD : T_WHITE);
            } else text_put(15, 15 + i, modes[i - cont], i == menu_sel ? T_GOLD : T_WHITE);
        }
        if (cont && menu_sel == 0) text_put(15, 19, DIFFS[save_diff].name, T_RED);
        if (menu_sel == cont + 1 && best_gaunt) { text_put(15, 19, "BEST", T_WHITE); put_time(20, 19, best_gaunt, T_GOLD); }
    } else if (menu_level == 1) {
        for (int i = 0; i < 3; i++) {
            text_put(13, 15 + i, i == menu_sel ? ">" : " ", T_GOLD);
            text_put(15, 15 + i, DIFFS[i].name, i == menu_sel ? T_GOLD : T_WHITE);
        }
    } else {
        static char fl[] = "< FLOOR 1 >";
        fl[8] = '1' + ta_floor;
        text_put(14, 15, fl, T_GOLD);
        text_put(15, 16, OPPS[ta_floor].art->name, T_WHITE);
        if (best_time[ta_floor]) { text_put(14, 18, "BEST", T_WHITE); put_time(19, 18, best_time[ta_floor], T_GOLD); }
    }
}

/* up up down down left right left right B A */
static const u16 konami_seq[10] = {KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_LEFT, KEY_RIGHT, KEY_B, KEY_A};
static int konami_i;
static int konami_feed(void) {
    if (!keys_hit) return 0;
    if (keys_hit == konami_seq[konami_i]) {
        if (++konami_i == 10) { konami_i = 0; return 1; }
    } else konami_i = keys_hit == KEY_UP ? (konami_i == 2 ? 2 : 1) : 0;
    return 0;
}
static void sfx_secret(void) { sq2(note(76), 2, 12, 2); sq1(note(83), 2, 10, 3, (1 << 4) | 0 | 1); }

static void draw_cheats(int row) {
    for (int r = row; r < row + NCHEATS + 4; r++) text_clear_row(r);
    text_center(row, "SECRETS OF THE PAGODA", T_GOLD);
    for (int i = 0; i <= NCHEATS; i++) {
        int y = row + 2 + i;
        text_put(4, y, i == cheat_sel ? ">" : " ", T_GOLD);
        text_put(6, y, cheat_names[i], i == cheat_sel ? T_GOLD : T_WHITE);
        if (i == NCHEATS) { static char fl[] = "1  "; fl[0] = '1' + start_floor; text_put(22, y, fl, start_floor ? T_RED : T_WHITE); }
        else text_put(22, y, (cheat_on >> i) & 1 ? "ON " : "OFF", (cheat_on >> i) & 1 ? T_RED : T_WHITE);
    }
}
/* returns 1 when the player backs out */
static int cheats_input(int row) {
    if (keys_hit & KEY_UP) { cheat_sel = (cheat_sel + NCHEATS) % (NCHEATS + 1); sfx_stance(); draw_cheats(row); }
    if (keys_hit & KEY_DOWN) { cheat_sel = (cheat_sel + 1) % (NCHEATS + 1); sfx_stance(); draw_cheats(row); }
    if ((keys_hit & KEY_A) && cheat_sel == NCHEATS) {
        start_floor = (start_floor + 1) % 5;
        if (start_floor) cheat_on |= CH_FLOOR; else cheat_on &= ~CH_FLOOR;
        sfx_menu(); draw_cheats(row);
    } else if (keys_hit & KEY_A) {
        cheat_on ^= 1 << cheat_sel;
        if (cheat_sel == 3 && (cheat_on & CH_TURBO)) cheat_on &= ~CH_BULLET;
        if (cheat_sel == 4 && (cheat_on & CH_BULLET)) cheat_on &= ~CH_TURBO;
        sfx_menu(); draw_cheats(row);
    }
    return (keys_hit & (KEY_B | KEY_START)) != 0;
}

static void scene_cheats(void) {
    if (scene_t == 0) { scene_dark = 13; show_fp = 0; text_clear(); draw_cheats(3);
        text_center(15, "A: TOGGLE    B: BACK", T_WHITE);
        text_center(17, "THE BEST SCORE IS NOT SAVED", T_RED);
        text_center(18, "WHILE A CHEAT IS ON", T_RED); }
    if (scene_t > 4 && cheats_input(3)) { sfx_menu(); goto_scene(S_TITLE); }
}

static void scene_title(void) {
    if (scene_t == 0) {
        if (stage_i != TITLE_STAGE || stairs_on) load_stage(TITLE_STAGE);
        grey_scene = 0; zoom = 64; dark_level = 0;
        reset_fighter(&fp, &art_lieve, -76, 1, 0);
        reset_fighter(&fe, &art_kenji, 200, -1, 1);
        set_opponent(&art_kenji);
        show_fp = 1; show_fe = 0; stain_n = 0;
        camC = 0; scene_dark = 3; fade = 16; menu_on = 0; konami_i = 0; menu_sel = 0; menu_level = 0;
        music_play(SONG_TITLE);
    }
    if (fade > 0 && (scene_t & 1)) fade--;
    {   /* eight views, a quarter second... no, half a second each: one slow turn every 4 s */
        static const u8 tfr[8] = {0, 1, 2, 1, 0, 4, 3, 4};
        static const s8 tdir[8] = {1, 1, 1, -1, -1, -1, 1, 1};
        int k = (scene_t / 30) & 7;
        fp.st = ST_POSE; fp.pose_anim = A_TURN; fp.pose_fr = tfr[k]; fp.dir = tdir[k];
    }
    if (flash_white > 0 && scene == S_TITLE) flash_white--;
    if (scene_t > 0 && konami_feed()) { cheats_unlocked = 1; sfx_secret(); goto_scene(S_CHEATS); return; }
    if (scene_t >= 4 && scene_t <= 36) {         /* the brush paints KOMO left to right, flicking ink */
        int n = (scene_t - 4) * LOGO_W / 32 + 1;
        if (n > LOGO_W) n = LOGO_W;
        for (int y = 0; y < LOGO_H; y++) for (int x = 0; x < n; x++) TEXT_MAP[(1 + y) * 32 + 10 + x] = logo_map[y * LOGO_W + x];
        if ((scene_t & 1) == 0) { spawn_gy = 70; spawn(1, (10 + n) * 8, 10 + (int)rnd(LOGO_H * 8), 1); }
    }
    if (scene_t == 44) {                          /* the seal comes down: thump, flash, ink */
        put_tilemap(26, 8, seal_map, SEAL_W, SEAL_H); sfx_taiko();
        flash_white = 3; shake = 10;
        spawn_gy = 130; spawn_up = 300;
        spawn(1, 26 * 8 + 12, 8 * 8 + 24, 14);
        spawn_up = 0;
    }
    if (scene_t == 60) text_put(10 + (16 - 14) / 2, 10, "NAGASAKI, 1651", T_WHITE);
    if (!menu_on) {
        if (scene_t == 70) title_text(18, "A GAME BY BEZOON", T_WHITE);
        if (scene_t > 70) {
            if ((scene_t & 31) == 0) title_text(15, "PRESS START", T_GOLD);
            if ((scene_t & 31) == 20) text_clear_row(15);
        }
        if (best_score && scene_t == 70) { text_put(11, 12, "BEST", T_WHITE); put_num(16, 12, best_score, 6, T_GOLD); }
        if (scene_t > 30 && (keys_hit & (KEY_START | KEY_A))) {
            sfx_menu(); fade = 0; menu_on = 1; draw_menu();
        }
        return;
    }
    if (keys_hit & KEY_UP) { menu_sel = (menu_sel + menu_items() - 1) % menu_items(); sfx_stance(); draw_menu(); }
    if (keys_hit & KEY_DOWN) { menu_sel = (menu_sel + 1) % menu_items(); sfx_stance(); draw_menu(); }
    if (menu_level == 2 && (keys_hit & (KEY_LEFT | KEY_RIGHT))) { ta_floor = (ta_floor + ((keys_hit & KEY_RIGHT) ? 1 : 4)) % 5; sfx_stance(); draw_menu(); }
    if (keys_hit & KEY_B) {
        if (menu_level) { menu_level = menu_level == 2 ? 1 : 0; menu_sel = 0; draw_menu(); return; }
        menu_on = 0; scene_t = 0; text_clear(); return;
    }
    if (keys_hit & (KEY_A | KEY_START)) {
        sfx_menu();
        int cont = save_floor >= 1;
        if (menu_level == 0) {
            if (cont && menu_sel == 0) {             /* back to the floor she reached */
                diff_i = save_diff; story_seen = 1; game_mode = MODE_STORY;
                score = 0; honour = save_honour; for (int i = 0; i < 7; i++) spared[i] = (save_spared >> i) & 1;
                ambush_opp = -1; rival_done = save_rival & 1; rival_help = (save_rival >> 1) & 1;
                floor_i = save_floor;
                begin_cutscene(pre_cut());
                return;
            }
            game_mode = menu_sel - cont;
            menu_level = 1; menu_sel = 1; draw_menu();
            return;
        }
        if (menu_level == 1) {
            diff_i = menu_sel;
            if (game_mode == MODE_TIME) { menu_level = 2; menu_sel = 0; draw_menu(); return; }
            if (game_mode == MODE_GAUNTLET) {
                score = 0; floor_i = 0; run_frames = 0; gaunt_hp = DF->hp_p + 2; honour = 0;
                menu_level = 0; begin_floor(); return;
            }
            menu_level = 0;
            if (!story_seen) { goto_scene(S_STORY); page = 0; }
            else start_match();
            return;
        }
        score = 0; floor_i = ta_floor; run_frames = 0; menu_level = 0;
        begin_floor();
    }
}

static void scene_story(void) {
    if (scene_t == 0) {
        show_fp = 0; show_fe = 0; scene_dark = 13;
        if (page == 0) music_play(SONG_STORY);
        text_clear();
        for (int i = 0; i < 6; i++) text_center(5 + i * 2, story[page][i], i == 0 && page == 0 ? T_GOLD : T_WHITE);
        text_put(18, 18, "B SKIP  A >", T_GOLD);
    }
    if (scene_t > 15 && (keys_hit & (KEY_A | KEY_START))) {
        sfx_menu();
        if (++page >= 4) begin_cutscene(CUT_PROLOGUE);
        else scene_t = -1;
    }
    if (keys_hit & KEY_B) begin_cutscene(CUT_PROLOGUE);
}

static void scene_howto(void) {
    if (scene_t == 0) {
        scene_dark = 13;
        text_center(1, "HOW TO FIGHT", T_GOLD);
        text_put(1, 3, "A        CUT", T_WHITE);
        text_put(1, 4, "A A A    CHAIN 3 CUTS", T_WHITE);
        text_put(1, 5, "UP,A     HIGH CUT, LONG REACH", T_WHITE);
        text_put(1, 6, "DOWN+A   LOW CUT", T_WHITE);
        text_put(1, 7, "B HOLD   GUARD (+UP/DOWN)", T_WHITE);
        text_put(1, 8, "UP/DOWN  JUMP / CROUCH", T_WHITE);
        text_put(1, 9, "L / R    STEP BACK / FORWARD", T_WHITE);
        text_put(1, 10, "< < > >  DODGE / DASH", T_WHITE);
        text_put(1, 11, "A+B      QUICK DRAW (KI FULL)", T_WHITE);
        text_put(1, 13, "GUARD AT HIS CUT'S HEIGHT.", T_GOLD);
        text_put(1, 14, "PRESS B JUST AS IT LANDS TO", T_GOLD);
        text_put(1, 15, "PARRY, THEN A TO COUNTER.", T_GOLD);
        text_put(1, 17, "EACH CUT COSTS ONE SPIRIT *", T_RED);
        text_put(1, 18, "WIN TWO ROUNDS.", T_RED);
        text_put(24, 19, "A >", T_GOLD);
    }
    if (scene_t > 15 && (keys_hit & (KEY_A | KEY_START | KEY_B))) { sfx_menu(); start_match(); }
}

/* ------------------------------------------------------------------ cutscenes
 * A tiny script language: each scene is a list of commands. A advances the text,
 * START skips the whole scene. */
enum { C_END, C_STAGE, C_FADEIN, C_FADEOUT, C_ACTOR, C_HIDE, C_POSE, C_WALK, C_SAY, C_WAIT, C_GREY, C_LIGHT, C_DIM, C_CARD, C_STILL, C_INK };
typedef struct { u8 op, a; s16 b, c, d; const char *s1, *s2; } Cmd;
static const Art *const cut_arts[8] = {&art_lieve, &art_kenji, &art_hozen, &art_gonzo, &art_kagero, &art_kurogane, &art_sailor, &art_morsel};
#define SAY(who, text) {C_SAY, 0, 0, 0, 0, who, text}
#define NARR(text) {C_SAY, 1, 0, 0, 0, 0, text}
#define STAGE(n) {C_STAGE, n, 0, 0, 0, 0, 0}
#define ACTORS(opp, ox) {C_ACTOR, 0, 0, -70, 1, 0, 0}, {C_ACTOR, 1, opp, ox, -1, 0, 0}
#define FADEIN {C_FADEIN, 0, 16, 0, 0, 0, 0}
#define FADEOUT {C_FADEOUT, 0, 16, 0, 0, 0, 0}
#define KEEP {C_STAGE, 255, 0, 0, 0, 0, 0}
#define POSE(w, an, fr) {C_POSE, w, an, fr, 0, 0, 0}
#define WAIT(n) {C_WAIT, 0, n, 0, 0, 0, 0}
#define DIM(n) {C_DIM, n, 0, 0, 0, 0, 0}
#define END {C_END, 0, 0, 0, 0, 0, 0}
#define STILL(n) {C_STILL, n, 0, 0, 0, 0, 0}
#define INK {C_INK, 0, 0, 0, 0, 0, 0}

static const Cmd cut_pre1[] = {STAGE(0), ACTORS(1, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    SAY("KENJI:A", "A DUTCH GIRL WITH A SWORD?|GO HOME. MY BLADE IS WOOD,|BUT IT STILL BREAKS BONES."),
    SAY("LIEVE:A", "THEN BREAK THEM."),
    SAY("KENJI", "I AM THE GATE. NO ONE CLIMBS|WHO CANNOT GET PAST ME."), INK, END};
static const Cmd cut_pre2[] = {STAGE(1), ACTORS(2, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    SAY("HOZEN", "I KNEW YOUR FATHER. HE CAME|HERE TO BUY SOMETHING THAT|WAS NOT FOR SALE."),
    SAY("LIEVE", "WHAT WAS IT?"),
    SAY("HOZEN", "ASK THE ONES ABOVE ME.|IF YOU LIVE THAT LONG."), INK, END};
static const Cmd cut_pre3[] = {STAGE(2), ACTORS(3, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    SAY("GONZO", "I GUARDED THE DOOR WHEN YOUR|FATHER MET THEM. I HEARD|EVERY WORD."),
    SAY("LIEVE:A", "TELL ME."),
    SAY("GONZO:A", "COME CLOSER, LITTLE ONE.|I WILL WHISPER IT."), INK, END};
static const Cmd cut_pre4[] = {STAGE(3), ACTORS(4, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    SAY("KAGERO", "THEY PAY ME TO WATCH.|THEY PAID ME TO WATCH|HIM DIE."),
    SAY("LIEVE:A", "WHO PAID YOU?"),
    SAY("KAGERO", "A MAN WITH RED HAIR.|LIKE YOURS."), INK, END};
static const Cmd cut_pre5[] = {STAGE(4), ACTORS(5, 50), POSE(1, A_IDLE_HIGH, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("KUROGANE:A", "THE MERCHANT'S DAUGHTER."),
    SAY("KUROGANE:A", "MORSEL PAID ME FOR ONE|OOSTERLING. HE WILL NOT|MIND IF I KILL TWO."),
    SAY("LIEVE:A", "FOR SYBRAND OOSTERLING."), {C_LIGHT, 0, 0, 0, 0, 0, 0}, INK, END};
static const Cmd cut_post1[] = {KEEP, DIM(4),
    NARR("KENJI WAS THE GATEKEEPER.|HE TURNED BACK THE UNWORTHY|WITHOUT KILLING THEM."),
    NARR("SHE WAS NOT TURNED BACK."),
    NARR("IN HIS SLEEVE: A PAGE TORN|FROM HER FATHER'S LEDGER."),
    NARR("SHE TIES ON HIS WHITE|HEADBAND, AND CLIMBS."), INK, END};
static const Cmd cut_post2[] = {KEEP, DIM(4),
    NARR("HOZEN'S PRAYER BEADS ROLL|ACROSS THE TATAMI."),
    NARR("THE LEDGER PAGE NAMES A|CARGO: SILVER. TEN CHESTS.|NO SHOGUN'S SEAL."), INK, END};
static const Cmd cut_post3[] = {KEEP, DIM(4),
    NARR("THE LANTERNS GUTTER OUT."),
    NARR("SILVER WAS LEAVING JAPAN,|AGAINST THE SHOGUN'S LAW."),
    NARR("SYBRAND MEANT TO REPORT|IT. SOMEONE STOPPED HIM."), INK, END};
static const Cmd cut_post4[] = {KEEP, DIM(4),
    NARR("A PURSE OF DUTCH SILVER|LIES ON THE WET BOARDS."),
    NARR("IT BEARS THE SEAL OF|ADRIAAN MORSEL."),
    NARR("HER FATHER'S DEPUTY."), INK, END};
static const Cmd cut_post5[] = {KEEP, DIM(4), {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("THE STORM BREAKS."),
    NARR("ON THE GIANT'S BELT HANGS|MORSEL'S OWN SEAL."), INK,
    STILL(1), FADEIN,
    NARR("AT DAWN LIEVE WALKS DOWN TO|DEJIMA WITH THE LEDGER,|THE SILVER AND THE SEAL."),
    NARR("THE MAGISTRATE'S MEN ARE|ALREADY WAITING FOR HIM."), FADEOUT, END};
static const Cmd cut_prologue[] = {STILL(0), FADEIN,
    NARR("NAGASAKI, 1651. THE NIGHT|OF THE STORM."),
    NARR("SYBRAND OOSTERLING, THE|FINEST DUTCH TRADER, RODE|IT OUT IN THE HARBOUR."), INK,
    STAGE(4), {C_GREY, 1, 0, 0, 0, 0, 0}, {C_HIDE, 0, 0, 0, 0, 0, 0},
    {C_ACTOR, 1, 5, 30, -1, 0, 0}, POSE(1, A_IDLE_HIGH, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("THAT NIGHT A GIANT IN BLACK|ARMOUR CAME ABOARD, AND|SYBRAND OOSTERLING DIED."),
    {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("NOW HE WAITS ON THE ROOF OF|THE PAGODA OF FIVE FLOORS."),
    FADEOUT, {C_GREY, 0, 0, 0, 0, 0, 0}, END};
static const Cmd cut_dock_pre[] = {STAGE(6), ACTORS(6, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    NARR("DEJIMA, THE NIGHT BEFORE|THE CLIMB."),
    SAY("SAILOR:A", "OI! A GIRL ON THE COMPANY'S|PIER? AND WITH A JAPANESE|SWORD!"),
    SAY("LIEVE", "LET ME PASS. I HAVE BUSINESS|AT THE PAGODA."),
    SAY("SAILOR", "MORSEL PAYS A GUILDER TO|STOP THE OOSTERLING GIRL.|HIC. THAT'S YOU."), INK, END};
static const Cmd cut_dock_post[] = {KEEP, DIM(4),
    NARR("THE SAILOR WILL NOT TROUBLE|HER AGAIN."),
    NARR("SO MORSEL KNOWS SHE IS|HERE. THERE IS NO TURNING|BACK NOW."),
    NARR("ABOVE THE ROOFTOPS, THE|PAGODA WAITS IN THE RAIN."), INK, END};
static const Cmd cut_spire_pre[] = {STAGE(7), ACTORS(7, 60), POSE(1, A_IDLE_MID, 0), FADEIN,
    SAY("MORSEL", "YOU CLIMBED IT ALL. YOUR|FATHER WOULD BE PROUD. HE|WAS ALWAYS SENTIMENTAL."),
    SAY("LIEVE:A", "YOU SOLD THE SHOGUN'S SILVER|AND HAD MY FATHER KILLED|TO HIDE IT."),
    SAY("MORSEL:A", "I KEPT THE COMPANY'S BOOKS|IN ORDER. COME, THEN. THE|SUN IS UP. LET US FINISH IT."), INK, END};
static const Cmd cut_spire_post[] = {KEEP, DIM(4),
    NARR("MORSEL'S PISTOL FALLS|INTO THE CLOUDS."),
    NARR("IN HIS COAT: THE SECOND|LEDGER. EVERY CHEST OF|SILVER, EVERY BRIBE."), INK,
    STILL(1), FADEIN,
    NARR("AT DAWN LIEVE WALKS DOWN TO|DEJIMA WITH BOTH LEDGERS."),
    NARR("HER FATHER'S NAME IS|CLEARED. THE COMPANY'S|IS NOT."), FADEOUT, END};
static const Cmd cut_spire_spared[] = {KEEP, DIM(4),
    NARR("MORSEL KNEELS ON THE|COPPER ROOF, THE SUN|BEHIND HIM."),
    SAY("LIEVE", "THE MAGISTRATE WILL HEAR|ALL OF IT. FROM YOU."), INK,
    STILL(1), FADEIN,
    NARR("AT DAWN LIEVE WALKS DOWN TO|DEJIMA WITH THE LEDGERS|AND ADRIAAN MORSEL, BOUND."),
    NARR("SHE CLIMBED THE PAGODA|WITH HONOUR. ALL OF|NAGASAKI KNOWS IT."), FADEOUT, END};
static const Cmd cut_spared1[] = {KEEP, DIM(4),
    NARR("KENJI KNEELS AND LAYS|DOWN THE BOKKEN."),
    SAY("KENJI:R", "THE GATE IS OPEN. TAKE|THIS. YOU WILL NEED IT."),
    NARR("A WHITE HEADBAND, AND A PAGE|TORN FROM HER FATHER'S|LEDGER."), INK, END};
static const Cmd cut_spared2[] = {KEEP, DIM(4),
    NARR("HOZEN BOWS HIS HEAD AND|COUNTS HIS BEADS."),
    SAY("HOZEN:R", "TEN CHESTS OF SILVER, NO|SHOGUN'S SEAL. YOUR FATHER|WOULD NOT SIGN FOR THEM."), INK, END};
static const Cmd cut_spared3[] = {KEEP, DIM(4),
    NARR("GONZO SITS DOWN HEAVILY|AMONG THE GUTTERED LAMPS."),
    SAY("GONZO:H", "THE SILVER WAS LEAVING|JAPAN. SYBRAND MEANT TO|TELL THE MAGISTRATE."),
    SAY("GONZO:R", "THEY MADE SURE HE NEVER|DID."), INK, END};
static const Cmd cut_spared4[] = {KEEP, DIM(4),
    NARR("KAGERO PRESSES A PURSE OF|DUTCH SILVER INTO HER HAND."),
    SAY("KAGERO:R", "MORSEL'S SEAL. YOUR FATHER'S|OWN DEPUTY. GO UP, RED HAIR."), INK, END};
static const Cmd cut_spared5[] = {KEEP, DIM(4), {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("THE GIANT KNEELS IN THE|RAIN AND DROPS THE NODACHI."),
    SAY("KUROGANE:H", "MORSEL PAID ME. I WILL SAY|SO BEFORE THE MAGISTRATE."), INK,
    STILL(1), FADEIN,
    NARR("AT DAWN LIEVE WALKS DOWN TO|DEJIMA WITH THE LEDGER AND|A GIANT IN CHAINS."),
    NARR("THE MAGISTRATE'S MEN ARE|ALREADY WAITING FOR MORSEL."), FADEOUT, END};
static const Cmd cut_secret[] = {KEEP, DIM(4), {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("THE GIANT KNEELS IN THE|RAIN AND DROPS THE NODACHI."),
    SAY("KUROGANE:H", "YOU SPARE ME? THEN HEAR|THIS. MORSEL IS NOT ON|DEJIMA."),
    SAY("KUROGANE:R", "HE WATCHES FROM THE SPIRE|ABOVE US, WAITING TO SEE|WHICH OF US FALLS."),
    SAY("LIEVE", "THEN LET HIM SEE."), INK, END};
/* v5.5 the returning rival: a spared master meets her on the roof. With honour, he helps. */
static const Cmd cut_help1[] = {STAGE(4), ACTORS(1, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("KENJI:R", "YOU LEFT ME MY LIFE AT THE|GATE. I CLIMBED AFTER YOU|TO REPAY IT."),
    SAY("KENJI", "HIS ARMOUR IS LACQUER ON|OLD IRON. IT SPLITS AT THE|SEAMS. CUT THE SEAMS."),
    SAY("LIEVE", "THANK YOU, KENJI."), INK, END};
static const Cmd cut_help2[] = {STAGE(4), ACTORS(2, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("HOZEN:R", "I PRAYED FOR YOU ON EVERY|STAIR. NOW LISTEN."),
    SAY("HOZEN", "THE GIANT'S PLATES ARE TIED|WITH SILK. THE RAIN HAS|ROTTED IT. THEY WILL FALL."),
    SAY("LIEVE", "THEN I WILL CUT WHAT IS|LEFT."), INK, END};
static const Cmd cut_help3[] = {STAGE(4), ACTORS(3, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("GONZO:R", "I CARRIED HIS ARMOUR UP|THESE STAIRS FOR TEN YEARS.|I KNOW EVERY CRACK IN IT."),
    SAY("GONZO", "THE LEFT SIDE. ALWAYS THE|LEFT. GO ON, LITTLE ONE."), INK, END};
static const Cmd cut_help4[] = {STAGE(4), ACTORS(4, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("KAGERO:R", "I WATCHED HIM ARM HIMSELF.|THE CORDS OF HIS ARMOUR|ARE CUT. MY KNIVES."),
    SAY("LIEVE", "WHY HELP ME?"),
    SAY("KAGERO", "YOU LET ME LIVE. NO ONE|EVER PAID ME THAT."), INK, END};
/* ...without it, he comes for revenge */
static const Cmd cut_ambush1[] = {STAGE(4), ACTORS(1, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("KENJI:A", "I KNELT TO YOU AT THE GATE.|SINCE THEN YOU HAVE BOWED|TO NO ONE."),
    SAY("KENJI:A", "THE GATE WAS WRONG TO OPEN.|I WILL CLOSE IT."),
    SAY("LIEVE:A", "STAND ASIDE, KENJI."), INK, END};
static const Cmd cut_ambush2[] = {STAGE(4), ACTORS(2, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("HOZEN:A", "I PRAYED THAT MERCY WOULD|TEACH YOU MERCY. IT DID|NOT."),
    SAY("HOZEN:A", "SO I WILL TEACH YOU|MYSELF."), INK, END};
static const Cmd cut_ambush3[] = {STAGE(4), ACTORS(3, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("GONZO:A", "YOU LEFT ME ALIVE AND THEN|CUT DOWN THE REST LIKE|DOGS."),
    SAY("GONZO:A", "THE GIANT CAN WAIT. FIRST|YOU DEAL WITH ME."), INK, END};
static const Cmd cut_ambush4[] = {STAGE(4), ACTORS(4, 60), POSE(1, A_IDLE_MID, 0), FADEIN, {C_LIGHT, 0, 0, 0, 0, 0, 0},
    SAY("KAGERO:A", "MORSEL PAYS DOUBLE FOR YOU|NOW. AND YOU HAVE MADE|IT EASY TO TAKE."),
    SAY("LIEVE:A", "THEN EARN IT."), INK, END};
/* the low road: she becomes what she hunted */
static const Cmd cut_dark_end[] = {KEEP, DIM(4), {C_LIGHT, 0, 0, 0, 0, 0, 0},
    NARR("THE GIANT FALLS. SHE DOES|NOT STOP CUTTING."),
    NARR("SHE LIFTS THE BLACK HELMET|FROM THE ROOF, AND PUTS|IT ON."), INK,
    STILL(0), FADEIN,
    NARR("MORSEL'S MEN FIND A FIGURE|IN BLACK ARMOUR ON THE|ROOF. THEY DO NOT CLIMB."),
    NARR("NAGASAKI HAS A NEW|DEMON. NO ONE REMEMBERS|SYBRAND OOSTERLING."), FADEOUT, END};
static const Cmd *const cuts[NCUTS] = {cut_pre1, cut_pre2, cut_pre3, cut_pre4, cut_pre5,
                                    cut_post1, cut_post2, cut_post3, cut_post4, cut_post5, cut_prologue,
                                    cut_dock_pre, cut_dock_post, cut_spire_pre, cut_spire_post, cut_spire_spared,
                                    cut_spared1, cut_spared2, cut_spared3, cut_spared4, cut_spared5, cut_secret,
                                    cut_help1, cut_help2, cut_help3, cut_help4, cut_ambush1, cut_ambush2, cut_ambush3, cut_ambush4,
                                    cut_dark_end};
static int rival_of(void) {                  /* the last master she spared below the roof, or -1 */
    for (int i = 3; i >= 0; i--) if (spared[i]) return i;
    return -1;
}
static int pre_cut(void) {                   /* the scene before a floor's duel */
    if (floor_i == F_DOCK) return CUT_DOCK_PRE;
    if (floor_i == F_SPIRE) return CUT_SPIRE_PRE;
    if (floor_i == 4 && game_mode == MODE_STORY && !rival_done) {
        int r = rival_of();
        if (r >= 0 && honour >= 6) return CUT_RIVAL_HELP + r;
        if (r >= 0 && honour <= 0) return CUT_RIVAL_AMBUSH + r;
    }
    return CUT_PRE + floor_i;
}
static int post_cut(void) {                 /* which scene follows a won duel */
    if (ambush_opp >= 0) { ambush_opp = -1; rival_done = 1; return CUT_PRE + 4; }   /* past him, to the giant */
    if (floor_i == F_DOCK) return CUT_DOCK_POST;
    if (floor_i == F_SPIRE) return spared[F_SPIRE] ? CUT_SPIRE_SPARED : CUT_SPIRE_POST;
    if (floor_i == 4 && spared[4]) return honour >= 10 ? CUT_SECRET : CUT_SPARED + 4;
    if (floor_i == 4 && !spared[4] && honour <= -3) return CUT_DARK_END;
    return spared[floor_i] ? CUT_SPARED + floor_i : CUT_POST + floor_i;
}
static int ending_cut(int id) { return id == CUT_POST + 4 || id == CUT_SPARED + 4 || id == CUT_SPIRE_POST || id == CUT_SPIRE_SPARED || id == CUT_DARK_END; }

static int cut_id, cut_pc, cut_t, cut_len, cut_shown, ink_x, portrait_on;
/* an ink brush sweeps across the screen, left to right, with a ragged edge */
static void draw_ink(int x) {
    int full = x >> 3, part = x & 7;
    for (int r = 0; r < 20; r++)
        for (int c = 0; c < 30; c++) {
            if (c < full) TEXT_MAP[r * 32 + c] = (8 << 12) | (WIPE_TILE + 8);
            else if (c == full && part) TEXT_MAP[r * 32 + c] = (8 << 12) | (RAG_TILE + part - 1);
        }
}
/* close-ups of whoever speaks: Lieve on the left, the masters on the right */
static const char *const portrait_names[8] = {"LIEVE", "KENJI", "HOZEN", "GONZO", "KAGERO", "KUROGANE", "SAILOR", "MORSEL"};
static void portrait_hide(void) {
    if (!portrait_on) return;
    for (int r = 1; r <= 6; r++) for (int c = 0; c < 30; c++) TEXT_MAP[r * 32 + c] = 0;
    portrait_on = 0; portrait_pal = 0;
}
static void portrait_show(const char *who) {
    portrait_hide();
    if (!who) return;
    int idx = -1, expr = 0;
    for (int i = 0; i < 8; i++) {
        const char *a = who, *b = portrait_names[i];
        while (*a && *a != ':' && *a == *b) { a++; b++; }
        if ((!*a || *a == ':') && !*b) { idx = i; if (*a == ':') expr = a[1] == 'A' ? 1 : a[1] == 'H' ? 2 : a[1] == 'R' ? 3 : 0; }
    }
    if (idx < 0) return;
    /* v5.5: four faces each: calm, angry, hurt, respectful (the speaker's tag: "KENJI:A") */
    plat_copy32(VRAM + CB2 + PORTRAIT_TILE * 32, portrait_tiles + (idx * 4 + expr) * PORTRAIT_WORDS, PORTRAIT_WORDS);
    const u16 *pal = cut_arts[idx]->pal16;
    static u16 ppal[16];
    for (int i = 0; i < 16; i++) ppal[i] = pal[i];
    if (expr == 2) ppal[15] = BLOOD_RGB;                  /* index 15 is the blood on a hurt face */
    portrait_pal = ppal;
    for (int i = 0; i < 16; i++) PAL_BG[144 + i] = grey_scene ? grey(ppal[i]) : ppal[i];
    int left = idx == 0, c0 = left ? 1 : 23;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++) {
            int tx = left ? x : 5 - x;
            TEXT_MAP[(1 + y) * 32 + c0 + x] = (9 << 12) | (PORTRAIT_TILE + y * 6 + tx) | (left ? 0 : 0x400);
        }
    portrait_on = 1;
}
static const Cmd *cut_cmd(void) { return &cuts[cut_id][cut_pc]; }
static void begin_cutscene(int id) {
    cut_id = id; cut_pc = 0; cut_t = 0;
    paused = 0; tut_step = TUT_OFF; duel_on = 0;
    music_play(ending_cut(id) ? SONG_ENDING : id == CUT_DOCK_PRE ? SONG_DOCK : SONG_STORY);
    goto_scene(S_CUT);
}
static void cut_draw_text(const Cmd *c, int n) {
    for (int r = 13; r <= 18; r++) text_clear_row(r);
    if (c->s1) {                                   /* the speaker's name, without the face tag */
        char nm[12]; int k = 0;
        while (c->s1[k] && c->s1[k] != ':' && k < 11) { nm[k] = c->s1[k]; k++; }
        nm[k] = 0;
        text_put(2, 13, nm, c->s1[0] == 'L' ? T_GOLD : T_RED);
    }
    char line[32];
    int row = 15, col = 0, shown = 0;
    for (const char *p = c->s2; *p && shown < n; p++, shown++) {
        if (*p == '|') { line[col] = 0; text_put(2, row++, line, T_WHITE); col = 0; continue; }
        if (col < 28) line[col++] = *p;
    }
    line[col] = 0; text_put(2, row, line, T_WHITE);
}
static void cut_finish(void) {
    int id = cut_id;
    grey_scene = 0; scene_dark = 0; fade = 16;
    text_clear();
    if (id == CUT_PROLOGUE) { story_seen = 1; goto_scene(S_HOWTO); return; }
    if (id >= CUT_RIVAL_HELP && id < CUT_RIVAL_AMBUSH) { rival_done = 1; rival_help = 1; save_best(); begin_cutscene(CUT_PRE + 4); return; }
    if (id >= CUT_RIVAL_AMBUSH && id < CUT_DARK_END) { ambush_opp = id - CUT_RIVAL_AMBUSH; music_stop(); begin_floor(); return; }
    if (id < CUT_POST || id == CUT_DOCK_PRE || id == CUT_SPIRE_PRE) { music_stop(); begin_floor(); return; }
    if (ending_cut(id)) { ending_done = 1; goto_scene(S_CREDITS); return; }
    if (id == CUT_DOCK_POST) { floor_i = 0; begin_cutscene(CUT_PRE); return; }   /* up to the pagoda */
    if (id == CUT_SECRET) { floor_i = F_SPIRE; goto_scene(S_STAIRS); return; }
    floor_i++;
    goto_scene(S_STAIRS);
}
static void scene_cut(void) {
    if (keys_hit & KEY_START) { cut_finish(); return; }
    for (;;) {
        const Cmd *c = cut_cmd();
        switch (c->op) {
        case C_END: cut_finish(); return;
        case C_STAGE:
            if (c->a != 255) { fade = 16; if (stage_i != c->a || stairs_on) load_stage(c->a); camC = 0; zoom = 64; stain_n = 0; scene_dark = 0; }
            cut_pc++; continue;
        case C_GREY: grey_scene = c->a; load_palettes(); cut_pc++; continue;
        case C_STILL: fade = 16; load_still(c->a); show_fp = show_fe = 0; scene_dark = 0; cut_pc++; continue;
        case C_INK:
            if (cut_t == 0) ink_x = 0;
            cut_t++;
            ink_x += 14;
            draw_ink(ink_x);
            if (ink_x < 256) return;
            fade = 16; text_clear(); brush_clear(); portrait_hide();
            cut_t = 0; cut_pc++; continue;
        case C_LIGHT: lightning_now = 1; cut_pc++; continue;
        case C_DIM: scene_dark = c->a; cut_pc++; continue;
        case C_HIDE: if (c->a) show_fe = 0; else show_fp = 0; cut_pc++; continue;
        case C_ACTOR: {
            Fighter *f = c->a ? &fe : &fp;
            const Art *art = c->a ? cut_arts[c->b] : hero_art();
            reset_fighter(f, art, c->c, c->d, c->a);
            if (c->a) { set_opponent(art); show_fe = 1; } else show_fp = 1;
            if (stage_i == 4) { fp.kmul = 48; zoom = 48; }
            f->st = ST_POSE; f->pose_anim = A_IDLE_MID; f->pose_fr = 0;
            cut_pc++; continue;
        }
        case C_POSE: {
            Fighter *f = c->a ? &fe : &fp;
            f->st = ST_POSE; f->pose_anim = c->b; f->pose_fr = c->c;
            cut_pc++; continue;
        }
        case C_FADEIN:
            if (fade > 0) { if (cut_t++ & 1) fade--; return; }
            cut_t = 0; cut_pc++; continue;
        case C_FADEOUT:
            if (fade < 16) { if (cut_t++ & 1) fade++; return; }
            cut_t = 0; cut_pc++; continue;
        case C_WAIT:
            if (cut_t++ < c->b) return;
            cut_t = 0; cut_pc++; continue;
        case C_SAY: {
            if (cut_t == 0) { cut_len = slen(c->s2); cut_shown = 0; portrait_show(c->s1); }
            cut_t++;
            if (cut_shown < cut_len) {
                cut_shown += 2;
                if ((cut_t & 3) == 0) sq2(note(84 + (cut_t & 4)), 2, 3, 1);
                cut_draw_text(c, cut_shown);
                if (cut_t > 6 && (keys_hit & KEY_A)) { cut_shown = cut_len; cut_draw_text(c, cut_len); }
                if (cut_shown >= cut_len) text_put(26, 18, ">", T_GOLD);
                return;
            }
            if ((cut_t & 31) == 0) text_put(26, 18, ">", T_GOLD);
            if ((cut_t & 31) == 16) text_put(26, 18, " ", T_GOLD);
            if (keys_hit & KEY_A) {
                sfx_menu();
                for (int r = 13; r <= 18; r++) text_clear_row(r);
                portrait_hide();
                cut_t = 0; cut_pc++; continue;
            }
            return;
        }
        default: cut_pc++; continue;
        }
    }
}

/* ------------------------------------------------------------------ the stair climb between floors */
static void scene_stairs(void) {
    int t = scene_t;
    if (t == 0) {
        fade = 16;
        load_stairs(OPP_STAGE());
        show_fp = 0; show_fe = 0; camC = 0; zoom = 64; stain_n = 0; scene_dark = 0;
        for (int i = 0; i < NPART; i++) parts[i].life = 0;
        music_play(SONG_STAIRS);
    }
    if (t < 32 && (t & 1)) fade--;
    /* only her footsteps on the boards, climbing */
    if (t > 10 && t < 170 && (t % 18) == 0) noise_fx(6 + (t / 36), 1, 6, 2, 6);
    if (t == 24 && floor_i == F_SPIRE) { text_put(2, 4, "THE SPIRE", T_GOLD); sfx_taiko(); }
    if (t == 50 && floor_i == F_SPIRE) text_put(2, 6, "ABOVE THE STORM", T_WHITE);
    if (t == 24 && floor_i != F_SPIRE) {
        const u16 *km = kanji_maps[floor_i - 1];
        put_tilemap(2, 2, km, kanji_w[floor_i - 1], kanji_h[floor_i - 1]);
        text_put(2, 2 + kanji_h[floor_i - 1] + 1, OPP->floor, T_GOLD);
        sfx_taiko();
    }
    if (t == 50 && floor_i != F_SPIRE) text_put(2, 2 + kanji_h[floor_i - 1] + 3, floor_i == 1 ? "THE BAMBOO ROOM" : floor_i == 2 ? "THE LANTERN HALL" : floor_i == 3 ? "THE RAIN GALLERY" : "THE STORM ROOF", T_WHITE);
    if (t > 190 || (t > 40 && (keys_hit & (KEY_START | KEY_A)))) {
        if (scene_t < 191) { scene_t = 191; ink_x = 0; }
        ink_x += 14; draw_ink(ink_x);
        if (ink_x < 256) return;
        fade = 16; text_clear();
        begin_cutscene(pre_cut());
    }
}

static Amb *iai_leaf;
static void iai_begin(void) {
    iai_on = 1; iai_state = 1; iai_t = 0; iai_res = 0; iai_free = 0;
    cam_px = 120; cam_py = 112; cam_k = 64;
    iai_wait = 110 + rnd(130);
    iai_cpu = 21 - diff_i * 4 - (floor_i < 5 ? floor_i : 4) / 2 - OPP->skill / 2;    /* the masters draw faster as she climbs */
    if (floor_i == 4 || floor_i == F_SPIRE) iai_cpu -= 3;     /* the giant and Morsel draw fastest */
    if (iai_cpu < 11) iai_cpu = 11;                          /* never faster than a human can answer */
    iai_cpu += rnd(5);
    fp.x = -80 * 256; fe.x = 80 * 256; fp.z = fe.z = 0;
    fp.st = fe.st = ST_POSE; fp.pose_anim = fe.pose_anim = A_IAI;
    text_clear_row(17);
    draw_bars(20);
    if (!iai_seen) { text_center(5, "IAI: WATCH THE LEAF FALL", T_GOLD); text_center(6, "DRAW (A) WHEN IT HITS THE RING", T_WHITE); }
    for (int i = 0; i < NAMB; i++) if (amb[i].kind <= AK_DEBRIS) amb[i].life = 0;   /* only the one leaf */
    Amb *p = amb_add(AK_LEAF, 120, -6, 0, 0, 255, 1);       /* its fall is driven by iai_t (see iai_update) */
    iai_leaf = p;
    prop_want = 3; prop_load(3);
    if (p) p->age = 0;
    noise_fx(3, 7, 2, 7, 120);                    /* the wind */
}
static void iai_end(int res) {
    static const char *const msg[5] = {0, "IAI - IPPON!", "FIRST TO DRAW!", "TOO SLOW!", "TOO EAGER!"};
    iai_on = 0; iai_state = 0; iai_seen = 1; iai_last = res; cam_k = 64; text_clear_row(8);
    if (res <= 2) ch_iai_ok = 1;
    prop_want = 0; prop_load(0);
    draw_bars(0); for (int i = 0; i < 96; i++) hud_vram[i] = 0xFFFF;
    text_clear_row(5); text_clear_row(6); text_clear_row(7);
    text_clear_row(14); text_clear_row(15); text_clear_row(16); text_clear_row(17); text_clear_row(18); text_clear_row(19);
    fp.st = fe.st = ST_IDLE; fp.t = fe.t = 0;
    duel_on = 1;
    music_play(song_for_floor(floor_i));
    goto_scene(S_DUEL);
    banner(msg[res], res <= 2 ? T_GOLD : T_RED, 70);
    Fighter *a = res <= 2 ? &fp : &fe, *v = res <= 2 ? &fe : &fp;
    v->st = ST_STAGGER; v->t = 0;
    a->st = ST_SUPER; a->t = 0; a->h = MID; a->hitdone = 0; a->chain = 0;
    a->unblock = res == 1 ? 2 : 0; a->riposte = 0;                       /* a perfect draw kills; otherwise whoever draws first lands one clean cut */
    a->ghost_t = 14; super_dark = 0;
    if (res == 1) fe.hp = 1;
    sfx_super(); shock_t = 16;
}
static void iai_update(void) {
    iai_t++;
    draw_bars(20);
    fp.pose_fr = fe.pose_fr = (iai_t / 20) & 1;
    if (iai_leaf && iai_leaf->kind == AK_LEAF && iai_leaf->life == 255 && iai_t <= iai_wait)
        iai_leaf->y = -6 * 256 + (int)((118 * 256 * iai_t) / iai_wait);   /* lands in the reticule exactly on the cue */
    if (!iai_free) {                                 /* hands off: a held or mashed A doesn't count yet */
        if (keys & KEY_A) { if (iai_t > 6) text_center(7, "HANDS OFF THE SWORD...", T_RED); iai_t = iai_t > 40 ? 40 : iai_t; return; }
        if (iai_t > 40) { iai_free = 1; text_clear_row(7); if (iai_seen) text_center(7, "WAIT FOR THE LEAF", T_WHITE); }
        return;
    }
    if (iai_t < iai_wait) {
        if (keys_hit & KEY_A) { iai_end(4); return; }
        return;
    }
    if (iai_t == iai_wait) {                       /* the leaf touches the boards */
        for (int i = 0; i < NAMB; i++) if (amb[i].kind == AK_LEAF && amb[i].life == 255) amb[i].life = 1;
        sfx_taiko(); flash_white = 2; shock_t = 10; iai_state = 2;
        text_clear_row(7); text_center(8, "NOW!", T_RED);
    }
    int dt = iai_t - iai_wait;
    if (keys_hit & KEY_A) { iai_end(dt <= (diff_i == 0 ? 6 : diff_i == 1 ? 4 : 3) ? 1 : 2); return; }
    if (dt >= iai_cpu) iai_end(3);
}
static void scene_intro(void) {
    int t = scene_t;
    if (fade > 0 && (t & 1)) fade--;
    int bow = page;
    if (t == 2) {
        if (bow) {
            text_center(4, OPP->floor, T_GOLD);
            text_center(6, OPP->art->name, T_WHITE);
            text_center(7, OPP->title, T_WHITE);
        }
    }
    int b0 = bow ? 40 : 10, b1 = b0 + (bow ? 20 : 0), b2 = b1 + (bow ? 16 : 0), b3 = b2 + (bow ? 20 : 0), b4 = b3 + 12;
    int b5 = b4 + ((round_no == 1 || round_no == 3) && !tut_step ? 48 : 0);     /* the master's flourish */
    int bow_req = bow && game_mode == MODE_STORY && floor_i != F_DOCK && !tut_step;
    if (t == 2 && bow_req) { bowed = 0; text_center(17, "B: RETURN THE BOW", T_GOLD); }
    if (t == 2 && bow && game_mode == MODE_STORY && ambush_opp < 0 && !tut_step && floor_i <= 6) {   /* v5.5 the floor's challenge */
        text_center(9, (ch_done >> floor_i) & 1 ? "CHALLENGE (MET)" : "CHALLENGE", T_GOLD);
        text_center(10, CH_TXT[floor_i], T_WHITE);
    }
    if (bow_req && !bowed && t >= 4 && t < b1 && (keys_hit & KEY_B)) { bowed = 1; text_clear_row(17); sfx_stance(); }
    if (bow_req && t == b3) {
        text_clear_row(17);
        if (bowed) { honour++; text_center(17, "HONOUR +1", T_GOLD); }
        else { honour--; text_center(17, "SHE DID NOT RETURN THE BOW", T_RED); }
    }
    if (t > b5 && iai_on) { iai_update(); return; }
    if (t == b2 - 4 && floor_i == 4 && round_no > 1) lightning_now = 1;         /* the storm grows */
    int fr = 0;
    if (bow) {
        if (t >= b0 && t < b1) fr = (t - b0) * 5 / 20;
        else if (t >= b1 && t < b2) fr = 4;
        else if (t >= b2 && t < b3) fr = 4 - (t - b2) * 5 / 20;
    }
    if (t == b1 && bow) { text_clear_row(6); text_clear_row(7); text_clear_row(4); text_clear_row(9); text_clear_row(10); }
    if (t == b2 - 4) {
        static char r[] = "ROUND 1";
        r[6] = '0' + round_no;
        banner(round_no == 3 ? "FINAL ROUND" : r, T_WHITE, 30);
        const u16 *m = round_no == 1 ? cal_rnd1_map : round_no == 2 ? cal_rnd2_map : cal_rnd3_map;
        brush_start(m, CAL_RND1_W, CAL_RND1_H, 6, 10, 34);
    }
    if (t < b3) {
        fp.st = fe.st = ST_POSE;
        fp.pose_anim = fe.pose_anim = A_BOW;
        fp.pose_fr = fe.pose_fr = fr;
        if (bow_req && !bowed) { fp.pose_anim = A_IDLE_MID; fp.pose_fr = (t / 6) & 7; }
    } else if (t < b4) {
        fp.pose_anim = fe.pose_anim = A_RISE;
        fp.pose_fr = fe.pose_fr = (t - b3) * 3 / 12;
    } else if (t < b5) {
        fp.pose_anim = A_IDLE_MID; fp.pose_fr = ((t - b4) / 6) & 7;
        fe.pose_anim = A_INTRO; fe.pose_fr = (t - b4) / 6;
        if (t == b4 + 14) sfx_whoosh();
        if (t == b4 + 26 && floor_i == 4) lightning_now = 1;                    /* the nodachi overhead, in the lightning */
    } else if (t == b5 && game_mode != MODE_GAUNTLET && floor_i != F_DOCK && ambush_opp < 0 && !tut_step && wins_p == WINS_NEED() - 1
#ifdef HOST
               && !getenv("NOIAI")
#endif
               ) {
        iai_begin();                             /* match point: the draw-off */
    } else if (t == b5) {
        fp.st = fe.st = ST_IDLE;
        fp.t = fe.t = 0;
        sfx_taiko();
        duel_on = 1;
        if (!tut_step) music_play(song_for_floor(floor_i));
        goto_scene(S_DUEL);
        banner("HAJIME!", T_GOLD, 50);
        brush_start(cal_hajime_map, CAL_HAJIME_W, CAL_HAJIME_H, 6, 8, 40);
        if (tut_step) tut_set(tut_step);
    }
}

static void end_tutorial(void) {
    tut_done = 1;
    tut_step = TUT_OFF;
    score = 0; fp.meter = 0;
    text_clear_row(17); text_clear_row(18);
    start_round(0);
    page = 0;
}

static void draw_pause(void) {
    for (int r = 5; r <= 17; r++) text_clear_row(r);
    if (pause_page) { draw_cheats(5); text_center(17, "A: TOGGLE    B: BACK", T_WHITE); return; }
    text_center(7, "PAUSED", T_GOLD);
    const char *items[3] = {"RESUME", cheats_unlocked ? "CHEATS" : 0, game_mode == MODE_STORY ? "SAVE AND QUIT" : "QUIT TO TITLE"};
    int y = 9;
    for (int i = 0; i < 3; i++) {
        if (!items[i]) continue;
        text_put(9, y, i == pause_sel ? ">" : " ", T_GOLD);
        text_put(11, y, items[i], i == pause_sel ? T_GOLD : T_WHITE);
        y += 2;
    }
    if (game_mode == MODE_STORY) { text_center(15, "YOUR CLIMB IS SAVED AT", T_WHITE); text_center(16, "THE START OF EACH FLOOR", T_WHITE); }
}
static void pause_update(void) {
    if (pause_page) {
        if (cheats_input(5)) { pause_page = 0; draw_pause(); }
        return;
    }
    if (keys_hit & (KEY_UP | KEY_DOWN)) {
        int step = (keys_hit & KEY_UP) ? 2 : 1;
        do pause_sel = (pause_sel + step) % 3; while (pause_sel == 1 && !cheats_unlocked);
        sfx_stance(); draw_pause();
    }
    if (keys_hit & (KEY_START | KEY_B)) pause_sel = 0;
    if (keys_hit & (KEY_A | KEY_START | KEY_B)) {
        sfx_menu();
        if (pause_sel == 0) { paused = 0; scene_dark = 0; for (int r = 5; r <= 17; r++) text_clear_row(r); }
        else if (pause_sel == 1) { pause_page = 1; draw_pause(); }
        else { paused = 0; scene_dark = 0; tut_step = TUT_OFF; music_stop(); goto_scene(S_TITLE); }
    }
}

/* brush calligraphy painted onto the text layer, revealed column by column */
static const u16 *brush_map;
static int brush_w, brush_h, brush_col, brush_row, brush_t, brush_life, brush_rev;
static void brush_clear(void) {
    if (!brush_map) return;
    for (int y = 0; y < brush_h; y++)
        for (int x = 0; x < brush_w; x++) TEXT_MAP[(brush_row + y) * 32 + brush_col + x] = 0;
    brush_map = 0;
}
static void brush_start(const u16 *map, int w, int h, int row, int reveal, int life) {
    brush_clear();
    brush_map = map; brush_w = w; brush_h = h; brush_row = row; brush_col = 15 - w / 2;
    brush_t = 0; brush_rev = reveal; brush_life = life;
}
static void brush_update(void) {
    if (!brush_map) return;
    brush_t++;
    int n = brush_t >= brush_rev ? brush_w : brush_t * brush_w / brush_rev + 1;
    for (int y = 0; y < brush_h; y++)
        for (int x = 0; x < n && x < brush_w; x++) TEXT_MAP[(brush_row + y) * 32 + brush_col + x] = brush_map[y * brush_w + x];
    if (brush_t < brush_rev && (brush_t & 3) == 0) {   /* ink flicks off the brush */
        spawn_gy = (brush_row + brush_h) * 8 + 12;
        spawn(1, (brush_col + n) * 8, brush_row * 8 + 4 + (int)rnd(brush_h * 8), 1);
    }
    if (brush_t >= brush_life) brush_clear();
}
/* the killing blow: letterbox bars, a white-on-black frame, and 一本 painted across the screen */
static void draw_bars(int h) {
    int full = h >> 3, part = h & 7;
    for (int r = 0; r < 3; r++) {
        u16 e = r < full ? ((8 << 12) | (WIPE_TILE + 8)) : (r == full && part) ? ((8 << 12) | (WIPE_TILE + part)) : 0;
        for (int c = 0; c < 30; c++) TEXT_MAP[r * 32 + c] = e;
        int k = r, rb = 19 - k;
        u16 e2 = k < full ? ((8 << 12) | (WIPE_TILE + 8)) : (k == full && part) ? ((8 << 12) | (BAR_TILE + part)) : 0;
        for (int c = 0; c < 30; c++) TEXT_MAP[rb * 32 + c] = e2;
    }
}
static void cine_start(int ippon) {
    cine_t = 1; cine_ippon = ippon; cine_flash = 9; cine_bar = 0;
}
static void cine_update(void) {
    if (!cine_t) return;
    cine_t++;
    if (cine_flash > 0) cine_flash--;
    {   /* the smash zoom toward the cut while the figures are white on black */
        /* a smash cut to twice life size and back: at exactly 2x the enlarged sprite boxes tile without
           overlapping, which keeps the hardware within its per-line sprite budget (in-between zooms lose bands) */
        if (cine_flash > 0) cam_k = cine_flash > 3 ? 128 : 64;
        else if (spare_t || iai_on || replay_t) { /* their own camera */ }
        else cam_k = 64;
    }
    int hold = scene == S_DUEL && ko_side && ko_t < 100;
    int target = hold ? 20 : 0;
    if (cine_bar < target) cine_bar += 2;
    else if (cine_bar > target) cine_bar -= 2;
    draw_bars(cine_bar);
    if (cine_ippon && cine_t == 14) brush_start(cal_ippon_map, CAL_IPPON_W, CAL_IPPON_H, 7, 18, 90);
    if (cine_t > 20 && cine_bar == 0) {
        cine_t = 0;
        for (int i = 0; i < 96; i++) hud_vram[i] = 0xFFFF;
        text_clear_row(17); text_clear_row(18); text_clear_row(19);
    }
}

static int wipe_y;
static void draw_wipe(int y) {
    int full = y >> 3, part = y & 7;
    for (int r = 0; r < 20; r++) {
        u16 e = r < full ? ((8 << 12) | (WIPE_TILE + 8)) : (r == full && part) ? ((8 << 12) | (WIPE_TILE + part)) : 0;
        for (int c = 0; c < 30; c++) TEXT_MAP[r * 32 + c] = e;
    }
}
/* frames per second: a hardware timer ticks once a second; count the frames drawn between ticks */
static int fps_val = 60, fps_frames;
static u16 fps_sec;
static void fps_init(void) {
#ifndef HOST
    REG16(0x10E) = 0; REG16(0x10A) = 0;
    REG16(0x108) = 65536 - 16384;           /* 16.78 MHz / 1024 = 16384 Hz: overflows once a second */
    REG16(0x10C) = 0;
    REG16(0x10E) = 0x84;                     /* timer 3 counts timer 2's overflows */
    REG16(0x10A) = 0x83;                     /* timer 2 on, prescaler 1024 */
    fps_sec = REG16(0x10C);
#endif
}
static void fps_tick(void) {
    fps_frames++;
#ifdef HOST
    if (fps_frames >= 60) { fps_val = fps_frames; fps_frames = 0; }
#else
    u16 sec = REG16(0x10C);
    if (sec != fps_sec) { fps_sec = sec; fps_val = fps_frames; fps_frames = 0; }
#endif
    char b[8] = "FPS 00";
    int v = fps_val > 99 ? 99 : fps_val;
    b[4] = '0' + v / 10; b[5] = '0' + v % 10;
    text_put(0, 19, b, v >= 58 ? T_WHITE : v >= 45 ? T_GOLD : T_RED);
}

int spare_t_pub;
static void spare_update(void) {
    /* time stops; the camera pushes in on the kneeling master. Mashing A does nothing:
       the buttons must be let go, then A HELD to finish him, or B to spare him */
    static int released, hold_a;
    spare_t++; spare_t_pub = spare_t;
    if (spare_t == 1) { released = 0; hold_a = 0; }
    fe.pose_fr = spare_t / 8 > 3 ? 3 : spare_t / 8;
    /* a cut to a close-up of him alone at exactly twice life size: the enlarged sprite boxes then tile
       without overlapping, so the hardware's per-line sprite budget is never exceeded (no missing bands) */
    cam_k = 128 * 64 / fig_k(&fe);
    {   /* frame his chest at the centre of the screen, whatever his size */
        const u8 *an = fe.art->anc + fe.frame * 4;
        int cx = screen_x(&fe), cy = feet_y(&fe) - 60;
        if (an[0]) canvas_to_screen(&fe, an[0], an[1], &cx, &cy);
        if (cam_k != 64) { cam_px = (120 * 64 - cx * cam_k) / (64 - cam_k); cam_py = (98 * 64 - cy * cam_k) / (64 - cam_k); }
    }
    if ((spare_t % 48) == 1) drum_fx(7, 2, 0, 0, 6);      /* a heartbeat */
    if (spare_t == 30) { text_center(15, OPP->art->name, T_WHITE); text_center(16, "KNEELS AND WAITS.", T_WHITE); }
    if (spare_t < 50) return;
    if (!released) {
        if (keys & (KEY_A | KEY_B)) { if (spare_t == 50) text_center(18, "LET GO OF THE SWORD...", T_RED); return; }
        released = 1;
        text_clear_row(18);
        text_center(17, "B: SPARE    HOLD A: FINISH", T_GOLD);
    }
    if (keys_hit & KEY_B) {                       /* mercy */
        spare_t = 0; spare_t_pub = 0; cam_k = 64;
        for (int r = 15; r <= 18; r++) text_clear_row(r);
        spared[floor_i] = 1; spared_now = 1; honour += 3;
        banner("SPARED.  HONOUR +3", T_GOLD, 90);
        fp.st = ST_IDLE; fp.t = 0;
        ko_side = 1; ko_t = 0; finish_kind = 0;
        sq2(note(79), 1, 8, 2);
        return;
    }
    hold_a = (keys & KEY_A) ? hold_a + 1 : 0;
    {   static char bar[11];
        int n = hold_a * 10 / 40; if (n > 10) n = 10;
        for (int i = 0; i < 10; i++) bar[i] = i < n ? '*' : ' ';
        bar[10] = 0;
        text_center(18, bar, T_RED);
    }
    if (hold_a >= 40 || spare_t > 600) {          /* the final cut */
        spare_t = 0; spare_t_pub = 0; cam_k = 64;
        for (int r = 15; r <= 18; r++) text_clear_row(r);
        fp.st = ST_IDLE; fp.t = 0; fp.h = spare_h;
        spare_lock = 1; kill(&fp, &fe, spare_h); spare_lock = 0;
    }
}
static void poem_show(void) {
    int i = floor_i == F_DOCK ? 5 : floor_i == F_SPIRE ? 6 : floor_i;
    fade = 16;
    plat_copy32(VRAM, poem_tiles[i], poem_words[i]);
    plat_copy32(VRAM + SB_DOJO * 0x800, poem_maps[i], 1024);
    cur_wall_pal = poem_pal; stairs_on = 1;
    REG_DISPCNT &= ~0x0200;
    grey_amt = 0; wipe_y = 0; text_clear(); brush_clear();
    show_fp = show_fe = 0; scene_dark = 0; dawn_amt = 0; spot_on = 0; poem_on = 1;
    for (int k = 0; k < 96; k++) hud_buf[k] = 0;
    hud_dirty = 1;
    hdma_key[0] = hdma_key[1] = 0x7FFFFFFF;
    for (int k = 0; k < NAMB; k++) amb[k].life = 0;
    load_palettes(); last_light = -1;
}
static void rec_frame(void) {
    Snap *n = &rec[rec_head & 63];
    Fighter *f2[2] = {&fp, &fe};
    for (int i = 0; i < 2; i++) {
        Fighter *f = f2[i];
        n->x[i] = f->x; n->fr[i] = (s16)f->frame; n->z[i] = (s16)f->z; n->hop[i] = (s16)f->hop; n->yoff[i] = (s16)f->yoff;
        n->kmul[i] = (s16)f->kmul; n->dir[i] = (s8)f->dir; n->bl[i] = (u8)f->bloody; n->mos[i] = (u8)(f->mosaic < 40 ? f->mosaic : 0);
    }
    n->camC = (s16)camC; n->slt = (s8)slash_t; n->slx = (s16)slash_x; n->sly = (s16)slash_y; n->sla = (u8)slash_ang;
    rec_head++;
}
static Fighter rp_save[2];
static int rp_cam, rp_slash[4];
static void replay_update(void) {
    /* slowed to a third, at twice life size from low down: 24 frames before the cut, 16 after */
    if (replay_t == 1) {
        rp_save[0] = fp; rp_save[1] = fe; rp_cam = camC;
        rp_slash[0] = slash_t; rp_slash[1] = slash_x; rp_slash[2] = slash_y; rp_slash[3] = slash_ang;
        draw_bars(20); scene_dark = 5; drum_fx(8, 2, 0, 0, 12); brush_clear(); banner_t = 0;
        for (int r = 3; r < 17; r++) text_clear_row(r);
    }
    int i = replay_t / 3;
    u32 at = replay_mark - 24 + (u32)i;
    if (at >= rec_head) at = rec_head - 1;
    const Snap *n = &rec[at & 63];
    Fighter *f2[2] = {&fp, &fe};
    for (int k = 0; k < 2; k++) {
        Fighter *f = f2[k];
        f->x = n->x[k]; f->frame = n->fr[k]; f->z = n->z[k]; f->hop = n->hop[k]; f->yoff = n->yoff[k];
        f->kmul = n->kmul[k]; f->dir = n->dir[k]; f->bloody = n->bl[k]; f->mosaic = n->mos[k]; f->flash = 0; f->ghost_t = 0;
    }
    camC = n->camC; slash_t = n->slt; slash_x = n->slx; slash_y = n->sly; slash_ang = n->sla;
    if (at == replay_mark && (replay_t % 3) == 0) { sfx_whoosh(); shake = 4; }
    {   /* twice life size, framed on the meeting point at chest height, a little below centre (looking up) */
        const Snap *m = &rec[replay_mark & 63];
        int ax = m->x[0] >> 8, bx = m->x[1] >> 8;
        int tx = wx_to_screen((ax + bx) / 2), ty = feet_y(&fe) - 62;
        cam_k = 128;
        cam_px = 2 * tx - 120; cam_py = 2 * ty - 92;      /* (C*64 - t*k) / (64 - k) with k = 128 */
    }
    replay_t++;
    if (replay_t > 40 * 3 || (replay_t > 12 && (keys_hit & (KEY_A | KEY_START)))) {
        fp = rp_save[0]; fe = rp_save[1]; camC = rp_cam;
        slash_t = rp_slash[0]; slash_x = rp_slash[1]; slash_y = rp_slash[2]; slash_ang = rp_slash[3];
        fp.shown = fe.shown = -1;
        replay_t = 0; replay_ok = 0; cam_k = 64; scene_dark = 0; draw_bars(0);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 30; c++) TEXT_MAP[r * 32 + c] = 0;
    }
}
static void scene_duel(void) {
#ifdef HOST
    if (getenv("NOREPLAY")) replay_ok = 0;
#endif
    if (replay_ok && !replay_t && !spare_t && !spared_now && !paused && rec_head - replay_mark >= 20 && rec_head > 40) {
        replay_t = 1;                            /* v5.5: the final cut again, slowly */
    }
    if (replay_t) { replay_update(); return; }
    if (spare_t) { spare_update(); return; }
    if (game_mode != MODE_STORY && !paused && !ko_side && duel_on && run_frames < 599 * 60) run_frames++;
    if (!paused && !tut_step && (keys_hit & KEY_START) && !ko_side) {
        paused = 1; pause_sel = 0; pause_page = 0; scene_dark = 10; sfx_menu(); draw_pause(); return;
    }
    if (combo_t > 0 && --combo_t == 0) combo = 0;
    if (rip_t > 0 && --rip_t == 0) { rip_owner = 0; if (tut_step == TUT_COUNTER) tut_set(TUT_PARRY); }
    if (tut_step) {
        tut_t++;
        if (keys_hit & KEY_START) { end_tutorial(); return; }
        if (tut_step == TUT_HIGHLOW && tut_t > 600) tut_set(TUT_GUARD);
        if (tut_step == TUT_STEP && tut_t > 480) tut_set(TUT_MOVES);
        if (tut_step == TUT_MOVES && tut_t > 200) { banner("NOW FOR REAL!", T_GOLD, 60); end_tutorial(); return; }
        if (tut_t == 1 && tut_step == TUT_CUT) text_put(1, 19, "START: SKIP TUTORIAL", T_WHITE);
    }
    if (!ko_side) return;
    ko_t++;
    Fighter *w = ko_side == 1 ? &fp : &fe;
    Fighter *l = ko_side == 1 ? &fe : &fp;
    {   /* between rounds the fallen dissolve into blocks */
        int last = wins_p >= WINS_NEED() || wins_e >= WINS_NEED();
        if (!last && l->st == ST_DEAD && !l->fall && ko_t > 96 + chib_extra) { l->mosaic = 40 + (ko_t - 96 - chib_extra) / 2; if (l->mosaic > 54) l->mosaic = 54; }
    }
    if (ko_t == 40) {
        if (ko_side == 1) {
            wins_p++;
            add_score(1000);
            if (!round_hp_lost) { banner("PERFECT!", T_GOLD, 90); add_score(2000); }
            
        } else wins_e++;
    }
    {   /* she flicks the blood from the blade (chiburi) and sheathes it (noto) */
        int last = wins_p >= WINS_NEED() || wins_e >= WINS_NEED();
        if (w == &fp && !last && !fp.disarmed && ko_t >= 44 && (ko_t == 44 ? fp.st == ST_IDLE : fp.st == ST_POSE && chib_extra)) {
            int t = ko_t - 44;
            fp.st = ST_POSE; chib_extra = 36;
            if (t < 24) { fp.pose_anim = A_CHIBURI; fp.pose_fr = t / 4; }
            else { fp.pose_anim = A_NOTO; fp.pose_fr = (t - 26) / 5 > 7 ? 7 : (t - 26 < 0 ? 0 : (t - 26) / 5); }
            if (t == 9) chiburi_flick(&fp);
            if (t == 66) { sq2(note(91), 2, 6, 1); }             /* the click of the guard on the scabbard */
        }
    }
    if ((l->st == ST_DEAD || (spared_now && l == &fe)) && (w->st == ST_IDLE || (w->st == ST_POSE && chib_extra)) && ko_t > 70 && w->st != ST_VICTORY) {
        if (wins_p >= WINS_NEED() || wins_e >= WINS_NEED()) {
            w->st = ST_VICTORY; w->t = 0;
            if (ko_side == 1) { banner("VICTORY", T_GOLD, 1000); music_play(SONG_WIN); }
            else { banner("DEFEATED", T_RED, 1000); music_play(SONG_LOSE); }
        } else if (ko_t > 130 + chib_extra) {
            round_no++;
            start_round(0);
        }
    }
    if (w->st == ST_VICTORY && ko_side == 2) {
        /* the defeat, in the style of Kurosawa: the colour drains away, then the black comes down */
        int t = w->t;
        if (t == 10) {                               /* snapshot the colour and a black-and-white copy */
            for (int i = 0; i < 512; i++) split_col[i] = PAL_BG[i];
            for (int i = 0; i < 512; i++) { u16 c = split_col[i]; int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
                int l = (r * 77 + g * 150 + b * 29) >> 8; split_grey[i] = l | (l << 5) | (l << 10); }
            split_y = 1;
        }
        if (t > 10 && t < 100) split_y = 1 + (t - 10) * 2;    /* the line sweeps from top to bottom */
        if (t == 100) {                              /* all of it grey now: make it the ordinary palette */
            split_y = -1; grey_amt = 16; load_palettes(); last_light = -1;
            plat_split(-1, 0, 0);
        }
        if (t == 130) {                              /* a death poem over the still scene */
            int i = floor_i == F_DOCK ? 5 : floor_i == F_SPIRE ? 6 : floor_i;
            hud_hide = 1; scene_dark = 4;
            text_center(7, "- DEATH POEM -", T_RED);
            for (int k = 0; k < 3; k++) text_center(10 + k * 2, POEM_TXT[i][k], T_WHITE);
        }
        if (t > 460 || (t > 170 && (keys_hit & (KEY_A | KEY_START | KEY_B)))) goto_scene(S_CONTINUE);
        return;
    }
    if (w->st == ST_VICTORY && w->t > (w == &fp ? (trophy_on ? 230 : 150) : 110)) goto_scene(wins_p >= WINS_NEED() ? S_RESULT : S_CONTINUE);
}

/* ------------------------------------------------------------------ the credits, after the ending */
#define CR_PAGE 210
#define CR_PAGES 12
static void credits_page(int p) {
    text_clear(); show_fp = 0; show_fe = 0;
    switch (p) {
    case 0: put_tilemap(10, 3, logo_map, LOGO_W, LOGO_H); text_center(12, "NAGASAKI, 1651", T_WHITE); break;
    case 1: text_center(7, "A GAME BY", T_WHITE); text_center(10, "BEZOON", T_GOLD); break;
    case 2:
        reset_fighter(&fp, &art_lieve, -40, 1, 0); show_fp = 1;
        fp.st = ST_POSE; fp.pose_anim = A_TURN; fp.pose_fr = 2; set_frame(&fp, A_TURN, 2);
        text_put(14, 8, "LIEVE OOSTERLING", T_GOLD); text_put(14, 10, "THE MERCHANT'S", T_WHITE); text_put(14, 11, "DAUGHTER", T_WHITE);
        break;
    case 3: case 4: case 5: case 6: case 7: {
        const Opp *o = &OPPS[p - 3];
        reset_fighter(&fe, o->art, 72, -1, 1); set_opponent(o->art); show_fe = 1;
        fe.st = ST_POSE; fe.pose_anim = A_IDLE_MID; fe.pose_fr = 0; set_frame(&fe, A_IDLE_MID, 0);
        text_put(1, 7, o->art->name, T_GOLD); text_put(1, 9, o->title, T_WHITE); text_put(1, 11, o->floor, T_RED);
        break;
    }
    case 8:
        text_center(2, "MUSIC", T_GOLD);
        text_center(4, "KOMO", T_WHITE); text_center(5, "THE GATE", T_WHITE); text_center(6, "BAMBOO WIND", T_WHITE);
        text_center(7, "LANTERNS", T_WHITE); text_center(8, "RAIN ON THE CHAIN", T_WHITE); text_center(9, "KUROGANE", T_WHITE);
        text_center(10, "THE LEDGER", T_WHITE); text_center(11, "ASCENT", T_WHITE); text_center(12, "DAWN OVER DEJIMA", T_WHITE);
        break;
    case 9:
        text_center(5, "PROGRAMMING, ART AND MUSIC", T_WHITE); text_center(8, "MADE WITH CLAUDE", T_GOLD); text_center(10, "BY ANTHROPIC", T_WHITE);
        break;
    case 10: text_center(6, "IN HOMAGE TO THE SAMURAI", T_WHITE); text_center(8, "FILMS OF AKIRA KUROSAWA", T_WHITE); break;
    default:
        put_tilemap(10, 2, logo_map, LOGO_W, LOGO_H);
        text_center(11, "THANK YOU FOR PLAYING", T_GOLD); text_center(14, "THE END", T_WHITE);
        break;
    }
}
static void scene_credits(void) {
    static int page_t, cp;
    if (scene_t == 0) {
        fade = 16; load_stage(1); camC = 0; zoom = 64; stain_n = 0; scene_dark = 6;
        for (int i = 0; i < NPART; i++) parts[i].life = 0;
        music_play(SONG_ENDING);
        cp = 0; page_t = 0; credits_page(0);
    }
    page_t++;
    int last = cp == CR_PAGES - 1;
    if (page_t < 18) { if (page_t & 1) fade = fade > 0 ? fade - 2 : 0; }
    else if (!last && page_t > CR_PAGE - 18) { if (page_t & 1) fade = fade < 16 ? fade + 2 : 16; }
    if (keys_hit & (KEY_START | KEY_A)) {
        if (!last) { cp = CR_PAGES - 1; page_t = 0; fade = 16; credits_page(cp); return; }
        if (page_t > 30) { fade = 0; scene_dark = 11; goto_scene(S_RESULT); return; }
    }
    if (!last && page_t >= CR_PAGE) { cp++; page_t = 0; fade = 16; credits_page(cp); }
    if (last && page_t > CR_PAGE * 2) { fade = 0; goto_scene(S_RESULT); }
}

static void scene_result(void) {
    if (game_mode == MODE_GAUNTLET && floor_i < 4) {           /* straight on to the next master */
        if (scene_t == 0) { scene_dark = 11; text_clear(); static char f[] = "FLOOR 1 CLEARED"; f[6] = '1' + floor_i; text_center(6, f, T_GOLD);
                            put_time(11, 9, run_frames, T_WHITE); text_center(12, "NEXT: ", T_WHITE); text_center(13, OPPS[floor_i + 1].art->name, T_RED); }
        if (scene_t > 60 || (scene_t > 20 && (keys_hit & (KEY_A | KEY_START)))) {
            gaunt_hp = fp.hp + 1 > DF->hp_p + 2 ? DF->hp_p + 2 : fp.hp + 1;
            floor_i++; restore_colour(); begin_floor();
        }
        return;
    }
    if (game_mode != MODE_STORY) {                             /* the clock */
        if (scene_t == 0) {
            scene_dark = 11; text_clear();
            u16 *best = game_mode == MODE_GAUNTLET ? &best_gaunt : &best_time[floor_i];
            text_center(3, game_mode == MODE_GAUNTLET ? "THE GAUNTLET IS DONE" : "TIME ATTACK", T_GOLD);
            if (game_mode == MODE_TIME) text_center(5, OPP->art->name, T_WHITE);
            text_put(8, 8, "TIME", T_WHITE); put_time(15, 8, run_frames, T_GOLD);
            if (!cheat_on && (!*best || run_frames < *best)) { *best = (u16)run_frames; save_times(); text_center(11, "NEW RECORD!", T_GOLD); }
            else if (cheat_on) text_center(11, "CHEATS ON: NOT SAVED", T_RED);
            text_put(8, 9, "BEST", T_WHITE); if (*best) put_time(15, 9, *best, T_GOLD);
            text_center(15, DIFFS[diff_i].name, T_RED);
            text_center(18, "PRESS A", T_GOLD);
        }
        if (scene_t > 30 && (keys_hit & (KEY_START | KEY_A))) { sfx_menu(); restore_colour(); game_mode = MODE_STORY; goto_scene(S_TITLE); }
        return;
    }
    if (scene_t == 0) {
        scene_dark = 11;
        text_clear();
        if (ending_done) {
            text_center(2, "KOMO", T_GOLD);
            text_center(4, "LIEVE OOSTERLING CLIMBED", T_WHITE);
            text_center(5, "THE PAGODA OF FIVE FLOORS.", T_WHITE);
        } else {
            static char f[] = "FLOOR 1 CLEARED";
            f[6] = '1' + floor_i;
            text_center(3, floor_i == F_DOCK ? "THE PIER" : floor_i == F_SPIRE ? "THE SPIRE" : f, T_GOLD);
            text_center(5, OPP->art->name, T_WHITE);
            text_center(6, "IS DEFEATED.", T_WHITE);
        }
        if (ambush_opp < 0 && floor_i <= 6 && !ending_done) {         /* v5.5 the floor's challenge */
            int ok = floor_i == 0 ? !ch_guarded : floor_i == 1 ? ch_sweep_jumped : floor_i == 2 ? !ch_hit :
                     floor_i == 3 ? ch_iai_ok : floor_i == 4 ? wins_e == 0 : floor_i == F_DOCK ? ch_final_h == LOW : ch_pistol_dodged;
            if (ok && !((ch_done >> floor_i) & 1) && !cheat_on) {
                ch_done |= 1 << floor_i; honour += 2; add_score(2000); save_best();
                text_center(13, "CHALLENGE MET!", T_GOLD); text_center(14, "HONOUR +2  SCORE +2000", T_GOLD);
            } else if (ok) text_center(13, "CHALLENGE MET AGAIN", T_GOLD);
            else { text_center(13, "CHALLENGE MISSED:", T_RED); text_center(14, CH_TXT[floor_i], T_RED); }
        }
        text_put(8, 9, "SCORE", T_WHITE); put_num(16, 9, score, 6, T_GOLD);
        if (cheat_on) text_center(12, "CHEATS ON: NOT SAVED", T_RED);
        else if (score > best_score) { best_score = score; save_best(); text_center(12, "NEW RECORD!", T_GOLD); }
        text_put(8, 10, "BEST", T_WHITE); put_num(16, 10, best_score, 6, T_GOLD);
        text_put(8, 11, "HONOUR", T_WHITE);
        { static char hb[5]; int h = honour < 0 ? -honour : honour, n = 0; if (honour < 0) hb[n++] = '-'; if (h >= 10) hb[n++] = '0' + (h / 10) % 10; hb[n++] = '0' + h % 10; hb[n] = 0;
          text_put(17, 11, hb, honour < 0 ? T_RED : T_GOLD); }
        text_center(15, DIFFS[diff_i].name, T_RED);
        text_center(18, ending_done ? "THE END - PRESS START" : "PRESS A", T_GOLD);
    }
    if (scene_t > 30 && (keys_hit & (KEY_START | KEY_A))) {
        sfx_menu();
        if (ending_done) { ending_done = 0; goto_scene(S_TITLE); }
        else begin_cutscene(post_cut());
    }
}

static void restore_colour(void) { grey_amt = 0; wipe_y = 0; poem_on = 0; hud_hide = 0; split_y = -1; plat_split(-1, 0, 0); load_palettes(); last_light = -1; last_lantern = 99; }
static void scene_continue(void) {
    if (game_mode != MODE_STORY) {
        if (scene_t == 0) {
            scene_dark = 4; text_clear();
            if (game_mode == MODE_GAUNTLET) { text_center(5, "THE GAUNTLET ENDS", T_RED); static char f[] = "ON FLOOR 1"; f[9] = '1' + floor_i; text_center(7, f, T_WHITE); text_center(15, "PRESS A", T_GOLD); }
            else { text_center(5, "LIEVE FALLS.", T_RED); text_center(15, "A: TRY AGAIN   B: TITLE", T_WHITE); }
        }
        if (scene_t > 20 && game_mode == MODE_TIME && (keys_hit & (KEY_A | KEY_START))) { sfx_menu(); restore_colour(); run_frames = 0; begin_floor(); return; }
        if (scene_t > 20 && (keys_hit & (KEY_A | KEY_B | KEY_START))) { restore_colour(); game_mode = MODE_STORY; goto_scene(S_TITLE); }
        return;
    }
    int left = 9 - scene_t / 60;
    if (scene_t == 0) {
        scene_dark = 4;
        text_clear();
        text_center(4, "LIEVE FALLS.", T_RED);
        text_put(8, 7, "SCORE", T_WHITE); put_num(16, 7, score, 6, T_GOLD);
        text_center(10, "CONTINUE?", T_GOLD);
        text_center(15, "A: RISE AGAIN   B: GIVE UP", T_WHITE);
        if (!cheat_on && score > best_score) { best_score = score; save_best(); }
    }
    if (scene_t % 60 == 0) { static char n[] = "9"; n[0] = '0' + (left < 0 ? 0 : left); text_clear_row(12); text_center(12, n, T_WHITE); sfx_stance(); }
    if (scene_t > 20 && (keys_hit & (KEY_A | KEY_START))) { sfx_menu(); score = floor_score; restore_colour(); begin_floor(); return; }
    if ((scene_t > 20 && (keys_hit & KEY_B)) || left < 0) { restore_colour(); goto_scene(S_TITLE); }
}

/* ------------------------------------------------------------------ stage loading (screen faded) */
static void load_stage(int si) {
    const StageArt *a = &stage_art[si];
    stage_i = si; stairs_on = 0;
    plat_copy32(VRAM, a->bg2, a->bg2_words);
    plat_copy32(VRAM + SB_DOJO * 0x800, a->bg2_map, 1024);
    plat_copy32(VRAM + CB2, a->cb2, a->cb2_words);
    plat_copy32(VRAM + SB_OUT * 0x800, a->out_map, 512);
    cur_wall_pal = a->pw; cur_floor_pal = a->pf; cur_out_pal = a->po;
    REG_DISPCNT |= 0x0200;
    REG_BGCNT(1) = (wet_stage(stage_i) ? 1 : 3) | (2 << 2) | (SB_RAIN << 8);   /* storm rain blows over everything */
    if (si == TITLE_STAGE || si == 7) REG_DISPCNT &= ~0x0200;              /* a clear night; above the storm: no rain */
    dark_level = 0; dark_t = 900; dawn_amt = 0; fire_t = 0; pole_t = 0;
    hz_done[0] = hz_done[1] = 0;
    hdma_key[0] = hdma_key[1] = 0x7FFFFFFF;
    load_palettes(); last_light = -1; last_lantern = 99;
}
static void load_stairs(int next_stage) {
    load_stage(next_stage);                 /* the window shows the next floor's sky */
    plat_copy32(VRAM, stair_tiles, sizeof(stair_tiles) / 4);
    plat_copy32(VRAM + SB_DOJO * 0x800, stair_map, 1024);
    cur_wall_pal = stair_pal; stairs_on = 1;
    REG_BGCNT(1) = 3 | (2 << 2) | (SB_RAIN << 8);
    hdma_key[0] = hdma_key[1] = 0x7FFFFFFF;
    load_palettes(); last_light = -1;
}
static void load_still(int id) {
    load_stage(id ? 1 : 4);                      /* sky and rain behind it are irrelevant; reuse a stage's CB2 */
    plat_copy32(VRAM, id ? still1_tiles : still0_tiles, (id ? sizeof(still1_tiles) : sizeof(still0_tiles)) / 4);
    plat_copy32(VRAM + SB_DOJO * 0x800, id ? still1_map : still0_map, 1024);
    cur_wall_pal = id ? still1_pal : still0_pal; stairs_on = 1;
    if (id) REG_DISPCNT &= ~0x0200; else REG_DISPCNT |= 0x0200;   /* the dawn has no rain */
    hdma_key[0] = hdma_key[1] = 0x7FFFFFFF;
    load_palettes(); last_light = -1;
}
static void set_opponent(const Art *a) {
    opp_pal16 = a->pal16;
    fe.art = a; fe.shown = -1;
    load_palettes();
}

/* ------------------------------------------------------------------ init */
static void video_init(void) {
    REG_DISPCNT = 0x80;
    for (int i = 0; i < 1024; i++) TEXT_MAP[i] = 0;
    plat_copy32(VRAM + SB_RAIN * 0x800, rain_map, sizeof(rain_map) / 4);
    load_stage(0);
    plat_copy32(OBJ_VRAM + FX_SLASH * 32, fx_tiles, sizeof(fx_tiles) / 4);
    REG_BGCNT(0) = 0 | (2 << 2) | (SB_TEXT << 8);
    REG_BGCNT(2) = 2 | (0 << 2) | (SB_DOJO << 8) | (1 << 14);
    REG_BGCNT(3) = 3 | (2 << 2) | (SB_OUT << 8);
    REG_BGHOFS(0) = 0; REG_BGVOFS(0) = 0;
    oam_n = 0; oam_hide_rest();
    plat_copy32(OAM_MEM, oam, 256);
    REG_DISPCNT = 0 | 0x0040 | 0x0F00 | 0x1000;
}

int main(void) {
#ifndef HOST
    REG_WAITCNT = 0x4317;
    REG_DISPSTAT = 0x0008;
    REG_IE = 0x0001;
    REG_IME = 1;
#endif
    video_init();
    text_init();
    snd_init();
    load_best();
    load_progress();
    load_times();
    reset_fighter(&fp, &art_lieve, 0, 1, 0);
    reset_fighter(&fe, &art_kenji, 200, -1, 1);
    goto_scene(S_TITLE);
    fps_init();
    for (;;) {
        plat_vsync();
        vblank_commit();
        read_keys();
        rng_s += keys;
        frame_counter++;
        sound_tick();
        update_weather();
        g_scene_dbg = scene;

        int fighting = scene == S_DUEL || scene == S_INTRO;
        int steps = 1;
        static u16 held_hits;
        int incoming = scene == S_DUEL && duel_on && !ko_side && fp.hp > 0 && !fe.chain &&
                       (fe.st == ST_WINDUP || (fe.st == ST_SWING && !fe.hitdone)) &&
                       dist() <= reach(&fe, fe.h) + LUNGE[fe.h] + 12;
        if (incoming && !(cheat_on & CH_TURBO)) {
            int fc = frame_counter;
            if (diff_i == 0 ? (fc % 5 == 1 || fc % 5 == 3) : diff_i == 1 ? (fc % 4 == 1) : (fc % 8 == 1)) steps = 0;
        }
        if (scene == S_DUEL && !paused && !ko_side && !wipe_y) scene_dark = incoming ? 3 : 0;
        if (paused) { pause_update(); steps = 0; }
        else if (replay_t) steps = 0;
        else if (fighting && (cheat_on & CH_TURBO) && (frame_counter & 1)) steps = 2;
        else if (fighting && (cheat_on & CH_BULLET) && (frame_counter & 1)) steps = 0;
        if (scene == S_DUEL && (cheat_on & CH_KI)) fp.meter = 100;
        if (steps == 0) held_hits |= keys_hit;             /* presses during a slowed frame count on the next */
        else { keys_hit |= held_hits; held_hits = 0; }
        for (int step = 0; step < steps; step++) {
        if (step) keys_hit = 0;
        if (super_dark > 0) {
            super_dark--;
            update_fx();
            if (scene == S_DUEL) update_fighter(&fp, &fe);
        } else if (freeze > 0) {
            freeze--;
            if (flash_white > 0) flash_white--;
            if (darken_t > 0) darken_t--;
            update_fx();
        } else {
            if (darken_t > 0) darken_t--;
            if (scene == S_DUEL || scene == S_INTRO) {
                player_input(&fp);
                ai_update(&fe, &fp);
                if (!duel_on) clear_inputs(&fe);
                update_fighter(&fp, &fe);
                update_fighter(&fe, &fp);
                separate();
                if (fp.st == ST_GUARD && duel_on) ch_guarded = 1;
                if (scene == S_DUEL) rec_frame();
            } else if (scene == S_TITLE) {
                update_fighter(&fp, &fe);
            }
            update_fx();
            if (shake > 0) shake--;
        }
        }
        if (shock_t > 0 && !paused) shock_t--;
        if (!paused) { update_amb(); if (scene == S_DUEL || scene == S_INTRO) update_hazards(); }
        if (!paused && fighting && !freeze) { breathe(&fp, 0); breathe(&fe, 48); edge_fx(&fp); edge_fx(&fe); }
        build_bg3();
        if (paused) { update_camera(); build_hdma(); build_sprites(); continue; }
        if (banner_t > 0 && --banner_t == 0) text_clear_row(4);

        switch (scene) {
        case S_TITLE: scene_title(); break;
        case S_STORY: scene_story(); break;
        case S_HOWTO: scene_howto(); break;
        case S_INTRO: scene_intro(); break;
        case S_DUEL: scene_duel(); break;
        case S_RESULT: scene_result(); break;
        case S_CONTINUE: scene_continue(); break;
        case S_CHEATS: scene_cheats(); break;
        case S_CUT: scene_cut(); break;
        case S_STAIRS: scene_stairs(); break;
        case S_CREDITS: scene_credits(); break;
        }
        scene_t++;
        cine_update();
        brush_update();
        if ((scene == S_DUEL && !wipe_y && !cine_t) || (scene == S_INTRO && scene_t > 4)) draw_hud();
        fps_tick();

        update_camera();
        build_hdma();
        build_sprites();
        if ((keys & KEY_L) && (keys_hit & KEY_SELECT)) cpu_meter ^= 1;
        if (cpu_meter) {
            int lines = REG_VCOUNT;                      /* the frame's work began at line 160 */
            lines = lines >= 160 ? lines - 160 : lines + 68;
            int pct = lines * 100 / 228;
            if (pct > cpu_peak || --cpu_peak_t <= 0) { cpu_peak = pct; cpu_peak_t = 60; }
        }
    }
}
