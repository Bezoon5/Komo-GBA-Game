"""Build out/komo_designer.html from the template, renderer, poses and defaults."""
import json, os
D = os.path.join(os.path.dirname(__file__), "designer")
t = open(os.path.join(D, "template.html")).read()
t = t.replace("/*RENDER_JS*/", open(os.path.join(D, "render.js")).read())
t = t.replace("/*ANIMS*/", open(os.path.join(D, "anims.json")).read())
txt = open(os.path.join(D, "..", "characters.json")).read().strip()
if txt.startswith("KOMO CHARACTERS"):
    txt = txt.split("\n", 1)[1]
t = t.replace("/*DEFAULTS*/", json.dumps(json.loads(txt)))
import sys as _s
_s.path.insert(0, os.path.join(D, ".."))
import lieve_designs as _MD
t = t.replace("/*PRESETS*/", json.dumps([[n, s] for n, s in _MD.design_specs()[1:]]))
t = t.replace("/*BG*/", open(os.path.join(D, "bg.b64")).read().strip())
out = os.path.join(D, "..", "..", "out", "komo_designer.html")
open(out, "w").write(t)
print(out, len(t) // 1024, "KB")
