#pragma once

#include "VulkanCommon.h"
#include "RageV/Renderer/RHI/RHIResources.h"
#include <vk_mem_alloc.h>

namespace RageV::Vk
{
	class VulkanDevice;

	class VulkanBuffer final : public RHI::RHIBuffer
	{
	public:
		VulkanBuffer(VulkanDevice& device, const RHI::BufferDesc& desc);
		~VulkanBuffer() override;

		void  Upload(const void* data, uint64_t size, uint64_t offset = 0) override;
		void* GetMappedPointer() override { return m_Mapped; }

		VkBuffer GetHandle() const { return m_Buffer; }
		// The address a build or a shader reads this buffer at. Zero unless the
		// buffer was created with a usage that carries the address bit.
		uint64_t GetDeviceAddress() const override;

	private:
		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkBuffer      m_Buffer     = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		void*         m_Mapped     = nullptr;
		bool          m_HasAddress = false;
		// What this buffer registered with the GPU address-range registry, so
		// the deletion can hand both back and a fault address can be named.
		uint64_t      m_RangeBase   = 0;
		uint64_t      m_RangeSerial = 0;
	};

	// An acceleration structure (ENGINE-NOTES 7am), either level. Owns the
	// VkAccelerationStructureKHR, the buffer it lives in, and -- for a
	// top-level one -- the instance buffer and scratch a per-frame rebuild
	// writes into, sized once for `maxInstances` so a rebuild allocates
	// nothing. A static bottom-level one is built in the constructor,
	// immediately, with transient scratch, and never touched again; a
	// Dynamic one (7an) keeps its geometry and scratch and is built, then
	// refit, by BuildBottomLevel from the frame's command buffer.
	class VulkanAccelerationStructure final : public RHI::RHIAccelerationStructure
	{
	public:
		// Bottom level: builds now from the geometry, unless Dynamic.
		VulkanAccelerationStructure(VulkanDevice& device, const RHI::AccelerationGeometryDesc& geometry);
		// Top level: allocated for up to maxInstances; built by Build().
		VulkanAccelerationStructure(VulkanDevice& device, uint32_t maxInstances);
		~VulkanAccelerationStructure() override;

		VkAccelerationStructureKHR GetHandle() const { return m_Structure; }
		VkDeviceAddress GetDeviceAddress() const { return m_Address; }

		// Top level only. Packs the instances into the instance buffer, records
		// the build, and the barrier that makes it readable by shaders.
		void Build(VkCommandBuffer cmd, const RHI::AccelerationInstance* instances, uint32_t count);

		// Dynamic bottom level only. A full build the first time, an update in
		// place after, from the vertex buffer as it is now; ends with the
		// barrier a top-level build recorded after it needs.
		void BuildBottomLevel(VkCommandBuffer cmd);

	private:
		struct Backing
		{
			VkBuffer      Buffer = VK_NULL_HANDLE;
			VmaAllocation Allocation = VK_NULL_HANDLE;
			void*         Mapped = nullptr;
			VkDeviceAddress Address = 0;
			uint64_t      RangeSerial = 0;
		};
		Backing CreateBacking(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, const char* name);
		void    Destroy(Backing& backing);
		void    NoteStructureRange(const char* name, uint64_t size);
		void    VerifyInstanceReference(uint64_t reference, uint32_t index) const;

		// The structure's own device address, which a top-level instance
		// records and a ray query then follows. On some drivers it is the
		// storage buffer's address and on others it is not, so it is
		// registered separately when it differs -- a stale instance reference
		// faults *here*, not in the buffer.
		uint64_t m_StructureRangeBase = 0;
		uint64_t m_StructureRangeSerial = 0;

		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkAccelerationStructureKHR m_Structure = VK_NULL_HANDLE;
		VkDeviceAddress m_Address = 0;
		Backing m_Storage;    // the structure itself
		Backing m_Scratch;    // top level and dynamic bottom level: kept; static bottom level: transient
		Backing m_Instances;  // top level only, host visible

		// Dynamic bottom level only: the geometry as described at creation,
		// re-recorded by every BuildBottomLevel, and whether one has run yet
		// (the first is a build; the rest are updates of it).
		VkAccelerationStructureGeometryKHR m_Geometry{};
		uint32_t m_PrimitiveCount = 0;
		bool     m_Built = false;

		// Top level only: what the last build carried, so the next one can
		// decide between refitting the structure it already has and building
		// a fresh one. See Build().
		uint32_t m_TopInstances = 0;
		uint32_t m_TopRefits = 0;
		bool     m_TopBuilt = false;
	};

	class VulkanSampler final : public RHI::RHISampler
	{
	public:
		VulkanSampler(VulkanDevice& device, const RHI::SamplerDesc& desc);
		~VulkanSampler() override;

		VkSampler GetHandle() const { return m_Sampler; }

	private:
		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkSampler     m_Sampler = VK_NULL_HANDLE;
	};

	class VulkanTexture final : public RHI::RHITexture
	{
	public:
		VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc);
		// Wraps an image this class does not own -- used for render-target
		// attachments whose storage belongs to the target.
		VulkanTexture(VulkanDevice& device, const RHI::TextureDesc& desc,
					  VkImage image, VkImageView view, bool owned);
		~VulkanTexture() override;

