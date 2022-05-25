#include <rvpch.h>
//including this for now since glfw is the only window creation API in use
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#include "VulkanContext.h"

RageV::VulkanContext::VulkanContext(GLFWwindow* windowHandle) :m_WindowHandle(windowHandle)
{
	RV_CORE_ASSERT(windowHandle, "Window handle is null!");
}

RageV::VulkanContext::~VulkanContext()
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr)
		func(m_Instance, DebugMessenger, nullptr);

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
}

const RageV::GraphicsInfo& RageV::VulkanContext::GetGraphicsInfo() const
{
	// // O: insert return statement here
	return {
		"Test",
		"Vulkan"
	};
}
