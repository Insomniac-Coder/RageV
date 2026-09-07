"""RT-4 (the open measurement): the reflection goes *through* the temporal
resolve instead of being painted on after it.

**The structural finding behind this.** The whole reflection chain -- trace,
resolve, the reconstruction contract, composite -- ran after the TAA resolve
and added its picture on top of the finished frame. So the shiniest,
highest-contrast content in the scene was the one layer with:
  * no anti-aliasing of its own, on silhouettes that are jittered every frame;
  * no temporal filter at the final stage, only its own accumulator; and
  * an accumulator whose memory *shortens under motion* by design (the smear
    cap, down to MovingMemory frames).
Which is why it holds together standing still and comes apart the moment
anything moves -- and why a young-history blur of twelve texels had to exist at
all: it was hiding noise that TAA was never given the chance to remove.

Moving the block above the resolve costs nothing structurally: the trace reads
the G-buffer's depth and normal and the scene's acceleration structure, never
the lit colour or the resolve's output. Only the composite touched `shaded`,
and that is the point of the move.

The one thing that does change: the trace read *this* frame's ray-budget tile
map, which the allocator produces after the resolve. It now reads last frame's,
which is exactly what RT-2's occlusion and RT-3's bounce already do for the
same reason -- they run before the lit pass and the allocation they can see is
the previous one.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'RT-4: before the temporal resolve'):
    print('FrameGraphBuilder.cpp already composites before TAA')
    raise SystemExit(0)

nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)

# --- find the block by content, never by a remembered line number --------
start = next(i for i, l in enumerate(lines)
             if l.strip().startswith('// --- the opaque glossy reflection, traced and averaged'))
guard = next(i for i, l in enumerate(lines)
             if l.strip() == 'if (tracedReflections && currentReflections != kRGInvalid)')
assert 0 < guard - start <= 10, 'the guard is not just below the comment (%d..%d)' % (start, guard)

# the matching close: brace depth from the guard's own opening brace
depth = 0
end = None
for i in range(guard + 1, len(lines)):
    depth += lines[i].count('{') - lines[i].count('}')
    if depth == 0:
        end = i
        break
assert end is not None and lines[end].strip() == '}', 'no matching brace for the reflection block'
assert 'desc.Reflections->Advance();' in nl.join(lines[guard:end + 1]), 'wrong block: no Advance'

block = lines[start:end + 1]
print('reflection block: lines %d..%d (%d lines)' % (start + 1, end + 1, len(block)))

# --- the budget becomes last frame's -------------------------------------
text = nl.join(block)
text = text.replace(
    "\t\t\t// After the allocator so the trace can read this frame's tile map, and",
    "\t\t\t// **RT-4: before the temporal resolve, so the reflection passes through\n"
    "\t\t\t// it like everything else in the picture.** It used to run after, and\n"
    "\t\t\t// paint its result on top of the finished frame -- which left the one\n"
    "\t\t\t// layer with the sharpest content in the scene with no anti-aliasing of\n"
    "\t\t\t// its own, on silhouettes the jitter moves every frame, and no temporal\n"
    "\t\t\t// filter at the end of the chain. Standing still its own accumulator\n"
    "\t\t\t// carried it; moving, that accumulator shortens its memory by design and\n"
    "\t\t\t// nothing downstream was left to clean up what remained.\n"
    "\t\t\t//")
text = text.replace(
    "// before anything the next frame's scene pass needs: what it writes is\n"
    "\t\t\t// read a frame late through the screen-reflection hook (reflection_trace.rvshader).",
    "// What it writes is still read a frame late through the screen-reflection\n"
    "\t\t\t// hook (reflection_trace.rvshader), which is unaffected by where the\n"
    "\t\t\t// composite lands.")
# the tile map: last frame's, as the occlusion and the bounce already read it
text = text.replace("const bool budgetBound = hasRayBudget && rayBudgetMap != kRGInvalid;",
                    "// **Last frame's allocation** (RT-4): the allocator's own passes still\n"
                    "\t\t\t// run after the resolve, so what exists this early is the previous\n"
                    "\t\t\t// map -- the same one RT-2's occlusion and RT-3's bounce read, and\n"
                    "\t\t\t// for the same reason.\n"
                    "\t\t\tconst bool budgetBound = budgetPrevious != kRGInvalid && budgetHasHistory;")
text = text.replace("builder.Sample(rayBudgetMap);", "builder.Sample(budgetPrevious);")
text = text.replace("budgetMap = rayBudgetMap,", "budgetMap = budgetPrevious,")
assert 'rayBudgetMap' not in text, 'a rayBudgetMap reference survived the move'

# --- cut, then paste above the temporal block ----------------------------
rest = lines[:start] + lines[end + 1:]
anchor = next(i for i, l in enumerate(rest)
              if l.strip() == '// This frame\'s temporal history, for the debug view\'s confidence')
moved = text.split(nl) + ['']
out = rest[:anchor] + moved + rest[anchor:]
write(F, nl.join(out))
print('moved above the temporal resolve, %d lines' % len(moved))
