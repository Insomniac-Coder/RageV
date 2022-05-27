#pragma once
#include "RageV/Renderer/GraphicsContext.h"
#include <optional>

struct GLFWwindow;
namespace RageV
{

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
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
		VkQueue m_PresentQueue;
		VkSurfaceKHR m_Surface;
		SwapChainSupportDetails m_SwapChainSupportDetails;
		VkSurfaceFormatKHR m_SurfaceFormat;
		VkFormat m_ImageFormat;
		VkPresentModeKHR m_PresentMode;
		VkExtent2D m_Extent;
		VkSwapchainKHR m_SwapChain;
		std::vector<VkImage> m_SwapChainImages;
		std::vector<VkImageView> m_ImageViews;
		friend class GraphicsInfo;
	};

}