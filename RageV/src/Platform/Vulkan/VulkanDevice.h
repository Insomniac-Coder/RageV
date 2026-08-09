#pragma once

#include "VulkanCommon.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

#include <vk_mem_alloc.h>
#include <deque>
#include <functional>

struct GLFWwindow;

namespace RageV::Vk
{
	class VulkanCommandList;

	struct QueueFamilies
	{
		uint32_t Graphics = UINT32_MAX;
		uint32_t Present  = UINT32_MAX;
		uint32_t Transfer = UINT32_MAX;

		bool IsComplete() const { return Graphics != UINT32_MAX && Present != UINT32_MAX; }
	};

	// One slot in the frames-in-flight ring. Everything the CPU writes while
	// recording frame N lives here, so frame N+1 never touches it.
	struct FrameContext
	{
		VkCommandPool   CommandPool      = VK_NULL_HANDLE;
		VkCommandBuffer CommandBuffer    = VK_NULL_HANDLE;
		VkSemaphore     ImageAvailable   = VK_NULL_HANDLE;
		VkFence         InFlight         = VK_NULL_HANDLE;
	};

	class VulkanDevice final : public RHI::RHIDevice
	{
	public:
		explicit VulkanDevice(const RHI::DeviceDesc& desc);
		~VulkanDevice() override;

		RHI::Backend GetBackend() const override { return RHI::Backend::Vulkan; }
		const RHI::DeviceCaps& GetCaps() const override { return m_Caps; }

		RHI::RHICommandList* BeginFrame() override;
		void EndFrame() override;
		void WaitIdle() override;
		void RequestCapture(CaptureCallback callback) override;
		void CaptureSwapchainImage();
		void OnResize(uint32_t width, uint32_t height) override;
		void SetVSync(bool enabled) override;
		bool IsVSync() const override { return m_VSync; }

		uint32_t GetFramesInFlight() const override { return m_FramesInFlight; }
		uint32_t GetFrameIndex() const override { return m_FrameIndex; }

		RHI::Format GetSwapchainFormat() const override { return FromVkFormat(m_SwapchainFormat); }
		RHI::Format GetSwapchainDepthFormat() const override { return FromVkFormat(m_DepthFormat); }
		uint32_t GetSwapchainWidth()  const override { return m_SwapchainExtent.width; }
		uint32_t GetSwapchainHeight() const override { return m_SwapchainExtent.height; }

		RHI::Ref<RHI::RHIBuffer>       CreateBuffer(const RHI::BufferDesc& desc) override;
		RHI::Ref<RHI::RHITexture>      CreateTexture(const RHI::TextureDesc& desc) override;
		RHI::Ref<RHI::RHISampler>      CreateSampler(const RHI::SamplerDesc& desc) override;
		RHI::Ref<RHI::RHIShader>       CreateShader(const RHI::CompiledShader& compiled) override;
		RHI::Ref<RHI::RHIPipeline>     CreatePipeline(const RHI::GraphicsPipelineDesc& desc) override;
		RHI::Ref<RHI::RHIRenderTarget> CreateRenderTarget(const RHI::RenderTargetDesc& desc) override;
		RHI::Ref<RHI::RHIResourceSet>  CreateResourceSet(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set) override;

		// ------------------------------------------------------------------
		// Backend-internal accessors
		// ------------------------------------------------------------------
		VkDevice         GetDevice()         const { return m_Device; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkInstance       GetInstance()       const { return m_Instance; }
		VmaAllocator     GetAllocator()      const { return m_Allocator; }
		VkQueue          GetGraphicsQueue()  const { return m_GraphicsQueue; }
		uint32_t         GetGraphicsFamily() const { return m_QueueFamilies.Graphics; }
		// ImGui's backend takes a pool at init and keeps the handle for the
		// life of the process, allocating a set per texture as it goes. It gets
		// one of its own: sharing the renderer's meant a scene with enough
		// materials to fill that pool made every later ImGui texture fail to
		// allocate -- the editor's viewport image among them.
		VkDescriptorPool GetImGuiDescriptorPool() const { return m_ImGuiPool; }

		// Allocates from the pool chain, adding a block if the current one is
		// full, and returns the block that served the request -- which the
		// caller must keep, because a set is freed to the pool that owns it.
		// Returns VK_NULL_HANDLE and logs if the allocation genuinely failed.
		double GetTimestampPeriodNs() const override { return m_TimestampPeriodNs; }
		const std::vector<uint64_t>& GetResolvedTimestamps() const override { return m_ResolvedTicks; }
		const std::vector<uint8_t>& GetResolvedTimestampFlags() const override { return m_ResolvedWritten; }
		VkQueryPool GetTimestampPool() const { return m_TimestampPools.empty() ? VK_NULL_HANDLE : m_TimestampPools[m_FrameIndex]; }

		VkDescriptorPool AllocateDescriptorSets(const VkDescriptorSetLayout* layouts,
												uint32_t count, VkDescriptorSet* out);

		// Resources copy this so their destructors never touch the device
		// itself; see DeletionQueue for why that matters.
		const std::shared_ptr<DeletionQueue>& GetDeletionQueue() const { return m_Deletion; }

		VkImageView GetCurrentSwapchainImageView() const { return m_SwapchainImageViews[m_ImageIndex]; }
		VkImage     GetCurrentSwapchainImage()     const { return m_SwapchainImages[m_ImageIndex]; }
		VkImageView GetSwapchainDepthView()        const { return m_DepthImageView; }
		VkExtent2D  GetSwapchainExtent()           const { return m_SwapchainExtent; }

		// Records a one-shot command buffer and blocks until it completes.
		// Used for staging uploads and layout transitions at creation time.
		void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);

