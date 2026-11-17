# Third-party notices

Kōmō's code, art, music, story and death poems are original. The following third-party components are used:

| Component | Used for | Licence |
|---|---|---|
| [Noto Serif CJK](https://github.com/notofonts/noto-cjk) | Kanji and brush calligraphy, rendered into the ROM's graphics at build time | SIL Open Font License 1.1 |
| [DejaVu Sans](https://dejavu-fonts.github.io/) | Labels on development sheets (`tools/design_sheet.py`; not in the ROM) | Bitstream Vera / DejaVu licence |
| [EmulatorJS](https://github.com/EmulatorJS/EmulatorJS) | The web player (`play/index.html`), loaded from the EmulatorJS CDN; not included in this repository | GNU GPL v3 |
| [mGBA](https://mgba.io/) (as an EmulatorJS core) | Emulating the GBA in the web player; loaded from the EmulatorJS CDN | Mozilla Public License 2.0 |

The fonts are not redistributed in this repository; the build reads them from the system (`fonts-noto-cjk`, `fonts-noto-cjk-extra`).

The GBA BIOS is not included and not needed for the web player.
