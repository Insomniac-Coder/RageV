#pragma once

// Shared plumbing for the Vulkan backend: volk entry points, result checking
// and RHI <-> Vulkan enum conversion. volk owns every vkXxx symbol, so
// VK_NO_PROTOTYPES is set project-wide and nothing links vulkan-1 directly.

#include <volk.h>
#include "RageV/Renderer/RHI/RHITypes.h"

namespace RageV::Vk
{
	const char* ResultToString(VkResult result);

	namespace Detail
	{
		void ReportFailure(VkResult result, const char* expression, const char* file, int line);
	}
}

#define VK_CHECK(expr)                                                            \
	do {                                                                          \
		const VkResult rv_result_ = (expr);                                       \
		if (rv_result_ != VK_SUCCESS)                                             \
			::RageV::Vk::Detail::ReportFailure(rv_result_, #expr, __FILE__, __LINE__); \
	} while (false)

namespace RageV::Vk
{
	// **A lost device is not an error, it is the end of the device.**
	//
	// VK_ERROR_DEVICE_LOST means the driver has torn the whole thing down --
	// usually a GPU fault or a watchdog reset. Every subsequent call fails the
	// same way, including the wait and the submit at the top and bottom of a
	// frame, so treating it as an ordinary failure produces two error lines per
	// frame forever: a report of this arrived as two hundred and fifty
	// identical lines inside one second, with the actual first failure long
	// gone off the top.
	//
	// So it latches. The first one is reported in full and says what it means;
	// everything after it is silence, because there is nothing after it that is
	// not a consequence.
	bool DeviceLost();

	// Destruction has to be deferred: the GPU may still be reading a resource
	// from an in-flight frame when its last reference is dropped.
	//
	// This is held by shared_ptr from both the device and every resource, so it
	// outlives the device. A resource destroyed after the device then finds
	// DeviceAlive == false and does nothing -- which is correct, because
	// vkDestroyDevice already destroyed every child object. Reaching back into
	// a destroyed VulkanDevice instead is a use-after-free, and an easy mistake
	// to make from application code that keeps a Ref alive a little too long.
	struct DeletionQueue
	{
		bool DeviceAlive = true;
		// True between BeginFrame and EndFrame -- while a command buffer is
		// recording. Which slot a push belongs in depends on it; see Push.
		bool InFrame = false;
		uint32_t FrameIndex = 0;
		std::vector<std::vector<std::function<void()>>> PerFrame;

		void Push(std::function<void()> deleter)
		{
			if (!DeviceAlive || PerFrame.empty())
				return;

			// In frame: the resource may already be recorded into the frame
			// being built, so it waits in this frame's slot -- flushed only
			// after this frame's own fence.
			//
			// Out of frame -- which is where entity destruction happens, in
			// the simulation phase before BeginFrame -- this frame's slot is
			// about to be flushed *this very iteration*, right after a fence
			// wait that only covers a frame from two submissions ago. Pushed
			// there, the deleter runs microseconds later while the previous
			// frame may still be executing; that was a live race, hit by the
			// first game that destroyed material-bearing entities mid-play.
			// The last possible user is the most recently *submitted* frame,
			// so its slot -- whose flush waits that frame's fence -- is the
			// earliest safe home.
			const uint32_t count = (uint32_t)PerFrame.size();
			const uint32_t slot = InFrame ? FrameIndex
										  : (FrameIndex + count - 1) % count;
			PerFrame[slot].push_back(std::move(deleter));
		}

		void Flush(uint32_t frame)
		{
			if (frame >= PerFrame.size())
				return;
			for (auto& deleter : PerFrame[frame])
				deleter();
			PerFrame[frame].clear();
		}

		void FlushAll()
		{
			for (uint32_t i = 0; i < (uint32_t)PerFrame.size(); i++)
				Flush(i);
		}
	};

	using namespace RageV::RHI;

	VkFormat              ToVkFormat(Format format);
	Format                FromVkFormat(VkFormat format);
	VkBufferUsageFlags    ToVkBufferUsage(BufferUsage usage);
	VkImageUsageFlags     ToVkImageUsage(TextureUsage usage);
	VkShaderStageFlags    ToVkShaderStages(ShaderStage stages);
	VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage);
	VkDescriptorType      ToVkDescriptorType(ResourceType type);
	VkPrimitiveTopology   ToVkTopology(PrimitiveTopology topology);
	VkPolygonMode         ToVkPolygonMode(PolygonMode mode);
	VkCullModeFlags       ToVkCullMode(CullMode mode);
	VkFrontFace           ToVkFrontFace(FrontFace face);
	VkCompareOp           ToVkCompareOp(CompareOp op);
	VkFilter              ToVkFilter(FilterMode filter);
	VkSamplerMipmapMode   ToVkMipmapMode(MipmapMode mode);
	VkSamplerAddressMode  ToVkAddressMode(WrapMode wrap);
	VkIndexType           ToVkIndexType(IndexType type);
	VkAttachmentLoadOp    ToVkLoadOp(LoadOp op);
	VkAttachmentStoreOp   ToVkStoreOp(StoreOp op);

	void ApplyBlendPreset(BlendPreset preset, VkPipelineColorBlendAttachmentState& out);

	// A combined depth/stencil image's subresource range includes the stencil
	// aspect, and the spec forbids DEPTH_ATTACHMENT_OPTIMAL for such a range
	// (VUID-VkImageMemoryBarrier2-aspectMask-08703). Pick per format so barriers
	// and vkCmdBeginRendering always agree.
	VkImageLayout DepthAttachmentLayout(Format format);
	VkImageLayout DepthAttachmentLayout(VkFormat format);
}
