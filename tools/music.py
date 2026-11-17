"""Kōmō soundtrack: original pieces on Japanese scales for the GBA sound chip.

Each song has three voices, written one bar (16 sixteenth-note ticks) at a time:
  lead  square channel 2  - koto (plucked, decaying) or shakuhachi (held) tone
  bass  wave channel 3    - a soft shamisen/drone voice
  drum  noise channel 4   - K = taiko, S = rim/stick, H = both (one char per tick)
Notes are written NAME+OCTAVE.TICKS, e.g. "E5.4"; "r.4" is a rest.

Scales used:
  miyako-bushi (In) on E : E F A B C   - the classic "Japanese" sound
  hirajoshi on A         : A B C E F   - darker
  yo on D                : D E G A B   - folk, open, hopeful
"""
import math
import wave
import numpy as np

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def midi(tok):
    name, octv = tok[:-1], int(tok[-1])
    return 12 * (octv + 1) + NOTE[name]


def parse_voice(bars):
    ev = []
    for bar in bars:
        tot = 0
        for tok in bar.split():
            n, d = tok.split(".")
            d = int(d)
            ev.append((0 if n == "r" else midi(n), d))
            tot += d
        assert tot == 16, f"bar is {tot} ticks: {bar}"
    return ev


def parse_drums(bars):
    ev = []
    for bar in bars:
        assert len(bar) == 16, f"drum bar length {len(bar)}: {bar}"
        for ch in bar:
            code = {"-": 0, "K": 1, "S": 2, "H": 3}[ch]
            if code or not ev or ev[-1][1] >= 250:
                ev.append((code, 1))
            else:
                ev[-1] = (ev[-1][0], ev[-1][1] + 1)    # a hit lasts until the next event
    return ev


KOTO = dict(duty=2, vol=12, step=2)        # plucked: bright, fast decay
KOTO_SOFT = dict(duty=2, vol=9, step=3)
FLUTE = dict(duty=1, vol=9, step=0)        # shakuhachi: held, breathy narrow pulse
HARSH = dict(duty=1, vol=13, step=1)


SONGS = {}


def song(name, tempo, inst, lead, bass, drum, loop=True, bass_vol=2):
    lead_e, bass_e = parse_voice(lead), parse_voice(bass)
    drum_e = parse_drums(drum) if drum else [(0, 16 * len(lead))]
    n = 16 * len(lead)
    assert sum(d for _, d in bass_e) == n and sum(d for _, d in drum_e) == n, name
    SONGS[name] = dict(tempo=tempo, inst=inst, lead=lead_e, bass=bass_e, drum=drum_e, loop=loop, bass_vol=bass_vol, ticks=n)


# ---------------------------------------------------------------- title: "Kōmō" (koto, E miyako-bushi)
song("title", 9, KOTO, [
    "B4.4 C5.2 B4.2 A4.4 F4.4", "E4.6 F4.2 A4.8", "B4.2 C5.2 E5.4 C5.4 B4.2 C5.2", "A4.16",
    "B4.4 C5.2 B4.2 A4.4 F4.4", "E4.4 F4.4 A4.4 B4.4", "C5.4 B4.2 A4.2 F4.4 A4.2 F4.2", "E4.16",
    "E5.6 F5.2 E5.4 C5.4", "B4.6 C5.2 A4.8", "F4.2 A4.2 B4.4 C5.4 E5.4", "F5.12 E5.4",
    "C5.4 B4.2 C5.2 A4.4 B4.4", "F4.4 A4.4 B4.8", "C5.2 B4.2 A4.2 F4.2 E4.4 F4.4", "E4.16"],
    ["E2.8 E2.8", "F2.8 F2.8", "A2.8 A2.8", "E2.16", "E2.8 E2.8", "F2.16", "A2.16", "E2.16",
     "C3.8 C3.8", "F2.8 F2.8", "A2.8 A2.8", "B2.16", "A2.8 A2.8", "F2.16", "B2.8 A2.8", "E2.16"],
    ["K---------------", "----------------", "K-------S-------", "----------------"] * 4)

