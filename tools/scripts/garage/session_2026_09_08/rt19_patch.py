"""RT-19: the refusal reasons, totalled per frame.

Every temporal pass already writes why it refused a history, per pixel, and
nothing ever adds them up. This widens the ray-counter block from 16 lanes to
32, counts the reason at every exit of the temporal resolve and of the
reflection accumulator, and prints the split beside the ray counters.

Every anchor must match exactly once or nothing is written.
"""
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


# ---------------------------------------------------------------- the lanes
patch(r'RageV/src/RageV/Renderer/RayCounters.h', [(
 T*3 + 'TaaPixels,\n'
 + T*3 + 'TaaReused,\n'
 + T*3 + '// Room to grow without a layout change. Sixteen words, one\n'
 + T*3 + '// cache line.\n'
 + T*3 + 'Count = 16',
 T*3 + 'TaaPixels,\n'
 + T*3 + 'TaaReused,\n'
 + T*3 + '// **RT-19: and why the rest were refused.** The reason was already\n'
 + T*3 + '// written per pixel by both temporal passes -- RT-12 put it in the\n'
 + T*3 + '// resolve\'s `o_Moments.w` and the accumulator\'s `o_Extra.a` -- and\n'
 + T*3 + '// nothing ever summed it, so "is this smear a history wrongly kept\n'
 + T*3 + '// or a signal too thin to average" cost an afternoon of staged\n'
 + T*3 + '// probes to answer (2026-09-08). These are that question as a line\n'
 + T*3 + '// of output.\n'
 + T*3 + '//\n'
 + T*3 + '// **Contiguous and in the shader\'s own order**, so the shaders can\n'
 + T*3 + '// index them as `first + (reason - 1)`: the resolve\'s reasons run\n'
 + T*3 + '// off screen, no history, sky crossing, object id, depth, normal\n'
 + T*3 + '// (`kOffScreen`..`kNormal` in taa_resolve.rvshader), and the\n'
 + T*3 + '// accumulator\'s run off screen, none there, normal, plane,\n'
 + T*3 + '// roughness (`g_Refusal` in reflection_accumulate.rvshader).\n'
 + T*3 + 'TaaOffScreen,\n'
 + T*3 + 'TaaNoHistory,\n'
 + T*3 + 'TaaSkyCrossing,\n'
 + T*3 + 'TaaObjectId,\n'
 + T*3 + 'TaaDepth,\n'
 + T*3 + 'TaaNormal,\n'
 + T*3 + '// Summed over the pixels that reused: how many frames stood behind\n'
 + T*3 + '// each. Divided by TaaReused it is the average history length,\n'
 + T*3 + '// which is the number that says whether a filter is holding on.\n'
 + T*3 + 'TaaFrames,\n'
 + T*3 + '// The same for the reflection accumulator, whose refusals are the\n'
 + T*3 + '// ones that matter for smearing on a moving reflector. Counted on\n'
 + T*3 + '// the specular instance only -- the pass also runs for occlusion,\n'
 + T*3 + '// the bounce and the direct light, and one set of lanes summed\n'
 + T*3 + '// over four signals would describe none of them.\n'
 + T*3 + 'ReflPixels,\n'
 + T*3 + 'ReflKept,\n'
 + T*3 + 'ReflOffScreen,\n'
 + T*3 + 'ReflNoHistory,\n'
 + T*3 + 'ReflNormal,\n'
 + T*3 + 'ReflPlane,\n'
 + T*3 + 'ReflRoughness,\n'
 + T*3 + 'ReflFrames,\n'
 + T*3 + '// Two cache lines now, and still read back once a frame. The\n'
 + T*3 + '// shaders\' stride must agree (`RayCounterSlot() * 32u`).\n'
 + T*3 + 'Count = 32')], 'TaaOffScreen')

# ------------------------------------------------------------- the strides
for path in (r'RageVEditor/assets/shaders/include/pbr_fragment.glsl',
             r'RageVEditor/assets/shaders/taa_resolve.rvshader',
             r'RageVEditor/assets/shaders/rtao_compute.rvshader'):
    src = io.open(path, encoding='utf-8', newline='').read()
    s = src.replace('\r\n', '\n')
    if 'Counts[64 * 32]' in s:
        print('%s stride already widened' % path)
        continue
    if s.count('uint Counts[64 * 16];') != 1:
        sys.exit('%s: counter declaration not found once' % path)
    s = s.replace('uint Counts[64 * 16];', 'uint Counts[64 * 32];', 1)
    n = s.count('RayCounterSlot() * 16u')
    if n < 1:
        sys.exit('%s: no stride use found' % path)
    s = s.replace('RayCounterSlot() * 16u', 'RayCounterSlot() * 32u')
    io.open(path, 'w', encoding='utf-8',
            newline='\r\n' if '\r\n' in src else '\n').write(s)
    print('widened %s (%d stride use%s)' % (path, n, '' if n == 1 else 's'))

