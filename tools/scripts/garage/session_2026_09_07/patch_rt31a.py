"""RT-3.1, part A: Dispatch grows a third colour output, and the guidance
downsample is registered.

Three attachments because the contract reads three separate lanes by texel
(depth, the normal lane, velocity) and packing them would mean teaching the
shared accumulate shader where each one moved to. Dispatch already carries the
two-attachment machinery for the GI denoiser; this is the same shape once more.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Renderer/PostProcess.h'
C = 'RageV/src/RageV/Renderer/PostProcess.cpp'

s = read(H)
if not has(s, 'GuideDownsample'):
    s = rep(s,
        "\t\t\t// RT-3: the traced bounce brought up to the G-buffer's resolution,\n"
        "\t\t\t// joint-bilateral, so the contract can accumulate it at the size its\n"
        "\t\t\t// surface tests are honest at.\n"
        "\t\t\tGiUpsample,\n",
        "\t\t\t// RT-3: the traced bounce brought up to the G-buffer's resolution,\n"
        "\t\t\t// joint-bilateral. Since RT-3.1 it runs at the *end* of the chain,\n"
        "\t\t\t// after the contract has filtered the signal at its own resolution.\n"
        "\t\t\tGiUpsample,\n"
        "\t\t\t// RT-3.1: the G-buffer's depth, normal and velocity lanes downsampled\n"
        "\t\t\t// by selection to a signal's own grid, so the contract's surface tests\n"
        "\t\t\t// stay honest there.\n"
        "\t\t\tGuideDownsample,\n")
    s = rep(s,
        "\t\t// RT-3: the half-resolution traced bounce to full resolution, weighted\n"
        "\t\t// by each tap's agreement with this pixel's depth. `giWidth`/`giHeight`\n"
        "\t\t// are the *source* grid the four taps are walked on.\n"
        "\t\tstatic void GiUpsample(RHI::RHICommandList& cmd,\n",
        "\t\t// RT-3.1: the G-buffer's three guidance lanes onto a signal's own grid,\n"
        "\t\t// one whole texel each, never averaged. `divisor` is how many\n"
        "\t\t// full-resolution texels there are to a side of the target's.\n"
        "\t\tstatic void GuideDownsample(RHI::RHICommandList& cmd,\n"
        "\t\t\t\t\t\t\t\t\tconst RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\tconst RHI::Ref<RHI::RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\tconst RHI::Ref<RHI::RHITexture>& velocity,\n"
        "\t\t\t\t\t\t\t\t\tuint32_t divisor,\n"
        "\t\t\t\t\t\t\t\t\tRHI::Format depthFormat,\n"
        "\t\t\t\t\t\t\t\t\tRHI::Format surfaceFormat,\n"
        "\t\t\t\t\t\t\t\t\tRHI::Format velocityFormat);\n\n"
        "\t\t// RT-3: the half-resolution traced bounce to full resolution, weighted\n"
        "\t\t// by each tap's agreement with this pixel's depth. `giWidth`/`giHeight`\n"
        "\t\t// are the *source* grid the four taps are walked on.\n"
        "\t\tstatic void GiUpsample(RHI::RHICommandList& cmd,\n")
    # the third output format on Dispatch
    s = rep(s,
        "\t\t\t\t\t\t\t RHI::Format secondOutputFormat = RHI::Format::Undefined,\n",
        "\t\t\t\t\t\t\t RHI::Format secondOutputFormat = RHI::Format::Undefined,\n"
        "\t\t\t\t\t\t\t // RT-3.1: a third, for the guidance downsample's three lanes.\n"
        "\t\t\t\t\t\t\t RHI::Format thirdOutputFormat = RHI::Format::Undefined,\n")
    write(H, s)
    print('PostProcess.h patched')
else:
    print('PostProcess.h already has GuideDownsample')

s = read(C)
if not has(s, 'gbuffer_guide.rvshader'):
    s = rep(s,
        '\t\t\t\tcase 35: return "assets/shaders/gi_upsample.rvshader";\n',
        '\t\t\t\tcase 35: return "assets/shaders/gi_upsample.rvshader";\n'
        '\t\t\t\tcase 36: return "assets/shaders/gbuffer_guide.rvshader";\n')
    s = rep(s,
        "\t\t\tstd::array<Ref<RHIShader>, 36> Shaders;\n",
        "\t\t\tstd::array<Ref<RHIShader>, 37> Shaders;\n")
    s = rep(s,
        "\t\tstatic_assert((int)Shader::Count <= 36,\n",
        "\t\tstatic_assert((int)Shader::Count <= 37,\n")
    # Dispatch: the third format through the key and the pipeline
    s = rep(s,
        "\t\t\t\t\t\t\t   Format secondOutputFormat,\n",
        "\t\t\t\t\t\t\t   Format secondOutputFormat,\n"
        "\t\t\t\t\t\t\t   Format thirdOutputFormat,\n")
    s = rep(s,
        "\t\tconst auto key = std::make_tuple(index, outputFormat, secondOutputFormat);\n",
        "\t\tconst auto key = std::make_tuple(index, outputFormat, secondOutputFormat,\n"
        "\t\t\t\t\t\t\t\t\t\t thirdOutputFormat);\n")
    s = rep(s,
        "\t\t\tif (secondOutputFormat != Format::Undefined)\n"
        "\t\t\t\tdesc.ColorFormats.push_back(secondOutputFormat);\n",
        "\t\t\tif (secondOutputFormat != Format::Undefined)\n"
        "\t\t\t\tdesc.ColorFormats.push_back(secondOutputFormat);\n"
        "\t\t\t// RT-3.1: and a third, for the guidance downsample. Ordered, not\n"
        "\t\t\t// optional: a third without a second would build a pipeline whose\n"
        "\t\t\t// attachment one is the shader's two.\n"
        "\t\t\tif (thirdOutputFormat != Format::Undefined)\n"
        "\t\t\t\tdesc.ColorFormats.push_back(thirdOutputFormat);\n")
    # the entry point, beside GiUpsample's
    s = rep(s,
        "\t// RT-3: the joint bilateral upsample of the traced bounce. The same\n",
        "\t// RT-3.1: the guidance downsample. One dispatch, three attachments, each\n"
        "\t// a whole G-buffer texel -- the shader's own note has why selection and\n"
        "\t// not averaging, and which texel it selects.\n"
        "\tvoid PostProcess::GuideDownsample(RHICommandList& cmd, const Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& velocity,\n"
        "\t\t\t\t\t\t\t\t\t  uint32_t divisor, Format depthFormat,\n"
        "\t\t\t\t\t\t\t\t\t  Format surfaceFormat, Format velocityFormat)\n"
        "\t{\n"
        "\t\tif (!s_Data || !depth || !surface)\n"
        "\t\t\treturn;\n\n"
        "\t\tPostParams params;\n"
        "\t\tparams.A = (float)Math::Max(divisor, 1u);\n\n"
        "\t\t// Every lane point sampled: this pass exists to stop the hardware\n"
        "\t\t// averaging surfaces, and a linear filter on any of the three would\n"
        "\t\t// put back exactly what it is here to prevent.\n"
        "\t\tDispatch(cmd, Shader::GuideDownsample, depthFormat, depth, surface,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Point, Sampling::Point,\n"
        "\t\t\t\t velocity ? velocity : TextureLoader::TransparentBlack(*s_Data->Device),\n"
        "\t\t\t\t Sampling::Point, nullptr, Sampling::Point, nullptr, nullptr,\n"
        "\t\t\t\t surfaceFormat, nullptr, velocityFormat);\n"
        "\t}\n\n"
        "\t// RT-3: the joint bilateral upsample of the traced bounce. The same\n")
    write(C, s)
    print('PostProcess.cpp patched')
else:
    print('PostProcess.cpp already has gbuffer_guide')