# ---------------------------------------------------------------- floor 1: "The Gate" (driving, E miyako-bushi)
song("floor1", 6, KOTO, [
    "E5.2 r.2 E5.2 F5.2 E5.4 C5.4", "B4.4 A4.4 B4.8", "E5.2 r.2 E5.2 F5.2 A5.4 F5.4", "E5.12 r.4",
    "C5.4 B4.2 C5.2 E5.4 F5.4", "E5.4 C5.4 B4.4 A4.4", "B4.2 C5.2 B4.2 A4.2 F4.4 A4.4", "E4.12 r.4",
    "A5.4 F5.4 E5.4 C5.4", "B4.2 C5.2 E5.4 F5.8", "E5.2 F5.2 E5.2 C5.2 B4.4 A4.4", "B4.12 r.4",
    "F5.4 E5.2 F5.2 A5.4 B5.4", "A5.4 F5.4 E5.8", "C5.2 B4.2 A4.2 B4.2 C5.4 B4.4", "E5.12 r.4"],
    ["E2.4 E2.4 E2.4 B1.4", "A1.4 A1.4 B1.4 B1.4", "E2.4 E2.4 E2.4 F2.4", "E2.8 B1.8",
     "A1.4 A1.4 C2.4 C2.4", "A1.4 A1.4 B1.4 B1.4", "F1.4 F1.4 A1.4 B1.4", "E2.8 E1.8",
     "F2.4 F2.4 F2.4 C2.4", "A1.4 A1.4 F1.4 F1.4", "C2.4 C2.4 A1.4 A1.4", "B1.8 E1.8",
     "F1.4 F1.4 A1.4 B1.4", "F1.4 F1.4 E1.4 E1.4", "A1.4 A1.4 F1.4 B1.4", "E2.8 B1.8"],
    (["K-----K-K---S---"] * 3 + ["K-----K-K-S-S-SS"] + ["K-----K-K---S---"] * 3 + ["K-K-K-K-KKS-S-SS"]) +
    (["K---S-K-K---S-S-"] * 3 + ["K-S-K-S-KKSSKSSS"] + ["K---S-K-K---S-S-"] * 3 + ["K-K-K-K-KKKKSSSS"]))

# ---------------------------------------------------------------- floor 2: "Bamboo Wind" (shakuhachi, D yo)
song("floor2", 7, FLUTE, [
    "A4.6 B4.2 A4.4 G4.4", "E4.8 D4.8", "G4.4 A4.4 B4.4 D5.4", "E5.12 D5.4",
    "D5.4 B4.2 A4.2 G4.4 A4.4", "B4.8 A4.8", "G4.4 E4.4 D4.4 E4.4", "D4.16",
    "B4.4 D5.4 E5.8", "D5.4 B4.4 A4.8", "G4.6 A4.2 B4.4 A4.4", "E4.16",
    "D5.4 E5.4 G5.8", "E5.4 D5.4 B4.8", "A4.4 B4.4 A4.4 G4.4", "A4.16"],
    ["D2.8 A2.8", "G1.8 D2.8", "E2.8 B1.8", "A1.8 E2.8", "G1.8 D2.8", "E2.8 A1.8", "G1.8 A1.8", "D2.16",
     "G1.8 D2.8", "B1.8 E2.8", "E2.8 G1.8", "A1.16", "G1.8 B1.8", "E2.8 A1.8", "D2.8 E2.8", "A1.16"],
    ["K-------S-------", "K-------S---S---"] * 4 + ["K---S---K---S---", "K---S---K-S-S---"] * 4)

# ---------------------------------------------------------------- floor 3: "Lanterns" (low and heavy, A hirajoshi)
song("floor3", 8, HARSH, [
    "A3.4 C4.4 B3.8", "A3.4 F3.4 E3.8", "A3.4 C4.4 E4.4 F4.4", "E4.16",
    "F4.4 E4.4 C4.4 B3.4", "C4.8 B3.8", "A3.4 B3.4 C4.4 B3.4", "A3.16",
    "E4.4 F4.4 E4.8", "C4.4 B3.4 A3.8", "F3.4 A3.4 B3.4 C4.4", "B3.16",
    "C4.4 E4.4 F4.8", "E4.4 C4.4 B3.8", "C4.4 B3.4 A3.4 F3.4", "E3.16"],
    ["A1.16", "F1.16", "A1.16", "E1.16", "F1.16", "E1.16", "F1.8 E1.8", "A1.16",
     "C2.16", "A1.16", "F1.16", "E1.16", "A1.16", "C2.16", "F1.16", "E1.16"],
    ["K-------K---K---"] * 7 + ["K---K---K-K-KKKK"] + ["K---S---K---K-S-"] * 7 + ["K-K-K-K-KKKKSSSS"], bass_vol=1)

