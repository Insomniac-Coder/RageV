"""Every glowing surface aimed at, with no count limit (RT-24, 2026-09-25): a one-pass weighted
reservoir pick in the reflections, the bounce's uniform pick over every row, the list uncapped, the
diagnostic --emitter-cap removed, and the lamp pool's draw packed so no row count is limited by it."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

ROOT = (_RAGEV + '/')


def edit(path, a, b, count=1):
    p = ROOT + path
    t = io.open(p, encoding='utf-8', newline='').read()
    crlf = t.count('\r\n') > t.count('\n') // 2
    t = t.replace('\r\n', '\n')
    assert t.count(a) == count, (path, t.count(a), a[:90])
    t = t.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n') if crlf else t)


T = 'RageVEditor/assets/shaders/reflection_trace.rvshader'
# -- the pick: one pass over every row, nothing kept per row -------------------------------------
edit(T, """// `drawn` and `totalScore`: the draw itself and every row's score summed, for o_LampSample (see its
// declaration): xy the draw, z its density per square metre of the fitting, w the row times two
// (plus one on the back face) or -1 where nothing was drawn.""",
     """// `drawn` and `totalScore`: the draw itself and every row's score summed, for o_LampSample (see its
// declaration): x the draw packed, y the row times two (plus one on the back face) or -1 where
// nothing was drawn, z the draw's density per square metre of the fitting.""")
edit(T, """	drawn = vec4(0.0, 0.0, 0.0, -1.0);
	totalScore = 0.0;
	// The row, by its share of the score. Kept in a register file rather than
	// scored twice: the first build walked the rows once to total them and
	// again to find the draw, and each walk is sixteen rows times three probes
	// times a GGX evaluation -- the single largest thing this pass added, and
	// it was buying the same sixteen numbers twice.
	float score[32];
	float total = 0.0;
	for (int i = 0; i < 32; ++i)
	{
		score[i] = i < emitters ? EmitterScore(P, N, V, roughness, u_Emitters[i]) : 0.0;
		total += score[i];
	}
	if (total <= 0.0)
		return vec3(0.0);
	totalScore = total;

	const float draw = GiRandom(seed) * total;
	int pick = emitters - 1;
	float walked = 0.0;
	for (int i = 0; i < 32; ++i)
	{
		walked += score[i];
		// `score[i] > 0` as well as the sum: a draw of exactly zero would
		// otherwise land on row zero whatever it is worth, and a row worth
		// nothing has a probability of nothing -- dividing by which turns
		// one texel into a white spot.
		if (score[i] > 0.0 && walked >= draw)
		{
			pick = i;
			break;
		}
	}
	const float chance = max(score[pick], 1.0e-9) / total;
	const GiEmitter light = u_Emitters[pick];
""", """	drawn = vec4(0.0, -1.0, 0.0, 0.0);
	totalScore = 0.0;
	// **The row, by its share of the score, over every row there is (RT-24, 2026-09-25).**
	//
	// This held sixteen scores in a register file and walked them twice, so the list could hold
	// sixteen fittings and no more; the garage has twenty-three, and the seven left out were found
	// only when a lobe ray happened to strike one -- a white speck at full strength, the speckle
	// a fast swing showed (measured: all 23 aimed at took the swing's speckle from 861 to 507 and
	// the pole's jitter from 15.2 to 9.6, near the sixteen-ray reference's 494 and 8.8). Now one
	// pass and nothing stored per row: weighted reservoir sampling (Chao 1982), each row taking
	// the pick with its score over the running total, so each ends up picked with its share of
	// the whole -- the same distribution for any number of rows. Each row's coin is its own hash
	// of this draw's seed, so the stream the rest of the draw reads advances exactly as before.
	// The walk is still linear in the rows; RT2-12's light structure replaces it for many-light
	// scenes.
	const uint rowSeed = seed;
	float total = 0.0;
	int pick = -1;
	float picked = 0.0;
	for (int i = 0; i < emitters; ++i)
	{
		const float s = EmitterScore(P, N, V, roughness, u_Emitters[i]);
		if (s <= 0.0)
			continue;
		total += s;
		const float coin = float(GiHash(rowSeed ^ (uint(i) * 0x9E3779B9u)) & 0x00FFFFFFu) / 16777216.0;
		if (coin * total < s)
		{
			pick = i;
			picked = s;
		}
	}
	// The one draw the old walk made from the stream, still made, so the point on the lamp is
	// drawn from the same place in it.
	GiRandom(seed);
	if (total <= 0.0 || pick < 0)
		return vec3(0.0);
	totalScore = total;
	const float chance = picked / total;
	const GiEmitter light = u_Emitters[pick];
""")
edit(T, """	if (light.CentreArea.w > 0.0)
		drawn = vec4(grid > 0 ? aimUv : vec2(su, sv), density / light.CentreArea.w,
					 float(pick) * 2.0 + (side < 0.0 ? 1.0 : 0.0));""",
     """	if (light.CentreArea.w > 0.0)
	{
		// Twelve bits a coordinate, packed into one exactly-held float: a tenth of a millimetre on a
		// fitting a metre across. The density beside it is the draw's own, so the rounding of the
		// point moves nothing the estimate divides by.
		const uvec2 q = uvec2(clamp(grid > 0 ? aimUv : vec2(su, sv) * 0.5 + 0.5, 0.0, 1.0) * 4095.0 + 0.5);
		drawn = vec4(float((q.x << 12u) | q.y), float(pick) * 2.0 + (side < 0.0 ? 1.0 : 0.0),
					 density / light.CentreArea.w, 0.0);
	}""")
