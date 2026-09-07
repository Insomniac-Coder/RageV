"""RT-3.1, part D: the upsample becomes signal-neutral, and the occlusion moves
to its own resolution.

The occlusion's raw buffer is `vec4(ao, linearDepth, 0, 0)` -- the compute pass
carries its own depth in G so the bilateral upsample has a tap depth. That
payload cannot go into the contract as it stands: the bound and the moments are
built from `Luma(rgb)`, and with a distance in metres in the green channel the
luma is almost entirely the depth. So the resolve that already strips it (white
scene, intensity folded in) simply moves *down* to the occlusion's own
resolution -- at 1:1 its bilateral weights collapse to a passthrough -- and the
contract sees a clean scalar, exactly as it does today. Order is preserved too:
the intensity curve is applied before accumulation, as in RT-2.
"""
import sys, os, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- the shader file becomes signal-neutral ------------------------------
OLD = 'RageVEditor/assets/shaders/gi_upsample.rvshader'
NEW = 'RageVEditor/assets/shaders/signal_upsample.rvshader'
if os.path.exists(OLD) and not os.path.exists(NEW):
    s = read(OLD)
    s = s.replace('// RT-3: the traced bounce, brought up to the G-buffer\'s resolution.',
                  '// RT-3.1: a reduced-resolution signal, brought up to the lit pass\'s grid.\n'
                  '//\n'
                  '// The last stage of any signal the contract filtered at its own\n'
                  '// resolution -- the traced bounce and the ambient occlusion both. It was\n'
                  '// RT-3\'s first stage before RT-3.1 moved the filtering down; the shader is\n'
                  '// unchanged, it just runs at the other end of the chain now.')
    s = s.replace('layout(set = 0, binding = 0) uniform sampler2D u_Gi;',
                  'layout(set = 0, binding = 0) uniform sampler2D u_Signal;')
    s = s.replace('texture(u_Gi,', 'texture(u_Signal,')
    s = s.replace('// The traced bounce at the trace\'s own resolution: RGB albedo-free\n'
                  '// irradiance, A the validity rtgi_trace wrote (1 where it ran, 1 with a zero\n'
                  '// bounce where the pixel had no surface).',
                  '// The signal at its own resolution, as the contract left it: RGB the value\n'
                  '// (albedo-free irradiance for the bounce, a scalar replicated for the\n'
                  '// occlusion), A the frames standing behind it -- zero where the accumulate\n'
                  '// found no surface, which this pass carries through as the validity the lit\n'
                  '// shader reads.')
    write(NEW, s)
    os.remove(OLD)
    print('shader renamed to signal_upsample.rvshader')
else:
    print('shader already renamed')

# --- PostProcess: the name, everywhere -----------------------------------
for p in ('RageV/src/RageV/Renderer/PostProcess.h', 'RageV/src/RageV/Renderer/PostProcess.cpp'):
    s = read(p)
    if 'GiUpsample' in s:
        s = s.replace('GiUpsample', 'SignalUpsample')
        s = s.replace('assets/shaders/gi_upsample.rvshader',
                      'assets/shaders/signal_upsample.rvshader')
        s = s.replace('// RT-3: the half-resolution traced bounce to full resolution, weighted\n'
                      '\t\t// by each tap\'s agreement with this pixel\'s depth. `giWidth`/`giHeight`\n'
                      '\t\t// are the *source* grid the four taps are walked on.',
                      '// RT-3.1: a signal at its own resolution up to the lit pass\'s, weighted\n'
                      '\t\t// by each tap\'s agreement with this pixel\'s depth. `srcWidth`/`srcHeight`\n'
                      '\t\t// are the *source* grid the four taps are walked on.')
        s = s.replace('uint32_t giWidth, uint32_t giHeight,', 'uint32_t srcWidth, uint32_t srcHeight,')
        s = s.replace('const Ref<RHITexture>& gi,', 'const Ref<RHITexture>& signal,')
        s = s.replace('if (!s_Data || !gi || !depth)', 'if (!s_Data || !signal || !depth)')
        s = s.replace('Math::Max(giWidth, 1u)', 'Math::Max(srcWidth, 1u)')
        s = s.replace('Math::Max(giHeight, 1u)', 'Math::Max(srcHeight, 1u)')
        s = s.replace('outputFormat, gi, depth,', 'outputFormat, signal, depth,')
        s = s.replace("// **The GI buffer's texel size, not the frame's**",
                      "// **The source's texel size, not the frame's**")
        write(p, s)
        print(p + ': renamed to SignalUpsample')
    else:
        print(p + ': already renamed')

