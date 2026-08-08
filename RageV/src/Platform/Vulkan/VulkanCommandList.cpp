#include <rvpch.h>
#include "VulkanCommandList.h"
#include "VulkanDevice.h"

namespace RageV::Vk
{
	VulkanCommandList::VulkanCommandList(VulkanDevice& device)
		: m_Device(device)
	{
	}

	void VulkanCommandList::Begin(VkCommandBuffer commandBuffer)
	{
		m_CommandBuffer = commandBuffer;
		m_BoundPipeline = nullptr;
		m_ActiveTarget = nullptr;
		m_InRenderPass = false;

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo));
	}

	void VulkanCommandList::End()
	{
		if (m_InRenderPass)
		{
			RV_CORE_WARN("Command list ended with a render pass still open");
			EndRenderPass();
		}

		// The swapchain image has to be back in PRESENT_SRC before submission.
		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = 0;
		barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_Device.GetCurrentSwapchainImage();
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(m_CommandBuffer, &dependency);

		VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));
		m_CommandBuffer = VK_NULL_HANDLE;
	}

	void VulkanCommandList::BeginRenderPass(const RHI::RenderPassBeginInfo& info)
	{
		RV_CORE_ASSERT(!m_InRenderPass, "A render pass is already open");

		auto* target = static_cast<VulkanRenderTarget*>(info.Target);
		m_ActiveTarget = target;

		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		bool hasDepth = false;
		VkExtent2D extent{};

		if (target)
		{
			extent = { target->GetWidth(), target->GetHeight() };

			for (const auto& texture : target->GetColorTextures())
			{
				texture->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				VkRenderingAttachmentInfo attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				attachment.imageView = texture->GetView();
				attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				attachment.loadOp = info.ClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				memcpy(attachment.clearValue.color.float32, info.Clear.Color, sizeof(float) * 4);
				colorAttachments.push_back(attachment);
			}

			if (VulkanTexture* depth = target->GetDepth())
			{
				depth->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

				depthAttachment.imageView = depth->GetView();
				depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthAttachment.loadOp = info.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				// Shadow maps are sampled afterwards, so depth must be kept.
				depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				depthAttachment.clearValue.depthStencil = { info.Clear.Depth, info.Clear.Stencil };
				hasDepth = true;
			}
		}
		else
		{
			extent = m_Device.GetSwapchainExtent();

			// The acquired swapchain image is in UNDEFINED (or PRESENT_SRC from
			// a previous frame); either way its contents are being cleared.
			VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_Device.GetCurrentSwapchainImage();
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;

			VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			vkCmdPipelineBarrier2(m_CommandBuffer, &dependency);

			VkRenderingAttachmentInfo attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			attachment.imageView = m_Device.GetCurrentSwapchainImageView();
			attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.loadOp = info.ClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			memcpy(attachment.clearValue.color.float32, info.Clear.Color, sizeof(float) * 4);
			colorAttachments.push_back(attachment);

			depthAttachment.imageView = m_Device.GetSwapchainDepthView();
			depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			depthAttachment.loadOp = info.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.clearValue.depthStencil = { info.Clear.Depth, info.Clear.Stencil };
			hasDepth = true;
		}

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea = { { 0, 0 }, extent };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = (uint32_t)colorAttachments.size();
		renderingInfo.pColorAttachments = colorAttachments.data();
		renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

		vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
		m_InRenderPass = true;

		// Sensible defaults so a caller that never touches viewport state still
		// renders to the whole target. Y is flipped so the RHI matches
		// OpenGL's bottom-left convention rather than Vulkan's top-left.
		RHI::Viewport viewport;
		viewport.X = 0.0f;
		viewport.Y = (float)extent.height;
		viewport.Width = (float)extent.width;
		viewport.Height = -(float)extent.height;
		SetViewport(viewport);

		SetScissor(RHI::Rect2D{ 0, 0, extent.width, extent.height });
	}

	void VulkanCommandList::EndRenderPass()
	{
		if (!m_InRenderPass)
			return;

		vkCmdEndRendering(m_CommandBuffer);
		m_InRenderPass = false;

		// Offscreen targets get read by a later pass (or by ImGui), so put them
		// straight back into a shader-readable layout.
		if (m_ActiveTarget)
		{
			for (const auto& texture : m_ActiveTarget->GetColorTextures())
				texture->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			if (VulkanTexture* depth = m_ActiveTarget->GetDepth())
			{
				if (RHI::HasFlag(depth->GetDesc().Usage, RHI::TextureUsage::Sampled))
					depth->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
		}

		m_ActiveTarget = nullptr;
	}

	void VulkanCommandList::SetViewport(const RHI::Viewport& viewport)
	{
		VkViewport vkViewport{};
		vkViewport.x = viewport.X;
		vkViewport.y = viewport.Y;
		vkViewport.width = viewport.Width;
		vkViewport.height = viewport.Height;
		vkViewport.minDepth = viewport.MinDepth;
		vkViewport.maxDepth = viewport.MaxDepth;
		vkCmdSetViewport(m_CommandBuffer, 0, 1, &vkViewport);
	}

	void VulkanCommandList::SetScissor(const RHI::Rect2D& scissor)
	{
		VkRect2D vkScissor{};
		vkScissor.offset = { scissor.X, scissor.Y };
		vkScissor.extent = { scissor.Width, scissor.Height };
		vkCmdSetScissor(m_CommandBuffer, 0, 1, &vkScissor);
	}

	void VulkanCommandList::BindPipeline(const RHI::Ref<RHI::RHIPipeline>& pipeline)
	{
		auto vulkanPipeline = std::static_pointer_cast<VulkanPipeline>(pipeline);
		if (m_BoundPipeline == vulkanPipeline.get())
			return;

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetHandle());
		m_BoundPipeline = vulkanPipeline.get();
	}

	void VulkanCommandList::BindResourceSet(uint32_t set, const RHI::Ref<RHI::RHIResourceSet>& resources)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "BindResourceSet requires a bound pipeline");

		auto vulkanSet = std::static_pointer_cast<VulkanResourceSet>(resources);
		VkDescriptorSet handle = vulkanSet->GetHandle();

		vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
								m_BoundPipeline->GetLayout(), set, 1, &handle, 0, nullptr);
	}

	void VulkanCommandList::BindVertexBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer, uint64_t offset)
	{
		auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
		VkBuffer handle = vulkanBuffer->GetHandle();
		vkCmdBindVertexBuffers(m_CommandBuffer, binding, 1, &handle, &offset);
	}

	void VulkanCommandList::BindIndexBuffer(const RHI::Ref<RHI::RHIBuffer>& buffer, RHI::IndexType type, uint64_t offset)
	{
		auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
		vkCmdBindIndexBuffer(m_CommandBuffer, vulkanBuffer->GetHandle(), offset, ToVkIndexType(type));
	}

	void VulkanCommandList::PushConstants(RHI::ShaderStage stages, uint32_t offset, uint32_t size, const void* data)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "PushConstants requires a bound pipeline");
		vkCmdPushConstants(m_CommandBuffer, m_BoundPipeline->GetLayout(),
						   ToVkShaderStages(stages), offset, size, data);
	}

	void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
								 uint32_t firstVertex, uint32_t firstInstance)
	{
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
										uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
	{
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void VulkanCommandList::PushDebugGroup(const char* name)
	{
		if (!vkCmdBeginDebugUtilsLabelEXT)
			return;
		VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		label.pLabelName = name;
		vkCmdBeginDebugUtilsLabelEXT(m_CommandBuffer, &label);
	}

	void VulkanCommandList::PopDebugGroup()
	{
		if (!vkCmdEndDebugUtilsLabelEXT)
			return;
		vkCmdEndDebugUtilsLabelEXT(m_CommandBuffer);
	}
}
