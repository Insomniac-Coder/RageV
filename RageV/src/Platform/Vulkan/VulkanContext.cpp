#include <rvpch.h>
//including this for now since glfw is the only window creation API in use
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#include "VulkanContext.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#include <set>

RageV::VulkanContext::VulkanContext(GLFWwindow* windowHandle) :m_WindowHandle(windowHandle)
{
	RV_CORE_ASSERT(windowHandle, "Window handle is null!");
}

RageV::VulkanContext::~VulkanContext()
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr)
		func(m_Instance, DebugMessenger, nullptr);

	vkDestroySwapchainKHR(m_LogicalDevice, m_SwapChain, nullptr);
	vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
	vkDestroyDevice(m_LogicalDevice, nullptr);
	vkDestroyInstance(m_Instance, nullptr);
}

void RageV::VulkanContext::SwapBuffers()
{
}

static bool checkValidationLayersSupport(const std::vector<const char*>& layers)
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	//RV_CORE_INFO("Available layers:");

	for (const auto& requiredLayer : layers)
	{
		bool isPresent = false;
		for (const auto& availableLayer : availableLayers)
		{
			//RV_CORE_INFO("{0}", availableLayer.layerName);
			if (strcmp(requiredLayer, availableLayer.layerName) == 0)
			{
				isPresent = true;
				break;
			}
		}
		if (!isPresent)
			return false;
	}
	return true;
}

static bool CheckDeviceExtensionSupport(VkPhysicalDevice physicalDevice, std::vector<const char*> requiredExtensions)
{
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, deviceExtensions.data());

	bool isPresent = false;

	for (auto& requiredExtension : requiredExtensions)
	{
		for (auto& deviceExtension : deviceExtensions)
		{
			if (strcmp(deviceExtension.extensionName, requiredExtension) == 0)
				isPresent = true;
		}
		if (!isPresent)
			return false;
	}

	return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback
(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData
)
{
	RV_CORE_TRACE(pCallbackData->pMessage);
	return VK_FALSE;
}

static void SetupDebugMessenger(VkInstance& instance,VkDebugUtilsMessengerEXT& debugMessenger)
{
	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = debugCallback;
	createInfo.pUserData = nullptr;

	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

	if (func != nullptr)
	{
		RV_CORE_INFO("Debug callback function setup successfully!");
		func(instance, &createInfo, nullptr, &debugMessenger);
	}
	else
		RV_CORE_ERROR("Could not setup debug callback!");
}

static VkPhysicalDevice SelectPhysicalDevice(VkInstance& instance)
{
	uint32_t deviceCount = 0;

	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

	if (deviceCount == 0)
		RV_CORE_ERROR("Your system doesn't support Vulkan LMAO!");

	std::vector<VkPhysicalDevice> devices(deviceCount);

	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

	for (auto& device : devices)
	{
		VkPhysicalDeviceProperties properties;
		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceProperties(device, &properties);
		vkGetPhysicalDeviceFeatures(device, &features);
		RV_CORE_INFO(properties.deviceName);
		if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && features.geometryShader && features.tessellationShader)
		{
			RV_CORE_INFO("{0} is a discrete GPU, selecting this as the physical device", properties.deviceName);
			return device;
		}
	}

	return VK_NULL_HANDLE;
}

static bool GetQueueFamilyProperties(VkPhysicalDevice& device, RageV::QueueFamilyIndices& queueIndicies, VkSurfaceKHR& surface)
{
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyProperties.data());
	VkBool32 presentSupport = false;

	int i = 0;
	for (const auto& queueFamilyProperty : queueFamilyProperties)
	{
		if (queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			queueIndicies.graphicsFamily = i;
		}
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if (presentSupport)
		{
			queueIndicies.presentFamily = i;
		}
		i++;
	}

	return queueIndicies.graphicsFamily.has_value() && queueIndicies.presentFamily.has_value();
}

static void CreateLogicalDevice(VkPhysicalDevice& physicalDevice, VkDevice& logicalDevice, RageV::QueueFamilyIndices& queueFamilies)
{

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies = { queueFamilies.graphicsFamily.value(), queueFamilies.presentFamily.value() };
	float queuePriority = 1.0f;

	for (uint32_t queueFamily : uniqueQueueFamilies)
	{
		VkDeviceQueueCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		createInfo.queueFamilyIndex = queueFamily;
		createInfo.queueCount = 1;
		createInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(createInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures{};
	
	VkDeviceCreateInfo deviceCreateInfo{};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	//Not required in newer implementations of Vulkan but still adding it
	const std::vector<const char*>validationLayers = { "VK_LAYER_KHRONOS_validation" };
	const bool enableValidationLayers = true;
	if (enableValidationLayers)
	{
		deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		deviceCreateInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else
		deviceCreateInfo.enabledLayerCount = 0;

	const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice) == VK_SUCCESS)
		RV_CORE_INFO("Logical device created successfully");
}

static void CreateSurface(VkInstance& instance, GLFWwindow* window, VkSurfaceKHR& surface)
{
	VkWin32SurfaceCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = glfwGetWin32Window(window);
	createInfo.hinstance = GetModuleHandle(nullptr);

	if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
		RV_CORE_ERROR("Surface creation failed!");
	else
		RV_CORE_INFO("Surface created successfully!");
}

static bool QuerySwapChainSupport(VkPhysicalDevice& physicalDevice, VkSurfaceKHR& surface, RageV::SwapChainSupportDetails& swapChainSupportDetails)
{
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &swapChainSupportDetails.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

	if (formatCount != 0)
	{
		swapChainSupportDetails.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, swapChainSupportDetails.formats.data());
	}

	uint32_t presentCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, nullptr);

	if (presentCount != 0)
	{
		swapChainSupportDetails.presentModes.resize(presentCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, swapChainSupportDetails.presentModes.data());
	}

	return !swapChainSupportDetails.formats.empty() && !swapChainSupportDetails.presentModes.empty();
}

