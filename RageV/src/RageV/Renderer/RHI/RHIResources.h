#pragma once

// GPU resource interfaces. Instances are created through RHIDevice and are
// immutable in shape once created -- resizing means creating a new one.

#include "RHITypes.h"

namespace RageV::RHI
{
	class RHIBuffer
	{
	public:
		virtual ~RHIBuffer() = default;

		const BufferDesc& GetDesc() const { return m_Desc; }
		uint64_t GetSize() const { return m_Desc.Size; }

		// Copies size bytes into the buffer. For MemoryDomain::HostVisible this
		// writes straight through the persistent mapping; for DeviceLocal it
		// goes via a staging buffer and is therefore far more expensive.
		virtual void Upload(const void* data, uint64_t size, uint64_t offset = 0) = 0;

		// Persistent pointer for HostVisible buffers, nullptr otherwise. Prefer
		// this over Upload for per-frame streaming -- it avoids a memcpy.
		virtual void* GetMappedPointer() = 0;

	protected:
		explicit RHIBuffer(const BufferDesc& desc) : m_Desc(desc) {}
		BufferDesc m_Desc;
	};

	// An acceleration structure (ENGINE-NOTES 7am): bottom level is one
	// triangle mesh, built once -- or, when created Dynamic, built and then
	// refit by RHICommandList::BuildBottomLevelAS as its vertices move
	// (7an); top level is a list of placed bottom-level ones, rebuilt per
	// frame by RHICommandList::BuildTopLevelAS. A shader traces into a
	// top-level one through a resource set binding.
	class RHIAccelerationStructure
	{
	public:
		virtual ~RHIAccelerationStructure() = default;
		bool IsTopLevel() const { return m_TopLevel; }
		// For a top-level structure: how many instances a build may carry.
		uint32_t GetMaxInstances() const { return m_MaxInstances; }
		// For a bottom-level structure: whether it was created to be refit.
		bool IsDynamic() const { return m_Dynamic; }

	protected:
		RHIAccelerationStructure(bool topLevel, uint32_t maxInstances, bool dynamic = false)
			: m_TopLevel(topLevel), m_MaxInstances(maxInstances), m_Dynamic(dynamic) {}
		bool     m_TopLevel = false;
		uint32_t m_MaxInstances = 0;
		bool     m_Dynamic = false;
	};

	class RHISampler
	{
	public:
		virtual ~RHISampler() = default;
		const SamplerDesc& GetDesc() const { return m_Desc; }

	protected:
		explicit RHISampler(const SamplerDesc& desc) : m_Desc(desc) {}
		SamplerDesc m_Desc;
	};

	class RHITexture
	{
	public:
		virtual ~RHITexture() = default;

		const TextureDesc& GetDesc() const { return m_Desc; }
		uint32_t GetWidth()  const { return m_Desc.Width; }
		uint32_t GetHeight() const { return m_Desc.Height; }
		Format   GetFormat() const { return m_Desc.Format; }

		virtual void Upload(const void* data, uint64_t size) = 0;

		// One array layer of mip 0. A cube's six faces are layers 0..5 in the
		// +X, -X, +Y, -Y, +Z, -Z order both APIs use, so a caller that gets the
		// order right for one backend has it right for the other.
		//
		// Separate from Upload rather than a defaulted parameter because the
		// caller must also decide when the chain is generated: doing it per
		// face would filter each face five times over and still be wrong at the
		// seams, so mips are generated once, after the last face lands.
		virtual void UploadLayer(const void* data, uint64_t size, uint32_t layer) = 0;

		// One mip level of one layer, for textures whose chain was built
		// offline. This is how a cooked texture arrives: a compressed image
		// cannot be blitted, so GenerateMips can never make its chain -- every
		// level is uploaded, and none is derived. `size` must be exactly
		// TextureDataSize for that level's dimensions.
		virtual void UploadMip(const void* data, uint64_t size, uint32_t mip,
							   uint32_t layer) = 0;

		virtual void GenerateMips() = 0;

		// Opaque handle for ImGui::Image. GL hands back the texture name;
		// Vulkan hands back a VkDescriptorSet allocated by the ImGui backend.
		virtual uint64_t GetImGuiHandle() = 0;

	protected:
		explicit RHITexture(const TextureDesc& desc) : m_Desc(desc) {}
		TextureDesc m_Desc;
	};

	// ---------------------------------------------------------------------
	// Render targets
	// ---------------------------------------------------------------------
	struct AttachmentDesc
	{
		Format  Format  = Format::R8G8B8A8_UNORM;
		LoadOp  Load    = LoadOp::Clear;
		StoreOp Store   = StoreOp::Store;
	};

	struct RenderTargetDesc
	{
		uint32_t                    Width  = 1280;
		uint32_t                    Height = 720;
		uint32_t                    Samples = 1;
		// May be empty: a shadow map is a depth-only target.
		std::vector<AttachmentDesc> ColorAttachments;
		AttachmentDesc              DepthAttachment;
		bool                        HasDepth = true;
		// >1 allocates an array target and renders to layer `Layer` of it,
		// which is how cascaded and cube shadow maps are filled.
		uint32_t                    Layers = 1;
		bool                        DepthSampled = false;   // shadow maps sample the depth afterwards
		std::string                 DebugName;
	};

	class RHIRenderTarget
	{
	public:
		virtual ~RHIRenderTarget() = default;

		const RenderTargetDesc& GetDesc() const { return m_Desc; }
		uint32_t GetWidth()  const { return m_Desc.Width; }
		uint32_t GetHeight() const { return m_Desc.Height; }

		virtual void Resize(uint32_t width, uint32_t height) = 0;
		virtual Ref<RHITexture> GetColorTexture(uint32_t index = 0) const = 0;
		virtual Ref<RHITexture> GetDepthTexture() const = 0;

	protected:
		explicit RHIRenderTarget(const RenderTargetDesc& desc) : m_Desc(desc) {}
		RenderTargetDesc m_Desc;
	};
}
