#include <rvpch.h>
#include "VulkanCommandList.h"
#include "VulkanDevice.h"

namespace RageV::Vk
{
	namespace
	{
		// What a multisample resolve write is, for barrier purposes. The
		// colour twins get this for free -- it is exactly the access their
		// COLOR_ATTACHMENT_OPTIMAL layout implies -- but a *depth* twin's
		// layout implies the depth stages, and the resolve does not run
		// there: the spec puts every render pass resolve, whatever the
		// attachment's aspect, in the colour attachment output stage. A
		// barrier naming only the depth stages leaves the resolve write
		// unsynchronised against the layout transition that precedes it, and
		// that is a write-after-write the layers report on the first frame.
		constexpr VkPipelineStageFlags2 kResolveWriteStages =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		constexpr VkAccessFlags2 kResolveWriteAccess =
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	}

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
		m_SwapchainWritten = false;

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo));
	}

	void VulkanCommandList::Adopt(VkCommandBuffer commandBuffer)
	{
		m_CommandBuffer = commandBuffer;
		m_BoundPipeline = nullptr;
		m_ActiveTarget = nullptr;
		m_InRenderPass = false;
		m_SwapchainWritten = false;
	}

	void VulkanCommandList::End()
	{
		if (m_InRenderPass)
		{
			RV_CORE_WARN("Command list ended with a render pass still open");
			EndRenderPass();
		}

		// The swapchain image has to be back in PRESENT_SRC before submission.
		//
		// A frame that never rendered into it leaves it in whatever layout the
		// acquire produced, so claiming it was a colour attachment would be a
		// lie about state the driver is entitled to act on.
		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = m_SwapchainWritten
							  ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
							  : VkAccessFlags2(0);
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = 0;
		barrier.oldLayout = m_SwapchainWritten
						  ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
						  : VK_IMAGE_LAYOUT_UNDEFINED;
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

			const auto& textures = target->GetColorTextures();

			// The pass's own selection, or every attachment when it made none.
			std::vector<RHI::ColorBinding> bindings = info.ColorAttachments;
			if (bindings.empty())
			{
				bindings.reserve(textures.size());
				for (uint32_t i = 0; i < (uint32_t)textures.size(); i++)
				{
					RHI::ColorBinding binding;
					binding.Index = i;
					memcpy(binding.Clear, info.Clear.Color, sizeof(float) * 4);
					bindings.push_back(binding);
				}
			}

			for (const RHI::ColorBinding& binding : bindings)
			{
				if (binding.Index >= textures.size())
				{
					RV_CORE_ERROR("Render pass binds colour attachment {0} of a target "
								  "that has {1}", binding.Index, textures.size());
					continue;
				}

				const auto& texture = textures[binding.Index];
				texture->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				VkRenderingAttachmentInfo attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				attachment.imageView = texture->GetView();
				attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				attachment.loadOp = info.ClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				memcpy(attachment.clearValue.color.float32, binding.Clear, sizeof(float) * 4);

				// Multisampled: name the single-sampled twin and let the pass
				// resolve into it on the way out. Dynamic rendering carries
				// this on the attachment, so there is no second pass and no
				// shader -- AVERAGE is the box filter §7o had to write by hand
				// for SSAA, done by the hardware.
				if (VulkanTexture* resolve = target->GetResolve(binding.Index))
				{
					resolve->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
					attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
					attachment.resolveImageView = resolve->GetView();
					attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}

				colorAttachments.push_back(attachment);
			}

			if (VulkanTexture* depth = info.UseDepth ? target->GetDepth() : nullptr)
			{
				const VkImageLayout depthLayout = DepthAttachmentLayout(depth->GetFormat());
				depth->TransitionTo(m_CommandBuffer, depthLayout);

				depthAttachment.imageView = depth->GetView();
				depthAttachment.imageLayout = depthLayout;
				depthAttachment.loadOp = info.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				// Shadow maps are sampled afterwards, so depth must be kept.
				depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				depthAttachment.clearValue.depthStencil = { info.Clear.Depth, info.Clear.Stencil };

				// The depth's single-sampled twin, resolved the same way the
				// colours are -- except by SAMPLE_ZERO rather than AVERAGE. A
				// depth is a position, and the average of two positions either
				// side of a silhouette is a place where nothing is: it would
				// put a one-texel rim of invented geometry around every edge
				// in the frame, which the occlusion and the reflections would
				// then both believe. Sample zero is a place that was really
				// there, and it is the only mode Vulkan guarantees. It is also
				// what OpenGL's resolve blit does, which keeps the two backends
				// reconstructing the same view space. ENGINE-NOTES 7ai.
				if (VulkanTexture* resolve = target->GetDepthResolve())
				{
					resolve->TransitionTo(m_CommandBuffer, depthLayout,
										  kResolveWriteStages, kResolveWriteAccess);
					depthAttachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
					depthAttachment.resolveImageView = resolve->GetView();
					depthAttachment.resolveImageLayout = depthLayout;
				}

				hasDepth = true;
			}
		}
		else
		{
			extent = m_Device.GetSwapchainExtent();

			// The first pass of a frame acquires the image; a later one
			// inherits what the previous pass left.
			//
			// The distinction is not cosmetic. UNDEFINED as an old layout
			// permits the driver to discard the image contents, so a second
			// pass that loads rather than clears -- the runtime's UI over its
			// scene -- could legally come back to garbage. It also has to name
			// the previous pass's write as the source, or the layout
			// transition races the store it is meant to follow.
			//
			// srcStageMask must be COLOR_ATTACHMENT_OUTPUT in both cases, not
			// TOP_OF_PIPE: the submit waits on the acquire semaphore at that
			// stage, and a TOP_OF_PIPE source forms no execution dependency
			// with it. The layout transition -- a write -- could then run
			// before the presentation engine finished reading the image.
			VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.srcAccessMask = m_SwapchainWritten
								  ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
								  : VkAccessFlags2(0);
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
									VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			barrier.oldLayout = m_SwapchainWritten
							  ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
							  : VK_IMAGE_LAYOUT_UNDEFINED;
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
			m_SwapchainWritten = true;

			VkRenderingAttachmentInfo attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			attachment.imageView = m_Device.GetCurrentSwapchainImageView();
			attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachment.loadOp = info.ClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			memcpy(attachment.clearValue.color.float32, info.Clear.Color, sizeof(float) * 4);
			colorAttachments.push_back(attachment);

			if (info.UseDepth)
			{
				depthAttachment.imageView = m_Device.GetSwapchainDepthView();
				depthAttachment.imageLayout = DepthAttachmentLayout(m_Device.GetSwapchainDepthFormat());
				depthAttachment.loadOp = info.ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				depthAttachment.clearValue.depthStencil = { info.Clear.Depth, info.Clear.Stencil };
				hasDepth = true;
			}
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

			// And the resolve twins, which are what GetColorTexture actually
			// hands out. Transitioning only the multisampled originals leaves
			// the image every later pass samples sitting in
			// COLOR_ATTACHMENT_OPTIMAL -- which the validation layers say
			// plainly and nothing else would.
			for (uint32_t i = 0; i < (uint32_t)m_ActiveTarget->GetColorTextures().size(); i++)
			{
				if (VulkanTexture* resolve = m_ActiveTarget->GetResolve(i))
					resolve->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			if (VulkanTexture* depth = m_ActiveTarget->GetDepth())
			{
				if (RHI::HasFlag(depth->GetDesc().Usage, RHI::TextureUsage::Sampled))
					depth->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}

			// And the depth's twin, which is what GetDepthTexture hands out
			// when there is one -- the same trap the colour resolves were,
			// one attachment further down.
			if (VulkanTexture* depthResolve = m_ActiveTarget->GetDepthResolve())
				depthResolve->TransitionTo(m_CommandBuffer,
										   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
										   kResolveWriteStages, kResolveWriteAccess);
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
		VulkanPipelineCommon* common = vulkanPipeline.get();
		if (m_BoundPipeline == common)
			return;

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, common->GetHandle());
		m_BoundPipeline = common;
	}

	void VulkanCommandList::BindComputePipeline(const RHI::Ref<RHI::RHIComputePipeline>& pipeline)
	{
		auto vulkanPipeline = std::static_pointer_cast<VulkanComputePipeline>(pipeline);
		VulkanPipelineCommon* common = vulkanPipeline.get();
		if (m_BoundPipeline == common)
			return;

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, common->GetHandle());
		m_BoundPipeline = common;
	}

	void VulkanCommandList::BuildTopLevelAS(const RHI::Ref<RHI::RHIAccelerationStructure>& tlas,
											 const RHI::AccelerationInstance* instances, uint32_t count)
	{
		RV_CORE_ASSERT(!m_InRenderPass, "BuildTopLevelAS must be recorded outside a render pass");
		auto structure = std::static_pointer_cast<VulkanAccelerationStructure>(tlas);
		if (!structure)
			return;
		structure->Build(m_CommandBuffer, instances, count);
	}

	void VulkanCommandList::BuildBottomLevelAS(const RHI::Ref<RHI::RHIAccelerationStructure>& blas)
	{
		RV_CORE_ASSERT(!m_InRenderPass, "BuildBottomLevelAS must be recorded outside a render pass");
		auto structure = std::static_pointer_cast<VulkanAccelerationStructure>(blas);
		if (!structure)
			return;
		structure->BuildBottomLevel(m_CommandBuffer);
	}

	void VulkanCommandList::BindResourceSet(uint32_t set, const RHI::Ref<RHI::RHIResourceSet>& resources)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "BindResourceSet requires a bound pipeline");

		// The base rather than the per-frame class: the bindless heap is a
		// set too, and which kind this is does not matter here.
		auto vulkanSet = std::static_pointer_cast<VulkanSetBase>(resources);
		VkDescriptorSet handle = vulkanSet->GetHandle();

		// The bound pipeline's own bind point, not a fixed one: a set bound
		// to the graphics point is invisible to a dispatch, and the dispatch
		// reads whatever the last graphics draw left there.
		vkCmdBindDescriptorSets(m_CommandBuffer, m_BoundPipeline->GetBindPoint(),
								m_BoundPipeline->GetLayout(), set, 1, &handle, 0, nullptr);

		// Under validation only: lets a later Commit on this same set name
		// the rewrite-after-bind hazard instead of leaving the layer's
		// anonymous "invalidated" message as the whole story.
		m_Device.NoteSetBound(handle);
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

		// The spec requires stageFlags to include every stage of every range
		// that overlaps these bytes, so the shader's own declaration decides
		// this rather than the caller. Passing a narrower mask than the shader
		// declares is a validation error that is easy to write and easy to
		// miss, since it does not necessarily misbehave.
		RHI::ShaderStage effective = RHI::ShaderStage::None;
		for (const auto& range : m_BoundPipeline->GetCommonReflection().PushConstants)
		{
			const uint32_t rangeEnd = range.Offset + range.Size;
			if (offset < rangeEnd && range.Offset < offset + size)
				effective = effective | range.Stages;
		}

		if (effective == RHI::ShaderStage::None)
			effective = stages;   // no reflected range; trust the caller

		vkCmdPushConstants(m_CommandBuffer, m_BoundPipeline->GetLayout(),
						   ToVkShaderStages(effective), offset, size, data);
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

	namespace
	{
		// What a use means to Vulkan: which stage touches the buffer, and how.
		void DescribeSync(RHI::BufferSync sync, VkPipelineStageFlags& stage, VkAccessFlags& access)
		{
			switch (sync)
			{
				case RHI::BufferSync::ComputeWrite:
					stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					access = VK_ACCESS_SHADER_WRITE_BIT;
					break;
				case RHI::BufferSync::ComputeRead:
					stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					access = VK_ACCESS_SHADER_READ_BIT;
					break;
				case RHI::BufferSync::ShaderRead:
					// Both graphics shader stages, because the caller said
					// "a shader reads it" and narrowing that to the one it
					// happens to be today is how a barrier stops covering a
					// use somebody added later.
					stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
						  | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
					break;
				case RHI::BufferSync::VertexInput:
					stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
					access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
					break;
				case RHI::BufferSync::IndirectRead:
					stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
					access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
					break;
				case RHI::BufferSync::TransferWrite:
					stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
					access = VK_ACCESS_TRANSFER_WRITE_BIT;
					break;
				case RHI::BufferSync::AccelerationBuild:
					// Build inputs -- vertices, indices, instances -- are a
					// shader read at the build stage, per the specification;
					// the acceleration-structure access bits are for the
					// structure itself, which BuildBottomLevelAS orders.
					stage = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
					access = VK_ACCESS_SHADER_READ_BIT;
					break;
				default:
					stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
					access = 0;
					break;
			}
		}
	}

	void VulkanCommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "Dispatch requires a bound compute pipeline");
		RV_CORE_ASSERT(!m_InRenderPass, "Dispatch must be recorded outside a render pass");

		// Zero groups is legal and does nothing, but it is almost always a
		// count that divided to zero rather than an intent.
		if (groupsX == 0 || groupsY == 0 || groupsZ == 0)
			return;

		vkCmdDispatch(m_CommandBuffer, groupsX, groupsY, groupsZ);
	}

	void VulkanCommandList::BufferBarrier(const RHI::Ref<RHI::RHIBuffer>& buffer,
										  RHI::BufferSync from, RHI::BufferSync to)
	{
		if (!buffer)
			return;

		auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);

		VkPipelineStageFlags sourceStage = 0, destinationStage = 0;
		VkAccessFlags sourceAccess = 0, destinationAccess = 0;
		DescribeSync(from, sourceStage, sourceAccess);
		DescribeSync(to, destinationStage, destinationAccess);

		VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = sourceAccess;
		barrier.dstAccessMask = destinationAccess;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = vulkanBuffer->GetHandle();
		barrier.offset = 0;
		barrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(m_CommandBuffer, sourceStage, destinationStage, 0,
							 0, nullptr, 1, &barrier, 0, nullptr);
	}

	void VulkanCommandList::WriteTimestamp(uint32_t slot)
	{
		VkQueryPool pool = m_Device.GetTimestampPool();
		if (pool == VK_NULL_HANDLE || slot >= RHI::RHIDevice::kTimestampSlots)
			return;

		// ALL_COMMANDS, so the timestamp waits for everything recorded before
		// it. TOP_OF_PIPE would be written as soon as the GPU reached this
		// point in the stream, which is not when the preceding work finished --
		// and every duration would come out implausibly short.
		vkCmdWriteTimestamp2(m_CommandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool, slot);
	}

	void VulkanCommandList::GenerateMips(const RHI::Ref<RHI::RHITexture>& texture)
	{
		if (!texture)
			return;

		// Into this command buffer, so the blits run after whatever wrote mip 0
		// earlier in the same frame.
		std::static_pointer_cast<VulkanTexture>(texture)->RecordGenerateMips(m_CommandBuffer);
	}

	void VulkanCommandList::CopyToTextureLayer(const RHI::Ref<RHI::RHITexture>& source,
											   const RHI::Ref<RHI::RHITexture>& destination,
											   uint32_t layer, uint32_t mip)
	{
		RV_CORE_ASSERT(!m_InRenderPass, "CopyToTextureLayer inside a render pass");

		if (!source || !destination)
			return;

		auto* src = static_cast<VulkanTexture*>(source.get());
		auto* dst = static_cast<VulkanTexture*>(destination.get());

		src->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		dst->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		const int32_t width = (int32_t)source->GetWidth();
		const int32_t height = (int32_t)source->GetHeight();

		// The destination's own size at this mip, not the source's.
		//
		// Writing the source's rectangle into the destination is right only
		// while the two agree, which they did for as long as every caller
		// copied a face into a cube of the same face size. It stops being
		// right the moment anything resamples -- a 256-pixel probe filtered
		// into a 128-pixel array slice -- and the failure is quiet: the blit
		// fills a corner and leaves the rest of the slice holding whatever it
		// had. That is the same class of bug the OpenGL path's stale-attachment
		// comment below is about, arriving from the other side.
		const int32_t dstWidth  = (int32_t)Math::Max(destination->GetWidth() >> mip, 1u);
		const int32_t dstHeight = (int32_t)Math::Max(destination->GetHeight() >> mip, 1u);

		// Read bottom-to-top, write top-to-bottom.
		//
		// The face is captured with the same camera basis every cube-map
		// tutorial uses, which puts the face's first texel row at the *bottom*
		// of the rendered image. OpenGL stores a rendered image bottom-up and
		// therefore needs nothing; this backend renders through a
		// negative-height viewport, so its row 0 is the top, and the flip
		// belongs here. Getting this backwards produces an upside-down
		// reflection, which on a sphere is not obviously upside down -- it just
		// looks wrong.
		// A point light's shadow is a cube of depth, so this has to carry the
		// depth aspect as well as the colour one. Both ends always agree --
		// nothing copies a colour image into a depth one.
		const bool depth = RHI::IsDepthFormat(source->GetFormat());
		const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT
												: VK_IMAGE_ASPECT_COLOR_BIT;

		VkImageBlit region{};
		region.srcSubresource.aspectMask = aspect;
		region.srcSubresource.layerCount = 1;
		region.srcOffsets[0] = { 0, height, 0 };
		region.srcOffsets[1] = { width, 0, 1 };
		region.dstSubresource.aspectMask = aspect;
		region.dstSubresource.mipLevel = mip;
		region.dstSubresource.baseArrayLayer = layer;
		region.dstSubresource.layerCount = 1;
		region.dstOffsets[0] = { 0, 0, 0 };
		region.dstOffsets[1] = { dstWidth, dstHeight, 1 };

		// NEAREST is not a quality choice for depth: the specification requires
		// it. Filtering depth would average two distances and produce one that
		// nothing in the scene is at.
		//
		// For colour it is a quality choice, and only when the two sizes differ
		// -- a same-size copy lands on texel centres either way, so the faces
		// every existing caller copies are bit-identical to what they were.
		const bool rescaling = dstWidth != width || dstHeight != height;
		const VkFilter filter = (depth || !rescaling) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

		vkCmdBlitImage(m_CommandBuffer,
					   src->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   1, &region, filter);

		src->TransitionTo(m_CommandBuffer, depth
										 ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
										 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		dst->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VulkanCommandList::CopyStripToTextureLayers(const RHI::Ref<RHI::RHITexture>& source,
													 const RHI::Ref<RHI::RHITexture>& destination,
													 uint32_t baseLayer, uint32_t layerCount,
													 uint32_t mip)
	{
		RV_CORE_ASSERT(!m_InRenderPass, "CopyStripToTextureLayers inside a render pass");

		if (!source || !destination || layerCount == 0)
			return;

		auto* src = static_cast<VulkanTexture*>(source.get());
		auto* dst = static_cast<VulkanTexture*>(destination.get());

		// One pair of transitions for every slice, which is the point.
		src->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		dst->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		const int32_t sliceWidth = (int32_t)(source->GetWidth() / layerCount);
		const int32_t height = (int32_t)source->GetHeight();
		const int32_t dstWidth  = (int32_t)Math::Max(destination->GetWidth() >> mip, 1u);
		const int32_t dstHeight = (int32_t)Math::Max(destination->GetHeight() >> mip, 1u);

		const bool depth = RHI::IsDepthFormat(source->GetFormat());
		const VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT
												: VK_IMAGE_ASPECT_COLOR_BIT;
		const bool rescaling = dstWidth != sliceWidth || dstHeight != height;
		const VkFilter filter = (depth || !rescaling) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

		// Each slice read bottom-to-top and written top-to-bottom, exactly as
		// CopyToTextureLayer does for one face and for the same reason.
		std::vector<VkImageBlit> regions(layerCount);
		for (uint32_t i = 0; i < layerCount; i++)
		{
			VkImageBlit& region = regions[i];
			region = {};
			region.srcSubresource.aspectMask = aspect;
			region.srcSubresource.layerCount = 1;
			region.srcOffsets[0] = { (int32_t)i * sliceWidth, height, 0 };
			region.srcOffsets[1] = { (int32_t)(i + 1) * sliceWidth, 0, 1 };
			region.dstSubresource.aspectMask = aspect;
			region.dstSubresource.mipLevel = mip;
			region.dstSubresource.baseArrayLayer = baseLayer + i;
			region.dstSubresource.layerCount = 1;
			region.dstOffsets[0] = { 0, 0, 0 };
			region.dstOffsets[1] = { dstWidth, dstHeight, 1 };
		}

		vkCmdBlitImage(m_CommandBuffer,
					   src->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   dst->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   layerCount, regions.data(), filter);

		src->TransitionTo(m_CommandBuffer, depth
										 ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
										 : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		dst->TransitionTo(m_CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