# ---------------------------------------------------------------- floor 4: "Rain on the Chain" (fast koto runs, E miyako-bushi)
song("floor4", 5, KOTO, [
    "E5.1 F5.1 E5.1 C5.1 B4.2 A4.2 B4.2 C5.2 E5.4", "F5.2 E5.2 C5.2 B4.2 A4.4 F4.4",
    "E5.1 F5.1 E5.1 C5.1 B4.2 A4.2 B4.2 C5.2 F5.4", "E5.8 r.4 B4.2 C5.2",
    "E5.2 F5.2 A5.2 F5.2 E5.2 C5.2 B4.4", "C5.2 B4.2 A4.2 F4.2 E4.4 F4.4",
    "A4.2 B4.2 C5.2 E5.2 F5.2 E5.2 C5.2 B4.2", "A4.8 E4.8",
    "A5.2 F5.2 E5.2 C5.2 A5.2 F5.2 E5.2 C5.2", "B4.4 C5.4 E5.8", "F5.1 E5.1 C5.1 B4.1 A4.2 B4.2 C5.2 E5.2 F5.4", "E5.8 C5.4 B4.4",
    "A4.2 C5.2 E5.2 A5.2 B5.4 A5.4", "F5.2 E5.2 C5.2 B4.2 A4.4 B4.4", "C5.2 B4.2 A4.2 F4.2 A4.2 B4.2 C5.2 B4.2", "E5.8 B4.8"],
    ["E2.2 E2.2 B1.2 E2.2 E2.2 E2.2 B1.2 E2.2", "F2.2 F2.2 C2.2 F2.2 A1.2 A1.2 E1.2 A1.2",
     "E2.2 E2.2 B1.2 E2.2 E2.2 E2.2 B1.2 E2.2", "E2.2 E2.2 B1.2 E2.2 B1.2 B1.2 C2.2 C2.2",
     "A1.2 A1.2 E2.2 A1.2 C2.2 C2.2 B1.2 B1.2", "A1.2 A1.2 F1.2 F1.2 E1.2 E1.2 F1.2 F1.2",
     "A1.2 A1.2 C2.2 C2.2 F1.2 F1.2 B1.2 B1.2", "A1.2 A1.2 A1.2 A1.2 E1.2 E1.2 E1.2 E1.2",
     "F2.2 F2.2 F2.2 F2.2 C2.2 C2.2 C2.2 C2.2", "A1.2 A1.2 A1.2 A1.2 E1.2 E1.2 E1.2 E1.2", "F2.2 F2.2 F2.2 F2.2 A1.2 A1.2 A1.2 A1.2", "E2.2 E2.2 E2.2 E2.2 B1.2 B1.2 B1.2 B1.2", "A1.2 A1.2 A1.2 A1.2 C2.2 C2.2 C2.2 C2.2", "F1.2 F1.2 F1.2 F1.2 A1.2 A1.2 A1.2 A1.2", "F1.2 F1.2 F1.2 F1.2 B1.2 B1.2 B1.2 B1.2", "E2.2 E2.2 E2.2 E2.2 B1.2 B1.2 B1.2 B1.2"],
    ["K-S-K-S-K-SSK-S-"] * 7 + ["K-S-K-S-KKSSKSSS"] + ["K-SSK-S-K-SSK-SS"] * 7 + ["KKSSKKSSKKSSHHHH"])

