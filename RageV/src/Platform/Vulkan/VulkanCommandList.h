#pragma once

#include "VulkanCommon.h"
#include "RageV/Renderer/RHI/RHICommandList.h"
#include "VulkanPipeline.h"
#include "VulkanResources.h"

namespace RageV::Vk
{
	class VulkanDevice;

	// Records into the current frame's command buffer. Render passes use
	// VK_KHR_dynamic_rendering, so there are no VkRenderPass or VkFramebuffer
	// objects to create, cache or keep compatible with pipelines.
	class VulkanCommandList final : public RHI::RHICommandList
	{
	public:
		explicit VulkanCommandList(VulkanDevice& device);

		void Begin(VkCommandBuffer commandBuffer);
		void End();

		void BeginRenderPass(const RHI::RenderPassBeginInfo& info) override;
		void EndRenderPass() override;

		void SetViewport(const RHI::Viewport& viewport) override;
		void SetScissor(const RHI::Rect2D& scissor) override;

		void BindPipeline(const RHI::Ref<RHI::RHIPipeline>& pipeline) override;
		void BindResourceSet(uint32_t set, const RHI::Ref<RHI::RHIResourceSet>& resources) override;
		void BindVertexBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer, uint64_t offset = 0) override;
		void BindIndexBuffer(const RHI::Ref<RHI::RHIBuffer>& buffer, RHI::IndexType type, uint64_t offset = 0) override;

		void PushConstants(RHI::ShaderStage stages, uint32_t offset, uint32_t size, const void* data) override;

		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
				  uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
						 uint32_t firstIndex = 0, int32_t vertexOffset = 0,
						 uint32_t firstInstance = 0) override;

		void CopyToTextureLayer(const RHI::Ref<RHI::RHITexture>& source,
								const RHI::Ref<RHI::RHITexture>& destination,
								uint32_t layer) override;

		void PushDebugGroup(const char* name) override;
		void PopDebugGroup() override;

		VkCommandBuffer GetHandle() const { return m_CommandBuffer; }
		void* GetNativeHandle() const override { return (void*)m_CommandBuffer; }

	private:
		VulkanDevice&   m_Device;
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		VulkanPipeline* m_BoundPipeline = nullptr;
		// Which target the open render pass writes to; nullptr means the
		// swapchain. Needed at EndRenderPass to restore image layouts.
		VulkanRenderTarget* m_ActiveTarget = nullptr;
		bool m_InRenderPass = false;

		// Whether the swapchain image has already been rendered into this frame.
		//
		// The editor never draws to it more than once -- the scene goes to an
		// offscreen target and only the UI reaches the swapchain -- so the second
		// pass case did not exist until the standalone runtime drew its scene
		// there and the UI on top.
		bool m_SwapchainWritten = false;
	};
}