# --- the frame graph: the GI call site's new name -------------------------
F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if 'PostProcess::GiUpsample' in s:
    s = s.replace('PostProcess::GiUpsample', 'PostProcess::SignalUpsample')
    write(F, s)
    print('FrameGraphBuilder.cpp: GI call site renamed')

# --- the occlusion, moved to its own resolution ---------------------------
s = read(F)
if has(s, 'OcclusionResolve'):
    print('FrameGraphBuilder.cpp already has the occlusion rework')
    raise SystemExit(0)

s = rep(s,
    "\t\t\t// Full resolution, depth-aware: the apply shader against a white scene\n"
    "\t\t\t// is exactly the upsample it did before multiplying, with the\n"
    "\t\t\t// intensity folded in, so the contract runs at the G-buffer's size\n"
    "\t\t\t// and the lit shader reads by texel.\n"
    "\t\t\tRGTargetDesc aoDesc;\n"
    "\t\t\taoDesc.Name = \"OcclusionFresh\";\n"
    "\t\t\taoDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\taoDesc.Depth = Format::Undefined;\n"
    "\t\t\taoDesc.Scale = (float)supersample;\n"
    "\t\t\tconst RGResource aoFresh = graph.CreateTarget(aoDesc);\n"
    "\t\t\tgraph.AddPass(\"OcclusionUpsample\",\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(aoFresh);\n"
    "\t\t\t\t\tbuilder.Sample(aoRaw);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[aoRaw, sceneHDR, aoWidth, aoHeight, aoIntensity,\n"
    "\t\t\t\t nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tPostProcess::SsaoApply(context.Cmd, TextureLoader::White(Renderer::GetDevice()),\n"
    "\t\t\t\t\t\t\t\t\t\t   context.Color(aoRaw), context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t   aoWidth, aoHeight, nearZ, farZ, aoIntensity,\n"
    "\t\t\t\t\t\t\t\t\t\t   Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\t\t});\n"
    "\t\t\tocclusionLit = aoFresh;\n"
    "\t\t\tTemporalHistory& occlusion = *desc.Occlusion;\n"
    "\t\t\tocclusion.Prepare(Renderer::GetDevice(),\n"
    "\t\t\t\t\t\t\t  desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,\n"
    "\t\t\t\t\t\t\t  Format::R16G16B16A16_SFLOAT, \"OcclusionSignal\",\n"
    "\t\t\t\t\t\t\t  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\tif (occlusion.Current() && occlusion.Previous())\n"
    "\t\t\t{\n"
    "\t\t\t\tconst RGResource previousOcclusion = graph.Import(occlusion.Previous(), \"OcclusionPrevious\");\n"
    "\t\t\t\tcurrentOcclusion = graph.Import(occlusion.Current(), \"OcclusionCurrent\");\n"
    "\t\t\t\tRGTargetDesc aoBlurDesc = aoDesc;\n"
    "\t\t\t\taoBlurDesc.Name = \"OcclusionBlurred\";\n"
    "\t\t\t\tstatic const SignalPassNames kOcclusionPasses =\n"
    "\t\t\t\t\t{ \"OcclusionAccumulate\", { \"OcclusionBlur\", \"OcclusionBlur2\", \"OcclusionBlur4\" } };\n"
    "\t\t\t\tocclusionLit = addSignal(kOcclusionPasses, Renderer3D::AoSignal(),\n"
    "\t\t\t\t\t\t\t\t\t\t aoFresh, currentOcclusion, previousOcclusion, occlusion.HasHistory(),\n"
    "\t\t\t\t\t\t\t\t\t\t &occlusion.Motion(), aoBlurDesc, false);\n"
    "\t\t\t\tocclusion.Advance();\n"
    "\t\t\t}\n"
    "\t\t}\n",
    "\t\t\t// **RT-3.1: the resolve at the occlusion's own resolution, not the\n"
    "\t\t\t// frame's.** Against a white scene the apply shader is a joint\n"
    "\t\t\t// bilateral resample with the intensity curve folded in; run at 1:1 the\n"
    "\t\t\t// resample collapses to a passthrough and what is left is the part the\n"
    "\t\t\t// contract needs -- the depth stripped out of the green channel, where\n"
    "\t\t\t// the compute pass keeps it for its own tap weights, and the scalar\n"
    "\t\t\t// replicated into RGB. Without that the contract's bound and moments,\n"
    "\t\t\t// which are built from Luma(rgb), would be almost entirely a distance in\n"
    "\t\t\t// metres. Doing it here also keeps RT-2's order: the intensity curve is\n"
    "\t\t\t// applied before accumulation, as it was when the look was accepted.\n"
    "\t\t\tconst SignalGuidance aoGuide = guidanceFor(aoDivisor);\n"
    "\t\t\tRGTargetDesc aoDesc = aoRawDesc;\n"
    "\t\t\taoDesc.Name = \"OcclusionFresh\";\n"
    "\t\t\tconst RGResource aoFresh = graph.CreateTarget(aoDesc);\n"
    "\t\t\tgraph.AddPass(\"OcclusionResolve\",\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(aoFresh);\n"
    "\t\t\t\t\tbuilder.Sample(aoRaw);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tif (aoGuide.Depth != kRGInvalid)\n"
    "\t\t\t\t\t\tbuilder.Sample(aoGuide.Depth);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[aoRaw, sceneHDR, aoWidth, aoHeight, aoIntensity, aoGuide,\n"
    "\t\t\t\t nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\t// The depth on this pass's own grid: the guidance lane where there\n"
    "\t\t\t\t\t// is one, and it holds clip depth exactly as the G-buffer wrote it,\n"
    "\t\t\t\t\t// which is what LinearDepth in the shader expects either way.\n"
    "\t\t\t\t\tPostProcess::SsaoApply(context.Cmd, TextureLoader::White(Renderer::GetDevice()),\n"
    "\t\t\t\t\t\t\t\t\t\t   context.Color(aoRaw),\n"
    "\t\t\t\t\t\t\t\t\t\t   aoGuide.Depth != kRGInvalid\n"
    "\t\t\t\t\t\t\t\t\t\t\t   ? context.Color(aoGuide.Depth)\n"
    "\t\t\t\t\t\t\t\t\t\t\t   : context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t   aoWidth, aoHeight, nearZ, farZ, aoIntensity,\n"
    "\t\t\t\t\t\t\t\t\t\t   Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\t\t});\n"
    "\t\t\tRGResource aoSettled = aoFresh;\n"
    "\t\t\tTemporalHistory& occlusion = *desc.Occlusion;\n"
    "\t\t\tocclusion.Prepare(Renderer::GetDevice(), aoWidth, aoHeight,\n"
    "\t\t\t\t\t\t\t  Format::R16G16B16A16_SFLOAT, \"OcclusionSignal\",\n"
    "\t\t\t\t\t\t\t  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\tif (occlusion.Current() && occlusion.Previous())\n"
    "\t\t\t{\n"
    "\t\t\t\tconst RGResource previousOcclusion = graph.Import(occlusion.Previous(), \"OcclusionPrevious\");\n"
    "\t\t\t\tcurrentOcclusion = graph.Import(occlusion.Current(), \"OcclusionCurrent\");\n"
    "\t\t\t\tRGTargetDesc aoBlurDesc = aoDesc;\n"
    "\t\t\t\taoBlurDesc.Name = \"OcclusionBlurred\";\n"
    "\t\t\t\tstatic const SignalPassNames kOcclusionPasses =\n"
    "\t\t\t\t\t{ \"OcclusionAccumulate\", { \"OcclusionBlur\", \"OcclusionBlur2\", \"OcclusionBlur4\" } };\n"
    "\t\t\t\taoSettled = addSignal(kOcclusionPasses, Renderer3D::AoSignal(),\n"
    "\t\t\t\t\t\t\t\t\t  aoFresh, currentOcclusion, previousOcclusion, occlusion.HasHistory(),\n"
    "\t\t\t\t\t\t\t\t\t  &occlusion.Motion(), aoBlurDesc, false, aoGuide);\n"
    "\t\t\t\tocclusion.Advance();\n"
    "\t\t\t}\n"
    "\n"
    "\t\t\t// And up to the lit pass's grid, once, at the end.\n"
    "\t\t\tRGTargetDesc aoFullDesc;\n"
    "\t\t\taoFullDesc.Name = \"OcclusionFull\";\n"
    "\t\t\taoFullDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\taoFullDesc.Depth = Format::Undefined;\n"
    "\t\t\taoFullDesc.Scale = (float)supersample;\n"
    "\t\t\tconst RGResource aoFull = graph.CreateTarget(aoFullDesc);\n"
    "\t\t\tgraph.AddPass(\"OcclusionUpsample\",\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(aoFull);\n"
    "\t\t\t\t\tbuilder.Sample(aoSettled);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[aoSettled, sceneHDR, aoWidth, aoHeight,\n"
    "\t\t\t\t nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tPostProcess::SignalUpsample(context.Cmd, context.Color(aoSettled),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   aoWidth, aoHeight, nearZ, farZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\t   Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\t\t});\n"
    "\t\t\tocclusionLit = aoFull;\n"
    "\t\t}\n")

write(F, s)
print('FrameGraphBuilder.cpp: the occlusion runs at its own resolution')
