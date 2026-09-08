"""Two debug views for the water layer, so this class of question is a flag.

Every time the sea has been in question this session the answer has come from a
hand-staged probe that had to be written, run and restored. The engine has
twenty-seven debug views and none of them looks at the water's own layer, which
is why. `--debug-view=water-motion` draws the wave's screen motion about grey
the way the reflection's does; `--debug-view=water-mask` draws where the layer
says a wave was written at all -- which is the first thing to ask when the sea
behaves as though it were not there.
"""
import io, sys

T = '\t'


def patch(path, edits, marker):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker in s:
        print('%s already patched, skipped' % path)
        return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


patch(r'RageV/src/RageV/Core/EngineConfig.h', [
 ('ReflectionDirection, ReflectionDirectionDelta };',
  'ReflectionDirection, ReflectionDirectionDelta,\n'
  + T*7 + '   // **RT-8: the water\'s own layer.** Its motion -- the wave\n'
  + T*7 + '   // evaluated at last frame\'s time as well as this one\'s -- and\n'
  + T*7 + '   // the mask that says a wave was written at this pixel at all.\n'
  + T*7 + '   // The sea is the one surface with a G-buffer of its own, and\n'
  + T*7 + '   // until these existed the only way to ask what was in it was\n'
  + T*7 + '   // to stage a probe by hand.\n'
  + T*7 + '   WaterMotion, WaterMask };'),
], 'WaterMotion')

patch(r'RageV/src/RageV/Core/EngineConfig.cpp', [
 ('\t\t\telse if (lowered == "reflection-motion" || lowered == "reflectionmotion")\n'
  '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionMotion;',
  '\t\t\telse if (lowered == "reflection-motion" || lowered == "reflectionmotion")\n'
  '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionMotion;\n'
  '\t\t\t// RT-8: the water layer. Grey is no motion; black in the mask view\n'
  '\t\t\t// means the layer holds no wave at that pixel, whatever the sea\n'
  '\t\t\t// looks like in the picture.\n'
  '\t\t\telse if (lowered == "water-motion" || lowered == "watermotion")\n'
  '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::WaterMotion;\n'
  '\t\t\telse if (lowered == "water-mask" || lowered == "watermask")\n'
  '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::WaterMask;'),
], 'WaterMotion')

patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
 ('\t\t\tcase EngineConfig::DebugViewMode::ReflectionMotion:',
  '\t\t\t// **RT-8: the water\'s own layer**, attachment 3 of the sea\'s surface\n'
  '\t\t\t// pass: xy the wave\'s screen motion, z the mask. Drawn about grey\n'
  '\t\t\t// like the reflection\'s motion, and at the same scale, so the two\n'
  '\t\t\t// read the same way. "No source" where the scene has no water at\n'
  '\t\t\t// all, which is a different statement from a black frame.\n'
  '\t\t\tcase EngineConfig::DebugViewMode::WaterMotion:\n'
  '\t\t\t\tspec.Aux = waterSurface; spec.Attachment = 3; spec.Display = 3;\n'
  '\t\t\t\tspec.Scale = 0.02f; spec.Name = "water-motion";\n'
  '\t\t\t\tspec.Missing = kMissingWater;\n'
  '\t\t\t\tbreak;\n'
  '\t\t\t// The mask alone, as a number: one where the layer holds a wave.\n'
  '\t\t\t// The first thing to ask when the sea behaves as though its layer\n'
  '\t\t\t// were empty -- as it did on 2026-09-08, when it was.\n'
  '\t\t\tcase EngineConfig::DebugViewMode::WaterMask:\n'
  '\t\t\t\tspec.Aux = waterSurface; spec.Attachment = 3; spec.Channel = 2;\n'
  '\t\t\t\tspec.Scale = 1.0f; spec.Name = "water-mask";\n'
  '\t\t\t\tspec.Missing = kMissingWater;\n'
  '\t\t\t\tbreak;\n'
  '\t\t\tcase EngineConfig::DebugViewMode::ReflectionMotion:'),
 ('\t\t\tstatic constexpr const char* kMissingTaa = "the temporal resolve runs under TAA only";',
  '\t\t\tstatic constexpr const char* kMissingTaa = "the temporal resolve runs under TAA only";\n'
  '\t\t\tstatic constexpr const char* kMissingWater = "this scene has no water";'),
], 'kMissingWater')

print('done')
