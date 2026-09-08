"""RT-19, part two: the accumulator counts its own refusals, and both get printed."""
import io, sys

T = '\t'


def patch(path, edits, marker):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker in s:
        print('%s already patched, skipped' % path)
        return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


counter = (
 '// **RT-19: how often this pass keeps a history, and what refuses the rest.**\n'
 '//\n'
 '// The reason has been written to `o_Extra.a` since RT-12 and read by one\n'
 '// debug view; nothing ever counted it. On the specular instance only --\n'
 '// this shader also runs for the occlusion, the bounce and the direct\n'
 '// light, and one set of lanes summed over four signals would describe\n'
 '// none of them. The counter block and the slot function come from\n'
 '// pbr_fragment.glsl, which this shader already includes, and the buffer\n'
 '// is already bound to this pass at set 0 binding 21 with the lamp set.\n'
 '//\n'
 '// Pixels that had nothing glossy in them return before this and are not\n'
 '// counted, so the denominator is the pixels that had a temporal decision\n'
 '// to make.\n'
 'const uint RAY_LANE_REFL_PIXELS  = 19u;\n'
 'const uint RAY_LANE_REFL_KEPT    = 20u;\n'
 'const uint RAY_LANE_REFL_REFUSED = 21u;   // and the four after it\n'
 'const uint RAY_LANE_REFL_FRAMES  = 26u;\n'
 '\n'
 'void CountSignal(bool kept, int refusal, float frames)\n'
 '{\n'
 + T + 'const bool real = !gl_HelperInvocation;\n'
 + T + 'const uvec4 realLanes = subgroupBallot(real);\n'
 + T + 'const bool leader = real && gl_SubgroupInvocationID == subgroupBallotFindLSB(realLanes);\n'
 + T + 'const uint base = RayCounterSlot() * 32u;\n'
 + T + 'const uint pixels = subgroupAdd(real ? 1u : 0u);\n'
 + T + 'const uint held = subgroupAdd((real && kept) ? 1u : 0u);\n'
 + T + 'const uint frameSum = subgroupAdd((real && kept) ? uint(max(frames, 0.0)) : 0u);\n'
 + T + 'if (leader && pixels > 0u)\n'
 + T + '{\n'
 + T*2 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_PIXELS], pixels);\n'
 + T*2 + 'if (held > 0u)\n'
 + T*2 + '{\n'
 + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_KEPT], held);\n'
 + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_FRAMES], frameSum);\n'
 + T*2 + '}\n'
 + T + '}\n'
 + T + '// One reason per refused pixel, numbered as g_Refusal is: off screen,\n'
 + T + '// none there, normal, plane, roughness. They sum to the refused count\n'
 + T + '// by construction, which is the check if a printed line looks wrong.\n'
 + T + 'for (uint r = 1u; r <= 5u; ++r)\n'
 + T + '{\n'
 + T*2 + 'const uint n = subgroupAdd((real && !kept && uint(refusal) == r) ? 1u : 0u);\n'
 + T*2 + 'if (leader && n > 0u)\n'
 + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_REFUSED + (r - 1u)], n);\n'
 + T + '}\n'
 '}\n'
 '\n')

patch(r'RageVEditor/assets/shaders/reflection_accumulate.rvshader', [
 # A flag that survives the block the history decision is made in.
 ('bool g_HaveMotion = false;',
  'bool g_HaveMotion = false;\n'
  '// RT-19: whether this texel ended up blending a history at all. `have` is\n'
  '// scoped to the block that decides it and the count happens at the end.\n'
  'bool g_Kept = false;'),
 (T*2 + 'if (have)\n'
  + T*2 + '{\n'
  + T*3 + '// **The bound: the fresh neighbourhood\'s spread, widened on smooth',
  T*2 + 'if (have)\n'
  + T*2 + '{\n'
  + T*3 + 'g_Kept = true;\n'
  + T*3 + '// **The bound: the fresh neighbourhood\'s spread, widened on smooth'),
 # The counter, before main.
 ('void main()\n{\n' + T + 'const ivec2 texel = ivec2(gl_FragCoord.xy);',
  '#if defined(RV_RAY_COUNTERS) && !defined(RV_SIGNAL_DIFFUSE)\n'
  + counter +
  '#else\n'
  'void CountSignal(bool kept, int refusal, float frames) {}\n'
  '#endif\n'
  '\n'
  'void main()\n{\n' + T + 'const ivec2 texel = ivec2(gl_FragCoord.xy);'),
 # And the call, at the one exit that made a decision.
 (T + 'o_Extra = vec4(PackMaterial(roughness, g_Metallic), momMean, momMeanSq,\n'
  + T*2 + '\t\t   0.5 * choice + float(g_Refusal));',
  T + 'o_Extra = vec4(PackMaterial(roughness, g_Metallic), momMean, momMeanSq,\n'
  + T*2 + '\t\t   0.5 * choice + float(g_Refusal));\n'
  + T + 'CountSignal(g_Kept, g_Refusal, frames);'),
], 'RAY_LANE_REFL_PIXELS')

