"""RT-2.1 (b): `--terrain-lod-error=<ratio>`, a measurement flag for the
terrain's ground veto (Terrain::kLevelErrorRatio, 0.0003), so the veto's
share of the G-buffer pass can be measured without a rebuild."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')


def load(p):
    s = open(p, 'rb').read().decode('utf-8')
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(s, nl, old, new):
    for ending in (nl, '\n' if nl == '\r\n' else '\r\n'):
        o = old.replace('\n', ending)
        if s.count(o) == 1:
            return s.replace(o, new.replace('\n', ending))
    raise AssertionError('anchor not unique or missing: ' + old[:90])


out = {}

p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
assert 'TerrainLevelError' not in s
s = rep(s, nl, """		bool  AoSignal = true;
""", """		bool  AoSignal = true;
		// --terrain-lod-error=<ratio>: the terrain's ground veto, the LOD
		// error a chunk may carry as a fraction of its distance
		// (Terrain::kLevelErrorRatio when 0, the default). A measurement
		// flag (RT-2.1): the veto exists for the ray tracer, which traces
		// level 0 whatever is drawn, so loosening it is a cost experiment
		// and not a setting.
		float TerrainLevelError = 0.0f;
""")
m = re.search(r'^(//\s+--ao-signal[^\r\n]*)$', s, re.M)
if m:
    line = m.group(0)
    s = s.replace(line, line + nl + "//   --terrain-lod-error=R    the terrain veto's error ratio (measurement; 0 = the engine's)", 1)
    print('doc line added after:', line.strip()[:60])
else:
    print('no --ao-signal doc line in the header; skipped the key list')
out[p] = s

p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
assert 'terrain-lod-error' not in s
s = rep(s, nl, """		if (key == "ao-signal" || key == "aosignal")
			return ParseBool(value, config.AoSignal);
""", """		if (key == "ao-signal" || key == "aosignal")
			return ParseBool(value, config.AoSignal);

		if (key == "terrain-lod-error" || key == "terrainloderror")
		{
			try
			{
				const float ratio = std::stof(value);
				config.TerrainLevelError = ratio > 0.0f ? ratio : 0.0f;
			}
			catch (...)
			{
				RV_CORE_WARN("terrain-lod-error expects a ratio, got '{0}'", value);
				return false;
			}
			return true;
		}
""")
out[p] = s

p = 'RageV/src/RageV/Renderer/Terrain.cpp'; s, nl = load(p)
assert 'TerrainLevelError' not in s
s = rep(s, nl, """#include "RageV/Core/Log.h"
""", """#include "RageV/Core/Log.h"
#include "RageV/Core/EngineConfig.h"
""")
s = rep(s, nl, """		m_LodReport = LodReport{};
		m_LodReport.Chunks = (uint32_t)m_Chunks.size();
""", """		m_LodReport = LodReport{};
		m_LodReport.Chunks = (uint32_t)m_Chunks.size();
		// The veto's ratio: the engine's, or the measurement flag's (RT-2.1).
		const float errorRatio = EngineConfig::Get().TerrainLevelError > 0.0f
			? EngineConfig::Get().TerrainLevelError : kLevelErrorRatio;
""")
s = rep(s, nl, """			const float budget = distance * kLevelErrorRatio;
""", """			const float budget = distance * errorRatio;
""")
out[p] = s

for p, s in out.items():
    open(p, 'wb').write(s.encode('utf-8'))
    print('ok', p)
print('RT-2.1b flag patched')
