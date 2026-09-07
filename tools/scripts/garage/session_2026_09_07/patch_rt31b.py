"""RT-3.1, part B: the contract runs at the signal's own resolution.

`addSignal` gains a guidance set -- the G-buffer's depth, normal and velocity
lanes downsampled by selection to the signal's grid -- and a divisor. Passing
the full-resolution lanes and a divisor of 1 is exactly what every signal does
today, so the reflections and the direct light are untouched by construction.

The texel-denominated tuning scales with the divisor: `Slack`, `SmearTexels`,
`YoungRadius` and `MaxRadius` are all counted in texels, and a half-resolution
texel covers twice the screen. Left alone, a signal moved to half resolution
would quietly keep its history twice as long under motion and blur half as far
across the picture.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'struct SignalGuidance'):
    print('FrameGraphBuilder.cpp already has the guidance')
    raise SystemExit(0)

# --- the guidance set, and addSignal taking it ---------------------------
s = rep(s,
    "\t\tstruct SignalPassNames { const char* Accumulate; const char* Blur[3]; };\n"
    "\t\tauto addSignal = [&](const SignalPassNames& names, const Renderer3D::SignalParams& params,\n"
    "\t\t\t\t\t\t\t RGResource fresh, RGResource current, RGResource previous, bool hasHistory,\n"
    "\t\t\t\t\t\t\t CameraMotion* motion, RGTargetDesc blurDesc, bool pair) -> RGResource\n"
    "\t\t{\n",
    "\t\tstruct SignalPassNames { const char* Accumulate; const char* Blur[3]; };\n"
    "\t\t// **RT-3.1: which buffers the contract validates against, and at what\n"
    "\t\t// scale.** The accumulate and the blurs read the surface under each texel\n"
    "\t\t// with `texelFetch(..., ivec2(gl_FragCoord.xy))`, so the lanes they read\n"
    "\t\t// have to be on the signal's own grid. At full resolution that is the\n"
    "\t\t// G-buffer itself and `Divisor` is one -- which is what the reflections\n"
    "\t\t// and the direct light pass, so nothing changes for them. A signal traced\n"
    "\t\t// at half or quarter passes the downsampled lanes and its divisor, and the\n"
    "\t\t// whole contract runs there instead of at four or sixteen times the texels.\n"
    "\t\tstruct SignalGuidance\n"
    "\t\t{\n"
    "\t\t\tRGResource Depth = kRGInvalid;      // kRGInvalid means the G-buffer's own\n"
    "\t\t\tRGResource Surface = kRGInvalid;\n"
    "\t\t\tRGResource Velocity = kRGInvalid;\n"
    "\t\t\tuint32_t   Divisor = 1;\n"
    "\t\t};\n"
    "\t\tauto addSignal = [&](const SignalPassNames& names, Renderer3D::SignalParams params,\n"
    "\t\t\t\t\t\t\t RGResource fresh, RGResource current, RGResource previous, bool hasHistory,\n"
    "\t\t\t\t\t\t\t CameraMotion* motion, RGTargetDesc blurDesc, bool pair,\n"
    "\t\t\t\t\t\t\t SignalGuidance guide = {}) -> RGResource\n"
    "\t\t{\n"
    "\t\t\t// **The texel-denominated tuning follows the grid.** Every one of these\n"
    "\t\t\t// four is counted in texels of the signal's own target, and a texel at\n"
    "\t\t\t// half resolution covers twice the screen: unscaled, a signal moved down\n"
    "\t\t\t// would hold its history through twice the camera motion before the\n"
    "\t\t\t// smear cap bit, and blur half as far across the picture. Scaling them\n"
    "\t\t\t// keeps what they mean -- a distance on screen -- the same.\n"
    "\t\t\tif (guide.Divisor > 1)\n"
    "\t\t\t{\n"
    "\t\t\t\tconst float scale = 1.0f / (float)guide.Divisor;\n"
    "\t\t\t\tparams.Slack *= scale;\n"
    "\t\t\t\tparams.SmearTexels *= scale;\n"
    "\t\t\t\tparams.YoungRadius *= scale;\n"
    "\t\t\t\tparams.MaxRadius *= scale;\n"
    "\t\t\t}\n"
    "\t\t\tconst RGResource guideDepth = guide.Depth != kRGInvalid ? guide.Depth : sceneHDR;\n"
    "\t\t\tconst RGResource guideSurface = guide.Surface != kRGInvalid ? guide.Surface : sceneHDR;\n"
    "\t\t\tconst RGResource guideVelocity = guide.Velocity != kRGInvalid ? guide.Velocity : sceneHDR;\n"
    "\t\t\t// The lane index inside whichever target: the G-buffer keeps its\n"
    "\t\t\t// attachments, the guidance target has one lane apiece.\n"
    "\t\t\tconst uint32_t guideNormalLane = guide.Surface != kRGInvalid ? 1u : normalIndex;\n"
    "\t\t\tconst uint32_t guideVelocityLane = guide.Velocity != kRGInvalid ? 2u : velocityIndex;\n"
    "\t\t\tconst bool ownGuide = guide.Depth != kRGInvalid;\n")

# the accumulate pass reads the guidance
s = rep(s,
    "\t\t\tgraph.AddPass(names.Accumulate,\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(current);\n"
    "\t\t\t\t\tbuilder.Sample(fresh);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tif (hasHistory)\n"
    "\t\t\t\t\t\tbuilder.Sample(previous);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[params, fresh, sceneHDR, normalIndex, velocityIndex, current, previous, hasHistory, motion, pair]\n"
    "\t\t\t\t(RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tRenderer3D::AccumulateSignal(params,\n"
    "\t\t\t\t\t\tcontext.Color(fresh), context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\tcontext.Color(sceneHDR, normalIndex),\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous) : nullptr,\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous, 1) : nullptr,\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous, 2) : nullptr,\n"
    "\t\t\t\t\t\tcontext.Color(sceneHDR, velocityIndex),\n"
    "\t\t\t\t\t\t*motion, hasHistory,\n"
    "\t\t\t\t\t\tpair ? context.Color(fresh, 1) : nullptr,\n"
    "\t\t\t\t\t\tpair && hasHistory ? context.Color(previous, 3) : nullptr);\n"
    "\t\t\t\t});\n",
    "\t\t\tgraph.AddPass(names.Accumulate,\n"
    "\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tbuilder.Write(current);\n"
    "\t\t\t\t\tbuilder.Sample(fresh);\n"
    "\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\tif (ownGuide)\n"
    "\t\t\t\t\t\tbuilder.Sample(guideDepth);\n"
    "\t\t\t\t\tif (hasHistory)\n"
    "\t\t\t\t\t\tbuilder.Sample(previous);\n"
    "\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t},\n"
    "\t\t\t\t[params, fresh, sceneHDR, current, previous, hasHistory, motion, pair,\n"
    "\t\t\t\t guideDepth, guideSurface, guideVelocity, guideNormalLane, guideVelocityLane, ownGuide]\n"
    "\t\t\t\t(RGPassContext& context)\n"
    "\t\t\t\t{\n"
    "\t\t\t\t\tRenderer3D::AccumulateSignal(params,\n"
    "\t\t\t\t\t\tcontext.Color(fresh),\n"
    "\t\t\t\t\t\t// The guidance target keeps its depth in a colour lane; the\n"
    "\t\t\t\t\t\t// G-buffer's is a depth attachment. Both are a sampler2D whose\n"
    "\t\t\t\t\t\t// red is clip depth as written, which is all the shader reads.\n"
    "\t\t\t\t\t\townGuide ? context.Color(guideDepth) : context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\tcontext.Color(guideSurface, guideNormalLane),\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous) : nullptr,\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous, 1) : nullptr,\n"
    "\t\t\t\t\t\thasHistory ? context.Color(previous, 2) : nullptr,\n"
    "\t\t\t\t\t\tcontext.Color(guideVelocity, guideVelocityLane),\n"
    "\t\t\t\t\t\t*motion, hasHistory,\n"
    "\t\t\t\t\t\tpair ? context.Color(fresh, 1) : nullptr,\n"
    "\t\t\t\t\t\tpair && hasHistory ? context.Color(previous, 3) : nullptr);\n"
    "\t\t\t\t});\n")

# the blur passes read the guidance too
s = rep(s,
    "\t\t\t\tgraph.AddPass(names.Blur[pass],\n"
    "\t\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tbuilder.Write(output);\n"
    "\t\t\t\t\t\tbuilder.Sample(input);\n"
    "\t\t\t\t\t\tif (input != current)\n"
    "\t\t\t\t\t\t\tbuilder.Sample(current);\n"
    "\t\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t\t},\n"
    "\t\t\t\t\t[params, input, current, sceneHDR, normalIndex, stride, pair](RGPassContext& context)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tRenderer3D::BlurSignal(params, context.Color(input),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Color(sceneHDR, normalIndex),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Color(current, 1),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   stride,\n",
    "\t\t\t\tgraph.AddPass(names.Blur[pass],\n"
    "\t\t\t\t\t[&](RGPassBuilder& builder)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tbuilder.Write(output);\n"
    "\t\t\t\t\t\tbuilder.Sample(input);\n"
    "\t\t\t\t\t\tif (input != current)\n"
    "\t\t\t\t\t\t\tbuilder.Sample(current);\n"
    "\t\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\t\tif (ownGuide)\n"
    "\t\t\t\t\t\t\tbuilder.Sample(guideDepth);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n"
    "\t\t\t\t\t},\n"
    "\t\t\t\t\t[params, input, current, sceneHDR, stride, pair,\n"
    "\t\t\t\t\t guideDepth, guideSurface, guideNormalLane, ownGuide](RGPassContext& context)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tRenderer3D::BlurSignal(params, context.Color(input),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   ownGuide ? context.Color(guideDepth)\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t: context.Depth(sceneHDR),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Color(guideSurface, guideNormalLane),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   context.Color(current, 1),\n"
    "\t\t\t\t\t\t\t\t\t\t\t   stride,\n")

write(F, s)
print('FrameGraphBuilder.cpp: addSignal takes guidance')
