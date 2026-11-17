"""Render the Lieve design proposals as a comparison sheet."""
import sys, base64, io, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(__file__))
import fighters as F
import lieve_designs as MD

D = os.path.dirname(__file__)
bg = np.array(Image.open(io.BytesIO(base64.b64decode(open(os.path.join(D, 'designer', 'bg.b64')).read()))).convert('RGB'))
snap = lambda a: ((a >> 3) << 3) | ((a >> 3) >> 2)
starts, i = {}, 0
for n, p, l in F.build_anims():
    starts[n] = i
    i += len(p)


def comp(f, pal, crop, bgx=30):
    canvas = bg[0:160, bgx:bgx + 192].copy()
    m = f != 0
    canvas[m] = pal[f][m]
    x0, y0, x1, y1 = crop
    return canvas[y0:y1, x0:x1]


def up(a, k):
    return np.array(Image.fromarray(a).resize((a.shape[1] * k, a.shape[0] * k), Image.NEAREST))


cols = []
for name, S in MD.design_specs()[1:]:
    S = dict(S); S['name'] = 'lieve'; S['palette'] = [tuple(c) for c in S['palette']]
    fr, tb = F.render_char(S)
    pal = snap(np.array(S['palette'], np.uint8))
    idle = fr[starts['idle_mid'] + 1]
    walk = fr[starts['walk_mid'] + 3]
    strike = fr[starts['strike_high'] + 4]
    face = up(comp(idle, pal, (50, 36, 98, 84)), 3)          # 144x144 head and shoulders
    full = up(comp(idle, pal, (8, 4, 152, 160)), 1)          # 144x156
    act = up(comp(walk, pal, (8, 4, 152, 160)), 1)
    act2 = up(comp(strike, pal, (8, 4, 152, 160)), 1)
    body = np.concatenate([full, act], 1)
    body = np.array(Image.fromarray(body).resize((144, 78), Image.NEAREST)) if False else body
    cols.append((name, face, full, act, act2))

W = 4 + len(cols) * 296
sheet = Image.new('RGB', (W, 30 + 144 + 4 + 156 + 4 + 156 + 8), (28, 22, 20))
d = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
except Exception:
    font = None
for k, (name, face, full, act, act2) in enumerate(cols):
    x = 4 + k * 296
    d.text((x + 4, 7), f"{k + 1}. {name}", fill=(242, 194, 92), font=font)
    sheet.paste(Image.fromarray(face), (x + 76, 30))
    sheet.paste(Image.fromarray(full), (x, 178))
    sheet.paste(Image.fromarray(act), (x + 146, 178))
    sheet.paste(Image.fromarray(act2), (x + 73, 338))
sheet.save(sys.argv[1] if len(sys.argv) > 1 else 'out/preview/lieve_designs.png')
print(sheet.size)
