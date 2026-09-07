"""RT-2.1 (a): the terrain's levels of detail as a benchmark line.

Terrain::SelectLod fills a LodReport (what the distance rule wanted, how many
chunks the ground's veto and the neighbour cap then held finer, where they
ended up); the scene's terrain draw counts the chunks drawn per level with
their triangles into Renderer3D's TerrainStats; the benchmark prints one line.
Two-phase: every replacement is computed before any file is saved."""
import os
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

# --- Terrain.h ---------------------------------------------------------------
p = 'RageV/src/RageV/Renderer/Terrain.h'; s, nl = load(p)
assert 'LodReport' not in s
s = rep(s, nl, """		bool SkirtsDrawn() const { return m_SkirtsDrawn; }
""", """		bool SkirtsDrawn() const { return m_SkirtsDrawn; }

		// What the last SelectLod decided and why, for the benchmark's terrain
		// line (RT-2.1): how many chunks the distance rule alone would have
		// put at each level, how many of them the ground's veto then held
		// finer, how many the neighbour cap did, and where they ended up.
		struct LodReport
		{
			uint32_t Chunks = 0;
			uint32_t ByDistance[kLevels] = {};
			uint32_t Final[kLevels] = {};
			uint32_t Vetoed = 0;
			uint32_t Capped = 0;
		};
		const LodReport& GetLodReport() const { return m_LodReport; }
""")
s = rep(s, nl, """		bool m_SkirtsDrawn = true;
""", """		bool m_SkirtsDrawn = true;
		LodReport m_LodReport;
		// The levels before the neighbour cap, so the report can say how many
		// chunks it moved; a member so that no frame allocates it.
		std::vector<int> m_LevelsBeforeCap;
""")
out[p] = s

# --- Terrain.cpp -------------------------------------------------------------
p = 'RageV/src/RageV/Renderer/Terrain.cpp'; s, nl = load(p)
assert 'm_LodReport' not in s
s = rep(s, nl, """		const float width = GetChunkWidth() * scale;
""", """		const float width = GetChunkWidth() * scale;
		m_LodReport = LodReport{};
		m_LodReport.Chunks = (uint32_t)m_Chunks.size();
""")
s = rep(s, nl, """			int level = LevelFor(distance, width);
			const float budget = distance * kLevelErrorRatio;
			while (level > 0 && chunk.LevelError[level] * scale > budget)
				--level;
			chunk.Level = level;
""", """			const int wanted = LevelFor(distance, width);
			int level = wanted;
			const float budget = distance * kLevelErrorRatio;
			while (level > 0 && chunk.LevelError[level] * scale > budget)
				--level;
			chunk.Level = level;
			m_LodReport.ByDistance[wanted]++;
			if (level < wanted)
				m_LodReport.Vetoed++;
""")
s = rep(s, nl, """		// the loop is bounded by the level count either way.
		for (int pass = 0; pass < kLevels; ++pass)
""", """		// the loop is bounded by the level count either way.
		m_LevelsBeforeCap.resize(m_Chunks.size());
		for (size_t i = 0; i < m_Chunks.size(); ++i)
			m_LevelsBeforeCap[i] = m_Chunks[i].Level;
		for (int pass = 0; pass < kLevels; ++pass)
""")
s = rep(s, nl, """		// The skirts, only from above the ground. A skirt is a vertical drop
""", """		for (size_t i = 0; i < m_Chunks.size(); ++i)
		{
			m_LodReport.Final[m_Chunks[i].Level]++;
			if (m_Chunks[i].Level < m_LevelsBeforeCap[i])
				m_LodReport.Capped++;
		}

		// The skirts, only from above the ground. A skirt is a vertical drop
""")
out[p] = s

# --- Renderer3D.h ------------------------------------------------------------
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
assert 'TerrainStats' not in s
s = rep(s, nl, """		static unsigned int GetTriangleCount();
		static unsigned int GetIndirectDrawCount();
""", """		static unsigned int GetTriangleCount();
		static unsigned int GetIndirectDrawCount();

		// The terrain's levels of detail this frame (RT-2.1's measurement):
		// the chunks drawn at each level with their triangles, and what the
		// level rule decided -- how many chunks distance alone would have put
		// at each level, how many the ground's error veto then held finer,
		// how many the neighbour cap did. Counted by the scene's terrain draw
		// once per camera view; the pending draws are rasterised by the
		// G-buffer half and the lit half both, so a chunk here is two draws.
		static constexpr int kTerrainLevels = 4;
		struct TerrainStats
		{
			uint32_t Chunks = 0;
			uint32_t Drawn[kTerrainLevels] = {};
			uint32_t Triangles[kTerrainLevels] = {};
			uint32_t ByDistance[kTerrainLevels] = {};
			uint32_t Vetoed = 0;
			uint32_t Capped = 0;
		};
		static void CountTerrainChunk(int level, uint32_t triangles);
		static void ReportTerrainLod(uint32_t chunks, const uint32_t* byDistance,
									 uint32_t vetoed, uint32_t capped);
		static const TerrainStats& GetTerrainStats();
""")
out[p] = s