# ---------------------------------------------------------------- floor 5: "Kurogane" (boss, A hirajoshi, war taiko)
song("floor5", 5, HARSH, [
    "A4.2 r.2 A4.2 r.2 A4.2 B4.2 C5.4", "B4.4 A4.4 F4.8", "A4.2 r.2 A4.2 r.2 A4.2 C5.2 E5.4", "F5.12 E5.4",
    "F5.2 E5.2 C5.2 B4.2 C5.4 E5.4", "F5.4 E5.4 C5.4 B4.4", "A4.2 B4.2 C5.2 B4.2 A4.4 F4.4", "E4.8 A4.8",
    "C5.4 B4.2 C5.2 E5.4 F5.4", "E5.8 C5.8", "B4.2 C5.2 B4.2 A4.2 F4.4 E4.4", "F4.12 r.4",
    "A5.2 r.2 A5.2 r.2 A5.2 F5.2 E5.4", "F5.4 E5.4 C5.4 B4.4", "C5.2 B4.2 A4.2 F4.2 E4.4 F4.4", "A4.16"],
    ["A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2", "F1.2 F1.2 F1.2 F1.2 F1.2 F1.2 F1.2 F1.2",
     "A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2", "F1.2 F1.2 F1.2 F1.2 E1.2 E1.2 E1.2 E1.2",
     "F1.2 F1.2 F1.2 F1.2 C2.2 C2.2 C2.2 C2.2", "F1.2 F1.2 E1.2 E1.2 C2.2 C2.2 B1.2 B1.2",
     "A1.2 A1.2 A1.2 A1.2 F1.2 F1.2 F1.2 F1.2", "E1.2 E1.2 E1.2 E1.2 A1.2 A1.2 A1.2 A1.2",
     "C2.2 C2.2 C2.2 C2.2 F1.2 F1.2 F1.2 F1.2", "C2.2 C2.2 C2.2 C2.2 A1.2 A1.2 A1.2 A1.2", "E1.2 E1.2 E1.2 E1.2 F1.2 F1.2 F1.2 F1.2", "F1.2 F1.2 F1.2 F1.2 F1.2 F1.2 F1.2 F1.2", "A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2", "F1.2 F1.2 F1.2 F1.2 C2.2 C2.2 C2.2 C2.2", "A1.2 A1.2 A1.2 A1.2 E1.2 E1.2 E1.2 E1.2", "A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2 A1.2"],
    ["K-K-S-K-K-K-S-KS"] * 3 + ["K-K-S-K-KKKKSSSS"] + ["H-K-S-K-H-K-S-KS"] * 3 + ["HKHKHKHKSSSSHHHH"] + ["K---K---K-K-S---"] * 3 + ["K-K-K-K-KKKKSSSS"] + ["H-K-S-K-H-K-S-KS"] * 3 + ["HKHKHKHKHKHKHHHH"])

# ---------------------------------------------------------------- the story scenes: "The Ledger" (slow, sad)
song("story", 10, KOTO_SOFT, [
    "B4.8 A4.4 F4.4", "E4.16", "F4.4 A4.4 B4.8", "C5.8 B4.8",
    "A4.8 F4.4 A4.4", "B4.16", "C5.4 B4.4 A4.4 F4.4", "E4.16"],
    ["E2.16", "C2.16", "F2.16", "A1.16", "F2.16", "E2.16", "A1.16", "E2.16"],
    ["K---------------"] + ["----------------"] * 7, bass_vol=1)

# ---------------------------------------------------------------- the stairs: "Ascent" (short, climbing)
song("stairs", 6, KOTO, [
    "E4.2 F4.2 A4.2 B4.2 C5.2 E5.2 F5.2 A5.2", "B5.8 A5.8", "E4.2 F4.2 A4.2 B4.2 C5.4 E5.4", "F5.16"],
    ["E2.16", "E2.16", "F2.16", "E2.16"],
    ["K---S---K---S---"] * 3 + ["K---S---K-K-KKKK"])

# ---------------------------------------------------------------- jingles
song("win", 6, KOTO, ["E5.2 A5.2 B5.2 E6.10", "r.16"], ["A2.16", "r.16"], ["K---K---K-K-K---", "----------------"], loop=False)
song("lose", 9, KOTO_SOFT, ["B4.4 A4.4 F4.4 E4.4", "E3.16"], ["E2.16", "E1.16"], ["K---------------", "K---------------"], loop=False)

# ---------------------------------------------------------------- ending: "Dawn over Dejima" (D yo, hopeful)
song("ending", 9, FLUTE, [
    "D5.4 E5.4 G5.4 A5.4", "B5.8 A5.8", "G5.4 A5.4 B5.4 D6.4", "E6.16",
    "D6.4 B5.4 A5.4 G5.4", "A5.8 G5.4 E5.4", "D5.4 E5.4 G5.4 E5.4", "D5.16"],
    ["G1.16", "D2.16", "E2.16", "A1.16", "B1.16", "D2.16", "G1.8 A1.8", "D2.16"],
    ["K-------S-------"] * 7 + ["K---S---K---K---"])

