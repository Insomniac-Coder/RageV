"""RT-8, first piece: the sea records the motion it has always computed.

The water vertex stage already evaluates the wave twice -- this frame's time and
last frame's -- and builds a previous clip position from the previous wave, with
a comment saying why. And then there is nowhere to put it: the water's surface
prepass writes the normal with roughness and wind, the colour with the specular
dial, and the world position with its mask, and no velocity; the transparent
variant it shares code with binds only two attachments and has none either. So
the sea's motion is computed every frame and read by nobody, which is why it
reads as standing still and why the long still feedback had to be switched off
for it.

The surface prepass is the water's own G-buffer layer -- a layer and not a lane,
because it is one sample where the scene may be four, and because the depth
under a wave is the seabed. So the velocity joins that layer, beside the mask
that says a wave was drawn.

Every anchor must match exactly once or nothing is written.
"""
import io, sys

T = '\t'


def patch(path, edits, marker=None):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker and marker in s:
        print('%s already patched, skipped' % path)
        return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
 (T*3 + 'surfaceDesc.Color = Format::R32G32B32A32_SFLOAT;\n'
  + T*3 + 'surfaceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT,\n'
  + T*4 + '\t\t\t\t\t\tFormat::R32G32B32A32_SFLOAT };',
  T*3 + 'surfaceDesc.Color = Format::R32G32B32A32_SFLOAT;\n'
  + T*3 + '// **RT-8: and the wave\'s own motion.** The vertex stage has always\n'
  + T*3 + '// evaluated the wave at last frame\'s time as well as this one\'s and\n'
  + T*3 + '// built a previous clip position from it -- and until now there was\n'
  + T*3 + '// no attachment to write the difference into, so every temporal\n'
  + T*3 + '// filter read the sea as standing still and the long still feedback\n'
  + T*3 + '// had to be turned off for it. xy the screen motion in the same\n'
  + T*3 + '// units the scene\'s velocity lane uses, z the mask again so a\n'
  + T*3 + '// reader needs one fetch rather than two, w spare. Half floats: a\n'
  + T*3 + '// motion vector is a fraction of the screen and a half resolves a\n'
  + T*3 + '// thousandth of it.\n'
  + T*3 + 'surfaceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT,\n'
  + T*4 + '\t\t\t\t\t\tFormat::R32G32B32A32_SFLOAT,\n'
  + T*4 + '\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT };'),
], 'RT-8')

patch(r'RageVEditor/assets/shaders/include/pbr_fragment.glsl', [
 ('layout(location = 2) out vec4 o_PositionWater;\n'
  '\n'
  '#elif defined(RV_TRANSPARENT)',
  'layout(location = 2) out vec4 o_PositionWater;\n'
  '// **RT-8: the wave\'s motion, which was computed and thrown away.**\n'
  '// `v_PrevClipPos` is built in water_vertex.glsl from the wave evaluated at\n'
  '// last frame\'s time -- not from the flat grid, deliberately -- and no\n'
  '// attachment existed to receive the difference, so the sea reported no\n'
  '// motion to any temporal filter and the still feedback had to be off for\n'
  '// it. xy in the scene velocity lane\'s own units and sign, z the mask, so\n'
  '// one fetch answers both "did the sea cover this pixel" and "where was it".\n'
  'layout(location = 3) out vec4 o_MotionWater;\n'
  '\n'
  '#elif defined(RV_TRANSPARENT)'),
], 'o_MotionWater')

print('done')