# --- Renderer3D.cpp ----------------------------------------------------------
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
assert 'TerrainStats' not in s
s = rep(s, nl, """			unsigned int IndirectDraws = 0;

			bool Ready = false;
""", """			unsigned int IndirectDraws = 0;
			// The terrain's levels this frame (RT-2.1), see Renderer3D.h.
			Renderer3D::TerrainStats Terrain;

			bool Ready = false;
""")
s = rep(s, nl, """		s_Data->Culled = 0;
		s_Data->IndirectDraws = 0;
""", """		s_Data->Culled = 0;
		s_Data->IndirectDraws = 0;
		s_Data->Terrain = TerrainStats{};
""")
s = rep(s, nl, """	unsigned int Renderer3D::GetIndirectDrawCount() { return s_Data ? s_Data->IndirectDraws : 0; }
""", """	unsigned int Renderer3D::GetIndirectDrawCount() { return s_Data ? s_Data->IndirectDraws : 0; }

	void Renderer3D::CountTerrainChunk(int level, uint32_t triangles)
	{
		if (!s_Data || level < 0 || level >= kTerrainLevels)
			return;
		s_Data->Terrain.Drawn[level]++;
		s_Data->Terrain.Triangles[level] += triangles;
	}

	void Renderer3D::ReportTerrainLod(uint32_t chunks, const uint32_t* byDistance,
									  uint32_t vetoed, uint32_t capped)
	{
		if (!s_Data)
			return;
		s_Data->Terrain.Chunks += chunks;
		for (int i = 0; i < kTerrainLevels; i++)
			s_Data->Terrain.ByDistance[i] += byDistance[i];
		s_Data->Terrain.Vetoed += vetoed;
		s_Data->Terrain.Capped += capped;
	}

	const Renderer3D::TerrainStats& Renderer3D::GetTerrainStats()
	{
		static const TerrainStats kNone;
		return s_Data ? s_Data->Terrain : kNone;
	}
""")
out[p] = s

# --- Scene.cpp ---------------------------------------------------------------
p = 'RageV/src/RageV/Scene/Scene.cpp'; s, nl = load(p)
assert 'CountTerrainChunk' not in s
s = rep(s, nl, """						Renderer3D::DrawLayeredMesh(mesh, transform.World, layers,
													ProbeSlotFor(centre), component.Static,
													terrain.DrawIndexCount(chunk),
													&transform.PreviousWorld);
					}
				});
""", """						Renderer3D::DrawLayeredMesh(mesh, transform.World, layers,
													ProbeSlotFor(centre), component.Static,
													terrain.DrawIndexCount(chunk),
													&transform.PreviousWorld);
						Renderer3D::CountTerrainChunk(chunk.Level, terrain.DrawIndexCount(chunk) / 3);
					}

					// The benchmark's terrain line (RT-2.1): what the level
					// rule decided for this camera, beside what was drawn.
					static_assert(Terrain::kLevels == Renderer3D::kTerrainLevels,
								  "the terrain's level count and the renderer's stats disagree");
					const Terrain::LodReport& report = terrain.GetLodReport();
					Renderer3D::ReportTerrainLod(report.Chunks, report.ByDistance,
												 report.Vetoed, report.Capped);
				});
""")
out[p] = s

# --- FrameProfiler.cpp -------------------------------------------------------
p = 'RageV/src/RageV/Core/FrameProfiler.cpp'; s, nl = load(p)
assert 'GetTerrainStats' not in s
s = rep(s, nl, """					 Renderer3D::GetDrawCallCount(), Renderer3D::GetIndirectDrawCount(),
					 Renderer3D::GetTriangleCount(), Renderer3D::GetCulledCount());
""", """					 Renderer3D::GetDrawCallCount(), Renderer3D::GetIndirectDrawCount(),
					 Renderer3D::GetTriangleCount(), Renderer3D::GetCulledCount());
		// The terrain's levels of detail (RT-2.1): what was drawn at each
		// level, and why the level rule held chunks finer than distance alone
		// would have. Each chunk here is rasterised by the G-buffer half and
		// the lit half both.
		const Renderer3D::TerrainStats& terrain = Renderer3D::GetTerrainStats();
		if (terrain.Chunks > 0)
		{
			RV_CORE_INFO("[benchmark]   terrain: {0} chunks; drawn L0 {1} ({2} tris), L1 {3} ({4}), "
						 "L2 {5} ({6}), L3 {7} ({8}); by distance alone L0 {9} / L1 {10} / "
						 "L2 {11} / L3 {12}; the ground's veto held {13} finer, the neighbour cap {14}",
						 terrain.Chunks, terrain.Drawn[0], terrain.Triangles[0],
						 terrain.Drawn[1], terrain.Triangles[1], terrain.Drawn[2], terrain.Triangles[2],
						 terrain.Drawn[3], terrain.Triangles[3],
						 terrain.ByDistance[0], terrain.ByDistance[1], terrain.ByDistance[2],
						 terrain.ByDistance[3], terrain.Vetoed, terrain.Capped);
		}
""")
out[p] = s

for p, s in out.items():
    open(p, 'wb').write(s.encode('utf-8'))
    print('ok', p)
print('RT-2.1a instrument patched')
