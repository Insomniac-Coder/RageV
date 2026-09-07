"""RT-6, part C: TemporalResolve takes the identity lanes, and the frame graph
keeps them."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- TemporalResolve's two extra images ----------------------------------
H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if not has(s, 'guidePrevious'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t\tMath::Vec2 jitter, float stillFeedback);\n",
        "\t\t\t\t\t\t\t\t\tMath::Vec2 jitter, float stillFeedback,\n"
        "\t\t\t\t\t\t\t\t\t // RT-6: the identity lanes, this frame's and last\n"
        "\t\t\t\t\t\t\t\t\t // frame's. Null on either leaves the resolve on the\n"
        "\t\t\t\t\t\t\t\t\t // colour box alone, which is what it had before.\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& guideCurrent = nullptr,\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& guidePrevious = nullptr);\n")
    write(H, s)
    print('PostProcess.h: TemporalResolve takes the lanes')
else:
    print('PostProcess.h already done')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
if not has(s, 'guidePrevious'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t  Math::Vec2 jitter, float stillFeedback)\n",
        "\t\t\t\t\t\t\t\t\t  Math::Vec2 jitter, float stillFeedback,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guideCurrent,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guidePrevious)\n")
    s = rep(s,
        "\t\t\t// The feedback for a pixel that did not move; zero for \"the same\".\n"
        "\t\t\tfloat StillFeedback = 0.0f;\n"
        "\t\t\tfloat Pad = 0.0f;\n"
        "\t\t};\n",
        "\t\t\t// The feedback for a pixel that did not move; zero for \"the same\".\n"
        "\t\t\tfloat StillFeedback = 0.0f;\n"
        "\t\t\t// RT-6: whether the geometric test may run this frame.\n"
        "\t\t\tfloat Geometry = 0.0f;\n"
        "\t\t};\n")
    s = rep(s,
        "\t\tfull.StillFeedback = Math::Clamp(stillFeedback, 0.0f, 0.98f);\n",
        "\t\tfull.StillFeedback = Math::Clamp(stillFeedback, 0.0f, 0.98f);\n"
        "\t\t// Both lanes, and a history to compare against: with either missing the\n"
        "\t\t// test would be comparing this frame to uninitialised memory, which\n"
        "\t\t// refuses every pixel and turns the resolve off without saying so.\n"
        "\t\tfull.Geometry = (guideCurrent && guidePrevious && hasHistory) ? 1.0f : 0.0f;\n")
    s = rep(s,
        "\t\t\t\t nullptr, nullptr, momentsFormat, Format::Undefined,\n"
        "\t\t\t\t // The validity lane's count (WR-16 S0): declared by the\n",
        "\t\t\t\t nullptr, nullptr, momentsFormat, Format::Undefined,\n"
        "\t\t\t\t // RT-6's two lanes ride at bindings 6 and 7, after the counters\n"
        "\t\t\t\t // below -- see the Dispatch declaration for why they start there.\n"
        "\t\t\t\t // The validity lane's count (WR-16 S0): declared by the\n")
    write(C, s)
    print('PostProcess.cpp: TemporalResolve wired')
else:
    print('PostProcess.cpp already done')

# --- the graph desc and both layers --------------------------------------
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.h'
s = read(F)
if not has(s, 'TaaGuide'):
    s = rep(s,
        "\t\tTemporalHistory* GiLight = nullptr;\n",
        "\t\tTemporalHistory* GiLight = nullptr;\n"
        "\n"
        "\t\t// RT-6: the G-buffer's depth, normal and object id, kept for the next\n"
        "\t\t// frame's temporal resolve to validate its history against. The\n"
        "\t\t// G-buffer itself is single-buffered and transient, so a copy is the\n"
        "\t\t// only way last frame's identity survives into this one. Null leaves\n"
        "\t\t// the resolve on the colour box alone.\n"
        "\t\tTemporalHistory* TaaGuide = nullptr;\n")
    write(F, s)
    print('FrameGraphBuilder.h: TaaGuide on the desc')
else:
    print('FrameGraphBuilder.h already done')

for h, c, member, field in (
        ('RageVRuntime/src/RuntimeLayer.h', 'RageVRuntime/src/RuntimeLayer.cpp', 'm_TaaGuide', 'frame'),):
    s = read(h)
    if not has(s, member):
        s = rep(s,
            "\t// RT-3: the traced bounce's accumulation. See FrameDesc::GiLight.\n"
            "\tRageV::TemporalHistory m_GiLight;\n",
            "\t// RT-3: the traced bounce's accumulation. See FrameDesc::GiLight.\n"
            "\tRageV::TemporalHistory m_GiLight;\n"
            "\t// RT-6: last frame's identity lanes. See FrameDesc::TaaGuide.\n"
            "\tRageV::TemporalHistory m_TaaGuide;\n")
        write(h, s)
        print(h + ' patched')
    s = read(c)
    if not has(s, member):
        s = rep(s, "\tframe.GiLight = &m_GiLight;\n",
                   "\tframe.GiLight = &m_GiLight;\n"
                   "\tframe.TaaGuide = &m_TaaGuide;\n")
        write(c, s)
        print(c + ' patched')

H2 = 'RageVEditor/src/EditorLayer.h'
s = read(H2)
if not has(s, 'm_SceneTaaGuide'):
    s = rep(s,
        "\tRageV::TemporalHistory m_SceneGiLight;\n"
        "\tRageV::TemporalHistory m_GameGiLight;\n",
        "\tRageV::TemporalHistory m_SceneGiLight;\n"
        "\tRageV::TemporalHistory m_GameGiLight;\n"
        "\t// RT-6: last frame's identity lanes, one per view -- the two cameras\n"
        "\t// are not consecutive frames of anything.\n"
        "\tRageV::TemporalHistory m_SceneTaaGuide;\n"
        "\tRageV::TemporalHistory m_GameTaaGuide;\n")
    write(H2, s)
    print('EditorLayer.h patched')

C2 = 'RageVEditor/src/EditorLayer.cpp'
s = read(C2)
if not has(s, 'm_SceneTaaGuide'):
    s = rep(s, "\tscene.GiLight = &m_SceneGiLight;\n",
               "\tscene.GiLight = &m_SceneGiLight;\n"
               "\tscene.TaaGuide = &m_SceneTaaGuide;\n")
    s = rep(s, "\t\tgame.GiLight = &m_GameGiLight;\n",
               "\t\tgame.GiLight = &m_GameGiLight;\n"
               "\t\tgame.TaaGuide = &m_GameTaaGuide;\n")
    write(C2, s)
    print('EditorLayer.cpp patched')

# --- the flag -------------------------------------------------------------
EH = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(EH)
if not has(s, 'TaaGeometry'):
    s = rep(s, "\t\tbool  GiSignal = true;\n",
               "\t\tbool  GiSignal = true;\n"
               "\t\t// RT-6: --taa-geometry=off puts the temporal resolve back on the\n"
               "\t\t// colour box alone, which is the reference arm.\n"
               "\t\tbool  TaaGeometry = true;\n")
    write(EH, s)
    print('EngineConfig.h patched')

EC = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(EC)
if not has(s, '"taa-geometry"'):
    s = rep(s,
        '\t\t// RT-3: the reference arm for the traced bounce as a signal.\n'
        '\t\tif (key == "gi-signal" || key == "gisignal")\n'
        '\t\t\treturn ParseBool(value, config.GiSignal);\n',
        '\t\t// RT-3: the reference arm for the traced bounce as a signal.\n'
        '\t\tif (key == "gi-signal" || key == "gisignal")\n'
        '\t\t\treturn ParseBool(value, config.GiSignal);\n'
        '\n'
        '\t\t// RT-6: the reference arm for the resolve\'s geometric test.\n'
        '\t\tif (key == "taa-geometry" || key == "taageometry")\n'
        '\t\t\treturn ParseBool(value, config.TaaGeometry);\n')
    write(EC, s)
    print('EngineConfig.cpp patched')