# ------------------------------------------------------------------ printing
patch(r'RageV/src/RageV/Core/FrameProfiler.cpp', [(
 '\t\t\t\tif (lanes[RayCounters::TaaPixels] > 0.0)\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   temporal confidence: {0:.1f}% of pixels reused "\n'
 '\t\t\t\t\t\t\t\t "their history",\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaReused] / lanes[RayCounters::TaaPixels]);\n'
 '\t\t\t\telse\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   temporal confidence: no temporal resolve ran");',

 '\t\t\t\t// **RT-19: the acceptance rate, the memory depth, and the split by\n'
 '\t\t\t\t// what refused the rest.** Both temporal passes have written the\n'
 '\t\t\t\t// reason per pixel since RT-12 and nothing summed it, so the\n'
 '\t\t\t\t// question "is this artefact a history wrongly kept or a signal too\n'
 '\t\t\t\t// thin to average" was answered by staging probes. The percentages\n'
 '\t\t\t\t// are of the pixels each pass actually decided about, and the\n'
 '\t\t\t\t// refusals sum to the complement of the acceptance rate.\n'
 '\t\t\t\tif (lanes[RayCounters::TaaPixels] > 0.0)\n'
 '\t\t\t\t{\n'
 '\t\t\t\t\tconst double px = lanes[RayCounters::TaaPixels];\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   temporal confidence: {0:.1f}% of pixels reused "\n'
 '\t\t\t\t\t\t\t\t "their history, {1:.1f} frames deep on average",\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaReused] / px,\n'
 '\t\t\t\t\t\t\t\t lanes[RayCounters::TaaFrames]\n'
 '\t\t\t\t\t\t\t\t\t / std::max(lanes[RayCounters::TaaReused], 1.0));\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   temporal refusals: off screen {0:.1f}%, no history "\n'
 '\t\t\t\t\t\t\t\t "{1:.1f}%, sky {2:.1f}%, object id {3:.1f}%, depth {4:.1f}%, "\n'
 '\t\t\t\t\t\t\t\t "normal {5:.1f}%",\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaOffScreen] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaNoHistory] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaSkyCrossing] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaObjectId] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaDepth] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::TaaNormal] / px);\n'
 '\t\t\t\t}\n'
 '\t\t\t\telse\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   temporal confidence: no temporal resolve ran");\n'
 '\t\t\t\tif (lanes[RayCounters::ReflPixels] > 0.0)\n'
 '\t\t\t\t{\n'
 '\t\t\t\t\tconst double px = lanes[RayCounters::ReflPixels];\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   reflection history: {0:.1f}% of glossy pixels kept "\n'
 '\t\t\t\t\t\t\t\t "one, {1:.1f} frames deep on average",\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflKept] / px,\n'
 '\t\t\t\t\t\t\t\t lanes[RayCounters::ReflFrames]\n'
 '\t\t\t\t\t\t\t\t\t / std::max(lanes[RayCounters::ReflKept], 1.0));\n'
 '\t\t\t\t\tRV_CORE_INFO("[benchmark]   reflection refusals: off screen {0:.1f}%, none there "\n'
 '\t\t\t\t\t\t\t\t "{1:.1f}%, normal {2:.1f}%, plane {3:.1f}%, roughness {4:.1f}%",\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflOffScreen] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflNoHistory] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflNormal] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflPlane] / px,\n'
 '\t\t\t\t\t\t\t\t 100.0 * lanes[RayCounters::ReflRoughness] / px);\n'
 '\t\t\t\t}')], 'RT-19')

print('done')
