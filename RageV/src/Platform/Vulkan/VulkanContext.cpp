#include <rvpch.h>
#include "vulkan.h"
#include "VulkanContext.h"

RageV::VulkanContext::VulkanContext(GLFWwindow* windowHandle) :m_WindowHandle(windowHandle)
{
	RV_CORE_ASSERT(windowHandle, "Window handle is null!");
}

void RageV::VulkanContext::SwapBuffers()
{
}

void RageV::VulkanContext::Init()
{
	unsigned int extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	RV_CORE_INFO("Supported vulkan extension count: {0}", extensionCount);

}

const RageV::GraphicsInfo& RageV::VulkanContext::GetGraphicsInfo() const
{
	// // O: insert return statement here
	return {
		"Test",
		"Vulkan"
	};
}