# ---------------------------------------------------------------- the dock: "Dejima Jig" (a Dutch sailors' tune, D major)
song("dock", 6, KOTO, [
    "D5.2 F#5.2 A5.2 F#5.2 D5.2 F#5.2 A5.4", "G5.2 B5.2 A5.2 F#5.2 E5.4 D5.4",
    "D5.2 F#5.2 A5.2 F#5.2 D5.2 F#5.2 B5.4", "A5.4 F#5.2 E5.2 D5.8",
    "E5.2 G5.2 B5.2 G5.2 E5.2 G5.2 B5.4", "A5.2 F#5.2 D5.2 F#5.2 A5.8",
    "G5.2 F#5.2 E5.2 D5.2 C#5.2 D5.2 E5.4", "D5.12 r.4"],
    ["D2.4 A1.4 D2.4 A1.4", "G1.4 D2.4 G1.4 D2.4", "D2.4 A1.4 D2.4 A1.4", "A1.4 E2.4 A1.4 E2.4",
     "E2.4 B1.4 E2.4 B1.4", "D2.4 A1.4 D2.4 A1.4", "A1.4 E2.4 A1.4 C#2.4", "D2.8 D1.8"],
    ["K---S---K---S-S-"] * 7 + ["K---S---K-K-S---"])

# ---------------------------------------------------------------- the spire: "Above the Storm" (Morsel, E miyako-bushi)
song("spire", 5, HARSH, [
    "E5.2 E5.2 F5.2 E5.2 C5.4 B4.4", "A4.2 B4.2 C5.2 E5.2 F5.8", "E5.2 E5.2 F5.2 A5.2 B5.4 A5.4", "F5.4 E5.4 C5.8",
    "B5.2 A5.2 F5.2 E5.2 F5.4 A5.4", "B5.8 C6.8", "A5.2 F5.2 E5.2 C5.2 B4.2 A4.2 F4.4", "E4.16"],
    ["E2.2 E2.2 B1.2 E2.2 E2.2 E2.2 B1.2 E2.2", "F2.2 F2.2 C2.2 F2.2 F2.2 F2.2 C2.2 F2.2",
     "E2.2 E2.2 B1.2 E2.2 E2.2 E2.2 B1.2 E2.2", "A1.2 A1.2 E2.2 A1.2 A1.2 A1.2 E2.2 A1.2",
     "F2.2 F2.2 C2.2 F2.2 F2.2 F2.2 C2.2 F2.2", "B1.2 B1.2 F2.2 B1.2 C2.2 C2.2 G2.2 C2.2",
     "A1.2 A1.2 E2.2 A1.2 F1.2 F1.2 C2.2 F1.2", "E2.2 E2.2 E2.2 E2.2 E1.2 E1.2 E1.2 E1.2"],
    ["K-K-S-K-K-K-S-KS"] * 7 + ["HKHKHKHKSSSSHHHH"])

ORDER = ["title", "floor1", "floor2", "floor3", "floor4", "floor5", "story", "stairs", "win", "lose", "ending", "dock", "spire"]


# ---------------------------------------------------------------- export to the game
def wave_reg(m):
    f = 440.0 * 2 ** ((m - 69) / 12.0)
    return max(0, min(2047, int(round(2048 - 65536 / f))))


WAVE = [int(round(7.5 + 7.4 * (0.75 * (1 - 4 * abs(i / 32 - 0.5)) * 2 - 0.75 + 0.25 * math.sin(2 * math.pi * i / 16)))) for i in range(32)]
WAVE = [max(0, min(15, w)) for w in WAVE]


def stream_bytes(ev, loop):
    out = []
    for n, d in ev:
        while d > 250:
            out += [n, 250]
            d -= 250
        out += [n, d]
    out.append(255 if loop else 254)
    return out


# ---------------------------------------------------------------- preview: a rough model of the GBA channels
SR = 22050


