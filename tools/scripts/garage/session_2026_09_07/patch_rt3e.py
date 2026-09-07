"""RT-3, part E: the plumbing -- the GI signal's history on the graph desc and
in both layers, and --gi-signal=on|off."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- the graph desc -------------------------------------------------------
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.h'
s = read(F)
if not has(s, 'GiLight'):
    s = rep(s,
        "\t\t// Null disables it, and then AO behaves exactly as it did.\n"
        "\t\tTemporalHistory* Occlusion = nullptr;\n",
        "\t\t// Null disables it, and then AO behaves exactly as it did.\n"
        "\t\tTemporalHistory* Occlusion = nullptr;\n"
        "\n"
        "\t\t// RT-3: the traced bounce's accumulation, when it runs as a signal of\n"
        "\t\t// this frame. Its own history and not `Indirect` above, deliberately:\n"
        "\t\t// that one is the one-frame-late buffer with two attachments and the\n"
        "\t\t// contract wants three, and keeping them apart is what lets\n"
        "\t\t// `--gi-signal=off` be a true reference arm rather than the same\n"
        "\t\t// storage in a different shape. Null disables the signal, and the\n"
        "\t\t// traced bounce goes back through gi_denoise and `Indirect`.\n"
        "\t\tTemporalHistory* GiLight = nullptr;\n")
    write(F, s)
    print('FrameGraphBuilder.h patched')
else:
    print('FrameGraphBuilder.h already has GiLight')

# --- the runtime layer ----------------------------------------------------
for h, c, member, field in (
        ('RageVRuntime/src/RuntimeLayer.h', 'RageVRuntime/src/RuntimeLayer.cpp',
         'm_GiLight', 'frame'),):
    s = read(h)
    if not has(s, member):
        s = rep(s,
            "\t// Ray-traced occlusion's accumulation. See FrameDesc::Occlusion.\n"
            "\tRageV::TemporalHistory m_Occlusion;\n",
            "\t// Ray-traced occlusion's accumulation. See FrameDesc::Occlusion.\n"
            "\tRageV::TemporalHistory m_Occlusion;\n"
            "\t// RT-3: the traced bounce's accumulation. See FrameDesc::GiLight.\n"
            "\tRageV::TemporalHistory m_GiLight;\n")
        write(h, s)
        print(h + ' patched')
    else:
        print(h + ' already has ' + member)
    s = read(c)
    if not has(s, member):
        s = rep(s,
            "\tframe.Occlusion = &m_Occlusion;\n",
            "\tframe.Occlusion = &m_Occlusion;\n"
            "\tframe.GiLight = &m_GiLight;\n")
        write(c, s)
        print(c + ' patched')
    else:
        print(c + ' already has ' + member)

# --- the editor layer -----------------------------------------------------
H = 'RageVEditor/src/EditorLayer.h'
s = read(H)
if not has(s, 'm_SceneGiLight'):
    s = rep(s,
        "\tRageV::TemporalHistory m_SceneOcclusion;\n"
        "\tRageV::TemporalHistory m_GameOcclusion;\n",
        "\tRageV::TemporalHistory m_SceneOcclusion;\n"
        "\tRageV::TemporalHistory m_GameOcclusion;\n"
        "\t// RT-3: the traced bounce's accumulation, one per view for the reason\n"
        "\t// every other history here is one per view -- the two cameras are not\n"
        "\t// consecutive frames of anything.\n"
        "\tRageV::TemporalHistory m_SceneGiLight;\n"
        "\tRageV::TemporalHistory m_GameGiLight;\n")
    write(H, s)
    print('EditorLayer.h patched')
else:
    print('EditorLayer.h already has m_SceneGiLight')

C = 'RageVEditor/src/EditorLayer.cpp'
s = read(C)
if not has(s, 'm_SceneGiLight'):
    s = rep(s, "\tscene.Occlusion = &m_SceneOcclusion;\n",
              "\tscene.Occlusion = &m_SceneOcclusion;\n"
              "\tscene.GiLight = &m_SceneGiLight;\n")
    s = rep(s, "\t\tgame.Occlusion = &m_GameOcclusion;\n",
              "\t\tgame.Occlusion = &m_GameOcclusion;\n"
              "\t\tgame.GiLight = &m_GameGiLight;\n")
    write(C, s)
    print('EditorLayer.cpp patched')
else:
    print('EditorLayer.cpp already has m_SceneGiLight')

# --- the flag -------------------------------------------------------------
H = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(H)
if not has(s, 'GiSignal'):
    s = rep(s, "\t\tbool AoSignal = true;\n",
              "\t\tbool AoSignal = true;\n"
              "\t\t// RT-3: --gi-signal=off puts the traced bounce back on the\n"
              "\t\t// one-frame-late buffer and gi_denoise, which is the reference arm.\n"
              "\t\tbool GiSignal = true;\n")
    write(H, s)
    print('EngineConfig.h patched')
else:
    print('EngineConfig.h already has GiSignal')

C = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(C)
if not has(s, '"gi-signal"'):
    s = rep(s,
        '\t\tif (key == "ao-signal")\n',
        '\t\tif (key == "gi-signal")\n'
        '\t\t{\n'
        '\t\t\tconfig.GiSignal = BoolValue(value, true);\n'
        '\t\t\treturn true;\n'
        '\t\t}\n'
        '\t\tif (key == "ao-signal")\n')
    write(C, s)
    print('EngineConfig.cpp patched')
else:
    print('EngineConfig.cpp already has gi-signal')
