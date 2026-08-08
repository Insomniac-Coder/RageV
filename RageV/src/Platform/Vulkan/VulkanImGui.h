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
	void ImGuiVulkanNewFrame();
	// Must be called inside an open render pass targeting the swapchain.
	void ImGuiVulkanRenderDrawData(RHI::RHICommandList& commandList);
}
