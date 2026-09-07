"""RT-6.1, part D: the composite moves above the temporal resolve again -- this
time handing it the motion to reproject by.

Part of this is the RT-4 move that was reverted on 2026-09-07: the reflection
chain reads only the G-buffer and the acceleration structure, so it lifts above
the resolve cleanly and reads last frame's ray-budget tile map, as RT-2's
occlusion and RT-3's bounce already do. What was missing then, and is here now,
is the velocity: the resolve reprojected the reflection by the floor's motion
and smeared the wet ground into horizontal bands. The composite now says which
motion each pixel moves by, and the resolve uses that lane instead of the
scene's.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'RT-6.1: before the temporal resolve'):
    print('already moved')
    raise SystemExit(0)

nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)

start = next(i for i, l in enumerate(lines)
             if l.strip().startswith('// --- the opaque glossy reflection, traced and averaged'))
guard = next(i for i, l in enumerate(lines)
             if l.strip() == 'if (tracedReflections && currentReflections != kRGInvalid)')
assert 0 < guard - start <= 10, 'the guard moved away from its comment'
depth, end = 0, None
for i in range(guard + 1, len(lines)):
    depth += lines[i].count('{') - lines[i].count('}')
    if depth == 0:
        end = i
        break
assert end is not None and lines[end].strip() == '}', 'no matching brace'
assert 'desc.Reflections->Advance();' in nl.join(lines[guard:end + 1]), 'wrong block'
text = nl.join(lines[start:end + 1])
print('reflection block: %d lines' % (end - start + 1))

text = text.replace(
    "\t\t// After the allocator so the trace can read this frame's tile map, and\n"
    "\t\t// before anything the next frame's scene pass needs: what it writes is\n"
    "\t\t// read a frame late through the screen-reflection hook (reflection_trace.rvshader).",
    "\t\t// **RT-6.1: before the temporal resolve, and it hands the resolve the\n"
    "\t\t// motion to reproject by.** The chain reads only the G-buffer and the\n"
    "\t\t// scene's structure, never the lit colour, so it lifts above the resolve\n"
    "\t\t// cleanly; the tile map it reads becomes last frame's, as RT-2's\n"
    "\t\t// occlusion and RT-3's bounce already read it. Moving it without the\n"
    "\t\t// motion lane was tried on 2026-09-07 and smeared the wet floor into\n"
    "\t\t// horizontal bands, because the resolve dragged the reflection along the\n"
    "\t\t// floor's velocity; the composite now says which motion each pixel has.\n"
    "\t\t// What it writes is still read a frame late through the screen-reflection\n"
    "\t\t// hook (reflection_trace.rvshader), which this does not affect.")
text = text.replace("const bool budgetBound = hasRayBudget && rayBudgetMap != kRGInvalid;",
                    "const bool budgetBound = budgetPrevious != kRGInvalid && budgetHasHistory;")
text = text.replace("builder.Sample(rayBudgetMap);", "builder.Sample(budgetPrevious);")
text = text.replace("budgetMap = rayBudgetMap,", "budgetMap = budgetPrevious,")
assert 'rayBudgetMap' not in text, 'a rayBudgetMap reference survived'

# --- the composite gains its motion attachment and its two inputs --------
text = text.replace(
    "\t\t\t\tRGTargetDesc compositeDesc;\n"
    "\t\t\t\tcompositeDesc.Name = \"ReflectionComposited\";\n"
    "\t\t\t\tcompositeDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\t\tcompositeDesc.Depth = Format::Undefined;\n",
    "\t\t\t\tRGTargetDesc compositeDesc;\n"
    "\t\t\t\tcompositeDesc.Name = \"ReflectionComposited\";\n"
    "\t\t\t\tcompositeDesc.Color = Format::R16G16B16A16_SFLOAT;\n"
    "\t\t\t\t// RT-6.1: and the velocity the resolve reprojects by, per pixel.\n"
    "\t\t\t\tcompositeDesc.ExtraColors = { Format::R16G16_SFLOAT };\n"
    "\t\t\t\tcompositeDesc.Depth = Format::Undefined;\n")
text = text.replace(
    "\t\t\t\t\t\tbuilder.Write(composited);\n"
    "\t\t\t\t\t\tbuilder.Sample(before);\n"
    "\t\t\t\t\t\tbuilder.Sample(blurred);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n",
    "\t\t\t\t\t\tbuilder.Write(composited);\n"
    "\t\t\t\t\t\tbuilder.Sample(before);\n"
    "\t\t\t\t\t\tbuilder.Sample(blurred);\n"
    "\t\t\t\t\t\tbuilder.Sample(currentReflections);\n"
    "\t\t\t\t\t\tbuilder.Sample(sceneHDR);\n"
    "\t\t\t\t\t\tbuilder.DisableDepth();\n")
text = text.replace(
    "\t\t\t\t\t[before, blurred](RGPassContext& context)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tPostProcess::ReflectionComposite(context.Cmd, context.Color(before),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t context.Color(blurred),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t Format::R16G16B16A16_SFLOAT);\n"
    "\t\t\t\t\t});\n"
    "\t\t\t\tshaded = composited;\n",
    "\t\t\t\t\t[before, blurred, currentReflections, sceneHDR, velocityIndex]\n"
    "\t\t\t\t\t(RGPassContext& context)\n"
    "\t\t\t\t\t{\n"
    "\t\t\t\t\t\tPostProcess::ReflectionComposite(context.Cmd, context.Color(before),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t context.Color(blurred),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t Format::R16G16B16A16_SFLOAT,\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t // RT-6.1: the accumulator's fourth lane and\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t // the scene's, and the lane it writes.\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t context.Color(currentReflections, 3),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t context.Color(sceneHDR, velocityIndex),\n"
    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t Format::R16G16_SFLOAT);\n"
    "\t\t\t\t\t});\n"
    "\t\t\t\tshaded = composited;\n"
    "\t\t\t\t// The resolve reads this instead of the scene's lane.\n"
    "\t\t\t\treflectionMotion = composited;\n")

rest = lines[:start] + lines[end + 1:]
anchor = next(i for i, l in enumerate(rest)
              if l.strip() == "// This frame's temporal history, for the debug view's confidence")
out = rest[:anchor] + ['\t\t// RT-6.1: the composite\'s velocity lane, when it ran.',
                       '\t\tRGResource reflectionMotion = kRGInvalid;'] \
    + text.split(nl) + [''] + rest[anchor:]
s = nl.join(out)

# --- and the resolve reads it --------------------------------------------
old = ("\t\t\t\t\t\t\t// The velocity attachment of the scene target,\n"
       "\t\t\t\t\t\t\t// which is the same target `source` is when SSAA\n"
       "\t\t\t\t\t\t\t// is off -- and SSAA and TAA cannot both be on.\n"
       "\t\t\t\t\t\t\tcontext.Color(source, velocityIndex),\n").replace('\n', nl)
new = ("\t\t\t\t\t\t\t// **RT-6.1: the composite's lane where the reflection ran**,\n"
       "\t\t\t\t\t\t\t// which is the scene's velocity everywhere the pixel is\n"
       "\t\t\t\t\t\t\t// mostly its surface and the virtual image's where it is\n"
       "\t\t\t\t\t\t\t// mostly reflection. Otherwise the scene's own attachment,\n"
       "\t\t\t\t\t\t\t// which is the same target `source` is when SSAA is off --\n"
       "\t\t\t\t\t\t\t// and SSAA and TAA cannot both be on.\n"
       "\t\t\t\t\t\t\treflectionMotion != kRGInvalid\n"
       "\t\t\t\t\t\t\t\t? context.Color(reflectionMotion, 1)\n"
       "\t\t\t\t\t\t\t\t: context.Color(source, velocityIndex),\n").replace('\n', nl)
assert s.count(old) == 1, 'the resolve velocity anchor matched %d' % s.count(old)
s = s.replace(old, new)

old2 = ("\t\t\t\t\t[source, previous, velocityIndex, feedback, stillFeedback, hasHistory, jitter,\n"
        "\t\t\t\t\t taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory](RGPassContext& context)\n").replace('\n', nl)
new2 = ("\t\t\t\t\t[source, previous, velocityIndex, feedback, stillFeedback, hasHistory, jitter,\n"
        "\t\t\t\t\t taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory,\n"
        "\t\t\t\t\t reflectionMotion](RGPassContext& context)\n").replace('\n', nl)
assert s.count(old2) == 1, 'the resolve capture anchor matched %d' % s.count(old2)
s = s.replace(old2, new2)

old3 = ("\t\t\t\t\t\tbuilder.Sample(previous);\n"
        "\t\t\t\t\t\tif (taaGuideCurrent != kRGInvalid)\n").replace('\n', nl)
new3 = ("\t\t\t\t\t\tbuilder.Sample(previous);\n"
        "\t\t\t\t\t\tif (reflectionMotion != kRGInvalid)\n"
        "\t\t\t\t\t\t\tbuilder.Sample(reflectionMotion);\n"
        "\t\t\t\t\t\tif (taaGuideCurrent != kRGInvalid)\n").replace('\n', nl)
assert s.count(old3) == 1, 'the resolve builder anchor matched %d' % s.count(old3)
s = s.replace(old3, new3)

write(F, s)
print('FrameGraphBuilder.cpp: moved, and the resolve reads the composite lane')
