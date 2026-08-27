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
	// How a material's fragments reach the frame.
	//
	// **A routing decision, not a shading one**, which is why it lives on the
	// Material rather than in the parameter block: it decides which draw list a
	// mesh joins and which pipeline that list is issued with, and the shader
	// never asks it anything. What the fragment itself needs -- the alpha -- is
	// already in BaseColor.
	// What the loader learned about an emissive map's content. Declared, not
	// included: a material only ever holds one and hands it on, so the header
	// that describes materials need not also describe texture loading.
	struct TextureStats;

	enum class BlendMode : int32_t
	{
		// Alpha ignored. Every material in this project was this until the
		// showroom's car arrived wanting glass.
		Opaque = 0,

		// Weighted-blended order-independent transparency: the fragment goes
		// into the accumulate/revealage pair the transparent pass owns and is
		// resolved over the opaque image. Depth-tested and not depth-written,
		// because two panes of glass both have to survive.
		Blend = 1,

		// **Alpha-tested.** The fragment is kept whole or discarded outright,
		// on `AlphaCutoff`; nothing is ever partly transparent. That makes it
		// opaque in every way that matters to the frame -- it writes depth, it
		// sorts with the opaque geometry, and it needs no blending -- and it
		// is what a railing, a grate, a chain-link fence or a leaf wants. The
		// alternative is modelling every gap as geometry, which is what this
		// engine required until now and what makes a suspension bridge
		// expensive.
		//
		// It is not a cheaper `Blend` and must not be routed like one: a
		// masked fragment that went into the OIT pair would stop writing
		// depth, and a railing that writes no depth stops occluding anything
		// behind it.
		Masked = 2,
	};

	// **Two questions, and only one of them used to have an answer.**
	//
	// Every site that cared wrote `!= BlendMode::Opaque`, which was exact
	// while there were two modes and silently wrong the moment there were
	// three -- it would have routed every cutout into the transparent pass.
	// The two meanings are separated here so that adding a fourth mode is a
	// compile-time question at each site rather than a behaviour change at all
	// of them.

	// Goes through the blended pass: accumulate/revealage, no depth write.
	// Only true blending does.
	constexpr bool IsBlended(BlendMode mode) { return mode == BlendMode::Blend; }

	// Whether this material's geometry is carried in the ray acceleration
	// structure. Blended geometry is excluded so a car keeps its interior
	// (Scene.cpp says why at length), and masked geometry is excluded for
	// now as well -- the structure has no per-triangle alpha test, so a
	// cutout in it would cast the solid shadow of its bounding sheet, which
	// is a louder wrong than casting none. Lifting that is the ray-query
	// traversal work, tracked separately.
	constexpr bool TracedAsGeometry(BlendMode mode) { return mode == BlendMode::Opaque; }

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

		// How deep the height map displaces, in UV units. 0.05 reads as a few
		// centimetres of relief on a metre-scale tile; 0 turns parallax off
		// even when a map is bound.
		float HeightScale = 0.05f;

		// **Below this, the fragment is discarded**; at or above it the
		// fragment is fully opaque. glTF's own default, and the value every
		// authoring tool assumes. Only read by the masked variant of the lit
		// shader, so it costs nothing for the other two modes.
		//
		// In the padding word rather than appended, for the reason `Specular`
		// gives above: the block stays 80 bytes and the std140 layout does not
		// move, so no cooked material on disk has to be reissued.
		float AlphaCutoff = 0.5f;

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

	// What the bindless variant reads instead of a bound material set
	// (ENGINE-NOTES 7al): the eight heap slots and the three scalars the
	// bound path still keeps in its uniform block. The rest of MaterialParams
	// is already per instance. One record per distinct material per frame,
	// written into a storage buffer the instance indexes. Mirrors GpuMaterial
	// in include/pbr_fragment.glsl, std430.
	struct GpuMaterial
	{
		// Base colour, normal, occlusion, emissive.
		uint32_t Maps0[4] = { 0, 0, 0, 0 };
		// Roughness, metallic, specular, height.
		uint32_t Maps1[4] = { 0, 0, 0, 0 };
		Vec4     UvTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
		int32_t  MapFlags = 0;
		float    Specular = 0.5f;
		float    HeightScale = 0.05f;
		// As MaterialParams::AlphaCutoff, in the word this struct was already
		// padding to 64 bytes with. The bindless path reads the cutoff from
		// here, per instance, which is what lets one masked pipeline draw many
		// different cutouts in a single indirect call.
		float    AlphaCutoff = 0.5f;
	};
	static_assert(sizeof(GpuMaterial) == 64,
				  "Must match GpuMaterial in include/pbr_fragment.glsl");

	class TextureHeap;

	enum MaterialMap : int32_t
	{
		MaterialMap_BaseColor         = 1 << 0,
		MaterialMap_Normal            = 1 << 1,
		MaterialMap_Occlusion         = 1 << 3,
		MaterialMap_Emissive          = 1 << 4,

		// The same two as separate greyscale maps, which is how every texture
		// library that is not a glTF ships them -- ambientCG, Poly Haven and
		// Quixel all do. Supporting only the packed form meant every downloaded
		// material had to be repacked before it could be used, which is a
		// processing step standing between the engine and its own asset
		// pipeline.
		//
		// glTF is the exception: it packs the two into one texture. Rather
		// than carry a second path through the material, the descriptor set
		// and the shader for one file format, the importer splits it once and
		// writes the halves as real assets.
		MaterialMap_Roughness         = 1 << 5,
		MaterialMap_Metallic          = 1 << 6,

		// Dielectric reflectance, greyscale, modulating the Specular scalar.
		MaterialMap_Specular          = 1 << 7,

		// A height field, driving parallax. This is the map that makes a
		// ground plane read as having depth: normals change shading, parallax
		// changes *where the texels are* as the view moves, and the second is
		// the cue the eye actually uses on a surface seen at an angle.
		MaterialMap_Height            = 1 << 8,
	};

	class Material
	{
	public:
		Material(RHI::RHIDevice& device, std::string name);

		const std::string& GetName() const { return m_Name; }
		MaterialParams& GetParams() { return m_Params; }
		const MaterialParams& GetParams() const { return m_Params; }

		// Which pass this material's meshes are drawn in. Deliberately *not*
		// part of the batch key: two materials differing only in this are never
		// in one run anyway, because they are in different lists.
		BlendMode GetBlendMode() const { return m_Blend; }
		void SetBlendMode(BlendMode mode) { m_Blend = mode; }

		// Passing nullptr clears the map and reverts to the scalar parameter.
		void SetBaseColorMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetNormalMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetOcclusionMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetEmissiveMap(const RHI::Ref<RHI::RHITexture>& texture);
		// The emissive map's average, in linear space, or white when there is
		// no map. What the emitter list multiplies the scalar by so that a
		// partly-lit surface emits the power it actually emits -- see
		// TextureLoader::MeanColor.
		const Vec3& GetEmissiveMean() const { return m_EmissiveMean; }
		// The same texture's cell distribution, for a sampler that wants to
		// aim at the lit part rather than at the whole surface. Null when the
		// map is uniform, black, or absent.
		const std::shared_ptr<const TextureStats>&
			GetEmissiveStats() const { return m_EmissiveStats; }

		// Separate greyscale maps, read from the red channel.
		void SetRoughnessMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetMetallicMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetSpecularMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetHeightMap(const RHI::Ref<RHI::RHITexture>& texture);

		// Marks every frame's descriptor set and parameter buffer as needing a
		// rewrite. Call after touching GetParams() directly.
		void Invalidate();

		// Builds the descriptor set against a pipeline layout. Materials are
		// created before any pipeline exists, so this is deferred rather than
		// done in the constructor.
		void Bind(RHI::RHICommandList& commandList, const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		// The bindless path's half of Bind (ENGINE-NOTES 7al): registers every
		// map in the heap and writes the record the shader reads instead of a
		// bound set. A pure function of the material's state; the renderer
		// decides where the record goes and which instance names it. Absent
		// maps take the same neutral 1x1 fallbacks the bound path binds, so
		// the two variants sample identical texels and the pixel comparison
		// between them is exact.
		void WriteRecord(TextureHeap& heap, GpuMaterial& out) const;

		// What makes two materials interchangeable to a batched draw.
		//
		// The eight maps, the sampler, and every scalar the shader still reads
		// from the material *block* -- MapFlags, Specular, HeightScale,
		// UvTransform -- which is everything the descriptor set holds, and
		// deliberately not the parameters the renderer sends per instance. Two
		// materials with the same key produce the same bound state, so their
		// objects can be one draw even when they are different colours. That
		// is the difference between instancing collapsing a scene of props and
		// doing nothing at all, because a scene where every object carries its
		// own Material has no two objects sharing one.
		//
		// The block's scalars were missing from this key until the bindless
		// parity check found two materials sharing maps and differing in
		// tiling drawn as one run -- see the .cpp.
		//
		// On the bindless path nothing is bound per material at all -- the
		// maps are heap slots in a per-instance record -- so the key is zero
		// and a run breaks on mesh alone. That is the one-line branch the
		// design promised the batching loop would need.
		uint64_t GetBatchKey(bool bindless = false) const;

		static RHI::Ref<Material> CreateDefault(RHI::RHIDevice& device);

		// Drops the sampler every material shares. Called at renderer shutdown,
		// for the same reason the texture caches are: it holds a GPU object and
		// the device is about to go.
		static void ReleaseShared();

		// The three maps a layered material reads from each of its layers, and
		// the sampler it reads them with (ENGINE-NOTES 7aq). Null when absent;
		// the layered material binds the same neutral fallbacks Bind does.
		const RHI::Ref<RHI::RHITexture>& GetBaseColorMap() const { return m_BaseColor; }
		const RHI::Ref<RHI::RHITexture>& GetNormalMap() const { return m_Normal; }
		const RHI::Ref<RHI::RHITexture>& GetRoughnessMap() const { return m_Roughness; }
		const RHI::Ref<RHI::RHITexture>& GetEmissiveMap() const { return m_Emissive; }
		const RHI::Ref<RHI::RHISampler>& GetSampler() const { return m_Sampler; }

	private:
		// The sets for one pipeline layout (ENGINE-NOTES 7bc). A material used
		// to build its sets once, for the first pipeline that asked, and hand
		// them to every pipeline after -- right for one lit pipeline, wrong
		// for two layouts: on OpenGL a set's texture units are the pipeline's
		// own flat assignment, and the voxeliser binds materials against a
		// pipeline of its own. So the sets are kept per pipeline, each with
		// its own per-frame dirty flags; the parameter buffer is shared, since
		// its bytes are the same whoever reads them.
		struct PipelineSets
		{
			const RHI::RHIPipeline* Key = nullptr;
			std::vector<RHI::Ref<RHI::RHIResourceSet>> Sets;   // per frame in flight
			std::vector<bool> Dirty;
		};
		PipelineSets& EnsureResources(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		RHI::RHIDevice& m_Device;
		std::string m_Name;
		MaterialParams m_Params;
		BlendMode m_Blend = BlendMode::Opaque;

		RHI::Ref<RHI::RHITexture> m_BaseColor;
		RHI::Ref<RHI::RHITexture> m_Normal;
		RHI::Ref<RHI::RHITexture> m_Occlusion;
		RHI::Ref<RHI::RHITexture> m_Emissive;
		Vec3 m_EmissiveMean{ 1.0f, 1.0f, 1.0f };
		std::shared_ptr<const TextureStats> m_EmissiveStats;
		RHI::Ref<RHI::RHITexture> m_Roughness;
		RHI::Ref<RHI::RHITexture> m_Metallic;
		RHI::Ref<RHI::RHITexture> m_Specular;
		RHI::Ref<RHI::RHITexture> m_Height;
		RHI::Ref<RHI::RHISampler> m_Sampler;

		// Per frame in flight: the parameter block is host-visible and may be
		// rewritten while a previous frame still reads it.
		std::vector<RHI::Ref<RHI::RHIBuffer>>      m_ParamBuffers;
		std::vector<PipelineSets> m_PipelineSets;

		// Whether this frame's buffer still matches the material; each
		// pipeline's sets carry the same flag for themselves.
		//
		// Bind used to upload and commit on every single draw. A descriptor set
		// that is already bound must not be rewritten -- Vulkan reports it as
		// "destroyed or updated without UPDATE_AFTER_BIND" -- and it happened
		// whenever one material was used by two objects, or when the same scene
		// was drawn into two viewports. Writing only on an actual change fixes
		// the hazard and removes a per-draw descriptor write.
		std::vector<bool> m_FrameDirty;
	};

	// --- a layered material (ENGINE-NOTES 7aq) --------------------------------

	// The block the layered variant of the lit shader reads at set 1, binding
	// 0: four layers' scalars, how the mesh's texture coordinate reaches the
	// weight map, and -- on the bindless path -- the heap slots of the
	// thirteen maps the variant samples. One layout for both paths; the slots
	// are zero and unread on the bound one, where the maps are the set's own
	// samplers. Mirrors LayeredData in include/pbr_fragment.glsl, std140.
	struct LayeredParams
	{
		static constexpr uint32_t kLayers = 4;

		Vec4 BaseColor[kLayers];
		Vec4 EmissiveColor[kLayers];
		// metallic, roughness, occlusion, normal scale
		Vec4 Surface[kLayers];
		// xy scale, zw offset, as MaterialParams::UvTransform.
		Vec4 UvTransform[kLayers];
		// One dielectric reflectance per layer.
		Vec4 Specular{ 0.5f, 0.5f, 0.5f, 0.5f };
		// The layer's MaterialMap flags for the three maps read, plus
		// LayeredMap_Active for a layer that has a material at all.
		int32_t MapFlags[kLayers] = { 0, 0, 0, 0 };
		// The mesh's uv * xy + zw is the weight map's coordinate.
		Vec4 WeightUv{ 1.0f, 1.0f, 0.0f, 0.0f };
		// Heap slots, bindless path only.
		uint32_t BaseColorSlots[kLayers] = { 0, 0, 0, 0 };
		uint32_t NormalSlots[kLayers] = { 0, 0, 0, 0 };
		uint32_t RoughnessSlots[kLayers] = { 0, 0, 0, 0 };
		// x = the weight map's slot; the rest padding.
		uint32_t WeightSlot[4] = { 0, 0, 0, 0 };
	};
	static_assert(sizeof(LayeredParams) == 368,
				  "Must match LayeredData in include/pbr_fragment.glsl");

	// A layer with a material assigned. Above every MaterialMap bit.
	enum LayeredMap : int32_t
	{
		LayeredMap_Active = 1 << 15,
	};

	// A surface made of up to four materials in proportions read from a
	// weight map -- the terrain's paint (ENGINE-NOTES 7aq). Beside Material
	// rather than a kind of it: it binds a different block and a different set
	// of samplers, and the lit shader that reads it is a third pipeline whose
	// surface is assembled from the four layers before the one lighting every
	// mesh gets runs over it.
	//
	// The state it binds is a function of the four layer materials' *current*
	// state, so Refresh rebuilds the block from them once per frame and
	// rewrites the set only when something changed; a layer edited in the
	// inspector reaches the terrain the same frame with no dirty protocol
	// between the two classes.
	class LayeredMaterial
	{
	public:
		static constexpr uint32_t kLayers = LayeredParams::kLayers;

		// Set 1's bindings in the layered variant. The block, the weights, and
		// each map kind as an array of four (one binding, four descriptors),
		// which is what keeps a layer's index a constant in the shader.
		static constexpr uint32_t kBindingParams = 0;
		static constexpr uint32_t kBindingWeights = 1;
		static constexpr uint32_t kBindingBaseColor = 2;
		static constexpr uint32_t kBindingNormal = 3;
		static constexpr uint32_t kBindingRoughness = 4;

		LayeredMaterial(RHI::RHIDevice& device, std::string name);

		const std::string& GetName() const { return m_Name; }

		// Null makes the layer inactive: its weight is dropped from the
		// normalisation. Layer 0 is expected never to be null -- the caller
		// substitutes the renderer's default, as it does for a mesh.
		void SetLayer(uint32_t index, const RHI::Ref<Material>& material);
		const RHI::Ref<Material>& GetLayer(uint32_t index) const { return m_Layers[index]; }

		// The weight texture (RGBA8, one channel per layer, clamped at its
		// edges) and how the mesh's uv reaches it.
		void SetWeights(const RHI::Ref<RHI::RHITexture>& weights, const Vec4& weightUv);
		const RHI::Ref<RHI::RHITexture>& GetWeights() const { return m_Weights; }
		const Vec4& GetWeightUv() const { return m_WeightUv; }

		// Rebuilds the block and the texture list from the layers' current
		// state. Once per frame, before Bind or GetBatchKey; `heap` is the
		// bindless heap, or null on the bound path, and decides which of the
		// two the set will carry. Cheap when nothing changed.
		void Refresh(TextureHeap* heap);

		// Binds set `set` of `pipeline`: the block, and on the bound path the
		// thirteen samplers. Writes this frame's set only when Refresh found a
		// change since it was last written.
		void Bind(RHI::RHICommandList& commandList, const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		// What makes two layered materials interchangeable to a batched draw:
		// a hash of the block and of every texture and sampler the set binds.
		// Recomputed by Refresh.
		uint64_t GetBatchKey() const { return m_Key; }

		const LayeredParams& GetParams() const { return m_Params; }

		// The sampler the weight map is read with, shared by every layered
		// material: filtered, clamped to edge -- repeat would blend the far
		// rim's paint into the near rim's last texel.
		static const RHI::Ref<RHI::RHISampler>& WeightSampler(RHI::RHIDevice& device);
		static void ReleaseShared();

	private:
		void EnsureResources(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		RHI::RHIDevice& m_Device;
		std::string m_Name;

		RHI::Ref<Material> m_Layers[kLayers];
		RHI::Ref<RHI::RHITexture> m_Weights;
		Vec4 m_WeightUv{ 1.0f, 1.0f, 0.0f, 0.0f };

		// What Refresh last built, and what Bind writes.
		LayeredParams m_Params;
		// The bound path's texture list, in binding order: weights, then base
		// colour, normal, roughness of each layer; and each layer's sampler.
		RHI::Ref<RHI::RHITexture> m_Textures[1 + 3 * kLayers];
		RHI::Ref<RHI::RHISampler> m_Samplers[kLayers];
		bool m_Bindless = false;
		uint64_t m_Key = 0;

		std::vector<RHI::Ref<RHI::RHIBuffer>>      m_ParamBuffers;
		std::vector<RHI::Ref<RHI::RHIResourceSet>> m_Sets;
		std::vector<bool> m_FrameDirty;
		bool m_Built = false;
	};
}