static void ChooseSwapSurfaceFormat(VkSurfaceFormatKHR& surfaceFormat, const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
	bool found = false;
	for (const auto& availableFormat : availableFormats)
	{
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
		{
			surfaceFormat = availableFormat;
			found = true;
			break;
		}
	}

	if(!found)
		surfaceFormat = availableFormats[0];
}

static VkExtent2D ChooseExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilites)
{
	if (capabilites.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		return capabilites.currentExtent;
	}
	else
	{
		int width;
		int height;
		glfwGetFramebufferSize(window, &width, &height);
		width = static_cast<int>(std::clamp(static_cast<uint32_t>(width), capabilites.minImageExtent.width, capabilites.maxImageExtent.height));
		height = static_cast<int>(std::clamp(static_cast<uint32_t>(height), capabilites.minImageExtent.height, capabilites.maxImageExtent.width));
		return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
	}
}

static void ChoosePresentationMode(VkPresentModeKHR& presentMode, const std::vector<VkPresentModeKHR>& availableModes)
{
	bool found = false;
	for (const auto& availableMode : availableModes)
	{
		if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			presentMode = availableMode;
			found = true;
			break;
		}
	}

	if (!found)
		presentMode = VK_PRESENT_MODE_FIFO_KHR;
}

static VkSwapchainKHR CreateSwapChain(VkDevice& logicalDevice, 
							RageV::SwapChainSupportDetails& swapchainSupportDetails, 
							VkSurfaceKHR& surface, VkSurfaceFormatKHR& surfaceFormat, 
							VkExtent2D& extent, RageV::QueueFamilyIndices& queueFamilyIndices, 
							VkPresentModeKHR& presentMode)
{
	VkSwapchainCreateInfoKHR createInfo{};

	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = swapchainSupportDetails.capabilities.minImageCount + 1;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; //this has to be parameterized since it will be later on used for framebuffers(I guess)
	createInfo.preTransform = swapchainSupportDetails.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily)
	{
		uint32_t familyIndices[] = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value()};
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = familyIndices;
	}
	else
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	VkSwapchainKHR swapChain;

	if (vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapChain) != VK_SUCCESS)
	{
		RV_CORE_ERROR("Swapchain creation succeeded!");
	}
	else
	{
		RV_CORE_INFO("Swapchain created!");
	}

	return swapChain;
}

void RageV::VulkanContext::Init()
{
	unsigned int extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	RV_CORE_INFO("Supported vulkan extension count: {0}", extensionCount);

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "RageV";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "RageV Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;

	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> instanceExtensions;

	for (int i = 0; i < glfwExtensionCount; i++)
	{
		instanceExtensions.push_back(glfwExtensions[i]);
	}

	instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	createInfo.enabledExtensionCount = instanceExtensions.size();
	createInfo.ppEnabledExtensionNames = instanceExtensions.data();

	const std::vector<const char*>validationLayers = { "VK_LAYER_KHRONOS_validation" };
	const bool enableValidationLayers = true;

	if (enableValidationLayers && !checkValidationLayersSupport(validationLayers))
	{
		RV_CORE_ERROR("Requested validation layers are not available");
	}
	else {
		RV_CORE_INFO("Validation layers enabled!");
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	}
	
	if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
	{
		RV_CORE_ERROR("Could not instantiate Vulkan");
	}
	else
	{
		RV_CORE_INFO("Vulkan instantiated successfully");
	}

	if (enableValidationLayers)
		SetupDebugMessenger(m_Instance, DebugMessenger);

	m_PhysicalDevice = SelectPhysicalDevice(m_Instance);
	
	//Creating surface here because it's needed to look for presentation queues
	CreateSurface(m_Instance, m_WindowHandle, m_Surface);
	std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	bool isDeviceGood = GetQueueFamilyProperties(m_PhysicalDevice, m_QueueIndicies, m_Surface) && 
						CheckDeviceExtensionSupport(m_PhysicalDevice, deviceExtensions) &&
						QuerySwapChainSupport(m_PhysicalDevice, m_Surface, m_SwapChainSupportDetails);
	if (isDeviceGood)
		RV_CORE_INFO("Chosen physical device has all the relevant features");

	CreateLogicalDevice(m_PhysicalDevice, m_LogicalDevice, m_QueueIndicies);
	vkGetDeviceQueue(m_LogicalDevice, m_QueueIndicies.graphicsFamily.value(), 0, &m_GraphicsQueue);
	vkGetDeviceQueue(m_LogicalDevice, m_QueueIndicies.presentFamily.value(), 0, &m_PresentQueue);

	ChooseSwapSurfaceFormat(m_SurfaceFormat, m_SwapChainSupportDetails.formats);
	ChoosePresentationMode(m_PresentMode, m_SwapChainSupportDetails.presentModes);
	m_Extent = ChooseExtent(m_WindowHandle, m_SwapChainSupportDetails.capabilities);
	m_SwapChain = CreateSwapChain(m_LogicalDevice, m_SwapChainSupportDetails, m_Surface, m_SurfaceFormat, m_Extent, m_QueueIndicies, m_PresentMode);
}

const RageV::GraphicsInfo& RageV::VulkanContext::GetGraphicsInfo() const
{
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
	std::string versionName = "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "."
										+ std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + "."
										+ std::to_string(VK_API_VERSION_PATCH(properties.apiVersion));
	// // O: insert return statement here
	return {
		versionName,
		properties.deviceName
	};
}
