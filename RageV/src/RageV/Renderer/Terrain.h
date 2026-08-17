#pragma once

// A heightfield terrain as a source of meshes (ENGINE-NOTES 7ap).
//
// Built from a TerrainData (the asset's heights) and a component's dimensions
// -- metres a side, metres of height, metres per texture repeat -- into
// chunks of 64 quads, each an ordinary Mesh at four levels of detail. The
// renderer never learns the word terrain: a chunk is drawn, shadowed, traced,
// culled, picked and batched exactly as a crate is, because it *is* a Mesh.
// What this class adds is the builder, the per-frame choice of level, and
// HeightAt -- and the geometry rule is written once here and read by the
// physics world through TerrainData::Sample, so the surface a body rests on is
// the surface drawn, to the triangle.
//
// Levels are chosen per chunk per frame from the camera's distance
// (SelectLod), and *read* by every consumer -- the scene pass, the shadow
// casters, the ray-instance list -- so the geometry that casts a shadow is the
// geometry that receives it. Cracks between neighbouring levels are hidden by
// skirts, not stitching; there is no geomorphing, so a chunk pops when its
// level changes.
//
// The pure builder (BuildChunkGeometry) needs no device, which is what lets
// the suite assert vertex counts, skirt depths and normals headlessly.

#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Asset/TerrainData.h"
#include "RageV/Asset/Asset.h"
#include "RageV/Math/Math.h"

#include <vector>

namespace RageV
{
	struct TerrainComponent;

	class Terrain
	{
	public:
		// Quads a chunk spans at full detail; smaller terrains use fewer.
		static constexpr uint32_t kChunkQuads = 64;
		// Levels of detail per chunk: every 1st, 2nd, 4th, 8th sample.
		static constexpr int kLevels = 4;

		struct Dimensions
		{
			float Size = 256.0f;          // metres a side
			float Height = 40.0f;         // metres at a full sample
			float TextureScale = 4.0f;    // metres per texture repeat

			bool operator==(const Dimensions& other) const
			{
				return Size == other.Size && Height == other.Height &&
					   TextureScale == other.TextureScale;
			}
		};

		struct Chunk
		{
			// The chunk's first sample along x and z.
			uint32_t SampleX = 0;
			uint32_t SampleZ = 0;
			// Terrain-local box over the finest level, skirts included; every
			// coarser level's vertices are a subset, so it holds for all.
			AABB Bounds;
			Vec3 Centre{ 0.0f };
			// One mesh per level; null when built without a device.
			RHI::Ref<Mesh> Levels[kLevels];
			// The level chosen by the last SelectLod. 0 until one runs.
			int Level = 0;

			const RHI::Ref<Mesh>& Selected() const { return Levels[Level]; }
		};

		// Builds every chunk at every level. `device` may be null: the chunks
		// then carry bounds, levels and no meshes, which is what a headless
		// caller wants. `data` is copied, so the manager's cache may go.
		static RHI::Ref<Terrain> Create(RHI::RHIDevice* device, const TerrainData& data,
										AssetHandle asset, const Dimensions& dimensions);

		// The terrain a component draws with, built on first use and rebuilt
		// -- as a *new* object, so a copy of the component that shares the old
		// one is not reshaped -- when the asset or a dimension changes. Null
		// when the asset is not loadable. Uses the renderer's device when
		// there is one.
		static const RHI::Ref<Terrain>& Resolve(TerrainComponent& component);

		// How the chunk vertices' uv (local metres / TextureScale) reaches the
		// weight map's texel centres (ENGINE-NOTES 7aq): uv * xy + zw. The
		// offset is one half in both axes; the scale is TextureScale / Size
		// times (R - 1) / R, so sample i lands on texel centre (i + 0.5) / R
		// rather than half a texel to one side. Pure, so the suite can ask it.
		static Vec4 WeightUvFor(const Dimensions& dimensions, uint32_t resolution);

		// The pure builder. One chunk at one level, into `vertices` and
		// `indices` (cleared first), in terrain-local metres: x and z in
		// [-Size/2, Size/2], y in [0, Height]; uv = local metres /
		// TextureScale; normals from central differences of the heights.
		// Skirts on every edge a neighbouring chunk shares, none on the
		// terrain's outer edge; `skirts` off is for the falsification and
		// nothing else.
		static void BuildChunkGeometry(const TerrainData& data, const Dimensions& dimensions,
									   uint32_t chunkX, uint32_t chunkZ, int level,
									   std::vector<MeshVertex>& vertices,
									   std::vector<uint32_t>& indices, bool skirts = true);

		// The level rule: 0 within four chunk widths of the camera, and one
		// coarser for each doubling of distance, capped at kLevels - 1.
		static int LevelFor(float distance, float chunkWidth);

		// Chooses every chunk's level from the camera's world position and
		// this terrain's world matrix. Once per frame, before anything reads
		// Chunk::Level.
		void SelectLod(const Vec3& cameraWorld, const Mat4& world);

		// The surface height in metres at a terrain-local (x, z), over the
		// same triangles the meshes and the collider use; clamped to the
		// terrain's extent.
		float HeightAt(float localX, float localZ) const;

		// The layered material every chunk draws with (7aq): the component's
		// four layer handles resolved to materials -- layer 0 to the renderer's
		// default when empty, the others left inactive -- over this terrain's
		// weight map. Once per frame, before the chunks are drawn; null when
		// this terrain was built without a device.
		const RHI::Ref<LayeredMaterial>& RefreshLayers(const TerrainComponent& component);
		const RHI::Ref<LayeredMaterial>& GetLayers() const { return m_Layers; }
		// The RGBA8 weight texture built from the asset's weights, or a 1x1
		// "all layer 0" texel when it has none.
		const RHI::Ref<RHI::RHITexture>& GetWeightMap() const { return m_WeightMap; }

		bool Matches(AssetHandle asset, const Dimensions& dimensions) const
		{
			return asset == m_Asset && dimensions == m_Dimensions;
		}

		const std::vector<Chunk>& GetChunks() const { return m_Chunks; }
		std::vector<Chunk>& GetChunks() { return m_Chunks; }
		const AABB& GetBounds() const { return m_Bounds; }
		const TerrainData& GetData() const { return m_Data; }
		const Dimensions& GetDimensions() const { return m_Dimensions; }
		AssetHandle GetAsset() const { return m_Asset; }
		uint32_t GetChunksPerSide() const { return m_ChunksPerSide; }
		// Quads a chunk spans on this terrain: kChunkQuads, or fewer on a
		// terrain smaller than a chunk.
		uint32_t GetChunkQuads() const { return m_ChunkQuads; }
		// Metres between samples, and metres a chunk spans.
		float GetCellSize() const { return m_Dimensions.Size / (float)m_Data.QuadCount(); }
		float GetChunkWidth() const { return GetCellSize() * (float)m_ChunkQuads; }

	private:
		TerrainData m_Data;
		AssetHandle m_Asset;
		Dimensions m_Dimensions;
		uint32_t m_ChunkQuads = kChunkQuads;
		uint32_t m_ChunksPerSide = 0;
		std::vector<Chunk> m_Chunks;
		AABB m_Bounds;
		RHI::Ref<RHI::RHITexture> m_WeightMap;
		RHI::Ref<LayeredMaterial> m_Layers;
	};
}
