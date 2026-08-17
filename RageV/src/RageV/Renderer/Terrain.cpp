#include <rvpch.h>
#include "Terrain.h"
#include "Renderer.h"
#include "Renderer3D.h"
#include "TextureLoader.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/Log.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace RageV
{
	namespace
	{
		float HeightMetres(const TerrainData& data, const Terrain::Dimensions& dims,
						   uint32_t sx, uint32_t sz)
		{
			return (float)data.At(sx, sz) / 65535.0f * dims.Height;
		}

		// Central differences of the heights, in metres, one-sided at the
		// grid's edge. The true slope of the surface at the sample: every
		// level carries the same normal at a shared vertex, so a level change
		// pops geometry and not lighting.
		Vec3 NormalAt(const TerrainData& data, const Terrain::Dimensions& dims,
					  uint32_t sx, uint32_t sz, float cell)
		{
			const uint32_t last = data.Resolution - 1;
			const uint32_t xl = sx > 0 ? sx - 1 : sx;
			const uint32_t xr = sx < last ? sx + 1 : sx;
			const uint32_t zl = sz > 0 ? sz - 1 : sz;
			const uint32_t zr = sz < last ? sz + 1 : sz;

			const float dhdx = (HeightMetres(data, dims, xr, sz) - HeightMetres(data, dims, xl, sz))
							 / ((float)(xr - xl) * cell);
			const float dhdz = (HeightMetres(data, dims, sx, zr) - HeightMetres(data, dims, sx, zl))
							 / ((float)(zr - zl) * cell);
			return Math::Normalize(Vec3(-dhdx, 1.0f, -dhdz));
		}
	}

	Vec4 Terrain::WeightUvFor(const Dimensions& dims, uint32_t resolution)
	{
		const float r = (float)Math::Max(resolution, 2u);
		const float scale = dims.TextureScale / Math::Max(dims.Size, 1e-6f) * (r - 1.0f) / r;
		return Vec4(scale, scale, 0.5f, 0.5f);
	}

	int Terrain::LevelFor(float distance, float chunkWidth)
	{
		if (chunkWidth <= 0.0f || distance <= 0.0f)
			return 0;
		const float ratio = distance / (4.0f * chunkWidth);
		if (ratio < 1.0f)
			return 0;
		const int level = (int)std::floor(std::log2(ratio)) + 1;
		return Math::Clamp(level, 0, kLevels - 1);
	}

	uint32_t Terrain::BuildChunkGeometry(const TerrainData& data, const Dimensions& dims,
										 uint32_t chunkX, uint32_t chunkZ, int level,
										 std::vector<MeshVertex>& vertices,
										 std::vector<uint32_t>& indices, bool skirts)
	{
		vertices.clear();
		indices.clear();
		if (!data.IsValid())
			return 0;

		const uint32_t quads = data.QuadCount();
		const uint32_t chunkQuads = Math::Min(kChunkQuads, quads);
		const uint32_t step = 1u << (uint32_t)Math::Clamp(level, 0, kLevels - 1);
		if (chunkQuads % step != 0)
			return 0;
		const uint32_t n = chunkQuads / step;   // quads a side at this level
		const float cell = dims.Size / (float)quads;

		const uint32_t sample0X = chunkX * chunkQuads;
		const uint32_t sample0Z = chunkZ * chunkQuads;
		if (sample0X + chunkQuads > quads || sample0Z + chunkQuads > quads)
			return 0;

		const float half = dims.Size * 0.5f;

		// --- the surface -----------------------------------------------------
		vertices.reserve((size_t)(n + 1) * (n + 1) + (skirts ? 4 * (n + 1) : 0));
		indices.reserve((size_t)6 * n * n + (skirts ? 48 * n : 0));

		for (uint32_t iz = 0; iz <= n; ++iz)
		{
			for (uint32_t ix = 0; ix <= n; ++ix)
			{
				const uint32_t sx = sample0X + ix * step;
				const uint32_t sz = sample0Z + iz * step;
				MeshVertex v;
				v.Position = Vec3(-half + (float)sx * cell,
								  HeightMetres(data, dims, sx, sz),
								  -half + (float)sz * cell);
				v.Normal = NormalAt(data, dims, sx, sz, cell);
				v.TexCoord = Vec2(v.Position.x / dims.TextureScale,
								  v.Position.z / dims.TextureScale);
				vertices.push_back(v);
			}
		}

		// The (x, z) -> (x + 1, z + 1) diagonal, Jolt's split, wound so the
		// geometric normal is +Y (the convention Primitives::Plane sets).
		for (uint32_t iz = 0; iz < n; ++iz)
		{
			for (uint32_t ix = 0; ix < n; ++ix)
			{
				const uint32_t i00 = iz * (n + 1) + ix;
				const uint32_t i10 = i00 + 1;
				const uint32_t i01 = i00 + (n + 1);
				const uint32_t i11 = i01 + 1;
				indices.push_back(i00); indices.push_back(i01); indices.push_back(i11);
				indices.push_back(i00); indices.push_back(i11); indices.push_back(i10);
			}
		}

		// Where the surface ends and the skirts begin: what a frame with the
		// camera under the ground draws up to, and no further.
		const uint32_t surfaceIndices = (uint32_t)indices.size();

		if (!skirts)
			return surfaceIndices;

		// --- the skirts ------------------------------------------------------
		// The chunk's edge vertices again, dropped by half the chunk's own
		// height range plus a sliver of the terrain's, joined to the edge by
		// a strip wound *both* ways so it is not culled from either side. The
		// range is over every sample in the chunk, not this level's, so each
		// level's skirt hangs the same and the coarser levels stay inside the
		// finest level's bounds. Both windings are what makes the strip a
		// wall from under the ground, which is why the skirts are drawn only
		// while the camera is above it (SelectLod, DrawIndexCount).
		//
		// Only on edges a neighbouring chunk shares: a crack needs two levels
		// meeting, and the terrain's outer edge meets nothing. A skirt there
		// would be a curtain hanging off the world's rim, seen from outside.
		const uint32_t chunksPerSide = quads / chunkQuads;
		const bool skirtNearZ = chunkZ > 0;
		const bool skirtFarZ = chunkZ + 1 < chunksPerSide;
		const bool skirtNearX = chunkX > 0;
		const bool skirtFarX = chunkX + 1 < chunksPerSide;
		if (!skirtNearZ && !skirtFarZ && !skirtNearX && !skirtFarX)
			return surfaceIndices;
		float low = std::numeric_limits<float>::max();
		float high = std::numeric_limits<float>::lowest();
		for (uint32_t sz = sample0Z; sz <= sample0Z + chunkQuads; ++sz)
		{
			for (uint32_t sx = sample0X; sx <= sample0X + chunkQuads; ++sx)
			{
				const float h = HeightMetres(data, dims, sx, sz);
				low = Math::Min(low, h);
				high = Math::Max(high, h);
			}
		}
		const float depth = Math::Max(0.5f * (high - low) + 0.02f * dims.Height, 0.01f);

		auto strip = [&](auto topIndexOf)
		{
			// The dropped copies first, then the strip between them and the
			// edge they copy.
			const uint32_t base = (uint32_t)vertices.size();
			for (uint32_t i = 0; i <= n; ++i)
			{
				MeshVertex v = vertices[topIndexOf(i)];
				v.Position.y -= depth;
				vertices.push_back(v);
			}
			for (uint32_t i = 0; i < n; ++i)
			{
				const uint32_t t0 = topIndexOf(i);
				const uint32_t t1 = topIndexOf(i + 1);
				const uint32_t b0 = base + i;
				const uint32_t b1 = base + i + 1;
				// One way ...
				indices.push_back(t0); indices.push_back(t1); indices.push_back(b1);
				indices.push_back(t0); indices.push_back(b1); indices.push_back(b0);
				// ... and the other.
				indices.push_back(t0); indices.push_back(b1); indices.push_back(t1);
				indices.push_back(t0); indices.push_back(b0); indices.push_back(b1);
			}
		};

		if (skirtNearZ) strip([n](uint32_t i) { return i; });                    // z = 0 edge
		if (skirtFarZ)  strip([n](uint32_t i) { return n * (n + 1) + i; });      // z = n edge
		if (skirtNearX) strip([n](uint32_t i) { return i * (n + 1); });          // x = 0 edge
		if (skirtFarX)  strip([n](uint32_t i) { return i * (n + 1) + n; });      // x = n edge
		return surfaceIndices;
	}

	RHI::Ref<Terrain> Terrain::Create(RHI::RHIDevice* device, const TerrainData& data,
									  AssetHandle asset, const Dimensions& dimensions)
	{
		if (!data.IsValid())
			return nullptr;

		auto terrain = std::make_shared<Terrain>();
		terrain->m_Device = device;
		terrain->m_Data = data;
		terrain->m_Asset = asset;
		terrain->m_Dimensions = dimensions;
		terrain->m_Dimensions.Size = Math::Max(dimensions.Size, 1.0f);
		terrain->m_Dimensions.Height = Math::Max(dimensions.Height, 0.0f);
		terrain->m_Dimensions.TextureScale = Math::Max(dimensions.TextureScale, 0.05f);

		const uint32_t quads = data.QuadCount();
		terrain->m_ChunkQuads = Math::Min(kChunkQuads, quads);
		terrain->m_ChunksPerSide = quads / terrain->m_ChunkQuads;

		terrain->m_Chunks.reserve((size_t)terrain->m_ChunksPerSide * terrain->m_ChunksPerSide);
		for (uint32_t cz = 0; cz < terrain->m_ChunksPerSide; ++cz)
		{
			for (uint32_t cx = 0; cx < terrain->m_ChunksPerSide; ++cx)
			{
				Chunk chunk;
				chunk.SampleX = cx * terrain->m_ChunkQuads;
				chunk.SampleZ = cz * terrain->m_ChunkQuads;
				terrain->RefreshBounds(chunk);
				for (int level = 0; level < kLevels; ++level)
					terrain->BuildLevel(chunk, cx, cz, level);
				terrain->m_Chunks.push_back(std::move(chunk));
			}
		}
		terrain->RefreshWholeBounds();

		// The paint (7aq): the asset's weights as an RGBA8 texture on the
		// heights' grid, or the 1x1 "all layer 0" texel when there are none --
		// the same picture, so a stage-1 terrain and an unpainted one draw
		// through the same code and look the same.
		if (device)
		{
			if (data.HasWeights())
			{
				RHI::TextureDesc desc;
				desc.Width = data.Resolution;
				desc.Height = data.Resolution;
				desc.Format = RHI::Format::R8G8B8A8_UNORM;
				desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
				desc.DebugName = "Terrain weights";
				terrain->m_WeightMap = device->CreateTexture(desc);
				terrain->m_WeightMap->Upload(data.Weights.data(), data.Weights.size());
			}
			else
			{
				terrain->m_WeightMap = TextureLoader::Red(*device);
			}

			terrain->m_Layers = std::make_shared<LayeredMaterial>(*device, "Terrain layers");
			terrain->m_Layers->SetWeights(terrain->m_WeightMap,
										  WeightUvFor(terrain->m_Dimensions, data.Resolution));
		}

		return terrain;
	}

	const RHI::Ref<LayeredMaterial>& Terrain::RefreshLayers(const TerrainComponent& component)
	{
		if (!m_Layers)
			return m_Layers;

		// Layer 0 is the material a stage-1 terrain had, and empty means the
		// renderer's default as it always did; the other three empty means
		// "not a layer", and their weight is dropped from the normalisation.
		RHI::Ref<Material> base = Assets::Manager::GetMaterial(component.Material);
		if (!base)
			base = Renderer3D::GetDefaultMaterial();
		m_Layers->SetLayer(0, base);
		m_Layers->SetLayer(1, Assets::Manager::GetMaterial(component.Layer1));
		m_Layers->SetLayer(2, Assets::Manager::GetMaterial(component.Layer2));
		m_Layers->SetLayer(3, Assets::Manager::GetMaterial(component.Layer3));
		m_Layers->Refresh(Renderer3D::GetTextureHeap());
		return m_Layers;
	}

	const RHI::Ref<Terrain>& Terrain::Resolve(TerrainComponent& component)
	{
		static const RHI::Ref<Terrain> kNone;

		const TerrainData* data = Assets::Manager::GetTerrain(component.Terrain);
		if (!data)
		{
			component.Runtime = nullptr;
			return kNone;
		}

		Dimensions dims;
		dims.Size = component.Size;
		dims.Height = component.Height;
		dims.TextureScale = component.TextureScale;

		if (component.Runtime && component.Runtime->Matches(component.Terrain, dims))
			return component.Runtime;

		// A new object, never a rebuild of the shared one: a duplicate of the
		// entity that changes its Size must not reshape the original.
		RHI::RHIDevice* device = Renderer::HasDevice() ? &Renderer::GetDevice() : nullptr;
		component.Runtime = Create(device, *data, component.Terrain, dims);
		if (component.Runtime)
		{
			RV_CORE_INFO("Terrain: built {0} chunk(s) at {1} levels from a {2}^2 grid, {3} m a side",
						 component.Runtime->m_Chunks.size(), kLevels, data->Resolution, dims.Size);
		}
		return component.Runtime;
	}

	void Terrain::SelectLod(const Vec3& cameraWorld, const Mat4& world)
	{
		// The chunk's width in world metres: its local width through the
		// matrix's largest axis. Non-uniform scale is legal and this is the
		// conservative reading of it.
		const float scale = Math::Max(Math::Length(Vec3(world[0])),
									  Math::Length(Vec3(world[1])),
									  Math::Length(Vec3(world[2])));
		const float width = GetChunkWidth() * scale;

		for (Chunk& chunk : m_Chunks)
		{
			const Vec3 centre = Vec3(world * Vec4(chunk.Centre, 1.0f));
			chunk.Level = LevelFor(Math::Distance(cameraWorld, centre), width);
		}

		// The skirts, only from above the ground. A skirt is a vertical drop
		// from an edge whose height is the ground at that (x, z): everything
		// below the edge is inside the ground, and a camera above the surface
		// reaches it only through a crack -- the job. A camera *under* the
		// surface sees the surface's back faces culled and every skirt as a
		// wall along its seam. So: the camera in terrain space against the
		// surface at its own (x, z), which HeightAt clamps to the extent, so
		// a camera off the rim compares with the rim nearest it.
		const Vec3 local = Vec3(Math::Inverse(world) * Vec4(cameraWorld, 1.0f));
		m_SkirtsDrawn = local.y >= HeightAt(local.x, local.z);

		// The level each chunk will draw this frame is the one that must not
		// be stale; the others wait for the stroke's release (7ar).
		RebuildStale(false);
	}

	uint32_t Terrain::DrawIndexCount(const Chunk& chunk) const
	{
		const RHI::Ref<Mesh>& mesh = chunk.Selected();
		if (!mesh)
			return 0;
		return m_SkirtsDrawn ? mesh->GetIndexCount() : chunk.SurfaceIndices[chunk.Level];
	}

	// --- editing (7ar) ------------------------------------------------------------

	void Terrain::BuildLevel(Chunk& chunk, uint32_t chunkX, uint32_t chunkZ, int level) const
	{
		chunk.Levels[level] = nullptr;
		chunk.SurfaceIndices[level] = 0;
		if (!m_Device)
			return;
		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		const uint32_t surface = BuildChunkGeometry(m_Data, m_Dimensions, chunkX, chunkZ, level,
													vertices, indices);
		if (vertices.empty())
			return;
		chunk.Levels[level] = std::make_shared<Mesh>(*m_Device, vertices, indices,
													 "Terrain chunk " + std::to_string(chunkX) + "," +
													 std::to_string(chunkZ) + " L" + std::to_string(level));
		chunk.SurfaceIndices[level] = surface;
	}

	void Terrain::RefreshBounds(Chunk& chunk) const
	{
		// From the heights, not from a mesh: the box has to be right before
		// any level is rebuilt, or a chunk raised above its old box culls
		// while it is being raised. The skirts hang below the lowest sample
		// by the same rule the builder uses; the terrain's outer rim wears
		// none, but a chunk that shares any edge does.
		const float cell = GetCellSize();
		const float half = m_Dimensions.Size * 0.5f;
		float low = std::numeric_limits<float>::max();
		float high = std::numeric_limits<float>::lowest();
		for (uint32_t sz = chunk.SampleZ; sz <= chunk.SampleZ + m_ChunkQuads; ++sz)
		{
			for (uint32_t sx = chunk.SampleX; sx <= chunk.SampleX + m_ChunkQuads; ++sx)
			{
				const float h = HeightMetres(m_Data, m_Dimensions, sx, sz);
				low = Math::Min(low, h);
				high = Math::Max(high, h);
			}
		}
		const float depth = m_ChunksPerSide > 1
			? Math::Max(0.5f * (high - low) + 0.02f * m_Dimensions.Height, 0.01f) : 0.0f;

		chunk.Bounds.Min = Vec3(-half + (float)chunk.SampleX * cell, low - depth,
								-half + (float)chunk.SampleZ * cell);
		chunk.Bounds.Max = Vec3(-half + (float)(chunk.SampleX + m_ChunkQuads) * cell, high,
								-half + (float)(chunk.SampleZ + m_ChunkQuads) * cell);
		chunk.Centre = chunk.Bounds.Centre();
	}

	void Terrain::RefreshWholeBounds()
	{
		AABB whole;
		whole.Min = Vec3(std::numeric_limits<float>::max());
		whole.Max = Vec3(std::numeric_limits<float>::lowest());
		for (const Chunk& chunk : m_Chunks)
		{
			whole.Min = Math::Min(whole.Min, chunk.Bounds.Min);
			whole.Max = Math::Max(whole.Max, chunk.Bounds.Max);
		}
		m_Bounds = whole;
	}

	void Terrain::Invalidate(const TerrainRect& rect)
	{
		if (rect.Empty() || m_Chunks.empty())
			return;

		// One sample either way: a height moves the central-difference
		// normal of its neighbours, and a neighbour on the far side of a
		// chunk edge belongs to the next chunk.
		const TerrainRect grown = rect.Grown(1, m_Data.Resolution);
		const uint32_t cx0 = grown.X0 / m_ChunkQuads;
		const uint32_t cz0 = grown.Z0 / m_ChunkQuads;
		const uint32_t cx1 = Math::Min(grown.X1 / m_ChunkQuads, m_ChunksPerSide - 1);
		const uint32_t cz1 = Math::Min(grown.Z1 / m_ChunkQuads, m_ChunksPerSide - 1);
		for (uint32_t cz = cz0; cz <= cz1; ++cz)
		{
			for (uint32_t cx = cx0; cx <= cx1; ++cx)
			{
				Chunk& chunk = m_Chunks[(size_t)cz * m_ChunksPerSide + cx];
				chunk.Stale = (uint8_t)((1u << kLevels) - 1u);
				RefreshBounds(chunk);
			}
		}
		RefreshWholeBounds();
	}

	bool Terrain::HasStale() const
	{
		for (const Chunk& chunk : m_Chunks)
			if (chunk.Stale)
				return true;
		return false;
	}

	void Terrain::RebuildStale(bool all)
	{
		if (!m_Device)
			return;
		for (uint32_t cz = 0; cz < m_ChunksPerSide; ++cz)
		{
			for (uint32_t cx = 0; cx < m_ChunksPerSide; ++cx)
			{
				Chunk& chunk = m_Chunks[(size_t)cz * m_ChunksPerSide + cx];
				if (!chunk.Stale)
					continue;
				for (int level = 0; level < kLevels; ++level)
				{
					const uint8_t bit = (uint8_t)(1u << level);
					if (!(chunk.Stale & bit) || (!all && level != chunk.Level))
						continue;
					BuildLevel(chunk, cx, cz, level);
					chunk.Stale &= (uint8_t)~bit;
				}
			}
		}
	}

	void Terrain::UploadWeightRows(const TerrainRect& rect)
	{
		if (!m_Device || !m_Data.HasWeights() || rect.Empty())
			return;

		// The paint arrived after the texture: the terrain was built unpainted
		// (the 1x1 all-layer-0 texel) and the brush has just written its
		// first weights. A real map now, uploaded whole; from here on, rows.
		if (!m_WeightMap || m_WeightMap->GetWidth() != m_Data.Resolution)
		{
			RHI::TextureDesc desc;
			desc.Width = m_Data.Resolution;
			desc.Height = m_Data.Resolution;
			desc.Format = RHI::Format::R8G8B8A8_UNORM;
			desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
			desc.DebugName = "Terrain weights";
			m_WeightMap = m_Device->CreateTexture(desc);
			m_WeightMap->Upload(m_Data.Weights.data(), m_Data.Weights.size());
			if (m_Layers)
				m_Layers->SetWeights(m_WeightMap, WeightUvFor(m_Dimensions, m_Data.Resolution));
			return;
		}

		// Whole rows of the rectangle: the map's texel row is the grid's, so
		// the region is contiguous per row and packed for the upload.
		const uint32_t width = rect.Width();
		std::vector<uint8_t> rows((size_t)width * rect.Height() * TerrainData::kLayers);
		for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
		{
			const uint8_t* from = &m_Data.Weights[((size_t)z * m_Data.Resolution + rect.X0) * TerrainData::kLayers];
			std::copy(from, from + (size_t)width * TerrainData::kLayers,
					  rows.begin() + (size_t)(z - rect.Z0) * width * TerrainData::kLayers);
		}
		m_WeightMap->UploadRegion(rect.X0, rect.Z0, width, rect.Height(), rows.data(), rows.size());
	}

	void Terrain::ApplyRegion(const TerrainData& source, const TerrainRect& rect)
	{
		if (rect.Empty() || !source.IsValid() || source.Resolution != m_Data.Resolution ||
			rect.X1 >= m_Data.Resolution || rect.Z1 >= m_Data.Resolution)
			return;

		// Heights: the rectangle copied, the chunks it touches marked.
		bool heightsMoved = false;
		for (uint32_t z = rect.Z0; z <= rect.Z1 && !heightsMoved; ++z)
		{
			const size_t at = (size_t)z * m_Data.Resolution + rect.X0;
			heightsMoved = std::memcmp(&source.Heights[at], &m_Data.Heights[at],
									   rect.Width() * sizeof(uint16_t)) != 0;
		}
		if (heightsMoved)
		{
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				const size_t at = (size_t)z * m_Data.Resolution + rect.X0;
				std::copy(source.Heights.begin() + at, source.Heights.begin() + at + rect.Width(),
						  m_Data.Heights.begin() + at);
			}
			Invalidate(rect);
		}

		// Weights: the same rectangle, and the texture's rows.
		if (source.HasWeights())
		{
			if (!m_Data.HasWeights())
				m_Data.Weights.assign(source.Weights.size(), 0);
			bool weightsMoved = false;
			for (uint32_t z = rect.Z0; z <= rect.Z1; ++z)
			{
				const size_t at = ((size_t)z * m_Data.Resolution + rect.X0) * TerrainData::kLayers;
				const size_t bytes = (size_t)rect.Width() * TerrainData::kLayers;
				if (std::memcmp(&source.Weights[at], &m_Data.Weights[at], bytes) != 0)
				{
					weightsMoved = true;
					std::copy(source.Weights.begin() + at, source.Weights.begin() + at + bytes,
							  m_Data.Weights.begin() + at);
				}
			}
			if (weightsMoved)
				UploadWeightRows(rect);
		}
	}

	bool Terrain::Raycast(const Vec3& localOrigin, const Vec3& localDirection, float& t) const
	{
		if (!m_Data.IsValid())
			return false;

		const Vec3 direction = Math::Normalize(localDirection);
		const float half = m_Dimensions.Size * 0.5f;

		// Clip the ray to the terrain's box in x and z (y is open: the
		// surface is somewhere between 0 and Height, and the march below
		// finds it), so the march starts at the rim and stops at the far one.
		float tNear = 0.0f;
		float tFar = std::numeric_limits<float>::max();
		for (int axis : { 0, 2 })
		{
			const float o = localOrigin[axis];
			const float d = direction[axis];
			if (Math::Abs(d) < 1e-8f)
			{
				if (o < -half || o > half)
					return false;
				continue;
			}
			float t0 = (-half - o) / d;
			float t1 = (half - o) / d;
			if (t0 > t1) { const float swap = t0; t0 = t1; t1 = swap; }
			tNear = Math::Max(tNear, t0);
			tFar = Math::Min(tFar, t1);
			if (tNear > tFar)
				return false;
		}
		// And in y: the surface lies between 0 and Height, so a ray has
		// nothing to meet above the top going up or below the base going
		// down, and a vertical ray -- which the box clip above leaves
		// unbounded -- ends where it leaves that slab.
		{
			const float o = localOrigin.y;
			const float d = direction.y;
			const float top = Math::Max(m_Dimensions.Height, 0.0f);
			if (Math::Abs(d) < 1e-8f)
			{
				if (o < 0.0f || o > top)
					return false;
			}
			else
			{
				float t0 = (0.0f - o) / d;
				float t1 = (top - o) / d;
				if (t0 > t1) { const float swap = t0; t0 = t1; t1 = swap; }
				tNear = Math::Max(tNear, t0);
				tFar = Math::Min(tFar, t1);
				if (tNear > tFar)
					return false;
			}
		}
		if (tFar <= 0.0f)
			return false;

		// March in half-cell steps -- fine enough that a slope between two
		// samples cannot be stepped over -- and bisect the first crossing
		// from above to below.
		const float step = GetCellSize() * 0.5f;
		auto above = [&](float at)
		{
			const Vec3 p = localOrigin + direction * at;
			return p.y - HeightAt(p.x, p.z);
		};

		float previous = tNear;
		float previousAbove = above(previous);
		if (previousAbove < 0.0f)
		{
			// Starting under the surface: no crossing from above ahead of us
			// unless the ray climbs out first, which a brush ray never does.
			return false;
		}
		for (float at = tNear + step; at <= tFar + step; at += step)
		{
			const float clamped = Math::Min(at, tFar);
			const float now = above(clamped);
			if (now <= 0.0f)
			{
				float lo = previous, hi = clamped;
				for (int i = 0; i < 24; ++i)
				{
					const float mid = 0.5f * (lo + hi);
					if (above(mid) > 0.0f) lo = mid; else hi = mid;
				}
				t = hi;
				return true;
			}
			previous = clamped;
			previousAbove = now;
			if (clamped >= tFar)
				break;
		}
		return false;
	}

	float Terrain::HeightAt(float localX, float localZ) const
	{
		if (!m_Data.IsValid())
			return 0.0f;
		const float cell = GetCellSize();
		const float half = m_Dimensions.Size * 0.5f;
		return m_Data.Sample((localX + half) / cell, (localZ + half) / cell) * m_Dimensions.Height;
	}
}
