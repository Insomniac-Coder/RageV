# -*- coding: utf-8 -*-
"""RT-8 job 3: the contract's own counters, on the sea.

The contract's acceptance and depth have been counted since RT-19, but on the
specular instance alone -- four signals summed into one set of lanes describe
none of them. The sea is a fifth, and "the contract's average is noisier than
the one it replaces" is not a thing to guess at: it is a history depth, and
there is a lane for that.

So the accumulate learns which signal it is (its slot, in a push-constant lane
it was not using) and the water signal counts into lanes of its own.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


patch(r'RageV/src/RageV/Renderer/RayCounters.h', [
(
"""			WaterGateClamped,""",
"""			WaterGateClamped,
			// **RT-8 job 3: and the contract's own numbers on the sea**, once
			// the sea is a signal. The same five reasons the reflection lanes
			// carry, counted for slot 4 alone -- because the whole point of
			// splitting them is that one set of lanes summed over five signals
			// describes none of them.
			WaterSignalPixels,
			WaterSignalKept,
			WaterSignalOffScreen,
			WaterSignalNoHistory,
			WaterSignalNormal,
			WaterSignalPlane,
			WaterSignalRoughness,
			WaterSignalFrames,""",
    'water signal lanes'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""		// RT-8: one where the depth slot carries the layer's own position.
		// The w lane, because the blur's z is its stride.
		push.Probe.w = signal.PositionLane ? 1.0f : 0.0f;""",
"""		// RT-8: one where the depth slot carries the layer's own position.
		// The w lane, because the blur's z is its stride.
		push.Probe.w = signal.PositionLane ? 1.0f : 0.0f;
		// RT-8: and which signal this is, so the counters can keep the sea's
		// numbers apart from the reflections'. The blur has no use for z.
		push.Probe.z = (float)index;""",
    'slot in probe.z'),
])

# ---------------------------------------------------------------- the shader
P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'RAY_LANE_WATER_SIGNAL' in s:
    sys.exit('accumulate already counted for the sea')

old = '''#if defined(RV_RAY_COUNTERS) && !defined(RV_SIGNAL_DIFFUSE)'''
new = '''#if defined(RV_RAY_COUNTERS)'''
if s.count(old) != 1:
    sys.exit('counter gate matched %d' % s.count(old))
s = s.replace(old, new, 1)

old = '''const uint RAY_LANE_REFL_PIXELS  = 19u;
const uint RAY_LANE_REFL_KEPT    = 20u;
const uint RAY_LANE_REFL_REFUSED = 21u;   // and the four after it
const uint RAY_LANE_REFL_FRAMES  = 26u;

void CountSignal(bool kept, int refusal, float frames)
{
	const bool real = !gl_HelperInvocation;'''
new = '''const uint RAY_LANE_REFL_PIXELS  = 19u;
const uint RAY_LANE_REFL_KEPT    = 20u;
const uint RAY_LANE_REFL_REFUSED = 21u;   // and the four after it
const uint RAY_LANE_REFL_FRAMES  = 26u;

// **RT-8: and the sea's own set, for slot 4.** Not because the sea deserves
// special treatment, but because summing five signals into one set of lanes
// describes none of them -- which is the same reason the reflections were
// split off the temporal resolve's in RT-19.
const uint RAY_LANE_WATER_SIGNAL_PIXELS  = 37u;
const uint RAY_LANE_WATER_SIGNAL_KEPT    = 38u;
const uint RAY_LANE_WATER_SIGNAL_REFUSED = 39u;   // and the four after it
const uint RAY_LANE_WATER_SIGNAL_FRAMES  = 44u;

// Which signal is running: the slot the engine put in Probe.z. Zero is the
// reflections, four the sea; everything between counts nothing, because a
// shared set of lanes is worse than none.
bool CountsAsReflection() { return u_Reflection.Probe.z < 0.5; }
bool CountsAsWater()      { return u_Reflection.Probe.z > 3.5; }

void CountSignal(bool kept, int refusal, float frames)
{
#ifdef RV_SIGNAL_DIFFUSE
	// The diffuse instances are the occlusion, the bounce, the direct light
	// and the sea. Only the sea has lanes.
	if (!CountsAsWater())
		return;
#else
	if (!CountsAsReflection())
		return;
#endif
	const uint lanePixels  = CountsAsWater() ? RAY_LANE_WATER_SIGNAL_PIXELS  : RAY_LANE_REFL_PIXELS;
	const uint laneKept    = CountsAsWater() ? RAY_LANE_WATER_SIGNAL_KEPT    : RAY_LANE_REFL_KEPT;
	const uint laneRefused = CountsAsWater() ? RAY_LANE_WATER_SIGNAL_REFUSED : RAY_LANE_REFL_REFUSED;
	const uint laneFrames  = CountsAsWater() ? RAY_LANE_WATER_SIGNAL_FRAMES  : RAY_LANE_REFL_FRAMES;
	const bool real = !gl_HelperInvocation;'''
if s.count(old) != 1:
    sys.exit('CountSignal head matched %d' % s.count(old))
s = s.replace(old, new, 1)

for old, new, what in (
    ('atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_PIXELS], pixels);',
     'atomicAdd(u_RayCounters.Counts[base + lanePixels], pixels);', 'pixels'),
    ('atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_KEPT], held);',
     'atomicAdd(u_RayCounters.Counts[base + laneKept], held);', 'kept'),
    ('atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_FRAMES], frameSum);',
     'atomicAdd(u_RayCounters.Counts[base + laneFrames], frameSum);', 'frames'),
    ('atomicAdd(u_RayCounters.Counts[base + RAY_LANE_REFL_REFUSED + (r - 1u)], n);',
     'atomicAdd(u_RayCounters.Counts[base + laneRefused + (r - 1u)], n);', 'refusals'),
):
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print(P, 'counts the sea too')

# --------------------------------------------------------------- the print
patch(r'RageV/src/RageV/Core/FrameProfiler.cpp', [
(
"""				// **RT-8 job 3: what the shared gate would do to the sea.** The""",
"""				// **RT-8 job 3: and what it actually does, once the sea is a
				// signal.** The lines above are the shadow measurement taken
				// while the sea kept its own average; these are the contract
				// running on it for real.
				if (lanes[RayCounters::WaterSignalPixels] > 0.0)
				{
					const double px = lanes[RayCounters::WaterSignalPixels];
					RV_CORE_INFO("[benchmark]   sea on the contract: {0:.1f}% of sea pixels kept "
								 "a history, {1:.1f} frames deep on average",
								 100.0 * lanes[RayCounters::WaterSignalKept] / px,
								 lanes[RayCounters::WaterSignalFrames]
									 / std::max(lanes[RayCounters::WaterSignalKept], 1.0));
					RV_CORE_INFO("[benchmark]   sea refusals: off screen {0:.1f}%, none there "
								 "{1:.1f}%, normal {2:.1f}%, plane {3:.1f}%, roughness {4:.1f}%",
								 100.0 * lanes[RayCounters::WaterSignalOffScreen] / px,
								 100.0 * lanes[RayCounters::WaterSignalNoHistory] / px,
								 100.0 * lanes[RayCounters::WaterSignalNormal] / px,
								 100.0 * lanes[RayCounters::WaterSignalPlane] / px,
								 100.0 * lanes[RayCounters::WaterSignalRoughness] / px);
				}
				// **RT-8 job 3: what the shared gate would do to the sea.** The""",
    'sea signal lines'),
])
