"""Design proposals for Lieve (all keep red hair, white top, blue hakama)."""
import copy, json, os

BASE = dict(scale=1.16, torso=30, neck=5.0, head_r=6.4, uarm=16, farm=15, thigh=24.5, shin=24.5,
            blade=42, build=0.68, chest=0.4, figure=0.7, eyes=1, lips=True, headband=False,
            upright=1, chest_depth=0.8, vneck=True,
            bangs=False, sidelocks=False, obi=1, flare=1.0)
SKIN = [[150, 84, 64], [212, 144, 108], [248, 202, 162]]
TOP = [[126, 122, 136], [198, 194, 200], [248, 246, 240]]
STEEL = [[240, 244, 252], [150, 160, 180]]


def pal(hair, hakama, skin=SKIN, top=TOP):
    return [[0, 0, 0], [24, 14, 20]] + skin + hair + top + hakama + STEEL


DESIGNS = [
    ("LIEVE", dict(hair="braid", tail_len=9, bangs=True, obi=0, build=0.9, figure=0.5, flare=1.2,
                   palette=pal([[130, 24, 12], [214, 60, 26], [252, 130, 60]], [[26, 30, 62], [46, 56, 104], [82, 98, 156]]))),
    ("RONIN ROSE", dict(hair="highpony", tail_len=7.5, bangs=True, obi=1,
                        palette=pal([[120, 20, 16], [196, 44, 28], [244, 104, 60]], [[26, 30, 62], [46, 56, 104], [82, 98, 156]]))),
    ("RIVER SILK", dict(hair="flowing", tail_len=7.0, sidelocks=True, obi=2, flare=1.1,
                        palette=pal([[110, 34, 18], [186, 70, 34], [236, 128, 70]], [[24, 30, 70], [44, 60, 120], [86, 110, 176]]))),
    ("LANTERN BRAID", dict(hair="braid", tail_len=5.5, bangs=True, obi=1,
                           palette=pal([[130, 24, 12], [214, 60, 26], [252, 130, 60]], [[20, 32, 72], [40, 62, 128], [76, 108, 180]]))),
    ("STORM CRANE", dict(hair="halfup", tail_len=6.5, headband=True, sidelocks=True, obi=0,
                         palette=pal([[100, 18, 16], [170, 40, 30], [226, 86, 60]], [[30, 38, 66], [56, 70, 112], [96, 116, 160]]))),
    ("DUTCH HEART", dict(hair="lowpony", tail_len=7.5, bangs=True, sidelocks=True, obi=1,
                         palette=pal([[140, 44, 18], [224, 92, 40], [252, 156, 84]], [[30, 44, 88], [56, 84, 148], [104, 136, 200]],
                                     skin=[[168, 104, 84], [226, 164, 132], [252, 214, 184]]))),
]


def design_specs():
    out = []
    for name, d in DESIGNS:
        S = copy.deepcopy(BASE)
        S.update(copy.deepcopy(d))
        out.append((name, S))
    return out


if __name__ == "__main__":
    print(json.dumps({n: s for n, s in design_specs()}, indent=0)[:400])
