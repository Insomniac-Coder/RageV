#pragma once

// Backend-agnostic enums and descriptions shared by the OpenGL and Vulkan RHI
// implementations. Nothing in this header may include a backend header.

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace RageV::RHI
{
	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T>
	using Scope = std::unique_ptr<T>;

	enum class Backend
	{
		OpenGL,
		Vulkan
	};

	enum class Format : uint8_t
	{
		Undefined = 0,

		R8_UNORM,
		R8G8_UNORM,
		R8G8B8A8_UNORM,
		R8G8B8A8_SRGB,
		B8G8R8A8_UNORM,
		B8G8R8A8_SRGB,

		R16_SFLOAT,
		R16G16_SFLOAT,
		R16G16B16A16_SFLOAT,
		R32_SFLOAT,
		R32G32_SFLOAT,
		R32G32B32_SFLOAT,
		R32G32B32A32_SFLOAT,

		// HDR render targets and IBL: same dynamic range as RGBA16F at half the
		// bandwidth, which matters for the lighting and bloom passes.
		B10G11R11_UFLOAT,
		R9G9B9E5_UFLOAT,

		R32_UINT,
		R32_SINT,
		// Integer vectors. Added for skinning: joint indices are a uvec4, and
		// without these the reflection fell back to a single component, which
		// makes the vertex stride wrong and scatters the mesh.
		R32G32_UINT,
		R32G32B32_UINT,
		R32G32B32A32_UINT,
		R32G32_SINT,
		R32G32B32_SINT,
		R32G32B32A32_SINT,

		// Block-compressed, for cooked textures (7.2). Four-by-four texel
		// blocks: BC1 and BC4 are 8 bytes a block, BC3 and BC5 are 16. A
		// compressed image cannot be blitted, so a cooked texture arrives
		// with its whole mip chain and GenerateMips never runs on it.
		BC1_UNORM,
		BC1_SRGB,
		BC3_UNORM,
		BC3_SRGB,
		BC4_UNORM,
		BC5_UNORM,
		// HDR block compression, 16 bytes a 4x4 block, RGB only. Today it is
		// a *file* format: BakedLighting stores probe cubes in it and decodes
		// on load, so no texture is created with it yet -- but the backend
		// mappings exist, so the day a probe samples it natively nothing
		// below this enum has to move.
		BC6H_UFLOAT,

		D16_UNORM,
		D32_SFLOAT,
		D24_UNORM_S8_UINT,
		D32_SFLOAT_S8_UINT,
	};

	bool IsDepthFormat(Format format);
	bool IsStencilFormat(Format format);
	// Bytes per pixel. Zero for the block-compressed formats, which have no
	// per-pixel size -- their bytes come from TextureDataSize.
	uint32_t FormatSize(Format format);

	bool IsCompressedFormat(Format format);
	// The bytes one mip level of `width` x `height` occupies, whatever the
	// format: pixels times size for ordinary formats, whole 4x4 blocks for
	// compressed ones -- a 2x2 BC mip still occupies a full block.
	uint64_t TextureDataSize(Format format, uint32_t width, uint32_t height);

	// ---------------------------------------------------------------------
	// Buffers
	// ---------------------------------------------------------------------
	enum class BufferUsage : uint32_t
	{
		None        = 0,
		Vertex      = 1u << 0,
		Index       = 1u << 1,
		Uniform     = 1u << 2,
		Storage     = 1u << 3,
		Indirect    = 1u << 4,
		TransferSrc = 1u << 5,
		TransferDst = 1u << 6,
		// Readable by an acceleration-structure build, by device address
		// (ENGINE-NOTES 7am). A creation-time property: a mesh's vertex and
		// index buffers made without it cannot be traced, and the failure is
		// a message about an address at build time rather than here. Ignored
		// on a device that cannot trace.
		AccelerationStructureInput = 1u << 7,
	};

	inline BufferUsage operator|(BufferUsage a, BufferUsage b)
	{
		return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline bool HasFlag(BufferUsage value, BufferUsage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	// Where a resource lives, and therefore how it is written to.
	enum class MemoryDomain : uint8_t
	{
		// GPU-only. Written via a staging copy. Use for data uploaded rarely.
		DeviceLocal,
		// Host-visible and persistently mapped. Use for data rewritten every
		// frame (the 2D vertex stream, per-frame uniforms).
		HostVisible,
	};

	struct BufferDesc
	{
		uint64_t      Size         = 0;
		BufferUsage   Usage        = BufferUsage::None;
		MemoryDomain  Memory       = MemoryDomain::DeviceLocal;
		std::string   DebugName;
	};

	enum class IndexType : uint8_t
	{
		UInt16,
		UInt32,
	};

	// The arguments of one indexed draw, laid out as the GPU writes them.
	//
	// **The layout is not ours to choose.** Vulkan reads it as
	// VkDrawIndexedIndirectCommand and OpenGL as DrawElementsIndirectCommand,
	// and the two agree field for field -- which is the only reason one struct
	// can serve both. A compute shader that fills these is writing a structure
	// defined by the driver, so the assert below is load-bearing rather than
	// decorative.
	//
	// FirstIndex counts indices, not bytes, on both. VertexOffset is signed on
	// both, although OpenGL calls it unsigned and means the same thing.
	struct DrawIndexedIndirectCommand
	{
		uint32_t IndexCount    = 0;
		// The field the GPU is here to write. Everything else is known when
		// the command is built; this is what a cull pass counts.
		uint32_t InstanceCount = 0;
		uint32_t FirstIndex    = 0;
		int32_t  VertexOffset  = 0;
		// **Left at zero by everything in this engine**, and read from a push
		// constant instead, because the two backends disagree about it:
		// Vulkan folds it into gl_InstanceIndex and OpenGL does not fold it
		// into gl_InstanceID. A shader written against one would index the
		// wrong instance on the other, silently.
		uint32_t FirstInstance = 0;
	};
	static_assert(sizeof(DrawIndexedIndirectCommand) == 20,
				  "Must match VkDrawIndexedIndirectCommand and DrawElementsIndirectCommand");

	// ---------------------------------------------------------------------
	// Acceleration structures (ENGINE-NOTES 7am)
	// ---------------------------------------------------------------------
	class RHIBuffer;
	class RHIAccelerationStructure;

	// One triangle mesh, as a bottom-level structure sees it: positions are
	// the first three floats of each vertex, `VertexStride` apart, indexed by
	// the index buffer. Both buffers must carry
	// BufferUsage::AccelerationStructureInput.
	struct AccelerationGeometryDesc
	{
		std::shared_ptr<RHIBuffer> Vertices;
		uint32_t  VertexStride = 0;
		uint32_t  VertexCount  = 0;
		uint64_t  VertexOffset = 0;
		std::shared_ptr<RHIBuffer> Indices;
		IndexType Type       = IndexType::UInt32;
		uint32_t  IndexCount = 0;
		uint64_t  IndexOffset = 0;
		// Opaque geometry lets a shadow ray stop at its first hit without
		// asking anyone; everything this engine draws lit is opaque.
		bool      Opaque = true;
		// The vertices will move (ENGINE-NOTES 7an): a skinned mesh posed
		// into this buffer each frame. Created for update and *not built at
		// creation* -- the buffer is empty then -- so the first
		// RHICommandList::BuildBottomLevelAS builds it and every later one
		// refits it in place from whatever the buffer holds. Same topology
		// throughout: the index buffer is the one thing that must not change.
		bool      Dynamic = false;
		std::string DebugName;
	};

	// One placement of a bottom-level structure in a top-level one.
	struct AccelerationInstance
	{
		// World transform. The backend takes the upper 3x4 of it.
		float    Transform[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
		std::shared_ptr<RHIAccelerationStructure> Blas;
		// What a shader reads back as the instance's custom index; 24 bits.
		uint32_t CustomIndex = 0;
		// Which ray masks see this instance. 0xFF is every ray.
		uint8_t  Mask = 0xFF;

		// **Report this instance's hits as candidates instead of committing
		// them**, so traversal can ask the shader whether the triangle is
		// really there. Alpha-tested geometry needs it: its shape is its
		// mesh minus whatever its base colour's alpha removes, and only a
		// texture fetch knows which.
		//
		// Per instance and not per geometry, because a bottom-level structure
		// is built per *mesh* and being cut out is a property of the
		// *material* -- the same mesh may be solid in one place and a cutout
		// in another, and duplicating the structure to say so would cost far
		// more than this bit.
		//
		// Left false, an instance is traversed exactly as before: the
		// hardware commits its hits without ever entering the loop, which is
		// what keeps a scene with no cutouts paying nothing for this.
		bool     ForceNoOpaque = false;
	};

	// Declared ahead of SamplerDesc, which needs it for comparison sampling.
	enum class CompareOp : uint8_t
	{
		Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always
	};

	// ---------------------------------------------------------------------
	// **Reverse-Z: the near plane is 1 and the far plane is 0.**
	//
	// A depth buffer stores 1/z, which is dense near the camera and sparse
	// far from it. A float stores its own values densely near zero. Line the
	// two up the conventional way -- near at 0, far at 1 -- and the two
	// densities land on the same end, so the far half of the scene is
	// described by almost no distinct values at all. At the Golden Gate's
	// scale, a 5 cm near clip against a 4 km far plane, that leaves the far
	// deck and the cables sharing depth values and z-fighting.
	//
	// Flipping the projection puts the float's dense end where 1/z is sparse
	// and the two errors cancel almost exactly. It is not a quality setting
	// or a trade: it is free, and the conventional arrangement is simply the
	// wrong way round for a float buffer.
	//
	// **It only pays on a float depth buffer.** Fixed-point depth quantises
	// uniformly and does not care which end is which, so a D24_UNORM target
	// gets nothing from this -- neither does it break. The scene and the
	// shadow maps are D32_SFLOAT, which is what makes it worth doing.
	//
	// **One convention, engine-wide.** Perspective, orthographic, shadow
	// maps, the UI: all of it. Two conventions in one engine is a pipeline
	// that forgot to override a default and a depth test that silently keeps
	// the wrong fragment -- which looks like a geometry bug, not a depth one.
	// So the rule is stated here and referred to, never re-typed.
	//
	// A pass that genuinely wants the other sense -- there are none today --
	// must say so out loud rather than leave the default alone.
	inline constexpr CompareOp kDepthCompare = CompareOp::GreaterOrEqual;
	inline constexpr float     kDepthClear   = 0.0f;

	// A depth bias pushes a fragment away from the camera to keep it off the
	// surface it is compared with. "Away" is now *down*, so every bias that
	// was positive is negative -- and a bias left with its old sign pulls the
	// fragment forward instead, which is shadow acne turned up rather than
	// off.

	// ---------------------------------------------------------------------
	// Textures and samplers
	// ---------------------------------------------------------------------
	enum class TextureUsage : uint32_t
	{
		None            = 0,
		Sampled         = 1u << 0,
		ColorAttachment = 1u << 1,
		DepthAttachment = 1u << 2,
		Storage         = 1u << 3,
		TransferSrc     = 1u << 4,
		TransferDst     = 1u << 5,
	};

	inline TextureUsage operator|(TextureUsage a, TextureUsage b)
	{
		return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline bool HasFlag(TextureUsage value, TextureUsage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	enum class TextureType : uint8_t
	{
		Texture2D,
		Texture2DArray,   // cascaded shadow maps, texture atlases
		TextureCube,      // point-light shadows, environment / irradiance maps
		// N cubes end to end, so a shader can pick one per object rather than
		// per draw. Layers is 6N: the faces of cube k are layers 6k..6k+5, in
		// the same face order a single cube uses.
		TextureCubeArray,

		// A volume, sampled with three coordinates and filtered along all
		// three. Colour-grading LUTs are the reason it exists (9.1).
		//
		// **Not the same thing as Texture2DArray**, and the difference is the
		// whole point rather than a technicality: an array's layers are
		// discrete and are never blended, a volume's depth slices are. A LUT
		// built on an array is a LUT with no interpolation along blue, which
		// grades the picture and grades it wrongly. ENGINE-NOTES 7t.
		Texture3D,
	};

	struct TextureDesc
	{
		uint32_t     Width     = 1;
		uint32_t     Height    = 1;
		// The third extent, read only for Texture3D. Separate from Layers
		// because the two are filtered differently -- see TextureType.
		uint32_t     Depth     = 1;
		uint32_t     MipLevels = 1;   // 0 means "generate the full chain"
		uint32_t     Layers    = 1;   // cube faces count as 6 layers
		TextureType  Type      = TextureType::Texture2D;
		Format       Format    = Format::R8G8B8A8_UNORM;
		TextureUsage Usage     = TextureUsage::Sampled;
		uint32_t     Samples   = 1;
		std::string  DebugName;
	};

	// How many array layers this description really asks for.
	//
	// A cube is six layers whatever its Layers field says, and every caller
	// that allocates one either sets 6 or leaves the default 1 -- so the floor
	// has to live somewhere. It lives here rather than in each backend because
	// it did live in each backend, and the two had already drifted: Vulkan
	// clamped and OpenGL did not, which for a plain cube costs nothing and for
	// a cube array is the difference between allocating N cubes and one.
	uint32_t EffectiveLayers(const TextureDesc& desc);

	enum class FilterMode : uint8_t { Nearest, Linear };
	enum class MipmapMode : uint8_t { Nearest, Linear };
	enum class WrapMode   : uint8_t { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };

	enum class BorderColor : uint8_t
	{
		TransparentBlack,
		OpaqueBlack,
		OpaqueWhite,      // the useful one for shadow maps: outside = fully lit
	};

	struct SamplerDesc
	{
		FilterMode MinFilter = FilterMode::Linear;
		FilterMode MagFilter = FilterMode::Linear;
		MipmapMode Mipmap    = MipmapMode::Linear;
		WrapMode   WrapU     = WrapMode::Repeat;
		WrapMode   WrapV     = WrapMode::Repeat;
		WrapMode   WrapW     = WrapMode::Repeat;
		float      MaxAnisotropy = 1.0f;
		float      MinLod = 0.0f;
		float      MaxLod = 1000.0f;
		BorderColor Border = BorderColor::OpaqueWhite;

		// A comparison sampler returns the filtered result of comparing the
		// sampled depth against a reference, which is how hardware PCF shadow
		// filtering works. Maps to sampler2DShadow in GLSL.
		bool      CompareEnable = false;
		CompareOp Compare = kDepthCompare;

		bool operator==(const SamplerDesc&) const = default;
	};

	// ---------------------------------------------------------------------
	// Pipeline state
	// ---------------------------------------------------------------------
	enum class PrimitiveTopology : uint8_t
	{
		TriangleList,
		TriangleStrip,
		LineList,
		LineStrip,
		PointList,
	};

	enum class PolygonMode : uint8_t { Fill, Line, Point };
	enum class CullMode    : uint8_t { None, Front, Back };
	enum class FrontFace   : uint8_t { CounterClockwise, Clockwise };

	enum class BlendPreset : uint8_t
	{
		Opaque,        // no blending
		AlphaBlend,    // src.a, 1-src.a
		Additive,      // one, one
		PremultipliedAlpha,

		// The two halves of weighted-blended order-independent transparency.
		// They are always used together, on two attachments of one draw, which
		// is the reason a pipeline can carry a blend state per attachment at
		// all -- see GraphicsPipelineDesc::BlendPerAttachment.
		//
		// Accumulation sums premultiplied colour and weight: order cannot
		// matter to a sum, which is the whole trick.
		WeightedAccumulate,   // one, one
		// Revealage multiplies what is left uncovered: also order-independent,
		// because multiplication commutes too.
		WeightedRevealage,    // zero, one-minus-src-colour
	};

	struct DepthStencilState
	{
		bool      DepthTestEnable  = true;
		bool      DepthWriteEnable = true;
		CompareOp DepthCompare     = kDepthCompare;

		bool operator==(const DepthStencilState&) const = default;
	};

	struct RasterizerState
	{
		PolygonMode Polygon   = PolygonMode::Fill;
		CullMode    Cull      = CullMode::Back;
		FrontFace   Front     = FrontFace::CounterClockwise;
		float       LineWidth = 1.0f;

		// Shadow-map passes need this to push depth away from the light and
		// avoid self-shadowing acne. Slope-scaled bias handles surfaces at
		// grazing angles that a constant bias cannot.
		bool  DepthBiasEnable = false;
		float DepthBiasConstant = 0.0f;
		float DepthBiasSlope    = 0.0f;
		float DepthBiasClamp    = 0.0f;

		bool operator==(const RasterizerState&) const = default;
	};

	// ---------------------------------------------------------------------
	// Vertex input
	// ---------------------------------------------------------------------
	struct VertexAttribute
	{
		uint32_t Location = 0;
		uint32_t Binding  = 0;
		Format   Format   = Format::R32G32B32_SFLOAT;
		uint32_t Offset   = 0;

		bool operator==(const VertexAttribute&) const = default;
	};

	struct VertexBinding
	{
		uint32_t Binding   = 0;
		uint32_t Stride    = 0;
		bool     PerInstance = false;

		bool operator==(const VertexBinding&) const = default;
	};

	struct VertexLayout
	{
		std::vector<VertexBinding>   Bindings;
		std::vector<VertexAttribute> Attributes;

		bool operator==(const VertexLayout&) const = default;
	};

	// ---------------------------------------------------------------------
	// Shaders
	// ---------------------------------------------------------------------
	enum class ShaderStage : uint32_t
	{
		None     = 0,
		Vertex   = 1u << 0,
		Fragment = 1u << 1,
		Compute  = 1u << 2,
		// VK_EXT_mesh_shader (roadmap 8.3's second half). Vulkan only: OpenGL
		// has no core equivalent, and a pipeline holding one of these fails
		// to build there -- which is why every caller checks
		// DeviceCaps::SupportsMeshShading first.
		Mesh     = 1u << 3,
	};

	inline ShaderStage operator|(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline bool HasFlag(ShaderStage value, ShaderStage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}

	const char* ShaderStageName(ShaderStage stage);

	enum class ResourceType : uint8_t
	{
		UniformBuffer,
		StorageBuffer,
		CombinedImageSampler,
		StorageImage,
		// `uniform accelerationStructureEXT`: a top-level structure a ray
		// query traces into (ENGINE-NOTES 7am). Vulkan only; a shader that
		// declares one cannot be cross-compiled to OpenGL and is never asked
		// to be.
		AccelerationStructure,
	};

	// One binding inside a resource set, recovered from SPIR-V reflection.
	struct ResourceBinding
	{
		uint32_t     Binding     = 0;
		// >1 for arrays such as u_Textures[32]. **0 for a runtime-sized
		// array** -- `sampler2D u_Textures[]` -- which is what a shader
		// declares to read the bindless heap (ENGINE-NOTES 7al). A set with one
		// is laid out by the device's shared heap layout rather than from this
		// description, and cannot be cross-compiled to OpenGL at all.
		uint32_t     Count       = 1;
		ResourceType Type        = ResourceType::UniformBuffer;
		ShaderStage  Stages      = ShaderStage::None;
		uint32_t     BlockSize   = 0;   // buffers only
		std::string  Name;

		bool operator==(const ResourceBinding&) const = default;
	};

	struct ResourceSetLayoutDesc
	{
		uint32_t                     Set = 0;
		std::vector<ResourceBinding> Bindings;

		bool operator==(const ResourceSetLayoutDesc&) const = default;
	};

	struct PushConstantRange
	{
		uint32_t    Offset = 0;
		uint32_t    Size   = 0;
		ShaderStage Stages = ShaderStage::None;

		bool operator==(const PushConstantRange&) const = default;
	};

	// How a buffer is about to be used, either side of a barrier.
	//
	// Deliberately a short list of *uses* rather than a general access/stage
	// pair. The two backends express synchronisation in incompatible terms --
	// Vulkan wants an access mask and a stage on each side, OpenGL wants a
	// bitfield naming what happens after -- and the only honest thing both can
	// implement is a statement of what the buffer was used for and what it is
	// used for next. Anything finer would be Vulkan's model with an OpenGL
	// approximation hiding inside it.
	enum class BufferSync : uint8_t
	{
		// Written by a compute shader.
		ComputeWrite,
		// Read by a compute shader.
		ComputeRead,
		// Read by a graphics shader -- vertex or fragment, storage or uniform.
		ShaderRead,
		// Written by any shader that shades -- vertex, fragment or compute --
		// through a storage buffer: the ray counters a fragment adds to with
		// an atomic (WR-16 S0). Wider than ComputeWrite on purpose. The write
		// can come from any stage, and a barrier that named the one stage
		// doing it today would stop covering the next; the cost of the wider
		// mask is nothing on a buffer this size.
		ShaderWrite,
		// Fetched as vertex or index data by the fixed-function input stage.
		VertexInput,
		// Read as the arguments of an indirect draw or dispatch.
		IndirectRead,
		// Written by a transfer: a buffer upload or a copy.
		TransferWrite,
		// Read as the vertex or index input of an acceleration-structure
		// build (ENGINE-NOTES 7an): a posed buffer a compute pass wrote and
		// a bottom-level refit is about to read. A shader read at the build
		// stage, which is what the specification calls a build input, and
		// not an acceleration-structure read -- that is the structure
		// itself, and BuildBottomLevelAS orders it.
		AccelerationBuild,
	};

	// The same, for a texture a shader writes through a storage image
	// (ENGINE-NOTES 7bc). Narrower than BufferSync because a texture is
	// written by exactly two kinds of pass -- a dispatch, or a fragment stage
	// doing imageStore -- and read by any stage through a sampler or an
	// imageLoad.
	enum class TextureSync : uint8_t
	{
		// Written by a compute shader's imageStore.
		ComputeWrite,
		// Written by a fragment shader's imageStore -- the voxeliser.
		FragmentWrite,
		// Read by any shader stage: sampled, or imageLoad.
		ShaderRead,
	};

	// ---------------------------------------------------------------------
	// Render passes
	// ---------------------------------------------------------------------
	enum class LoadOp  : uint8_t { Load, Clear, DontCare };
	enum class StoreOp : uint8_t { Store, DontCare };

	struct ClearValue
	{
		float    Color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float    Depth    = kDepthClear;
		uint32_t Stencil  = 0;
	};

	struct Viewport
	{
		float X = 0.0f, Y = 0.0f;
		float Width = 0.0f, Height = 0.0f;
		float MinDepth = 0.0f, MaxDepth = 1.0f;
	};

	struct Rect2D
	{
		int32_t  X = 0, Y = 0;
		uint32_t Width = 0, Height = 0;
	};

	// ---------------------------------------------------------------------
	// Device capabilities
	// ---------------------------------------------------------------------
	struct DeviceCaps
	{
		std::string DeviceName;
		std::string APIName;
		std::string DriverInfo;

		uint32_t MaxTextureSlots        = 16;
		uint32_t MaxTextureSize         = 2048;
		uint32_t MaxPushConstantSize    = 128;
		uint32_t UniformBufferAlignment = 256;
		uint64_t VideoMemoryBytes       = 0;

		// The largest MSAA sample count the scene target can actually use --
		// the intersection of what the device offers for a colour attachment,
		// a depth attachment, and sampling both, since the scene target is all
		// three at once. Always a power of two; 1 means no MSAA at all.
		//
		// **Queried rather than assumed.** Nothing used to ask, and the render
		// setting's own comment claimed it was "clamped to what the hardware
		// actually offers" while being clamped to a constant.
		uint32_t MaxSampleCount       = 1;

		bool SupportsAnisotropy       = false;
		float MaxAnisotropy           = 1.0f;
		bool SupportsDynamicRendering = false;
		// Whether RHIDevice::CreateBindlessTextureSet can succeed: a shader
		// may declare `sampler2D u_Textures[]` and index it per instance.
		// Vulkan 1.2's descriptor indexing, when the driver has the five
		// feature bits the heap needs; false on OpenGL, always, because 4.5
		// has no object for it (ENGINE-NOTES 7al).
		bool SupportsDescriptorIndexing = false;
		// How many textures one heap can hold. Zero when unsupported.
		uint32_t MaxBindlessTextures  = 0;
		// Whether a shader may trace rays into an acceleration structure
		// (ENGINE-NOTES 7am): the acceleration-structure, ray-query and
		// deferred-host-operations extensions plus the buffer-device-address
		// feature, all present. The first capability with no OpenGL
		// implementation at all -- false there, always, and the shadow-map
		// path is what runs.
		bool SupportsRayQuery         = false;
		// Whether a pipeline may replace the vertex stage with a mesh shader
		// (VK_EXT_mesh_shader): meshlet paths ask this before compiling
		// anything. False on OpenGL always, and false on Vulkan devices or
		// drivers without the extension -- the classic vertex path is what
		// runs there, drawing the identical image.
		bool SupportsMeshShading      = false;
		bool SupportsTimestampQueries = false;

		// Compute shaders and dispatch. Core in Vulkan and in OpenGL 4.3, so
		// this is true on anything that got far enough to create a device --
		// but it is asked rather than assumed, because a feature whose absence
		// silently does nothing is the kind that gets discovered by a user.
		bool SupportsCompute          = false;
		// Largest local_size_x a shader may declare, and the largest group
		// count a single dispatch may ask for.
		uint32_t MaxComputeWorkGroupSize  = 0;
		uint32_t MaxComputeWorkGroupCount = 0;

		// The most bytes of one storage buffer a shader may address.
		//
		// **This is what decides how many objects a pass may hold, and it is
		// the device's number rather than one this engine picked.** A buffer
		// larger than this may allocate perfectly well and then be readable
		// only up to the limit, so the shader indexes past the end and reads
		// whatever is there -- no error, no validation message, just wrong
		// answers from the tail of a big scene. Anything sizing a table by
		// element count must divide this by the element's size and say so
		// when the scene exceeds it.
		//
		// Vulkan guarantees at least 128 MB; OpenGL 4.3 guarantees 16 MB.
		uint64_t MaxStorageBufferBytes = 0;

		// A fragment shader may write a storage image (ENGINE-NOTES 7bc):
		// `fragmentStoresAndAtomics` on Vulkan, image load/store on OpenGL
		// 4.2. Compute writes need nothing beyond SupportsCompute; this is
		// the one the voxeliser asks before it draws.
		bool SupportsFragmentStores   = false;
	};
}
