"""RT-3, part A: the GI upsample registered in PostProcess."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Renderer/PostProcess.h'
C = 'RageV/src/RageV/Renderer/PostProcess.cpp'

# --- the enum ------------------------------------------------------------
s = read(H)
if not has(s, 'GiUpsample'):
    s = rep(s,
        '\t\t\t// The traced reflection added after the temporal filter.\n'
        '\t\t\tReflectionComposite,\n',
        '\t\t\t// The traced reflection added after the temporal filter.\n'
        '\t\t\tReflectionComposite,\n'
        '\t\t\t// RT-3: the traced bounce brought up to the G-buffer\'s resolution,\n'
        '\t\t\t// joint-bilateral, so the contract can accumulate it at the size its\n'
        '\t\t\t// surface tests are honest at.\n'
        '\t\t\tGiUpsample,\n')
    # the declaration, beside SsaoApply's
    s = rep(s,
        '\t\tstatic void SsaoApply(RHI::RHICommandList& cmd,',
        '\t\t// RT-3: the half-resolution traced bounce to full resolution, weighted\n'
        '\t\t// by each tap\'s agreement with this pixel\'s depth. `giWidth`/`giHeight`\n'
        '\t\t// are the *source* grid the four taps are walked on.\n'
        '\t\tstatic void GiUpsample(RHI::RHICommandList& cmd,\n'
        '\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& gi,\n'
        '\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& depth,\n'
        '\t\t\t\t\t\t\t   uint32_t giWidth, uint32_t giHeight,\n'
        '\t\t\t\t\t\t\t   float nearClip, float farClip,\n'
        '\t\t\t\t\t\t\t   RHI::Format outputFormat);\n\n'
        '\t\tstatic void SsaoApply(RHI::RHICommandList& cmd,')
    write(H, s)
    print('PostProcess.h patched')
else:
    print('PostProcess.h already has GiUpsample')

# --- the path table and the entry point ----------------------------------
s = read(C)
if not has(s, 'gi_upsample.rvshader'):
    s = rep(s,
        '\t\t\t\tcase 34: return "assets/shaders/reflection_composite.rvshader";\n',
        '\t\t\t\tcase 34: return "assets/shaders/reflection_composite.rvshader";\n'
        '\t\t\t\tcase 35: return "assets/shaders/gi_upsample.rvshader";\n')
    s = rep(s,
        '\tvoid PostProcess::SsaoApply(RHICommandList& cmd, const Ref<RHITexture>& scene,\n',
        '\t// RT-3: the joint bilateral upsample of the traced bounce. The same\n'
        '\t// shape as SsaoApply\'s -- the four source texels around each pixel,\n'
        '\t// bilinear weights times a depth agreement -- with two differences: the\n'
        '\t// value carried is RGB irradiance rather than a scalar, and the taps\'\n'
        '\t// depths are fetched from the depth buffer rather than read out of the\n'
        '\t// source\'s spare channel, which this signal does not have.\n'
        '\tvoid PostProcess::GiUpsample(RHICommandList& cmd, const Ref<RHITexture>& gi,\n'
        '\t\t\t\t\t\t\t\t const Ref<RHITexture>& depth,\n'
        '\t\t\t\t\t\t\t\t uint32_t giWidth, uint32_t giHeight,\n'
        '\t\t\t\t\t\t\t\t float nearClip, float farClip, Format outputFormat)\n'
        '\t{\n'
        '\t\tif (!s_Data || !gi || !depth)\n'
        '\t\t\treturn;\n\n'
        '\t\tPostParams params;\n'
        '\t\tparams.A = 0.0f;\n'
        '\t\tparams.B = nearClip;\n'
        '\t\tparams.C = farClip;\n'
        '\t\t// **The GI buffer\'s texel size, not the frame\'s** -- the upsample walks\n'
        '\t\t// the grid it is reading, the way SsaoApply walks the occlusion\'s.\n'
        '\t\tparams.TexelSize = { 1.0f / (float)Math::Max(giWidth, 1u),\n'
        '\t\t\t\t\t\t\t 1.0f / (float)Math::Max(giHeight, 1u) };\n\n'
        '\t\t// The taps land on exact source texel centres, so the linear filter\n'
        '\t\t// hands each one back whole and the weighting is the shader\'s own.\n'
        '\t\t// The depth is point sampled: a depth halfway between two surfaces is\n'
        '\t\t// the depth of neither.\n'
        '\t\tDispatch(cmd, Shader::GiUpsample, outputFormat, gi, depth,\n'
        '\t\t\t\t &params, sizeof(params), Sampling::Linear, Sampling::Point);\n'
        '\t}\n\n'
        '\tvoid PostProcess::SsaoApply(RHICommandList& cmd, const Ref<RHITexture>& scene,\n')
    write(C, s)
    print('PostProcess.cpp patched')
else:
    print('PostProcess.cpp already has gi_upsample')
