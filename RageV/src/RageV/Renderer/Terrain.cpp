#include <rvpch.h>
#include "Terrain.h"
#include "Renderer.h"
#include "Renderer3D.h"
#include "TextureLoader.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/Log.h"

#include <cmath>
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

	void Terrain::BuildChunkGeometry(const TerrainData& data, const Dimensions& dims,
									 uint32_t chunkX, uint32_t chunkZ, int level,
									 std::vector<MeshVertex>& vertices,
									 std::vector<uint32_t>& indices, bool skirts)
	{
		vertices.clear();
		indices.clear();
		if (!data.IsValid())
			return;

		const uint32_t quads = data.QuadCount();
		const uint32_t chunkQuads = Math::Min(kChunkQuads, quads);
		const uint32_t step = 1u << (uint32_t)Math::Clamp(level, 0, kLevels - 1);
		if (chunkQuads % step != 0)
			return;
		const uint32_t n = chunkQuads / step;   // quads a side at this level
		const float cell = dims.Size / (float)quads;

		const uint32_t sample0X = chunkX * chunkQuads;
		const uint32_t sample0Z = chunkZ * chunkQuads;
		if (sample0X + chunkQuads > quads || sample0Z + chunkQuads > quads)
			return;

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

		if (!skirts)
			return;

		// --- the skirts ------------------------------------------------------
		// The chunk's edge vertices again, dropped by half the chunk's own
		// height range plus a sliver of the terrain's, joined to the edge by
		// a strip wound *both* ways so it is not culled from either side. The
		// range is over every sample in the chunk, not this level's, so each
		// level's skirt hangs the same and the coarser levels stay inside the
		// finest level's bounds.
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
			return;
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
	}

	RHI::Ref<Terrain> Terrain::Create(RHI::RHIDevice* device, const TerrainData& data,
									  AssetHandle asset, const Dimensions& dimensions)
	{
		if (!data.IsValid())
			return nullptr;

		auto terrain = std::make_shared<Terrain>();
		terrain->m_Data = data;
		terrain->m_Asset = asset;
		terrain->m_Dimensions = dimensions;
		terrain->m_Dimensions.Size = Math::Max(dimensions.Size, 1.0f);
		terrain->m_Dimensions.Height = Math::Max(dimensions.Height, 0.0f);
		terrain->m_Dimensions.TextureScale = Math::Max(dimensions.TextureScale, 0.05f);

		const uint32_t quads = data.QuadCount();
		terrain->m_ChunkQuads = Math::Min(kChunkQuads, quads);
		terrain->m_ChunksPerSide = quads / terrain->m_ChunkQuads;

		AABB whole;
		whole.Min = Vec3(std::numeric_limits<float>::max());
		whole.Max = Vec3(std::numeric_limits<float>::lowest());

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		terrain->m_Chunks.reserve((size_t)terrain->m_ChunksPerSide * terrain->m_ChunksPerSide);

		for (uint32_t cz = 0; cz < terrain->m_ChunksPerSide; ++cz)
		{
			for (uint32_t cx = 0; cx < terrain->m_ChunksPerSide; ++cx)
			{
				Chunk chunk;
				chunk.SampleX = cx * terrain->m_ChunkQuads;
				chunk.SampleZ = cz * terrain->m_ChunkQuads;

				for (int level = 0; level < kLevels; ++level)
				{
					BuildChunkGeometry(data, terrain->m_Dimensions, cx, cz, level, vertices, indices);
					if (vertices.empty())
						continue;

					if (level == 0)
					{
						AABB bounds;
						bounds.Min = Vec3(std::numeric_limits<float>::max());
						bounds.Max = Vec3(std::numeric_limits<float>::lowest());
						for (const MeshVertex& v : vertices)
						{
							bounds.Min = Math::Min(bounds.Min, v.Position);
							bounds.Max = Math::Max(bounds.Max, v.Position);
						}
						chunk.Bounds = bounds;
						chunk.Centre = bounds.Centre();
						whole.Min = Math::Min(whole.Min, bounds.Min);
						whole.Max = Math::Max(whole.Max, bounds.Max);
					}

					if (device)
					{
						chunk.Levels[level] = std::make_shared<Mesh>(
							*device, vertices, indices,
							"Terrain chunk " + std::to_string(cx) + "," + std::to_string(cz) +
							" L" + std::to_string(level));
					}
				}

				terrain->m_Chunks.push_back(std::move(chunk));
			}
		}

		terrain->m_Bounds = whole;

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
