#pragma once

// A PBR material: the metallic-roughness parameterisation glTF uses, which is
// what almost every asset pipeline exports.
//
// Scalar parameters live in a uniform buffer and the maps in the same
// descriptor set, so binding a material is one BindResourceSet rather than a
// sequence of individual binds. Every map has a neutral 1x1 fallback, because a
// descriptor set with an unwritten sampler is a validation error even when the
// shader will not sample it.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// Mirrors the std140 MaterialData block in pbr.rvshader.
	struct MaterialParams
	{
		Vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec4 EmissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float Occlusion = 1.0f;
		float NormalScale = 1.0f;
		// Which maps are present, as bit flags; the shader falls back to the
		// scalar parameter for any that are not.
		int32_t MapFlags = 0;

		// Reflectance of the surface when it is *not* metal, on the convention
		// everyone uses: F0 = 0.08 * Specular, so 0.5 is the 4% that almost
		// every dielectric actually has and that the shader hardcoded until
		// now. Metals ignore it -- their F0 is their albedo.
		//
		// 0.5 by default, which reproduces the old constant exactly, so no
		// existing material changes appearance. Worth having because 4% is
		// wrong for a few real things: water is nearer 2%, gemstones and some
		// plastics considerably more.
		//
		// In the padding rather than after UvTransform, so the block stays 80
		// bytes and the std140 layout does not move.
		float Specular = 0.5f;
		int32_t _padding[2] = {};

		// How this material's maps meet the surface: xy scales the texture
		// coordinate, zw offsets it. (1, 1, 0, 0) is the mesh's own UVs.
		//
		// Needed because the built-in primitives give every face UVs spanning
		// 0..1, and scaling an entity does not touch them -- so a ground plane
		// scaled to twelve units stretches one copy of its texture across the
		// whole thing. A material for a real surface has a texel density, and
		// this is where it is said.
		//
		// On the material rather than per entity, unlike the scalar overrides.
		// Colour varies per object constantly; tiling is a property of the
		// texture set, and putting it in the instance stream would charge every
		// untextured object sixteen bytes for something almost none of them
		// use. Unity draws the line in the same place. Per-entity tiling, if it
		// is ever wanted, belongs in the instance stream beside the overrides.
		Vec4 UvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
	};

	// The instance stream has had one of these since it was written; this block
	// never did, and adding a field to it is precisely when that costs
	// something. A uniform block whose C++ and GLSL layouts disagree does not
	// fail to compile or to bind -- it reads the wrong sixteen bytes, and the
	// symptom is a material whose roughness is somebody else's tiling.
	static_assert(sizeof(MaterialParams) == 80,
				  "Must match the MaterialData block in include/pbr_fragment.glsl");

	enum MaterialMap : int32_t
	{
		MaterialMap_BaseColor         = 1 << 0,
		MaterialMap_Normal            = 1 << 1,
		// glTF's packing: roughness in green, metallic in blue, one texture.
		MaterialMap_MetallicRoughness = 1 << 2,
		MaterialMap_Occlusion         = 1 << 3,
		MaterialMap_Emissive          = 1 << 4,

		// The same two as separate greyscale maps, which is how every texture
		// library that is not a glTF ships them -- ambientCG, Poly Haven and
		// Quixel all do. Supporting only the packed form meant every downloaded
		// material had to be repacked before it could be used, which is a
		// processing step standing between the engine and its own asset
		// pipeline.
		//
		// A separate map *replaces* the packed one's channel rather than
		// multiplying with it; see the shader. Both forms exist because glTF
		// genuinely carries the packed one and splitting it at import would
		// mean decoding and rewriting somebody's textures.
		MaterialMap_Roughness         = 1 << 5,
		MaterialMap_Metallic          = 1 << 6,

		// Dielectric reflectance, greyscale, modulating the Specular scalar.
		MaterialMap_Specular          = 1 << 7,
	};

	class Material
	{
	public:
		Material(RHI::RHIDevice& device, std::string name);

		const std::string& GetName() const { return m_Name; }
		MaterialParams& GetParams() { return m_Params; }
		const MaterialParams& GetParams() const { return m_Params; }

		// Passing nullptr clears the map and reverts to the scalar parameter.
		void SetBaseColorMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetNormalMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetMetallicRoughnessMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetOcclusionMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetEmissiveMap(const RHI::Ref<RHI::RHITexture>& texture);

		// Separate greyscale maps, read from the red channel. Either one
		// overrides the packed metallic-roughness map's corresponding channel.
		void SetRoughnessMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetMetallicMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetSpecularMap(const RHI::Ref<RHI::RHITexture>& texture);

		// Marks every frame's descriptor set and parameter buffer as needing a
		// rewrite. Call after touching GetParams() directly.
		void Invalidate();

		// Builds the descriptor set against a pipeline layout. Materials are
		// created before any pipeline exists, so this is deferred rather than
		// done in the constructor.
		void Bind(RHI::RHICommandList& commandList, const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		// What makes two materials interchangeable to a batched draw.
		//
		// The five maps, the sampler and the map flags -- everything the
		// descriptor set holds -- and deliberately not the scalar parameters,
		// which the renderer sends per instance. Two materials with the same
		// key produce the same bound state, so their objects can be one draw
		// even when they are different colours. That is the difference between
		// instancing collapsing a scene of props and doing nothing at all,
		// because a scene where every object carries its own Material has no
		// two objects sharing one.
		uint64_t GetBatchKey() const;

		static RHI::Ref<Material> CreateDefault(RHI::RHIDevice& device);

		// Drops the sampler every material shares. Called at renderer shutdown,
		// for the same reason the texture caches are: it holds a GPU object and
		// the device is about to go.
		static void ReleaseShared();

	private:
		void EnsureResources(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		RHI::RHIDevice& m_Device;
		std::string m_Name;
		MaterialParams m_Params;

		RHI::Ref<RHI::RHITexture> m_BaseColor;
		RHI::Ref<RHI::RHITexture> m_Normal;
		RHI::Ref<RHI::RHITexture> m_MetallicRoughness;
		RHI::Ref<RHI::RHITexture> m_Occlusion;
		RHI::Ref<RHI::RHITexture> m_Emissive;
		RHI::Ref<RHI::RHITexture> m_Roughness;
		RHI::Ref<RHI::RHITexture> m_Metallic;
		RHI::Ref<RHI::RHITexture> m_Specular;
		RHI::Ref<RHI::RHISampler> m_Sampler;

		// Per frame in flight: the parameter block is host-visible and may be
		// rewritten while a previous frame still reads it.
		std::vector<RHI::Ref<RHI::RHIBuffer>>      m_ParamBuffers;
		std::vector<RHI::Ref<RHI::RHIResourceSet>> m_Sets;

		// Whether this frame's set and buffer still match the material.
		//
		// Bind used to upload and commit on every single draw. A descriptor set
		// that is already bound must not be rewritten -- Vulkan reports it as
		// "destroyed or updated without UPDATE_AFTER_BIND" -- and it happened
		// whenever one material was used by two objects, or when the same scene
		// was drawn into two viewports. Writing only on an actual change fixes
		// the hazard and removes a per-draw descriptor write.
		std::vector<bool> m_FrameDirty;
		bool m_Built = false;
	};
}
