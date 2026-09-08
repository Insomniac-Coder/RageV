"""RT-8 job 3: grow the counter block and give it the water-gate lanes.

The claim under test is that the signal contract's geometric history gate
cannot hold a sea. Nothing ever measured it. These lanes are that measurement.
"""
import io, sys

def load(p):
    src = io.open(p, encoding='utf-8', newline='').read()
    return src, ('\r\n' in src), src.replace('\r\n', '\n')

def save(p, crlf, s):
    io.open(p, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)

def once(s, old, new, what):
    if s.count(old) != 1:
        sys.exit('%s matched %d times' % (what, s.count(old)))
    return s.replace(old, new, 1)

# ---- RayCounters.h: the lanes -------------------------------------------
P = r'RageV/src/RageV/Renderer/RayCounters.h'
src, crlf, s = load(P)
if 'WaterGatePixels' in s:
    sys.exit('already patched')

s = once(s, '''			ReflRoughness,
			ReflFrames,
			// Two cache lines now, and still read back once a frame. The
			// shaders' stride must agree (`RayCounterSlot() * 32u`).
			Count = 32''', '''			ReflRoughness,
			ReflFrames,
			// **RT-8 job 3: would the shared history gate accept a water
			// pixel?** The item's third piece -- folding the sea's averaging
			// into the signal contract -- was argued against on the claim that
			// the contract's geometric gate cannot hold a sea, and **nobody
			// had measured it**: what had been measured was the sea with no
			// averaging at all, which is a different thing (2026-09-08).
			//
			// So the water's own accumulate now runs the contract's geometric
			// tests against the sea's surface a frame back and **acts on none
			// of them** -- the sea still keeps its own average, and every
			// picture is bit-identical. These lanes are the answer.
			WaterGatePixels,
			WaterGateKept,
			WaterGateOffScreen,
			WaterGateNoHistory,
			WaterGateNormal,
			WaterGatePlane,
			// The same gate with RT-15's exemption taken out -- the plane test
			// applied to a surface that moved, which is what the argument
			// assumed and what the gate did before RT-15 landed on the same day.
			WaterGateKeptStrict,
			WaterGatePlaneStrict,
			// And what the gate's own storage would cost: the plane distance
			// rounded to the half float the contract's surface attachment
			// actually is. A bay is a kilometre across and a half's step out
			// there is half a metre, so this is a real question for the item
			// and not a detail.
			WaterGatePlaneHalf,
			// How often the sea's own gate moved a history -- the neighbourhood
			// bound biting. The thing the shared gate would be replacing, so
			// the two numbers belong on the same screen.
			WaterGateClamped,
			// Four cache lines now, and still read back once a frame. The
			// shaders' stride must agree: `RAY_COUNTER_STRIDE` in
			// pbr_fragment.glsl, taa_resolve.rvshader and rtao_compute.rvshader,
			// which is the whole list.
			Count = 64''', 'lane block')
save(P, crlf, s)
print('RayCounters.h: lanes added, stride 32 -> 64')

# ---- the shaders' stride, named rather than spelled ----------------------
for p, decl in (
    ('RageVEditor/assets/shaders/include/pbr_fragment.glsl', True),
    ('RageVEditor/assets/shaders/taa_resolve.rvshader', True),
    ('RageVEditor/assets/shaders/rtao_compute.rvshader', True),
    ('RageVEditor/assets/shaders/reflection_accumulate.rvshader', False),
):
    src, crlf, s = load(p)
    if decl:
        s = once(s, 'uint Counts[64 * 32];', 'uint Counts[64 * 64];', p + ' array')
        # The stride constant goes beside the slot count each file already has.
        s = once(s, 'const uint RAY_COUNTER_SLOTS = 64u;',
                 'const uint RAY_COUNTER_SLOTS = 64u;\n'
                 '// **How many lanes one slot holds.** Named because it was spelled 32 in\n'
                 '// four files, and growing the block meant finding all four: this is the\n'
                 '// one place a slot\'s width is written now (RayCounters::Count on the CPU\n'
                 '// side; the two must agree).\n'
                 'const uint RAY_COUNTER_STRIDE = 64u;', p + ' stride const')
    n = s.count('RayCounterSlot() * 32u')
    if n == 0:
        sys.exit(p + ': no stride use')
    s = s.replace('RayCounterSlot() * 32u', 'RayCounterSlot() * RAY_COUNTER_STRIDE')
    save(p, crlf, s)
    print('%s: %d stride use(s) named' % (p, n))
