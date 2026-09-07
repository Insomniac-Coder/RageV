"""RT-6, part A: Dispatch grows two more texture slots, and the TAA guide pass.

The resolve already binds four images -- the frame, the history, the velocity
and the moments -- and the geometric test needs two more: the identity lanes as
they are now, and as they were last frame. Bindings 6 and 7, since 4 is the
acceleration structure and 5 the counters.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Renderer/PostProcess.h'
C = 'RageV/src/RageV/Renderer/PostProcess.cpp'

s = read(H)
if not has(s, 'TaaGuide'):
    s = rep(s,
        "\t\t\t// RT-3.1: the G-buffer's depth, normal and velocity lanes downsampled\n"
        "\t\t\t// by selection to a signal's own grid, so the contract's surface tests\n"
        "\t\t\t// stay honest there.\n"
        "\t\t\tGuideDownsample,\n",
        "\t\t\t// RT-3.1: the G-buffer's depth, normal and velocity lanes downsampled\n"
        "\t\t\t// by selection to a signal's own grid, so the contract's surface tests\n"
        "\t\t\t// stay honest there.\n"
        "\t\t\tGuideDownsample,\n"
        "\t\t\t// RT-6: the G-buffer's depth, normal and id packed into one lane and\n"
        "\t\t\t// kept, so next frame's temporal resolve can ask whether the history\n"
        "\t\t\t// it reprojected to is the same surface.\n"
        "\t\t\tTaaGuide,\n")
    s = rep(s,
        "\t\t// RT-3.1: the G-buffer's three guidance lanes onto a signal's own grid,\n",
        "\t\t// RT-6: the identity lanes packed into one RGBA32F for next frame's\n"
        "\t\t// resolve -- clip depth, the octahedral normal, the signed object id.\n"
        "\t\tstatic void TaaGuide(RHI::RHICommandList& cmd,\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& surfaceId,\n"
        "\t\t\t\t\t\t\t RHI::Format outputFormat);\n\n"
        "\t\t// RT-3.1: the G-buffer's three guidance lanes onto a signal's own grid,\n")
    # two more texture slots on Dispatch, appended last
    s = rep(s,
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHIBuffer>& counters = nullptr);\n",
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHIBuffer>& counters = nullptr,\n"
        "\t\t\t\t\t\t\t // **RT-6: bindings 6 and 7.** Four is the acceleration\n"
        "\t\t\t\t\t\t\t // structure and five the counters, so the fifth and sixth\n"
        "\t\t\t\t\t\t\t // images a pass may want start at six. Only the temporal\n"
        "\t\t\t\t\t\t\t // resolve declares them: the identity lanes now and as they\n"
        "\t\t\t\t\t\t\t // were last frame.\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& fifth = nullptr,\n"
        "\t\t\t\t\t\t\t Sampling fifthSampling = Sampling::Point,\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& sixth = nullptr,\n"
        "\t\t\t\t\t\t\t Sampling sixthSampling = Sampling::Point);\n")
    write(H, s)
    print('PostProcess.h patched')
else:
    print('PostProcess.h already has TaaGuide')

s = read(C)
if not has(s, 'taa_guide.rvshader'):
    s = rep(s,
        '\t\t\t\tcase 36: return "assets/shaders/gbuffer_guide.rvshader";\n',
        '\t\t\t\tcase 36: return "assets/shaders/gbuffer_guide.rvshader";\n'
        '\t\t\t\tcase 37: return "assets/shaders/taa_guide.rvshader";\n')
    s = rep(s, "\t\t\tstd::array<Ref<RHIShader>, 37> Shaders;\n",
               "\t\t\tstd::array<Ref<RHIShader>, 38> Shaders;\n")
    s = rep(s, "\t\tstatic_assert((int)Shader::Count <= 37,\n",
               "\t\tstatic_assert((int)Shader::Count <= 38,\n")
    # the two slots through Dispatch
    s = rep(s,
        "\t\t\t\t\t\t\t   const Ref<RHIBuffer>& counters)\n",
        "\t\t\t\t\t\t\t   const Ref<RHIBuffer>& counters,\n"
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& fifth, Sampling fifthSampling,\n"
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& sixth, Sampling sixthSampling)\n")
    s = rep(s,
        "\t\t// The frame's acceleration structure, for the one pass that traces.\n"
        "\t\tif (structure)\n"
        "\t\t\tset->SetAccelerationStructure(4, structure);\n",
        "\t\t// The frame's acceleration structure, for the one pass that traces.\n"
        "\t\tif (structure)\n"
        "\t\t\tset->SetAccelerationStructure(4, structure);\n"
        "\n"
        "\t\t// RT-6: bindings 6 and 7, past the structure and the counters. The\n"
        "\t\t// temporal resolve's identity lanes -- this frame's and last frame's.\n"
        "\t\tif (fifth)\n"
        "\t\t\tset->SetTexture(6, fifth, samplerFor(fifthSampling));\n"
        "\t\tif (sixth)\n"
        "\t\t\tset->SetTexture(7, sixth, samplerFor(sixthSampling));\n")
    # the entry point
    s = rep(s,
        "\t// RT-3.1: the guidance downsample. One dispatch, three attachments, each\n",
        "\t// RT-6: the identity lanes, packed and kept. Straight after the G-buffer\n"
        "\t// pass, so what it keeps is the opaque surface the velocity lane also\n"
        "\t// describes -- see the shader's own note on why that matters for the sea.\n"
        "\tvoid PostProcess::TaaGuide(RHICommandList& cmd, const Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& surfaceId, Format outputFormat)\n"
        "\t{\n"
        "\t\tif (!s_Data || !depth || !surface || !surfaceId)\n"
        "\t\t\treturn;\n\n"
        "\t\tPostParams params;\n"
        "\t\t// Every lane point sampled: this is a copy, and an id blended between\n"
        "\t\t// two objects names neither of them.\n"
        "\t\tDispatch(cmd, Shader::TaaGuide, outputFormat, depth, surface,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Point, Sampling::Point,\n"
        "\t\t\t\t surfaceId, Sampling::Point);\n"
        "\t}\n\n"
        "\t// RT-3.1: the guidance downsample. One dispatch, three attachments, each\n")
    write(C, s)
    print('PostProcess.cpp patched')
else:
    print('PostProcess.cpp already has taa_guide')
