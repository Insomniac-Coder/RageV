#pragma once
#include "RageV/Renderer/GraphicsContext.h"
#include <optional>

struct GLFWwindow;
namespace RageV
{

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
	};


	class VulkanContext : public GraphicsContext
	{
	public:
		VulkanContext(GLFWwindow* windowHandle);
		virtual ~VulkanContext();
		virtual void SwapBuffers() override;
		virtual void Init() override;
		const GraphicsInfo& GetGraphicsInfo() const override;

	private:
		GLFWwindow* m_WindowHandle;
		VkInstance m_Instance;
		VkDebugUtilsMessengerEXT DebugMessenger;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		QueueFamilyIndices m_QueueIndicies;
		VkDevice m_LogicalDevice;
		VkQueue m_GraphicsQueue;
	};

}