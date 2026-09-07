from PIL import Image
import os, sys
S = r'C:\Users\ism19\Code\RageV\build\garage_shots'
names = sys.argv[1:]
ims = [Image.open(os.path.join(S, n + '.png')).convert('RGB') for n in names]
w, h = ims[0].size
out = Image.new('RGB', (w * len(ims), h))
for i, im in enumerate(ims):
    out.paste(im, (i * w, 0))
out.save(os.path.join(S, 'cam_options.png'))
print('wrote cam_options.png', out.size)
