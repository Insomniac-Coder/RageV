"""RT-6, part D: the frame graph keeps the identity lanes and hands them to the
temporal resolve.

The write goes straight after the G-buffer pass -- not after the lit pass and
not after the water -- so what is kept is the opaque surface, which is the same
surface the velocity lane describes. A pixel of sea therefore keeps the opaque
behind it in both, and the resolve's two inputs agree about what they are
talking about. Making the sea itself answer is RT-8's.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'taaGuideCurrent'):
    print('FrameGraphBuilder.cpp already has the TAA guide')
    raise SystemExit(0)

# --- the pass, right after the guidance downsample helper ----------------
s = rep(s,
    "\t\t// The depth-to-view reconstruction every screen-space pass takes; above\n"
    "\t\t// the G-buffer pass since RT-2, because the occlusion signal runs there.\n",
    "\t\t// **RT-6: the identity lanes, kept for next frame's temporal resolve.**\n"
    "\t\t// Declared here and filled by a pass added after the G-buffer below; the\n"
    "\t\t// resolve reads `Previous` and, for this frame's side of the comparison,\n"
    "\t\t// `Current` -- so the pass has to have run by then, which it has, being\n"
    "\t\t// hundreds of lines earlier in the graph.\n"
    "\t\tRGResource taaGuideCurrent = kRGInvalid;\n"
    "\t\tRGResource taaGuidePrevious = kRGInvalid;\n"
    "\t\tbool taaGuideHasHistory = false;\n"
    "\n"
    "\t\t// The depth-to-view reconstruction every screen-space pass takes; above\n"
    "\t\t// the G-buffer pass since RT-2, because the occlusion signal runs there.\n")

# --- fill it after the G-buffer pass -------------------------------------
s = rep(s,
    "\t\tRGResource directTraced = kRGInvalid;\n",
    "\t\t// **RT-6: written here, straight after the G-buffer.** Everything the\n"
    "\t\t// resolve validates against is final by this point and nothing has yet\n"
    "\t\t// drawn over it -- the water and the transparent kinds write none of\n"
    "\t\t// these lanes, so waiting would keep the same values at more risk.\n"
    "\t\t// Only under TAA: no other filter reads a history per pixel, and a\n"
    "\t\t// full-resolution RGBA32F pair is not something to allocate for nobody.\n"
    "\t\tif (gbufferPass && desc.TaaGuide && PostProcess::IsReady()\n"
    "\t\t\t&& aa == AntiAliasing::TAA && config.TaaGeometry)\n"
    "\t\t{\n"
    "\t\t\tTemporalHistory& guide = *desc.TaaGuide;\n"
    "\t\t\tguide.Prepare(Renderer::GetDevice(),\n"
    "\t\t\t\t\t\t  desc.Width * (uint32_t)supersample,\n"
    "\t\t\t\t\t\t  desc.Height * (uint32_t)supersample,\n"
    "\t\t\t\t\t\t  Format::R32G32B32A32_SFLOAT, \"TaaGuide\");\n"
    "\t\t\tif (guide.Current() && guide.Previous())\n"
    "\t\t\t{\n"
    "\t\t\t\ttaaGuideCurrent = graph.Import(guide.Current(), \"TaaGuideCurrent\");\n"
    "\t\t\t\ttaaGuidePrevious = graph.Import(guide.Previous(), \"TaaGuidePrevious\");\n"
    "\t\t\t\ttaaGuideHasHistory = guide.HasHistory();\n"
    "\t\t\t\tgraph.AddPass(\"TAA guide\",\n"
    "\t\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tbuilder.Write(taaGuideCurrent);\n"
    "\t\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t\t},\n"
    "\t\t\t\t\t[sceneHDR, normalIndex, surfaceIdIndex](RGPassContext& context)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tPostProcess::TaaGuide(context.Cmd,\n"
    "\t\t\t\t\t\t\t\t\t\t\t  context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\t  context.Color(sceneHDR, normalIndex),\n"
    "\t\t\t\t\t\t\t\t\t\t\t  context.Color(sceneHDR, surfaceIdIndex),\n"
    "\t\t\t\t\t\t\t\t\t\t\t  Format::R32G32B32A32_SFLOAT);\n"
    "\t\t\t\t\t});\n"
    "\t\t\t\t// Swapped once the pass is declared, like every other pair here:\n"
    "\t\t\t\t// what was written this frame is what the next frame reads.\n"
    "\t\t\t\tguide.Advance();\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t\telse if (desc.TaaGuide)\n"
    "\t\t{\n"
    "\t\t\t// A history left standing would be resumed as truth the frame TAA or\n"
    "\t\t\t// the test comes back on, and it would describe another camera.\n"
    "\t\t\tdesc.TaaGuide->Invalidate();\n"
    "\t\t}\n"
    "\t\tRGResource directTraced = kRGInvalid;\n")

# --- hand them to the resolve --------------------------------------------
s = rep(s,
    "\t\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tbuilder.Write(current);\n"
    "\t\t\t\t\t\tbuilder.Sample(source);\n"
    "\t\t\t\t\t\tbuilder.Sample(previous);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t\t},\n"
    "\t\t\t\t\t[source, previous, velocityIndex, feedback, stillFeedback, hasHistory, jitter](RGPassContext& context)\n",
    "\t\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tbuilder.Write(current);\n"
    "\t\t\t\t\t\tbuilder.Sample(source);\n"
    "\t\t\t\t\t\tbuilder.Sample(previous);\n"
    "\t\t\t\t\t\tif (taaGuideCurrent != kRGInvalid)\n"
    "\t\t\t\t\t\t{\n"
    "\t\t\t\t\t\t\tbuilder.Sample(taaGuideCurrent);\n"
    "\t\t\t\t\t\t\tbuilder.Sample(taaGuidePrevious);\n"
    "\t\t\t\t\t\t}\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t\t},\n"
    "\t\t\t\t\t[source, previous, velocityIndex, feedback, stillFeedback, hasHistory, jitter,\n"
    "\t\t\t\t\t taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory](RGPassContext& context)\n")
s = rep(s,
    "\t\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT, jitter, stillFeedback);\n",
    "\t\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT, jitter, stillFeedback,\n"
    "\t\t\t\t\t\t\t// RT-6: the identity lanes. Only once the pair holds a real\n"
    "\t\t\t\t\t\t\t// frame -- on the first frame `Previous` is whatever the\n"
    "\t\t\t\t\t\t\t// driver left, and comparing against that refuses every\n"
    "\t\t\t\t\t\t\t// pixel, which would look like the resolve having stopped.\n"
    "\t\t\t\t\t\t\ttaaGuideCurrent != kRGInvalid && taaGuideHasHistory\n"
    "\t\t\t\t\t\t\t\t? context.Color(taaGuideCurrent) : nullptr,\n"
    "\t\t\t\t\t\t\ttaaGuidePrevious != kRGInvalid && taaGuideHasHistory\n"
    "\t\t\t\t\t\t\t\t? context.Color(taaGuidePrevious) : nullptr);\n")

write(F, s)
print('FrameGraphBuilder.cpp: the TAA guide pass and its wiring')
