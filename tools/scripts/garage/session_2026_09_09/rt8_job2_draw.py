# -*- coding: utf-8 -*-
"""RT-8 job 2: the water draw reads the reconstructed reflection.

Two consequences of putting the trace on the contract.

  * The ray distance the four taps weigh by is no longer in the picture's
    alpha -- the contract puts the frame count there and keeps the distance on
    its surface attachment. So the draw takes a second texture.
  * The four taps stop being the whole reconstruction and become what they
    should have been: the joint bilateral upsample at the end of one.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


patch(r'RageV/src/RageV/Core/EngineConfig.h', [
('\t\tbool  WaterContract = true;',
 '\t\tbool  WaterContract = true;\n'
 '\t\t// **--water-ray-contract=on|off (RT-8 job 2): whether the sea\'s traced\n'
 '\t\t// reflection is averaged over the frames behind it.**\n'
 '\t\t//\n'
 '\t\t// WR-16 S5 traces it at a fraction of the resolution and the water draw\n'
 '\t\t// reconstructs it with four taps weighted by ray distance -- a spatial\n'
 '\t\t// reconstruction of one frame\'s rays, with no temporal average anywhere\n'
 '\t\t// in the chain. On, the traced picture goes through the contract\'s\n'
 '\t\t// accumulate and blur at the trace\'s own resolution first, and the four\n'
 '\t\t// taps become the upsample at the end rather than the whole of it.\n'
 '\t\tbool  WaterRayContract = true;', 'WaterRayContract'),
])

patch(r'RageV/src/RageV/Core/EngineConfig.cpp', [
('\t\tif (key == "water-contract" || key == "watercontract")\n'
 '\t\t\treturn ParseBool(value, config.WaterContract);',
 '\t\tif (key == "water-contract" || key == "watercontract")\n'
 '\t\t\treturn ParseBool(value, config.WaterContract);\n'
 '\t\tif (key == "water-ray-contract" || key == "waterraycontract")\n'
 '\t\t\treturn ParseBool(value, config.WaterRayContract);', 'parse'),
])

# --- the renderer hands over both textures -------------------------------
patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
('\t\tstatic void SetWaterReflection(const RHI::Ref<RHI::RHITexture>& reflection);',
 '\t\t// **RT-8 job 2: and where its ray distances are.** The contract takes the\n'
 '\t\t// picture\'s alpha for its frame count, so the distance the water draw\'s\n'
 '\t\t// four taps weigh by moves to the accumulate\'s surface attachment. Null\n'
 '\t\t// means the picture still carries it, which is the un-accumulated path.\n'
 '\t\tstatic void SetWaterReflection(const RHI::Ref<RHI::RHITexture>& reflection,\n'
 '\t\t\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& distance = nullptr);', 'decl'),
])