def render_song(name, loops=2):
    s = SONGS[name]
    fps = 59.73
    tick_s = s["tempo"] / fps
    total = s["ticks"] * tick_s * (loops if s["loop"] else 1) + (1.0 if not s["loop"] else 0)
    n = int(total * SR)
    out = np.zeros(n)
    inst = s["inst"]
    duty = {0: 0.125, 1: 0.25, 2: 0.5, 3: 0.75}[inst["duty"]]
    for rep in range(loops if s["loop"] else 1):
        base = rep * s["ticks"] * tick_s
        # lead: square with the GBA volume envelope (step n: -1/15 every n/64 s)
        t0 = base
        for m, d in s["lead"]:
            dur = d * tick_s
            if m:
                i0, i1 = int(t0 * SR), min(n, int((t0 + dur) * SR))
                tt = np.arange(i1 - i0) / SR
                f = 440 * 2 ** ((m - 69) / 12)
                ph = (tt * f) % 1.0
                sq = np.where(ph < duty, 1.0, -1.0)
                if inst["step"]:
                    vol = np.maximum(0, inst["vol"] - np.floor(tt / (inst["step"] / 64.0))) / 15.0
                else:
                    vol = np.full_like(tt, inst["vol"] / 15.0)
                out[i0:i1] += 0.22 * sq * vol
            t0 += dur
        # bass: the 32-step wave table
        t0 = base
        wv = (np.array(WAVE) - 7.5) / 7.5
        bv = {1: 1.0, 2: 0.5, 3: 0.25}[s["bass_vol"]]
        for m, d in s["bass"]:
            dur = d * tick_s
            if m:
                i0, i1 = int(t0 * SR), min(n, int((t0 + dur) * SR))
                tt = np.arange(i1 - i0) / SR
                f = 440 * 2 ** ((m - 69) / 12)
                idx = ((tt * f * 32).astype(int)) % 32
                env = np.minimum(1.0, (i1 - i0 - np.arange(i1 - i0)) / (0.01 * SR))
                out[i0:i1] += 0.22 * bv * wv[idx] * env
            t0 += dur
        # drums: filtered noise bursts
        t0 = base
        rng = np.random.default_rng(1)
        for code, d in s["drum"]:
            if code:
                for kind in ((1,) if code == 1 else (2,) if code == 2 else (1, 2)):
                    ln = 0.22 if kind == 1 else 0.06
                    i0, i1 = int(t0 * SR), min(n, int((t0 + ln) * SR))
                    tt = np.arange(i1 - i0) / SR
                    noise = rng.uniform(-1, 1, i1 - i0)
                    if kind == 1:   # low taiko: noise through a heavy lowpass plus a falling thump
                        k = np.ones(18) / 18
                        noise = np.convolve(noise, k, mode="same") * 2.2
                        noise += 0.8 * np.sin(2 * np.pi * (90 - 60 * tt / ln) * tt)
                        env = np.exp(-tt * 14)
                        out[i0:i1] += 0.30 * noise * env
                    else:
                        env = np.exp(-tt * 60)
                        out[i0:i1] += 0.12 * noise * env
            t0 += d * tick_s
    peak = np.max(np.abs(out)) or 1
    out = out / peak * 0.85
    return (out * 32767).astype(np.int16)


def write_wav(path, data):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


if __name__ == "__main__":
    import os, subprocess, sys
    out = sys.argv[1] if len(sys.argv) > 1 else "out/music"
    os.makedirs(out, exist_ok=True)
    titles = {"title": "01 Komo (title)", "floor1": "02 The Gate (floor 1)", "floor2": "03 Bamboo Wind (floor 2)",
              "floor3": "04 Lanterns (floor 3)", "floor4": "05 Rain on the Chain (floor 4)", "floor5": "06 Kurogane (the roof)",
              "story": "07 The Ledger (story scenes)", "stairs": "08 Ascent (stairs)", "win": "09 Victory",
              "lose": "10 Lieve Falls", "ending": "11 Dawn over Dejima (ending)"}
    for nm in ORDER:
        wav = os.path.join(out, titles[nm] + ".wav")
        write_wav(wav, render_song(nm))
        mp3 = wav[:-4] + ".mp3"
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-b:a", "128k", mp3], check=True)
        os.remove(wav)
        print(mp3, SONGS[nm]["ticks"] * SONGS[nm]["tempo"] / 59.73, "s per loop")
