# Building and testing Kōmō

## Requirements

The build uses Clang and LLD to target the GBA's ARM7TDMI directly, and Python to generate every asset. On Ubuntu 24.04:

```sh
sudo apt install clang lld llvm python3 python3-numpy python3-pil \
                 fonts-noto-cjk fonts-noto-cjk-extra fonts-dejavu-core gcc
```

- `fonts-noto-cjk` and `fonts-noto-cjk-extra` provide the Noto Serif CJK weights used for the kanji and the brush calligraphy (the tools read them from `/usr/share/fonts/opentype/noto/`).
- `gcc` builds the desktop test harness.

Other systems work if you install the same tools and point the font paths in `tools/backgrounds.py` and `tools/brush.py` at your copies of Noto Serif CJK.

## Building

```sh
./build.sh
```

The steps:

1. `tools/make_assets.py` renders every fighter frame, stage, portrait, effect and song into `src/gen/assets.c` and `assets.h` (this takes about a minute).
2. Clang compiles `src/main.c`, `src/rt.c`, the assets and `src/crt0.s`; LLD links them with `src/gba.ld`.
3. `tools/fixheader.py` writes a valid GBA header and checksum.
4. GCC builds `out/host_test`, the desktop harness, with the address and undefined-behaviour sanitizers.

Output: `out/komo.gba`.

## The desktop harness

`host/host.c` includes the game's source and runs it without a screen, driven by a bot. It's how balance, story routes and graphics are checked.

```sh
gcc -O2 -w -Isrc host/host.c src/gen/assets.c -o out/host_fast
./out/host_fast FRAMES DUMP_EVERY BOT_MODE SEED DIFFICULTY
```

- `BOT_MODE`: 0 a decent player; 1 gauntlet; 2 time attack; 11 a scripted test of the controls.
- `DIFFICULTY`: 0 Rookie, 1 Samurai, 2 Bushido.
- `DUMPDIR=dir` with `DUMP_EVERY` saves frames as PPM images.

Useful environment variables:

| Variable | Effect |
|---|---|
| `FLOOR=n` | Where a story run starts: 1 the pier (then the first floor), 2–5 the pagoda's second floor up to the roof, 7 the spire |
| `LOG=file` | Write match results (used by the balance script) |
| `EVENTSHOT=1` | Save a screenshot at notable moments (iai, spare, binds, the replay, cutscenes...) |
| `BOTSPARE=1` | The bot spares every master |
| `FORCEHON=n` | Hold honour at n (to test the story routes) |
| `STORYLOG=1` | Print each scene and cutscene as the story moves on |
| `CYCSCAN=1` | Emulate the GBA's per-scanline sprite budget and report frames that would lose sprite lines |
| `NOLOCK`, `NOHEFT`, `NOREPLAY`, `NOIAI` | Switch a feature off for comparisons |

## The character designer

`play/designer.html` is a self-contained page for editing the fighters' proportions, clothes and colours. It renders with a JavaScript port of `tools/fighters.py` (`tools/designer/render.js`) that matches the game pixel for pixel.

- Rebuild it with `python3 tools/designer/export_anims.py && python3 tools/make_designer.py` (writes `out/komo_designer.html`).
- **Export** in the designer produces a `characters.json`. Replace `tools/characters.json` with it and run `./build.sh` to put the new designs in the game.

## Hardware notes

- The game streams each fighter's frame into sprite memory as runs of 8×8 tiles, so frames don't need to fit in video memory at once.
- Close-ups are always exactly 2× scale: the GBA draws at most 1,210 cycles of sprites per scanline, and at 2× the enlarged sprite boxes tile without overlapping.
- The black-and-white defeat wipe splits the palette mid-screen with a VCount interrupt (`src/crt0.s`).
