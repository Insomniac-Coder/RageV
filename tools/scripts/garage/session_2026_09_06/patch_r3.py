"""WR-16 R3: the RT optimisation preset's MirrorRays column sets how many
neighbours' rays the resolve gathers (in eights), since each texel fires one
ray and the gathered count is the quality dial now. Off keeps today's 24."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        n = new.replace('\n', nl)
        if isinstance(old, tuple):
            a = s.index(old[0].replace('\n', nl)); b = s.index(old[1].replace('\n', nl), a); b = s.index(nl, b) + len(nl)
            s = s[:a] + n + s[b:]; continue
        o = old.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:60]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

patch('RageV/src/RageV/Renderer/RenderSettings.h', [(
"""		// **Rays a glossy pixel draws from its roughness lobe** for the traced
		// reflection (2026-09-05: one exact mirror ray per pixel was a perfect
		// mirror whatever the roughness said; the garage floor's tubes were
		// knife-sharp where the reference has soft bands). Averaged in the
		// shader; the tile allocator will scale it per tile once the mirror
		// lane exists (RAY-BUDGET-DESIGN Part III 4.3.4, lane B.g).
		float MirrorRays;
""",
"""		// **Reflection samples a glossy pixel gathers, in eights** (WR-16 R3,
		// 2026-09-06). Every pixel fires one reflection ray; the resolve then
		// gathers the neighbours' rays, re-aimed through this pixel's lobe,
		// and that count is the quality dial: 8 x this many taps (Quality 32,
		// Off 24, Balanced 16, Performance 8). A tap is about thirteen texture
		// reads; the resolve is 1.5 ms at 24 taps, 1600x900. The name is the
		// column's old one (rays per pixel, 2026-09-05), kept because saved
		// projects carry it. Past ~16 taps on a glossy floor the disc runs out
		// of texels (it is 10 x 1 texels there) and more taps land on the same
		// rays -- see R3's table in RENDERING-REVAMP.
		float MirrorRays;
"""),
("""			default:                           return { ShadowRayFalloff::Off,    0.0f,   0.0f,   0.0f,   0.0f,   1.0f, 0.0f,          8.0f, 4.0f, 3.0f, 1.0f, 0, 1 };""",
"""			default:                           return { ShadowRayFalloff::Off,    0.0f,   0.0f,   0.0f,   0.0f,   1.0f, 0.0f,          8.0f, 4.0f, 3.0f, 3.0f, 0, 1 };""")])

patch('RageVEditor/assets/shaders/reflection_resolve.rvshader', [(
"""const int kTaps = 24;
""",
"""// Taps per texel: the preset's MirrorRays column, in eights (WR-16 R3) --
// Quality 32, Off 24, Balanced 16, Performance 8. Read in main.
const int kMaxTaps = 64;
"""),
("""	vec3 sum = vec3(0.0);
	float distanceSum = 0.0;
	float weightSum = 0.0;
	for (int i = -1; i < kTaps; ++i)
	{
		ivec2 at = texel;
		float r = 0.0;
		if (i >= 0)
		{
			r = sqrt((float(i) + 0.5) / float(kTaps));
""",
"""	const int taps = min(clamp(int(u_Scene.Indirect.w + 0.5), 1, 8) * 8, kMaxTaps);

	vec3 sum = vec3(0.0);
	float distanceSum = 0.0;
	float weightSum = 0.0;
	for (int i = -1; i < taps; ++i)
	{
		ivec2 at = texel;
		float r = 0.0;
		if (i >= 0)
		{
			r = sqrt((float(i) + 0.5) / float(taps));
""")])

patch('RageVEditor/assets/shaders/reflection_trace.rvshader', [(
("""	int count = clamp(int(u_Scene.Indirect.w + 0.5), 1, 8);
""", """	count = 1;
"""),
"""	// One ray a texel. The preset's MirrorRays column, which used to be the
	// count drawn here, sets how many neighbours' rays the resolve gathers
	// instead (WR-16 R3, 2026-09-06); the tile allocator's per-tile scaling
	// of that count went with it and would return as a scaling of the taps.
""")])
print('R3 patched')
