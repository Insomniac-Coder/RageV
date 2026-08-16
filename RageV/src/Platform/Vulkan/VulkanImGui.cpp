#include <rvpch.h>
#include "VulkanImGui.h"
#include "VulkanDevice.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

namespace RageV::Vk
{
	namespace
	{
		bool s_Initialized = false;

		void CheckResult(VkResult result)
		{
			if (result != VK_SUCCESS)
				RV_CORE_ERROR("ImGui Vulkan backend: {0}", ResultToString(result));
		}
	}

	bool InitImGuiVulkan(RHI::RHIDevice& device, GLFWwindow* window)
	{
		auto* vulkan = dynamic_cast<VulkanDevice*>(&device);
		if (!vulkan)
			return false;

		ImGui_ImplGlfw_InitForVulkan(window, true);

		ImGui_ImplVulkan_InitInfo info{};
		info.ApiVersion = VK_API_VERSION_1_3;
		info.Instance = vulkan->GetInstance();
		info.PhysicalDevice = vulkan->GetPhysicalDevice();
		info.Device = vulkan->GetDevice();
		info.QueueFamily = vulkan->GetGraphicsFamily();
		info.Queue = vulkan->GetGraphicsQueue();
		info.DescriptorPool = vulkan->GetImGuiDescriptorPool();
		info.MinImageCount = 2;
		info.ImageCount = Math::Max(2u, device.GetFramesInFlight() + 1);
		info.CheckVkResultFn = CheckResult;

		// The engine renders with dynamic rendering, so ImGui must too --
		// otherwise it would want a VkRenderPass this backend never creates.
		info.UseDynamicRendering = true;

		// Must outlive the call: the backend keeps the pointer.
		static VkFormat colorFormat = VK_FORMAT_UNDEFINED;
		colorFormat = ToVkFormat(device.GetSwapchainFormat());

		// Since ImGui 1.92.9 these live on PipelineInfoMain rather than on the
		// init info directly.
		info.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
		info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		info.PipelineInfoMain.PipelineRenderingCreateInfo =
			VkPipelineRenderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
		// ImGui draws over the swapchain with depth testing off, so it does not
		// need the depth attachment declared.

		if (!ImGui_ImplVulkan_Init(&info))
		{
			RV_CORE_ERROR("ImGui_ImplVulkan_Init failed");
			return false;
		}

		s_Initialized = true;
		RV_CORE_INFO("ImGui Vulkan backend initialised");
		return true;
	}

	void ShutdownImGuiVulkan()
	{
		if (!s_Initialized)
			return;
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		s_Initialized = false;
	}

	bool IsImGuiVulkanReady()
	{
		return s_Initialized;
	}

	void ImGuiVulkanNewFrame()
	{
		if (s_Initialized)
			ImGui_ImplVulkan_NewFrame();
	}

	void ImGuiVulkanRenderDrawData(RHI::RHICommandList& commandList)
	{
		if (!s_Initialized)
			return;

		auto* commandBuffer = (VkCommandBuffer)commandList.GetNativeHandle();
		if (!commandBuffer)
			return;

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
	}
}
