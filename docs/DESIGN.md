# Kōmō: design notes

> **Spoilers:** this document describes the whole story, every route and every ending.

**Kōmō** (紅毛, "red hair") was the Edo-period Japanese term for Dutch traders. It's a Game Boy Advance duelling game with the tower structure of a classic martial-arts film: one floor, one master, one duel. The fighters are big enough to fill the screen. It's a single-player campaign, built for the Game Boy Advance and the Analogue Pocket. Current version: **v5.5**.

## 1. Pitch

Nagasaki, 1651. A giant in black armour killed Sybrand Oosterling, the finest trader the Dutch company ever sent east. His daughter **Lieve**, red-haired and forbidden to set foot in Japan, follows the killer up a five-storey pagoda in which each floor is held by a master of a different fighting style. She starts as a rookie who has never held a sword. It's a story of revenge.

## 2. The campaign

```
Title → CONTINUE / NEW CLIMB / GAUNTLET / TIME ATTACK → difficulty (Rookie, Samurai, Bushido)
NEW CLIMB: story pages → prologue → How to Fight → THE PIER (the drunk sailor; tutorial on Rookie) → the pagoda
Each floor: pre-duel scene → bow (B) and the floor's challenge → master's intro → 始め → rounds → iai at match point
            → finish (hold A) or spare (B) → final-cut replay → results (honour, challenge) → post-duel scene → stairs
To the roof: a spared master returns: honour ≥ 6 he helps; honour ≤ 0 he ambushes her first
Roof: storm, ring-outs. Kill the giant → trophy and dawn → ending, or the dark ending at honour ≤ −3.
      Spare him → he confesses: honour ≥ 10 → THE SPIRE (Morsel) → ending; otherwise → ending
Lost match: black-and-white wipe → death poem over the still scene → continue
```

| Floor | Stage | Master | Weapon | Challenge |
|---|---|---|---|---|
| Prologue | The Pier | A drunk sailor | Cutlass | Finish him with a low cut |
| 1 | The Dojo | Kenji | Bokken | Win without guarding |
| 2 | The Bamboo Room | Hōzen | Bō staff | Leap over his sweep |
| 3 | The Lantern Hall | Gonzō | Two swords | Win without being hit |
| 4 | The Rain Gallery | Kagerō | Two knives | Win the iai draw |
| 5 | The Storm Roof | Kurogane | Nōdachi | Win without losing a round |
| Secret | The Spire | Adriaan Morsel | Rapier and flintlock | Evade his pistol shot |

### Honour
- +1 for returning the bow, −1 for not; +3 for sparing a beaten master; −2 for cutting the fallen, −4 for cutting one who yielded; +2 for each challenge met (once).
- **The returning rival:** on the way to the roof, the last master she spared returns. With honour 6 or more he tells her how the giant's armour fails (it no longer turns mid cuts, and she starts with full ki). With honour 0 or less he ambushes her in a one-round duel first.
- **Endings:** kill the giant (dawn); kill him with honour −3 or less (she puts on the black helmet; "Nagasaki has a new demon"); spare him (he testifies); spare him with honour 10 or more (the secret spire and Morsel, then two endings of its own).

## 3. Fighting

