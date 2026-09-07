"""RT-3, part H: the GI signal's debug views.

`--debug-view=gi-light` is the settled bounce as the lit shader reads it, and
`--debug-view=gi-refusal` is why each texel's history was refused, on the same
ramp the reflections and the direct light use. Without them the only way to ask
what the signal holds is to difference two finished frames, and this session
already spent a measurement on that: a probe writing a bright constant into
every texel moved the frame by exactly as much as the real signal did, because
neither was being read, and no comparison of final images could tell those
apart. RT-12 wants the full set; these two are what RT-3 could not be verified
without.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(H)
if not has(s, 'GiLight'):
    s = rep(s,
        "\t\t\t\t\t\t\t   DirectLight, DirectRefusal, Occlusion };\n",
        "\t\t\t\t\t\t\t   DirectLight, DirectRefusal, Occlusion,\n"
        "\t\t\t\t\t\t\t   // RT-3: the settled bounce, and its refusals.\n"
        "\t\t\t\t\t\t\t   GiLight, GiRefusal };\n")
    write(H, s)
    print('EngineConfig.h patched')
else:
    print('EngineConfig.h already has GiLight')

C = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(C)
if not has(s, '"gi-light"'):
    s = rep(s,
        '\t\t\t// RT-2: the accumulated occlusion signal, as the lit shader reads it.\n'
        '\t\t\telse if (lowered == "ao" || lowered == "occlusion")\n'
        '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::Occlusion;\n',
        '\t\t\t// RT-2: the accumulated occlusion signal, as the lit shader reads it.\n'
        '\t\t\telse if (lowered == "ao" || lowered == "occlusion")\n'
        '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::Occlusion;\n'
        '\t\t\t// RT-3: the settled bounce the lit shader adds -- albedo-free\n'
        '\t\t\t// irradiance, and dim, so the ramp is one rather than the direct\n'
        '\t\t\t// light\'s sixty-four; and why each texel\'s history was refused, on\n'
        '\t\t\t// the reflections\' ramp. `gi` is taken by the budget\'s importance\n'
        '\t\t\t// map, so these are named in full.\n'
        '\t\t\telse if (lowered == "gi-light" || lowered == "gilight")\n'
        '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::GiLight;\n'
        '\t\t\telse if (lowered == "gi-refusal" || lowered == "girefusal")\n'
        '\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::GiRefusal;\n')
    write(C, s)
    print('EngineConfig.cpp patched')
else:
    print('EngineConfig.cpp already has gi-light')

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if not has(s, 'DebugViewMode::GiLight'):
    s = rep(s,
        "\t\t\t\t\t\t\t  : view == EngineConfig::DebugViewMode::Occlusion ? 1.0f\n"
        "\t\t\t\t\t\t\t\t\t: 1.0f;\n",
        "\t\t\t\t\t\t\t  : view == EngineConfig::DebugViewMode::Occlusion ? 1.0f\n"
        "\t\t\t\t\t\t\t  // RT-3: the bounce is albedo-free irradiance and dim --\n"
        "\t\t\t\t\t\t\t  // one is the ramp that shows a room's indirect at all,\n"
        "\t\t\t\t\t\t\t  // where the direct light's sixty-four leaves it black.\n"
        "\t\t\t\t\t\t\t  : view == EngineConfig::DebugViewMode::GiLight ? 1.0f\n"
        "\t\t\t\t\t\t\t  : view == EngineConfig::DebugViewMode::GiRefusal ? 6.0f\n"
        "\t\t\t\t\t\t\t\t\t: 1.0f;\n")
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t\t : view == EngineConfig::DebugViewMode::Occlusion\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? currentOcclusion\n"
        "\t\t\t\t\t\t\t\t\t\t\t   : kRGInvalid;\n",
        "\t\t\t\t\t\t\t\t\t\t : view == EngineConfig::DebugViewMode::Occlusion\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? currentOcclusion\n"
        "\t\t\t\t\t\t\t\t\t\t : (view == EngineConfig::DebugViewMode::GiLight\n"
        "\t\t\t\t\t\t\t\t\t\t\t|| view == EngineConfig::DebugViewMode::GiRefusal)\n"
        "\t\t\t\t\t\t\t\t\t\t\t   ? currentGi\n"
        "\t\t\t\t\t\t\t\t\t\t\t   : kRGInvalid;\n")
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t\t : view == EngineConfig::DebugViewMode::DirectRefusal ? 2u\n"
        "\t\t\t\t\t\t\t\t\t\t : 0u;\n",
        "\t\t\t\t\t\t\t\t\t\t : view == EngineConfig::DebugViewMode::DirectRefusal ? 2u\n"
        "\t\t\t\t\t\t\t\t\t\t : view == EngineConfig::DebugViewMode::GiRefusal ? 2u\n"
        "\t\t\t\t\t\t\t\t\t\t : 0u;\n")
    write(F, s)
    print('FrameGraphBuilder.cpp patched')
else:
    print('FrameGraphBuilder.cpp already has the GI views')
