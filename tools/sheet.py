import sys, glob
from PIL import Image
files = sys.argv[3:]
cols = int(sys.argv[2])
ims = [Image.open(f) for f in files]
w, h = 240, 160
rows = (len(ims) + cols - 1) // cols
S = Image.new("RGB", (cols * (w + 4), rows * (h + 4)), (40, 40, 40))
for i, im in enumerate(ims):
    S.paste(im, ((i % cols) * (w + 4), (i // cols) * (h + 4)))
S.save(sys.argv[1])