edit(T, """	vec4 lampDrawn = vec4(0.0, 0.0, 0.0, -1.0);""", """	vec4 lampDrawn = vec4(0.0, -1.0, 0.0, 0.0);""")
edit(T, """	o_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, lampDrawn.w);""",
     """	o_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, 0.0);""")
edit(T, """	o_LampSample = vec4(0.0);
""", """	o_LampSample = vec4(0.0, -1.0, 0.0, 0.0);
""")
edit(T, """	const int emitters = clamp(int(u_Lamps.Nee.x + 0.5), 0, 32);""",
     """	const int emitters = max(int(u_Lamps.Nee.x + 0.5), 0);""")
edit(T, """// drawn at: xy the draw (the aiming table's uv where the row has one, the rectangle's (su, sv) where
// it radiates evenly), z the density the draw had per square metre of the fitting, w the texel's
// score total -- every row's EmitterScore summed, so a row's chance here is its score over this.
// Thirty-two bits a lane: the resolve divides by these, and a rounded density is a biased one.
// o_Aimed's w carries the row times two (plus one where the point was stepped out to the fitting's
// back face), or -1 where no row was drawn.""", """// drawn at: x the draw (the aiming table's uv where the row has one, the rectangle's (su, sv) where
// it radiates evenly), twelve bits a coordinate packed into one exactly-held float; y the row times
// two, plus one where the point was stepped out to the fitting's back face, or -1 where no row was
// drawn; z the density the draw had per square metre of the fitting; w the texel's score total --
// every row's EmitterScore summed, so a row's chance here is its score over this. Thirty-two bits a
// lane: the resolve divides by these, and a rounded density is a biased one.""")
edit(T, """	if (light.CentreArea.w > 0.0)
	{
		// Twelve bits""", """	if (light.CentreArea.w > 0.0)
	{
		// Twelve bits""")

# -- the resolve reads the packed draw ------------------------------------------------------------
S = 'RageVEditor/assets/shaders/reflection_resolve.rvshader'
edit(S, """// o_LampSample: xy the draw, z its density per square metre of the fitting, w the texel's score
// total; u_Aimed's w the row, -1 for none), for the pooling below.""", """// o_LampSample: x the draw packed, y the row times two plus the face or -1 for none, z its density
// per square metre of the fitting, w the texel's score total), for the pooling below.""")
edit(S, """	const float code = texelFetch(u_Aimed, at, 0).w;
	const vec4 s = texelFetch(u_LampSample, at, 0);
	total = s.w;""", """	const vec4 s = texelFetch(u_LampSample, at, 0);
	const float code = s.y;
	total = s.w;""")
