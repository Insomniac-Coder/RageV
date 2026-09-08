# -*- coding: utf-8 -*-
"""The water-gate lines in the benchmark's counter block."""
import io, sys

P = r'RageV/src/RageV/Core/FrameProfiler.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'WaterGatePixels' in s:
    sys.exit('already patched')

old = '''								 100.0 * lanes[RayCounters::ReflRoughness] / px);
				}'''
new = '''								 100.0 * lanes[RayCounters::ReflRoughness] / px);
				}
				// **RT-8 job 3: what the shared gate would do to the sea.** The
				// water's own accumulate runs the signal contract's geometric
				// tests beside its own and acts on none of them, so these lines
				// answer the question the item was argued over without a single
				// picture changing (2026-09-08).
				if (lanes[RayCounters::WaterGatePixels] > 0.0)
				{
					const double px = lanes[RayCounters::WaterGatePixels];
					RV_CORE_INFO("[benchmark]   water under the shared gate: {0:.1f}% of sea "
								 "pixels would keep a history; without RT-15's moving-surface "
								 "exemption {1:.1f}%",
								 100.0 * lanes[RayCounters::WaterGateKept] / px,
								 100.0 * lanes[RayCounters::WaterGateKeptStrict] / px);
					RV_CORE_INFO("[benchmark]   water gate refusals: off screen {0:.1f}%, none "
								 "there {1:.1f}%, normal {2:.1f}%, plane {3:.1f}% -- and the "
								 "plane alone would refuse {4:.1f}% without the exemption",
								 100.0 * lanes[RayCounters::WaterGateOffScreen] / px,
								 100.0 * lanes[RayCounters::WaterGateNoHistory] / px,
								 100.0 * lanes[RayCounters::WaterGateNormal] / px,
								 100.0 * lanes[RayCounters::WaterGatePlane] / px,
								 100.0 * lanes[RayCounters::WaterGatePlaneStrict] / px);
					RV_CORE_INFO("[benchmark]   water gate detail: a half-float plane would lose "
								 "{0:.1f}% of what it keeps; the sea's own bound moved a history "
								 "on {1:.1f}% of them",
								 100.0 * lanes[RayCounters::WaterGatePlaneHalf] / px,
								 100.0 * lanes[RayCounters::WaterGateClamped] / px);
				}'''
if s.count(old) != 1:
    sys.exit('anchor matched %d' % s.count(old))
io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(
    s.replace(old, new, 1))
print('FrameProfiler patched')
