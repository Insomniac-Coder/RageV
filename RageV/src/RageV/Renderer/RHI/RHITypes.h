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

	// Declared ahead of SamplerDesc, which needs it for comparison sampling.
	enum class CompareOp : uint8_t
	{
		Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always
	};

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
	};

	struct TextureDesc
	{
		uint32_t     Width     = 1;
		uint32_t     Height    = 1;
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
		CompareOp Compare = CompareOp::LessOrEqual;

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
		CompareOp DepthCompare     = CompareOp::LessOrEqual;

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
	};

	// One binding inside a resource set, recovered from SPIR-V reflection.
	struct ResourceBinding
	{
		uint32_t     Binding     = 0;
		uint32_t     Count       = 1;   // >1 for arrays such as u_Textures[32]
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
		// Fetched as vertex or index data by the fixed-function input stage.
		VertexInput,
		// Read as the arguments of an indirect draw or dispatch.
		IndirectRead,
		// Written by a transfer: a buffer upload or a copy.
		TransferWrite,
	};

	// ---------------------------------------------------------------------
	// Render passes
	// ---------------------------------------------------------------------
	enum class LoadOp  : uint8_t { Load, Clear, DontCare };
	enum class StoreOp : uint8_t { Store, DontCare };

	struct ClearValue
	{
		float    Color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float    Depth    = 1.0f;
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

		bool SupportsAnisotropy       = false;
		float MaxAnisotropy           = 1.0f;
		bool SupportsDynamicRendering = false;
		bool SupportsDescriptorIndexing = false;
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
	};
}
