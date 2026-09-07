"""RT-3.1, part F: the normal term in the upsample comes back out (measured).

The shader itself was rewritten by hand; this reverts the plumbing that fed it
the two extra textures. Its record is in the shader's own header: 0.081 levels
mean to 0.079, p99 unchanged at 2.00, for 0.28 ms.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if has(s, 'guideSurface'):
    s = rep(s,
        "\t\t\t\t\t\t\t   // The G-buffer's normal lane at this pass's resolution,\n"
        "\t\t\t\t\t\t\t   // and the guidance's on the signal's -- the bilateral's\n"
        "\t\t\t\t\t\t\t   // second term, which is what tells a corner from a plane.\n"
        "\t\t\t\t\t\t\t   // Null on either leaves the test depth-only.\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& normal,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& guideSurface,\n",
        "")
    write(H, s)
    print('PostProcess.h reverted')
else:
    print('PostProcess.h already reverted')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
if has(s, 'const Ref<RHITexture>& guideSurface'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& normal,\n"
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& guideSurface,\n", "")
    s = rep(s,
        "\t\t// The signal linear -- the taps land on exact source texel centres, so\n"
        "\t\t// the filter hands each one back whole and the weighting is the\n"
        "\t\t// shader's own. Everything else point: a depth or a normal halfway\n"
        "\t\t// between two surfaces describes neither.\n"
        "\t\tDispatch(cmd, Shader::SignalUpsample, outputFormat, signal, depth,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Linear, Sampling::Point,\n"
        "\t\t\t\t normal ? normal : TextureLoader::TransparentBlack(*s_Data->Device),\n"
        "\t\t\t\t Sampling::Point,\n"
        "\t\t\t\t guideSurface ? guideSurface : TextureLoader::TransparentBlack(*s_Data->Device),\n"
        "\t\t\t\t Sampling::Point);\n",
        "\t\t// The signal linear -- the taps land on exact source texel centres, so\n"
        "\t\t// the filter hands each one back whole and the weighting is the\n"
        "\t\t// shader's own. The depth point: a depth halfway between two surfaces\n"
        "\t\t// is the depth of neither.\n"
        "\t\tDispatch(cmd, Shader::SignalUpsample, outputFormat, signal, depth,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Linear, Sampling::Point);\n")
    write(C, s)
    print('PostProcess.cpp reverted')
else:
    print('PostProcess.cpp already reverted')

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, '.Surface != kRGInvalid'):
    for guide, tag, w, h in (('giGuide', 'gi', 'giTraceWidth', 'giTraceHeight'),
                             ('aoGuide', 'ao', 'aoWidth', 'aoHeight')):
        s = rep(s,
            "\t\t\t\t\t\t\t\t\t\t\t   context.Color(sceneHDR, normalIndex),\n"
            "\t\t\t\t\t\t\t\t\t\t\t   " + guide + ".Surface != kRGInvalid\n"
            "\t\t\t\t\t\t\t\t\t\t\t\t   ? context.Color(" + guide + ".Surface, 1) : nullptr,\n"
            "\t\t\t\t\t\t\t\t\t\t\t   " + w + ", " + h + ", nearZ, farZ,\n",
            "\t\t\t\t\t\t\t\t\t\t\t   " + w + ", " + h + ", nearZ, farZ,\n")
        s = rep(s,
            "\t\t\t\t[" + tag + "Settled, sceneHDR, normalIndex, " + guide + ", " + w + ", " + h + ",\n",
            "\t\t\t\t[" + tag + "Settled, sceneHDR, " + w + ", " + h + ",\n")
        s = rep(s,
            "\t\t\t\t\tif (" + guide + ".Surface != kRGInvalid)\n"
            "\t\t\t\t\t\tbuilder.Sample(" + guide + ".Surface);\n", "")
    write(F, s)
    print('FrameGraphBuilder.cpp reverted')
else:
    print('FrameGraphBuilder.cpp already reverted')
