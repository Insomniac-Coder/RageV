"""Render the garage with every one of its meshes marked moving, then put
the scene back. One question: do the tubes appear in the floor's mirror
rays once no hit in the garage is a Static hit?

A reflection ray that lands on a Static surface takes that surface's light
from the irradiance field (pbr_fragment.glsl, the `surface.Static` branch of
the hit shading) instead of walking the lights; with the garage baked and
the floor still black, this separates "the static hit path blacks the
mirror" from "the mirror ray never reaches the tubes at all".

    python tools/scripts/garage/static_test.py [tag]
"""
import io, os, sys, shutil, subprocess

SP = r'C:\Users\ism19\Code\RageV\tools\scripts\garage'
SCENE = r'C:\Users\ism19\Code\RageV\SampleProject\assets\scenes\showroom.rage'
sys.path.insert(0, SP)
import compare

tag = sys.argv[1] if len(sys.argv) > 1 else 'engine_nonstatic'
bak = SCENE + '.static_test.bak'
shutil.copy(SCENE, bak)
try:
    d = io.open(SCENE, 'r', encoding='utf-8', newline='').read()
    n = d.count('Static: true')
    d = d.replace('Static: true', 'Static: false')
    io.open(SCENE, 'w', encoding='utf-8', newline='').write(d)
    print('%d meshes flipped to moving' % n)
    compare.compare(compare.render(tag))
finally:
    shutil.copy(bak, SCENE)
    os.remove(bak)
    print('scene restored')