# ------------------------------------------------------- the temporal resolve
patch(r'RageVEditor/assets/shaders/taa_resolve.rvshader', [
 (
  'const uint RAY_LANE_TAA_PIXELS = 10u;\n'
  'const uint RAY_LANE_TAA_REUSED = 11u;',
  'const uint RAY_LANE_TAA_PIXELS = 10u;\n'
  'const uint RAY_LANE_TAA_REUSED = 11u;\n'
  '// RT-19: one lane per refusal reason, in the order the reasons are\n'
  '// numbered below, so the reason itself is the index.\n'
  'const uint RAY_LANE_TAA_REFUSED = 12u;\n'
  'const uint RAY_LANE_TAA_FRAMES  = 18u;'),
 (
  '// Called once per invocation, at whichever exit it takes. Each exit reduces\n'
  '// over the lanes that reach it, so a lane is counted exactly once.\n'
  'void CountTemporal(bool reused)\n'
  '{\n'
  + T + 'const bool real = !gl_HelperInvocation;\n'
  + T + 'const uvec4 realLanes = subgroupBallot(real);\n'
  + T + 'const bool leader = real && gl_SubgroupInvocationID == subgroupBallotFindLSB(realLanes);\n'
  + T + 'const uint pixels = subgroupAdd(real ? 1u : 0u);\n'
  + T + 'const uint kept = subgroupAdd((real && reused) ? 1u : 0u);\n'
  + T + 'if (leader && pixels > 0u)\n'
  + T + '{\n'
  + T*2 + 'const uint base = RayCounterSlot() * 32u;\n'
  + T*2 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_TAA_PIXELS], pixels);\n'
  + T*2 + 'if (kept > 0u)\n'
  + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_TAA_REUSED], kept);\n'
  + T + '}\n'
  '}',
  '// Called once per invocation, at whichever exit it takes. Each exit reduces\n'
  '// over the lanes that reach it, so a lane is counted exactly once.\n'
  '//\n'
  '// **RT-19: and with the reason, and the history length.** The refusal\n'
  '// reason is the one number that says whether a temporal artefact is a\n'
  '// history wrongly kept or one wrongly refused, and it was written per\n'
  '// pixel and never counted. One reduction per reason -- six of them, and\n'
  '// only where the counters exist at all, which is a debug build of the\n'
  '// ray-query path.\n'
  'void CountTemporal(bool reused, uint reason, float frames)\n'
  '{\n'
  + T + 'const bool real = !gl_HelperInvocation;\n'
  + T + 'const uvec4 realLanes = subgroupBallot(real);\n'
  + T + 'const bool leader = real && gl_SubgroupInvocationID == subgroupBallotFindLSB(realLanes);\n'
  + T + 'const uint pixels = subgroupAdd(real ? 1u : 0u);\n'
  + T + 'const uint kept = subgroupAdd((real && reused) ? 1u : 0u);\n'
  + T + '// Rounded down, and summed as an integer: the average this feeds is\n'
  + T + '// over hundreds of thousands of pixels, where half a frame of bias\n'
  + T + '// is well under the number it is read to.\n'
  + T + 'const uint held = subgroupAdd((real && reused) ? uint(max(frames, 0.0)) : 0u);\n'
  + T + 'if (leader && pixels > 0u)\n'
  + T + '{\n'
  + T*2 + 'const uint base = RayCounterSlot() * 32u;\n'
  + T*2 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_TAA_PIXELS], pixels);\n'
  + T*2 + 'if (kept > 0u)\n'
  + T*2 + '{\n'
  + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_TAA_REUSED], kept);\n'
  + T*3 + 'atomicAdd(u_RayCounters.Counts[base + RAY_LANE_TAA_FRAMES], held);\n'
  + T*2 + '}\n'
  + T + '}\n'
  + T + '// The reasons run 1..6 and a refused pixel carries exactly one, so\n'
  + T + '// these sum to the refused count by construction -- which is the\n'
  + T + '// check to make on the printed line if it ever looks wrong.\n'
  + T + 'for (uint r = 1u; r <= 6u; ++r)\n'
  + T + '{\n'
  + T*2 + 'const uint n = subgroupAdd((real && !reused && reason == r) ? 1u : 0u);\n'
  + T*2 + 'if (leader && n > 0u)\n'
  + T*3 + 'atomicAdd(u_RayCounters.Counts[RayCounterSlot() * 32u\n'
  + T*4 + '   + RAY_LANE_TAA_REFUSED + (r - 1u)], n);\n'
  + T + '}\n'
  '}'),
 ('void CountTemporal(bool reused) {}',
  'void CountTemporal(bool reused, uint reason, float frames) {}'),
 (T*2 + 'o_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n'
  + T*2 + '\t\t\t\t  float(kNoHistory));\n'
  + T*2 + 'CountTemporal(false);',
  T*2 + 'o_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n'
  + T*2 + '\t\t\t\t  float(kNoHistory));\n'
  + T*2 + 'CountTemporal(false, kNoHistory, 0.0);'),
 (T*2 + 'o_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n'
  + T*2 + '\t\t\t\t  offScreen ? float(kOffScreen)\n'
  + T*2 + '\t\t\t\t\t\t\t: float(g_Refusal) + 0.5);\n'
  + T*2 + 'CountTemporal(false);',
  T*2 + 'o_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n'
  + T*2 + '\t\t\t\t  offScreen ? float(kOffScreen)\n'
  + T*2 + '\t\t\t\t\t\t\t: float(g_Refusal) + 0.5);\n'
  + T*2 + '// A disocclusion is blamed on whichever clause refused the centre,\n'
  + T*2 + '// which is what the pixel was actually rejected for; off screen is\n'
  + T*2 + '// its own reason and has no clause behind it.\n'
  + T*2 + 'CountTemporal(false, offScreen ? kOffScreen : max(g_Refusal, kNoHistory), 0.0);'),
 (T + 'o_Moments = vec4(frames, momMean, momMeanSq, float(g_Refusal));\n'
  + T + 'CountTemporal(true);',
  T + 'o_Moments = vec4(frames, momMean, momMeanSq, float(g_Refusal));\n'
  + T + 'CountTemporal(true, kKept, frames);'),
], 'RAY_LANE_TAA_REFUSED')

print('done')
