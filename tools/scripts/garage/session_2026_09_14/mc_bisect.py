# -*- coding: utf-8 -*-
"""Which of the measured change's shader edits moves the bridge with the check off?

Stages one shader as it was before the change into the runtime's copies, renders
the Deck camera with no flag, restores the staged copy from source (checked byte
for byte) and compares with today's frames (mc_bridge_base).

Usage: mc_bisect.py taa|acc|direct
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
import mc_measure as MC  # noqa: E402

CRLF, LF = '\r\n', '\n'


def head(name):
    return subprocess.run(['git', 'show', 'HEAD:RageVEditor/assets/shaders/' + name], cwd=stage_run.ROOT,
                          capture_output=True, check=True).stdout


def unedited_accumulate():
    """The working copy with this session's three edits taken back out."""
    raw = io.open(os.path.join(stage_run.SRC_DIR, stage_run.ACC), encoding='utf-8', newline='').read()
    t = raw.replace(CRLF, LF)
    cuts = [
        ('\t// **Measured change (docs/RT-MEASURED-CHANGE.md): x one when binding 11\n'
         '\t// holds this signal\'s change map.** Zero is every frame before it existed.\n'
         '\tvec4 Change;\n} u_Reflection;\n\n'
         '// The change map, on the 3x3 block grid of this signal\'s own target: r the\n'
         '// share of the diffuse light measured to have changed since last frame, g the\n'
         '// highlight\'s. Black and unread where `Change.x` is zero.\n'
         'layout(set = 3, binding = 11) uniform sampler2D u_Change;\n\n'
         '// The map at this texel, read filtered between the block centres -- a block\'s\n'
         '// texel sits at the middle of its three.\n'
         'vec2 MeasuredChangeAt(ivec2 texel)\n{\n'
         '\tconst vec2 blocks = vec2(textureSize(u_Change, 0));\n'
         '\treturn texture(u_Change, ((vec2(texel) + 0.5) / 3.0) / blocks).rg;\n}',
         '} u_Reflection;'),
    ]
    for old, new in cuts:
        if t.count(old) != 1:
            sys.exit('accumulate push/binding cut matched %d' % t.count(old))
        t = t.replace(old, new)
    start = t.index('\t\t\t// **Measured change: where a share of the light is known to have')
    end = t.index('\t\t\tkept = mix(held, fresh.rgb, 1.0 / frames);')
    t = t[:start] + t[end:]
    twin = ('\t\t\t\t// Measured change: the highlight by its own share.\n'
            '\t\t\t\tif (measured.y > 0.0)\n'
            '\t\t\t\t\tframes2 = max(min(frames2, 1.0 / measured.y), 1.0);\n')
    if t.count(twin) != 1:
        sys.exit('accumulate twin cut matched %d' % t.count(twin))
    t = t.replace(twin, '')
    return (t.replace(LF, CRLF) if CRLF in raw else t).encode('utf-8')


def main(which):
    name = {'taa': stage_run.TAA, 'acc': stage_run.ACC, 'direct': stage_run.DIRECT}[which]
    text = unedited_accumulate() if which == 'acc' else head(name)
    staged = os.path.join(stage_run.STAGED_DIR, name)
    try:
        io.open(staged, 'wb').write(text)
        MC.run('bridge', 'bis_' + which, [])
    finally:
        print('staged copies restored and identical to source:', stage_run.restore())
    MC.same('bridge', 'base', 'bis_' + which)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