edit(S, """	light = u_Emitters[packed >> 1];
	side = (packed & 1) != 0 ? -1.0 : 1.0;
	y = EmitterPointFromDraw(light, s.xy, side);
	radiance = EmitterRadianceFromDraw(light, s.xy);""", """	light = u_Emitters[packed >> 1];
	side = (packed & 1) != 0 ? -1.0 : 1.0;
	// The draw, unpacked as reflection_trace packed it: the aiming table's uv, or the rectangle's
	// own (su, sv) in [-1, 1] where the row radiates evenly.
	const uint q = uint(s.x + 0.5);
	vec2 drawn = vec2(float(q >> 12u), float(q & 4095u)) / 4095.0;
	if (int(light.Aim.y + 0.5) <= 0)
		drawn = drawn * 2.0 - 1.0;
	y = EmitterPointFromDraw(light, drawn, side);
	radiance = EmitterRadianceFromDraw(light, drawn);""")

# -- the bounce: every row, still picked evenly ------------------------------------------------------
edit('RageVEditor/assets/shaders/rtgi_trace.rvshader',
     """	const int emitters = clamp(int(u_Gi.Emitters + 0.5), 0, 16);""",
     """	// Every row (RT-24, 2026-09-25): the cap of sixteen left the rest flagged as emitters, whose
	// glow the hit below subtracts, but never aimed at -- their light lost from the bounce.
	const int emitters = max(int(u_Gi.Emitters + 0.5), 0);""")

# -- the list: no cap; the diagnostic flag goes ------------------------------------------------------
edit('RageV/src/RageV/Renderer/Renderer3D.h', """		// The most rows the reflections' aimed sampler holds (reflection_trace, score[32]); the
		// list keeps EngineConfig::EmitterCap of them, 16 unless a diagnostic asks for more.
		static constexpr uint32_t kMaxAreaEmitters = 32;
""", "")
edit('RageV/src/RageV/Renderer/Renderer3D.cpp', """			if (s_Data->Emitters.size() >= Math::Min((uint32_t)Math::Max(EngineConfig::Get().EmitterCap, 0),
													 kMaxAreaEmitters))
				break;

""", "")
edit('RageV/src/RageV/Renderer/Renderer3D.cpp', """				   ? (float)Math::Min((uint32_t)s_Data->Emitters.size(), kMaxAreaEmitters) : 0.0f;""",
     """				   ? (float)s_Data->Emitters.size() : 0.0f;""", count=2)
edit('RageV/src/RageV/Core/EngineConfig.h', """		// **--emitter-cap=<n> (RT-24 diagnostic, 2026-09-25):** how many glowing surfaces the
		// area-emitter list keeps -- the rows the reflections aim at and the instances flagged as
		// emitters. 16 is today's list; the garage has 23 fittings, so 7 are never aimed at and
		// their lucky hits come back whole. Raised only to measure whether that is the speckle;
		// the bounce pass still reads its first 16 rows only, so this is a garage (baked) test.
		int EmitterCap = 16;""", "")
edit('RageV/src/RageV/Core/EngineConfig.cpp', """		// RT-24 diagnostic: how many glowing surfaces the emitter list keeps.
		if (key == "emitter-cap" || key == "emittercap")
		{
			try { config.EmitterCap = std::stoi(value); }
			catch (...) { RV_CORE_WARN("emitter-cap expects a count; got '{0}'", value); return false; }
			return true;
		}
""", "")
edit('RageV/src/RageV/Renderer/Light.h', """		// lost. Both used to happen -- see kMaxAreaEmitters in Renderer3D.h.""",
     """		// lost. Both used to happen while the list stopped at sixteen (until RT-24, 2026-09-25).""")
print('ok')