- **Three heights:** low, mid and high cuts, with matching guards (Down+B, B, Up+B). On Rookie a wrong-height guard still holds.
- **Parry:** a fresh guard press within 16, 11 or 7 frames of impact (Rookie, Samurai, Bushido) staggers the attacker and opens a counter-cut. A perfect parry (3 frames) of a high cut disarms; the weapon can be picked up with Down.
- **Evasion:** jump (low and mid cuts pass under; while cutting or guarding in the air only low cuts do); crouch (high cuts pass over, except overheads, counters and the Quick Draw); step to another of the seven planes with L/R; dodge (brief invulnerability); dash.
- **Moves can be cut short:** cut or guard out of a sidestep (the glide carries on), in the air (the leap carries on), out of a dodge or a dash; a cut thrown while walking forward steps in; the guard can edge in and out; the crouch can shuffle.
- **Sword binds:** if her cut lands while his is still swinging, the blades bind. Tap A to push against his weight; the loser is thrown open and the winner gets the counter-cut.
- **The iai:** at match point, both draw when a falling leaf lands in a red reticule. A perfect draw kills; otherwise whoever draws first lands one clean cut. A held or mashed A doesn't count.
- **Sparing:** at the end, time stops on the kneeling master. Let go of the buttons, then B spares him or A held finishes him.
- **Weapon weight:** hit-stop and screen shake depend on the weapon (the nōdachi stops longest and jolts hardest; knives barely pause).
- **Stage hazards:** bamboo stalks cut by missed blows, paper lamps that fall and burn, wet boards and a railing on the Rain Gallery, ring-outs on the pier, the roof and the spire.
- **The masters:** each has its own reach, timing, attack mix and special (Hōzen's sweep, Gonzō's barge and walking through cuts, Kagerō's jumps and feints, Kurogane's armour and overhead, Morsel's flintlock).

## 4. Presentation

- **Fighters** are rendered from a posed 2D skeleton (`tools/fighters.py`): shaded capsules with painted shading (three tones blended where they meet, contact shadows under nearer parts, edges in each colour's own dark tone), weighted motion in every move, a hakama hem that trails and swings, braid physics, clothing cuts and dismemberment.
- **Lighting:** each stage tints the fighters' lit tones (moonlight, bamboo daylight, lantern glow, storm blue); lightning throws black silhouettes and whitens the fighters after.
- **Kurosawa moments:** the killing blow drawn white on black with a smash cut, brush calligraphy, woodblock stills, a slow-motion replay of the final cut, and a black-and-white defeat that wipes down the screen into a death poem.
- **Portraits** in the cutscenes have four expressions each: calm, angry, hurt and respectful.
- **Music:** an original soundtrack on the GBA's sound channels, one theme per floor plus the pier, the spire, the stairs and the endings.

## 5. Engine notes

- **Sprites:** each fighter is a 192×160 canvas of nine 64×64 (and 64×32) blocks. Frames are stored as runs of non-empty 8×8 tiles and streamed into sprite memory as they're shown. Tiles 0–479 Lieve, 480–959 the opponent, 960–991 the slash or a prop (dropped weapon, bamboo top, iai reticule), 992–1023 effects.
- **Depth:** seven planes; farther planes are drawn smaller with affine sprites.
- **The per-scanline sprite budget:** the GBA draws at most 1,210 cycles of sprites per line (a normal sprite costs its width; an affine one 10 + 2× its box width). Close-ups are always exactly 2×, where the enlarged boxes tile without overlapping; in close-ups the fighters go first in sprite memory, and optional sprites (reflections, particles) are dropped when a line would overflow.
- **OBJ palettes:** 0 Lieve; 1 the opponent; 2 effects; 3 flash; 4–5 blood-stained copies; 6–7 ambient particles; 8 breath.
- **DMA:** 0 floor scroll; 1 the view outside; 2 the spotlight window; 3 copies. **Interrupts:** VBlank, plus VCount for the defeat wipe's palette split.
- **Saves (SRAM):** 0–11 best score and furthest floor; 12–25 best times; 26–31 the story so far (honour, spared masters, challenges, the rival), saved at the start of each floor.

## 6. Version history

- **v1–v3.4:** the first prototype; the arcade rework; depth lanes; seven planes; floors 2–5; cutscenes; dismemberment; soundtrack; braid physics; twin weapons; the Kurosawa defeat; height-matched blocking and timed parries.
- **v4.0–v4.2.1:** graphical flourishes (lightning silhouettes, living stages, hit feel, woodblock stills, portraits, ink transitions); the trophy finale; master intros; the killing-blow zoom; breath; the growing storm; ring-outs.
- **v5.0:** compact sprite storage; reflections, chiburi and nōtō, the spotlight, shockwaves, mosaic, clothing cuts, dawn; disarms, hazards, the iai, honour and sparing; the pier prologue, the secret spire; gauntlet and time attack; death poems.
- **v5.1:** L/R through the planes, Up jumps, Down crouches; a mash-proof spare and iai; the black-and-white defeat wipe.
- **v5.2:** character redesigns made in the designer.
- **v5.3:** fixes for sprites lost in close-ups and the defeat wipe's colours; the iai reticule; cuts and guards mid-move.
- **v5.4:** realistic motion for the walk and the cuts; painted shading.
- **v5.5:** weighted motion everywhere; hakama swing; stage light; sword binds; weapon weight; portrait expressions; the final-cut replay; the returning rival; challenges; the dark ending; saved story state.

## 7. Ideas for later

- Weighted motion and cloth for the bare-handed set and the gore deaths.
- Masters that read the player's habits; stamina.
- Sharper masters on floors 1–4 (they are easy for the test bot on Samurai).
- Drop the unused full-screen poem pictures from the ROM.
