#pragma once

// Keeps ImGui's Vulkan-specific setup out of ImGuiLayer, which only chooses a
// backend and drives the frame.

struct GLFWwindow;

namespace RageV::RHI { class RHIDevice; class RHICommandList; }

namespace RageV::Vk
{
	// Returns false when the device is not a Vulkan device.
	bool InitImGuiVulkan(RHI::RHIDevice& device, GLFWwindow* window);
	void ShutdownImGuiVulkan();

	// Whether the ImGui Vulkan backend is up.
	//
	// Needed because a texture's descriptor set is released on the deletion
	// queue, which is flushed when the device is destroyed -- and by then the
	// backend that owns the pool those sets came from is long gone. Freeing
	// one then is both unnecessary, since the pool went with it, and a crash.
	bool IsImGuiVulkanReady();
	void ImGuiVulkanNewFrame();
	// Must be called inside an open render pass targeting the swapchain.
	void ImGuiVulkanRenderDrawData(RHI::RHICommandList& commandList);
}
