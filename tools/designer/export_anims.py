"""Write tools/designer/anims.json from the game's animation list (tools/fighters.py).
Each master's intro and victory pose are stored per fighter under "by"."""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import fighters as F

SKIP = ("turn", "trophy", "headless", "bowf", "noto", "aim") + tuple(F.BARE)


def clean(p):
    return {k: (int(x) if k in ("behind", "trail") else float(x)) for k, x in p.items()}


def main():
    names = [S["name"] for S in F.load_chars()]
    per = {n: {a: ps for a, ps, _ in F.build_anims(dict(name=n))} for n in names}
    out = []
    for name, poses, loop in F.build_anims(None):
        if name in SKIP or name.startswith("death_"):
            continue
        a = dict(name=name, loop=bool(loop), motion=F.MOTION.get(name, 0.0), poses=[clean(p) for p in poses])
        by = {n: [clean(p) for p in per[n][name]] for n in names if per[n][name] != poses}
        if by:
            a["by"] = by
        if name in F.ONLY:
            a["only"] = [n for n in names if n in F.ONLY[name]]
        out.append(a)
    path = os.path.join(os.path.dirname(__file__), "anims.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(path, [a["name"] for a in out])


if __name__ == "__main__":
    main()
