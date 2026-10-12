"""Speckle diagnostics: --emitter-cap (how many glowing surfaces the reflections aim at; 16 = today),
the lamp pool's default to 8, a row code that holds any row, and the two staged arms."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

ROOT = (_RAGEV + '/')


def edit(path, a, b, count=1):
    p = ROOT + path
    t = io.open(p, encoding='utf-8', newline='').read()
    crlf = t.count('\r\n') > t.count('\n') // 2
    t = t.replace('\r\n', '\n')
    assert t.count(a) == count, (path, t.count(a), a[:80])
    t = t.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n') if crlf else t)


# -- the lamp pool: 8 by default (owner, 2026-09-25) ---------------------------------------------
edit('RageV/src/RageV/Core/EngineConfig.h', """		// Zero keeps each texel's own aimed sample alone (fix 1 of 2026-09-24). At most eight.
		int ReflectionLampPool = 4;""", """		// Zero keeps each texel's own aimed sample alone (fix 1 of 2026-09-24). At most eight;
		// eight by the owner's choice after judging it live (2026-09-25; +2.1 ms at 1600x900).
		int ReflectionLampPool = 8;
		// **--emitter-cap=<n> (RT-24 diagnostic, 2026-09-25):** how many glowing surfaces the
		// area-emitter list keeps -- the rows the reflections aim at and the instances flagged as
		// emitters. 16 is today's list; the garage has 23 fittings, so 7 are never aimed at and
		// their lucky hits come back whole. Raised only to measure whether that is the speckle;
		// the bounce pass still reads its first 16 rows only, so this is a garage (baked) test.
		int EmitterCap = 16;""")
edit('RageV/src/RageV/Core/EngineConfig.cpp', """		// RT-24: how many neighbours' lamp points a reflection texel pools.""", """		// RT-24 diagnostic: how many glowing surfaces the emitter list keeps.
		if (key == "emitter-cap" || key == "emittercap")
		{
			try { config.EmitterCap = std::stoi(value); }
			catch (...) { RV_CORE_WARN("emitter-cap expects a count; got '{0}'", value); return false; }
			return true;
		}
		// RT-24: how many neighbours' lamp points a reflection texel pools.""")

# -- the list: up to the cap, which may now pass 16 (the reflections' shader holds 32) ------------
edit('RageV/src/RageV/Renderer/Renderer3D.h', """		static constexpr uint32_t kMaxAreaEmitters = 16;""",
     """		// The most rows the reflections' aimed sampler holds (reflection_trace, score[32]); the
		// list keeps EngineConfig::EmitterCap of them, 16 unless a diagnostic asks for more.
		static constexpr uint32_t kMaxAreaEmitters = 32;""")
edit('RageV/src/RageV/Renderer/Renderer3D.cpp', """			if (s_Data->Emitters.size() >= kMaxAreaEmitters)
				break;""", """			if (s_Data->Emitters.size() >= Math::Min((uint32_t)Math::Max(EngineConfig::Get().EmitterCap, 0),
													 kMaxAreaEmitters))
				break;""")
edit('RageV/src/RageV/Renderer/Renderer3D.cpp', """				   ? (float)Math::Min((uint32_t)s_Data->Emitters.size(), 16u) : 0.0f;""",
     """				   ? (float)Math::Min((uint32_t)s_Data->Emitters.size(), kMaxAreaEmitters) : 0.0f;""", count=2)

# -- the trace: 32 rows; the row code packs the side in its lowest bit ------------------------------
T = 'RageVEditor/assets/shaders/reflection_trace.rvshader'
edit(T, """	float score[16];
	float total = 0.0;
	for (int i = 0; i < 16; ++i)""", """	float score[32];
	float total = 0.0;
	for (int i = 0; i < 32; ++i)""")
edit(T, """	for (int i = 0; i < 16; ++i)
	{
		walked += score[i];""", """	for (int i = 0; i < 32; ++i)
	{
		walked += score[i];""")
edit(T, """	const int emitters = clamp(int(u_Lamps.Nee.x + 0.5), 0, 16);""",
     """	const int emitters = clamp(int(u_Lamps.Nee.x + 0.5), 0, 32);""")
edit(T, """					 float(pick) + (side < 0.0 ? 16.0 : 0.0));""",
     """					 float(pick) * 2.0 + (side < 0.0 ? 1.0 : 0.0));""")
edit(T, """// declaration): xy the draw, z its density per square metre of the fitting, w the row (plus 16 on
// the back face) or -1 where nothing was drawn.""", """// declaration): xy the draw, z its density per square metre of the fitting, w the row times two
// (plus one on the back face) or -1 where nothing was drawn.""")
edit(T, """// o_Aimed's w carries the row (plus 16 where the point was stepped out to the fitting's back face),
// or -1 where no row was drawn.""", """// o_Aimed's w carries the row times two (plus one where the point was stepped out to the fitting's
// back face), or -1 where no row was drawn.""")

S = 'RageVEditor/assets/shaders/reflection_resolve.rvshader'
edit(S, """	light = u_Emitters[packed & 15];
	side = packed >= 16 ? -1.0 : 1.0;""", """	light = u_Emitters[packed >> 1];
	side = (packed & 1) != 0 ? -1.0 : 1.0;""")

# -- the staged arms ---------------------------------------------------------------------------------
W = 'tools/scripts/garage/watch_arm.py'
edit(W, """# RT-24 A/B (2026-09-25): fix 1 undone""", """# RT-24 speckle diagnostic (2026-09-25): the reflection's fewest frames kept at its still value under
# any motion -- RT-24's fix 2 undone -- to measure how much of the fast-swing speckle is the memory
# falling to one frame. (Fix 2 was for the ghosting; this is a measurement, not a candidate.)
VARIANTS['nofix2'] = ('reflection_accumulate.rvshader',
    '\\t\\t\\tconst float fewest = max(mix(1.0, max(u_Reflection.History.z, 1.0), gloss)\\n\\t\\t\\t\\t\\t\\t\\t\\t\\t / (1.0 + max(moved - slack, 0.0) / slack), 1.0);',
    '\\t\\t\\tconst float fewest = mix(1.0, max(u_Reflection.History.z, 1.0), gloss);')

# RT-24 A/B (2026-09-25): fix 1 undone""")
print('ok')
