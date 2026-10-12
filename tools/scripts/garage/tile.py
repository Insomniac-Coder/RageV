import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
from PIL import Image
import os, sys
S = (_RAGEV + r'\build\garage_shots')
names = sys.argv[1:]
ims = [Image.open(os.path.join(S, n + '.png')).convert('RGB') for n in names]
w, h = ims[0].size
out = Image.new('RGB', (w * len(ims), h))
for i, im in enumerate(ims):
    out.paste(im, (i * w, 0))
out.save(os.path.join(S, 'cam_options.png'))
print('wrote cam_options.png', out.size)