		// Defers `deleter` until the current frame's fence has been waited on.
		void DeferDestruction(std::function<void()> deleter);

		void SetDebugName(uint64_t handle, VkObjectType type, const char* name);

	private:
		void CreateInstance(bool enableValidation);
		void CreateSurface();
		void SelectPhysicalDevice();
		void CreateLogicalDevice();
		void CreateAllocator();
		void CreateFrameContexts();
		void CreateDescriptorPool();
		VkDescriptorPool CreateDescriptorPoolBlock();
		void CreateTimestampPools();
		// Reads back the pool this frame slot used last time round, then resets
		// it for this frame. Must run after the fence wait and after the
		// command buffer has begun -- vkCmdResetQueryPool is a command.
		void RecycleTimestampPool(VkCommandBuffer cmd);
		void CreateSwapchain();
		void DestroySwapchain();
		// The views and depth buffer only, leaving the swapchain and its
		// semaphores alive for the caller to retire in the right order.
		void DestroySwapchainResources();
		void RecreateSwapchain();
		void CreateDepthResources();
		VkFormat SelectDepthFormat() const;
		void QueryCaps();

	private:
		GLFWwindow* m_Window = nullptr;

		// Armed by RequestCapture, consumed and cleared by the next EndFrame.
		CaptureCallback m_Capture;

		VkInstance               m_Instance       = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR             m_Surface        = VK_NULL_HANDLE;
		VkPhysicalDevice         m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice                 m_Device         = VK_NULL_HANDLE;
		VmaAllocator             m_Allocator      = VK_NULL_HANDLE;

		QueueFamilies m_QueueFamilies;
		VkQueue       m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue       m_PresentQueue  = VK_NULL_HANDLE;

		VkSwapchainKHR           m_Swapchain = VK_NULL_HANDLE;
		VkFormat                 m_SwapchainFormat = VK_FORMAT_UNDEFINED;
		VkColorSpaceKHR          m_SwapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkExtent2D               m_SwapchainExtent{};
		VkPresentModeKHR         m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		std::vector<VkImage>     m_SwapchainImages;
		std::vector<VkImageView> m_SwapchainImageViews;
		// One per swapchain image, not per frame in flight: a present operation
		// keeps its semaphore busy until the image is reacquired, so reusing a
		// per-frame semaphore trips a validation error on 3+ image swapchains.
		std::vector<VkSemaphore> m_RenderFinished;

		VkImage       m_DepthImage      = VK_NULL_HANDLE;
		VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
		VkImageView   m_DepthImageView  = VK_NULL_HANDLE;
		VkFormat      m_DepthFormat     = VK_FORMAT_UNDEFINED;

		// Sets per block. Two frames in flight means a material costs two, so
		// this is a thousand materials before a second block is needed -- big
		// enough that most scenes never allocate one, small enough that a
		// project with ten thousand does not pay for them all at startup.
		static constexpr uint32_t kDescriptorSetsPerPool = 2000;

		// The newest block, which allocations are served from.
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		// Every block, oldest first, so all of them are destroyed.
		std::vector<VkDescriptorPool> m_DescriptorPools;
		// Not in the chain: ImGui holds this handle forever and cannot be moved
		// to a new block when one fills.
		VkDescriptorPool m_ImGuiPool = VK_NULL_HANDLE;

		// One pool per frame in flight, so a frame never resets a pool the GPU
		// is still writing into.
		std::vector<VkQueryPool> m_TimestampPools;
		// Whether each pool has been through a reset yet. Reading a query that
		// has never been reset is a validation error, and every pool starts
		// that way -- so the first pass through each frame slot resets without
		// reading.
		std::vector<uint8_t> m_TimestampPoolUsed;
		std::vector<uint64_t> m_ResolvedTicks;
		std::vector<uint8_t>  m_ResolvedWritten;
		// Scratch for the readback: value and availability per query.
		std::vector<uint64_t> m_TimestampScratch;
		double m_TimestampPeriodNs = 1.0;
		bool m_TimestampsSupported = false;

		std::shared_ptr<DeletionQueue> m_Deletion;
		std::vector<FrameContext> m_Frames;
		uint32_t m_FramesInFlight = 2;
		uint32_t m_FrameIndex = 0;
		uint32_t m_ImageIndex = 0;

		VkCommandPool m_ImmediatePool  = VK_NULL_HANDLE;
		VkFence       m_ImmediateFence = VK_NULL_HANDLE;

		RHI::Scope<VulkanCommandList> m_CommandList;

		RHI::DeviceCaps m_Caps;
		uint32_t m_PendingWidth = 0;
		uint32_t m_PendingHeight = 0;
		bool m_VSync = true;
		bool m_SwapchainDirty = false;
		bool m_FrameActive = false;
		bool m_ValidationEnabled = false;
		bool m_HasDebugUtils = false;
	};
}
