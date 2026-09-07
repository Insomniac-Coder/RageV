"""RT-3, part D: the traced bounce moves above the lit pass and goes through
the reconstruction contract.

Three edits to FrameGraphBuilder.cpp:
  1. the `giSignal` gate, beside RT-2's `aoSignal`;
  2. the trace + upsample + contract between the occlusion signal and the lit
     pass, and the lit pass told to read it;
  3. the old post-lit RT GI block runs only when the signal is off (the
     reference arm, and the screen-space forms' only path).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'

s = read(F)
if has(s, 'const bool giSignal'):
    print('FrameGraphBuilder.cpp already has the GI signal')
    raise SystemExit(0)

# --- 1. the gate, beside RT-2's ------------------------------------------
s = rep(s,
    "\t\tconst AoDetail aoSignalLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;\n"
    "\t\tconst bool aoSignal = gbufferPass && aoSignalLevel != AoDetail::Off && PostProcess::IsReady()\n"
    "\t\t\t\t\t\t   && config.AoSignal && desc.Occlusion != nullptr;\n",
    "\t\tconst AoDetail aoSignalLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;\n"
    "\t\tconst bool aoSignal = gbufferPass && aoSignalLevel != AoDetail::Off && PostProcess::IsReady()\n"
    "\t\t\t\t\t\t   && config.AoSignal && desc.Occlusion != nullptr;\n"
    "\t\t// RT-3: the traced bounce as a signal of *this* frame -- traced from the\n"
    "\t\t// G-buffer between it and the lit pass, upsampled to the lit pass's\n"
    "\t\t// resolution and settled on the same contract the direct light and the\n"
    "\t\t// occlusion take. The traced form only: the screen-space gather reads the\n"
    "\t\t// lit image, so it cannot run before the pass that makes it and stays on\n"
    "\t\t// the one-frame-late buffer of 7av. `--gi-signal=off` is the reference\n"
    "\t\t// arm and puts the traced form back on that buffer too.\n"
    "\t\tconst bool giSignal = gbufferPass && rayGi && wantIndirect && config.GiSignal\n"
    "\t\t\t\t\t\t   && PostProcess::IsReady() && desc.GiLight != nullptr\n"
    "\t\t\t\t\t\t   && Renderer3D::CanTraceGlobalIllumination();\n")

# --- 2. the chain, between the occlusion signal and the lit pass ----------
s = rep(s,
    "\t\tgraph.AddPass(\"Scene\",\n"
    "\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t{\n"
    "\t\t\t\tbuilder.WriteAttachments(sceneHDR,\n",
    "\t\t// --- RT-3: the bounce, traced and settled before the lighting ------\n"
    "\t\t//\n"
    "\t\t// The same three stages RT-2 gave the occlusion, for the same reason.\n"
    "\t\t// The trace runs at the quality dial's own resolution (half, at Medium\n"
    "\t\t// and below) because indirect light is the lowest-frequency thing in\n"
    "\t\t// the frame and was the only term that ever paid full rate for itself.\n"
    "\t\t// The upsample brings it to the lit pass's grid, where the contract's\n"
    "\t\t// surface tests are honest -- a half-resolution texel sits on the\n"
    "\t\t// corner of four full-resolution ones and 'the surface under this\n"
    "\t\t// texel' has four answers there. Then the contract, unchanged.\n"
    "\t\tRGResource giLit = kRGInvalid;        // what the lit pass reads\n"
    "\t\tRGResource currentGi = kRGInvalid;    // the accumulated signal, for the debug view\n"
    "\t\tif (giSignal)\n"
    "\t\t{\n"
    "\t\t\tconst uint32_t giDivisor = RayDetailDivisor(giDetail);\n"
    "\t\t\tconst uint32_t giTraceWidth = Math::Max(desc.Width / giDivisor, 1u);\n"
    "\t\t\tconst uint32_t giTraceHeight = Math::Max(desc.Height / giDivisor, 1u);\n"
    "\t\t\tRGTargetDesc giRawDesc;\n"
    "\t\t\tgiRawDesc.Name = \"GiRaw\";\n"
    "\t\t\tgiRawDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\tgiRawDesc.Depth = Format::Undefined;\n"
    "\t\t\tgiRawDesc.Scale = 1.0f / (float)giDivisor;\n"
    "\t\t\tconst RGResource giRaw = graph.CreateTarget(giRawDesc);\n"
    "\n"
    "\t\t\tRenderer3D::GiTraceView giView;\n"
    "\t\t\tgiView.NearClip = desc.NearClip;\n"
    "\t\t\tgiView.FarClip = desc.FarClip;\n"
    "\t\t\tgiView.InvProjection0 = desc.InvProjection0;\n"
    "\t\t\tgiView.InvProjection1 = desc.InvProjection1;\n"
    "\t\t\tgiView.View = desc.View;\n"
    "\n"
    "\t\t\t// Last frame's allocation, the way RT-2's occlusion reads it: the\n"
    "\t\t\t// allocator's own passes still run after TAA, so what is available\n"
    "\t\t\t// this early is the previous map.\n"
    "\t\t\tconst bool giBudget = budgetPrevious != kRGInvalid && budgetHasHistory;\n"
    "\t\t\tgraph.AddPass(\"GI trace\",\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(giRaw);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tif (giBudget)\n"
    "\t\t\t\t\t\tbuilder.Sample(budgetPrevious);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[sceneHDR, normalIndex, giView, giBudget, budgetPrevious,\n"
    "\t\t\t\t rays = RayDetailRays(giDetail)](RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tRayGpuScope rayTime(context.Cmd);\n"
    "\t\t\t\t\tRenderer3D::TraceGlobalIllumination(context.Cmd,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t   context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t   context.Color(sceneHDR, normalIndex),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t   giBudget ? context.Color(budgetPrevious) : nullptr,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t   Format::R16G16B16A16_SFLOAT,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t   giView, rays);\n"
    "\t\t\t\t});\n"
    "\n"
    "\t\t\tRGTargetDesc giDesc;\n"
    "\t\t\tgiDesc.Name = \"GiFresh\";\n"
    "\t\t\tgiDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\tgiDesc.Depth = Format::Undefined;\n"
    "\t\t\tgiDesc.Scale = (float)supersample;\n"
    "\t\t\tconst RGResource giFresh = graph.CreateTarget(giDesc);\n"
    "\t\t\tgraph.AddPass(\"GI upsample\",\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(giFresh);\n"
    "\t\t\t\t\tbuilder.Sample(giRaw);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[giRaw, sceneHDR, giTraceWidth, giTraceHeight,\n"
    "\t\t\t\t nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tPostProcess::GiUpsample(context.Cmd, context.Color(giRaw),\n"
    "\t\t\t\t\t\t\t\t\t\t\tcontext.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\tgiTraceWidth, giTraceHeight, nearZ, farZ,\n"
    "\t\t\t\t\t\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT);\n"
    "\t\t\t\t});\n"
    "\n"
    "\t\t\tgiLit = giFresh;\n"
    "\t\t\tTemporalHistory& gi = *desc.GiLight;\n"
    "\t\t\tgi.Prepare(Renderer::GetDevice(),\n"
    "\t\t\t\t\t   desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,\n"
    "\t\t\t\t\t   Format::R16G16B16A16_SFLOAT, \"GiSignal\",\n"
    "\t\t\t\t\t   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\tif (gi.Current() && gi.Previous())\n"
    "\t\t\t{\n"
    "\t\t\t\tconst RGResource previousGi = graph.Import(gi.Previous(), \"GiPrevious\");\n"
    "\t\t\t\tcurrentGi = graph.Import(gi.Current(), \"GiCurrent\");\n"
    "\t\t\t\tRGTargetDesc giBlurDesc = giDesc;\n"
    "\t\t\t\tgiBlurDesc.Name = \"GiBlurred\";\n"
    "\t\t\t\tstatic const SignalPassNames kGiPasses =\n"
    "\t\t\t\t\t{ \"GiAccumulate\", { \"GiBlur\", \"GiBlur2\", \"GiBlur4\" } };\n"
    "\t\t\t\tgiLit = addSignal(kGiPasses, Renderer3D::GiSignal(),\n"
    "\t\t\t\t\t\t\t\t  giFresh, currentGi, previousGi, gi.HasHistory(),\n"
    "\t\t\t\t\t\t\t\t  &gi.Motion(), giBlurDesc, false);\n"
    "\t\t\t\tgi.Advance();\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t\telse if (desc.GiLight)\n"
    "\t\t{\n"
    "\t\t\tdesc.GiLight->Invalidate();\n"
    "\t\t}\n"
    "\t\tgraph.AddPass(\"Scene\",\n"
    "\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t{\n"
    "\t\t\t\tbuilder.WriteAttachments(sceneHDR,\n")

# the lit pass declares and reads it
s = rep(s,
    "\t\t\t\tif (occlusionLit != kRGInvalid)\n"
    "\t\t\t\t\tbuilder.Sample(occlusionLit);\n"
    "\t\t\t},\n",
    "\t\t\t\tif (occlusionLit != kRGInvalid)\n"
    "\t\t\t\t\tbuilder.Sample(occlusionLit);\n"
    "\t\t\t\tif (giLit != kRGInvalid)\n"
    "\t\t\t\t\tbuilder.Sample(giLit);\n"
    "\t\t\t},\n")
s = rep(s,
    "\t\t\t\t\t\t\t  [drawLit = desc.DrawSceneLit, jitter, directLit, occlusionLit,\n"
    "\t\t\t\t\t\t\t   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)\n",
    "\t\t\t\t\t\t\t  [drawLit = desc.DrawSceneLit, jitter, directLit, occlusionLit, giLit,\n"
    "\t\t\t\t\t\t\t   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)\n")
s = rep(s,
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenOcclusion(\n"
    "\t\t\t\t\t\t\t\t\t  occlusionLit != kRGInvalid ? context.Color(occlusionLit) : nullptr);\n",
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenOcclusion(\n"
    "\t\t\t\t\t\t\t\t\t  occlusionLit != kRGInvalid ? context.Color(occlusionLit) : nullptr);\n"
    "\t\t\t\t\t\t\t\t  // RT-3: this frame's bounce, onto binding 16 in place of\n"
    "\t\t\t\t\t\t\t\t  // last frame's buffer. Null leaves that binding alone.\n"
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenIndirectSignal(\n"
    "\t\t\t\t\t\t\t\t\t  giLit != kRGInvalid ? context.Color(giLit) : nullptr);\n")
s = rep(s,
    "\t\t\t\t\t\t\t\t  Renderer3D::SetDirectLight(nullptr, nullptr);\n"
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenOcclusion(nullptr);\n",
    "\t\t\t\t\t\t\t\t  Renderer3D::SetDirectLight(nullptr, nullptr);\n"
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenOcclusion(nullptr);\n"
    "\t\t\t\t\t\t\t\t  Renderer3D::SetScreenIndirectSignal(nullptr);\n")

# --- 3. the old post-lit block runs only as the reference arm -------------
s = rep(s,
    "\t\telse if (wantIndirect && rayGi && currentIndirect != kRGInvalid\n"
    "\t\t\t\t && Renderer3D::CanTraceGlobalIllumination())\n",
    "\t\t// Only when the signal did not run: with `--gi-signal=on` the trace and\n"
    "\t\t// its accumulation happen above the lit pass and this whole chain --\n"
    "\t\t// the trace, gi_denoise, and the one frame of latency they carry -- is\n"
    "\t\t// what RT-3 replaced. Kept whole as the reference arm.\n"
    "\t\telse if (wantIndirect && rayGi && !giSignal && currentIndirect != kRGInvalid\n"
    "\t\t\t\t && Renderer3D::CanTraceGlobalIllumination())\n")

# the G-buffer pass tells the scene block which form is live, the way RT-2 does
s = rep(s,
    "\t\tauto drawGBuffer = [drawScene, directSignal, aoSignal](RGPassContext& context)\n"
    "\t\t{\n"
    "\t\t\tRenderer3D::SetGBufferPassActive(true);\n"
    "\t\t\t// Told before BeginScene fills the scene block, whose RayRates.w\n"
    "\t\t\t// bits 22 and 23 are the lit shader's switches.\n"
    "\t\t\tRenderer3D::SetDirectSignal(directSignal);\n"
    "\t\t\tRenderer3D::SetAoSignal(aoSignal);\n"
    "\t\t\tdrawScene(context);\n"
    "\t\t\tRenderer3D::SetDirectSignal(false);\n"
    "\t\t\tRenderer3D::SetAoSignal(false);\n"
    "\t\t\tRenderer3D::SetGBufferPassActive(false);\n"
    "\t\t};\n",
    "\t\tauto drawGBuffer = [drawScene, directSignal, aoSignal, giSignal](RGPassContext& context)\n"
    "\t\t{\n"
    "\t\t\tRenderer3D::SetGBufferPassActive(true);\n"
    "\t\t\t// Told before BeginScene fills the scene block, whose RayRates.w\n"
    "\t\t\t// bits 22, 23 and 24 are the lit shader's switches.\n"
    "\t\t\tRenderer3D::SetDirectSignal(directSignal);\n"
    "\t\t\tRenderer3D::SetAoSignal(aoSignal);\n"
    "\t\t\tRenderer3D::SetGiSignal(giSignal);\n"
    "\t\t\tdrawScene(context);\n"
    "\t\t\tRenderer3D::SetDirectSignal(false);\n"
    "\t\t\tRenderer3D::SetAoSignal(false);\n"
    "\t\t\tRenderer3D::SetGiSignal(false);\n"
    "\t\t\tRenderer3D::SetGBufferPassActive(false);\n"
    "\t\t};\n")

write(F, s)
print('FrameGraphBuilder.cpp patched')