		void Upload(const void* data, uint64_t size) override;
		void UploadLayer(const void* data, uint64_t size, uint32_t layer) override;
		void UploadRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
						  const void* data, uint64_t size) override;
		void UploadMip(const void* data, uint64_t size, uint32_t mip,
					   uint32_t layer) override;
		void GenerateMips() override;
		// The same blit chain, recorded into a caller's command buffer rather
		// than submitted on its own. What GenerateMips() is built from.
		void RecordGenerateMips(VkCommandBuffer cmd);
		uint64_t GetImGuiHandle() override;

		VkImage       GetImage()  const { return m_Image; }
		VkImageView   GetView()   const { return m_View; }
		VkImageLayout GetLayout() const { return m_Layout; }
		void SetLayout(VkImageLayout layout) { m_Layout = layout; }

		// A view of one mip level, for a storage-image binding (ENGINE-NOTES
		// 7bc): a compute pass writing level 3 binds level 3, and a view over
		// the whole chain is not a thing an image descriptor can take. Made
		// on first use and kept; the whole-chain view above stays what a
		// sampler sees.
		VkImageView GetMipView(uint32_t mip);

		// Whether this texture was created for storage and so lives in
		// VK_IMAGE_LAYOUT_GENERAL for its whole life. The descriptor writes
		// ask, because the layout they name has to be the one the image is in.
		bool IsStorage() const { return RHI::HasFlag(m_Desc.Usage, RHI::TextureUsage::Storage); }

		// Records a layout transition covering every mip and layer.
		//
		// The stage and access are implied by the two layouts, which is enough
		// for everything the backend does with an image it draws into or
		// samples. `extraStages`/`extraAccess` widen both halves of the
		// dependency for the one case that is not implied by a layout: a
		// multisample resolve target, whose write the spec puts in the colour
		// attachment output stage whatever the attachment's own aspect is.
		void TransitionTo(VkCommandBuffer cmd, VkImageLayout newLayout,
						  VkPipelineStageFlags2 extraStages = 0,
						  VkAccessFlags2 extraAccess = 0);

	private:
		void CreateImage();

		// Copies one mip of one array layer through a staging buffer. Shared
		// by every upload path; they differ only in which level and layer
		// they name.
		void StageInto(const void* data, uint64_t size, uint32_t mip, uint32_t layer);
		// The same staging copy into a rectangle of mip 0, layer 0.
		void StageRegion(const void* data, uint64_t size, uint32_t x, uint32_t y,
						 uint32_t width, uint32_t height);

		// A cube is six layers whether or not the desc said so, which is what
		// CreateImage allocated. Anything walking layers has to agree with it.
		uint32_t EffectiveLayers() const;

		VulkanDevice& m_Device;
		std::shared_ptr<DeletionQueue> m_Deletion;
		VkImage       m_Image      = VK_NULL_HANDLE;
		VkImageView   m_View       = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		VkImageLayout m_Layout     = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageAspectFlags m_Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		bool m_Owned = true;

		// Per-mip views, indexed by level; null until GetMipView asks.
		std::vector<VkImageView> m_MipViews;

		// Lazily created descriptor set for ImGui::Image.
		VkDescriptorSet m_ImGuiDescriptor = VK_NULL_HANDLE;
		VkSampler       m_ImGuiSampler    = VK_NULL_HANDLE;
	};

	class VulkanRenderTarget final : public RHI::RHIRenderTarget
	{
	public:
		VulkanRenderTarget(VulkanDevice& device, const RHI::RenderTargetDesc& desc);
		~VulkanRenderTarget() override;

		void Resize(uint32_t width, uint32_t height) override;
		RHI::Ref<RHI::RHITexture> GetColorTexture(uint32_t index = 0) const override;
		// The depth resolve where there is one, for the same reason
		// GetColorTexture hands out the colour resolve: a caller asking a
		// target for "the depth" wants something a sampler2D can read.
		RHI::Ref<RHI::RHITexture> GetDepthTexture() const override
		{
			return m_DepthResolve ? m_DepthResolve : m_Depth;
		}

		const std::vector<RHI::Ref<VulkanTexture>>& GetColorTextures() const { return m_Color; }
		VulkanTexture* GetDepth() const { return m_Depth.get(); }

		// The single-sampled copy the hardware resolves each attachment into,
		// or null when the target is not multisampled. Only the command list
		// wants these: everyone else asks GetColorTexture and is handed the
		// resolve without knowing there was one.
		VulkanTexture* GetResolve(uint32_t index) const
		{
			return index < m_Resolve.size() ? m_Resolve[index].get() : nullptr;
		}

		// And the depth's, which exists only when the depth is both
		// multisampled and sampled by something.
		VulkanTexture* GetDepthResolve() const { return m_DepthResolve.get(); }

	private:
		void Build();

		VulkanDevice& m_Device;
		std::vector<RHI::Ref<VulkanTexture>> m_Color;
		std::vector<RHI::Ref<VulkanTexture>> m_Resolve;
		RHI::Ref<VulkanTexture> m_Depth;
		RHI::Ref<VulkanTexture> m_DepthResolve;
	};
}
