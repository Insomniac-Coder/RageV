#include <rvpch.h>

#define VMA_IMPLEMENTATION
#include "VulkanDevice.h"
#include "VulkanResources.h"
#include "VulkanPipeline.h"
#include "VulkanCommandList.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <set>

namespace RageV::Vk
{
	namespace
	{
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void*)
		{
			// The original callback logged everything at TRACE, which buried
			// genuine validation errors in the noise.
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				RV_CORE_ERROR("[Vulkan] {0}", data->pMessage);
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				RV_CORE_WARN("[Vulkan] {0}", data->pMessage);
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
				RV_CORE_INFO("[Vulkan] {0}", data->pMessage);
			else
				RV_CORE_TRACE("[Vulkan] {0}", data->pMessage);

			return VK_FALSE;
		}

		bool HasLayer(const char* name)
		{
			uint32_t count = 0;
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> layers(count);
			vkEnumerateInstanceLayerProperties(&count, layers.data());
			for (const auto& layer : layers)
				if (strcmp(layer.layerName, name) == 0)
					return true;
			return false;
		}

		// layerName == nullptr queries the loader and ICDs only. Extensions
		// implemented *by* a layer -- VK_EXT_layer_settings is one -- are
		// invisible unless that layer is named explicitly.
		bool HasInstanceExtension(const char* name, const char* layerName = nullptr)
		{
			uint32_t count = 0;
			vkEnumerateInstanceExtensionProperties(layerName, &count, nullptr);
			std::vector<VkExtensionProperties> extensions(count);
			vkEnumerateInstanceExtensionProperties(layerName, &count, extensions.data());
			for (const auto& extension : extensions)
				if (strcmp(extension.extensionName, name) == 0)
					return true;
			return false;
		}

		bool HasDeviceExtensions(VkPhysicalDevice device, const std::vector<const char*>& required)
		{
			uint32_t count = 0;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
			std::vector<VkExtensionProperties> available(count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

			// The original version never reset its `found` flag between
			// required extensions, so one match satisfied all of them.
			for (const char* name : required)
			{
				bool found = false;
				for (const auto& extension : available)
				{
					if (strcmp(extension.extensionName, name) == 0)
					{
						found = true;
						break;
					}
				}
				if (!found)
					return false;
			}
			return true;
		}

		QueueFamilies FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
		{
			QueueFamilies families;

			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
			std::vector<VkQueueFamilyProperties> properties(count);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

			for (uint32_t i = 0; i < count; i++)
			{
				const bool graphics = (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
				if (graphics && families.Graphics == UINT32_MAX)
					families.Graphics = i;

				// Prefer a dedicated transfer queue when one exists.
				if ((properties[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !graphics)
					families.Transfer = i;

				VkBool32 present = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
				if (present && families.Present == UINT32_MAX)
					families.Present = i;
			}

			if (families.Transfer == UINT32_MAX)
				families.Transfer = families.Graphics;

			return families;
		}
	}

	VulkanDevice::VulkanDevice(const RHI::DeviceDesc& desc)
		// DeviceDesc carries the window as an opaque handle so the public
		// header names no GLFW type. This is the boundary where it becomes one
		// again, and the only place that assumes GLFW is the windowing library.
		: m_Window(static_cast<GLFWwindow*>(desc.Window))
		, m_FramesInFlight(std::max(1u, desc.FramesInFlight))
		, m_VSync(desc.VSync)
		, m_ValidationEnabled(desc.EnableValidation)
	{
		m_PendingWidth = desc.Width;
		m_PendingHeight = desc.Height;

		if (volkInitialize() != VK_SUCCESS)
		{
			RV_CORE_ERROR("Vulkan loader not found. Is a Vulkan-capable driver installed?");
			throw std::runtime_error("volkInitialize failed");
		}

		CreateInstance(desc.EnableValidation);
		CreateSurface();
		SelectPhysicalDevice();
		CreateLogicalDevice();
		CreateAllocator();
		QueryCaps();
		CreateFrameContexts();
		CreateDescriptorPool();
		CreateTimestampPools();
		CreateSwapchain();

		m_CommandList = std::make_unique<VulkanCommandList>(*this);

		RV_CORE_INFO("Vulkan device ready: {0} ({1})", m_Caps.DeviceName, m_Caps.APIName);
	}

	VulkanDevice::~VulkanDevice()
	{
		if (m_Device)
			vkDeviceWaitIdle(m_Device);

		// Anything queued for deferred destruction is safe to run now.
		if (m_Deletion)
			m_Deletion->FlushAll();

		for (auto& frame : m_Frames)
		{
			if (frame.CommandPool)    vkDestroyCommandPool(m_Device, frame.CommandPool, nullptr);
			if (frame.ImageAvailable) vkDestroySemaphore(m_Device, frame.ImageAvailable, nullptr);
			if (frame.InFlight)       vkDestroyFence(m_Device, frame.InFlight, nullptr);
		}
		m_Frames.clear();

		DestroySwapchain();

		for (VkDescriptorPool pool : m_DescriptorPools)
			vkDestroyDescriptorPool(m_Device, pool, nullptr);
		m_DescriptorPools.clear();
		m_DescriptorPool = VK_NULL_HANDLE;

		if (m_ImGuiPool) vkDestroyDescriptorPool(m_Device, m_ImGuiPool, nullptr);
		m_ImGuiPool = VK_NULL_HANDLE;

		for (VkQueryPool pool : m_TimestampPools)
			vkDestroyQueryPool(m_Device, pool, nullptr);
		m_TimestampPools.clear();
		if (m_ImmediateFence) vkDestroyFence(m_Device, m_ImmediateFence, nullptr);
		if (m_ImmediatePool)  vkDestroyCommandPool(m_Device, m_ImmediatePool, nullptr);
		if (m_Allocator)      vmaDestroyAllocator(m_Allocator);
		// Past this point any surviving resource must not touch Vulkan: its
		// objects died with the device. Resources hold the queue by shared_ptr
		// and check this flag, rather than dereferencing a destroyed device.
		if (m_Deletion)
			m_Deletion->DeviceAlive = false;

		if (m_Device)         vkDestroyDevice(m_Device, nullptr);
		if (m_Surface)        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

		if (m_DebugMessenger)
			vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);

		if (m_Instance) vkDestroyInstance(m_Instance, nullptr);
	}

	void VulkanDevice::CreateInstance(bool enableValidation)
	{
		VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.pApplicationName = "RageV";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
		appInfo.pEngineName = "RageV Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
		// Dynamic rendering is core in 1.3 and removes render pass and
		// framebuffer object management entirely.
		appInfo.apiVersion = VK_API_VERSION_1_3;

		std::vector<const char*> extensions = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};

		m_HasDebugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (m_HasDebugUtils)
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		std::vector<const char*> layers;
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		// Validation layers ship with the Vulkan SDK. Requesting one that is
		// not installed fails instance creation outright, so only ask for it
		// when it is actually present.
		if (enableValidation && HasLayer(validationLayer))
		{
			layers.push_back(validationLayer);
			RV_CORE_INFO("Vulkan validation layers enabled");
		}
		else if (enableValidation)
		{
			RV_CORE_WARN("Vulkan validation requested but VK_LAYER_KHRONOS_validation is not installed "
						 "(install the Vulkan SDK to enable it); continuing without validation");
			m_ValidationEnabled = false;
		}

		// Synchronization validation is off in the layer's default preset, but
		// it is the check that matters most here: it catches *missing barriers*,
		// which standard validation does not. Those show up otherwise as an
		// intermittently corrupt frame on one GPU and nothing on another.
		// Enabling it here rather than through vkconfig means it does not depend
		// on external configuration state.
		const VkBool32 enabled = VK_TRUE;
		const VkLayerSettingEXT layerSettings[] = {
			{ validationLayer, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enabled },
		};

		VkLayerSettingsCreateInfoEXT layerSettingsInfo{ VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT };
		layerSettingsInfo.settingCount = (uint32_t)std::size(layerSettings);
		layerSettingsInfo.pSettings = layerSettings;

		if (!layers.empty() && HasInstanceExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME, validationLayer))
		{
			extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
			RV_CORE_INFO("Vulkan synchronization validation enabled");
		}
		else
		{
			layerSettingsInfo.settingCount = 0;
		}

		VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = (uint32_t)extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.enabledLayerCount = (uint32_t)layers.size();
		createInfo.ppEnabledLayerNames = layers.data();

		// Chaining the messenger onto instance creation catches errors raised
		// during vkCreateInstance itself.
		VkDebugUtilsMessengerCreateInfoEXT messengerInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
										VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messengerInfo.pfnUserCallback = DebugCallback;

		if (!layers.empty())
		{
			// messenger -> layer settings, so validation output from instance
			// creation itself is still routed through our callback.
			createInfo.pNext = &messengerInfo;
			if (layerSettingsInfo.settingCount > 0)
				messengerInfo.pNext = &layerSettingsInfo;
		}

		VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_Instance));
		volkLoadInstance(m_Instance);

		if (!layers.empty() && m_HasDebugUtils)
			VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_Instance, &messengerInfo, nullptr, &m_DebugMessenger));
	}

	void VulkanDevice::CreateSurface()
	{
		VkWin32SurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		createInfo.hwnd = glfwGetWin32Window(m_Window);
		createInfo.hinstance = GetModuleHandle(nullptr);
		VK_CHECK(vkCreateWin32SurfaceKHR(m_Instance, &createInfo, nullptr, &m_Surface));
	}

	void VulkanDevice::SelectPhysicalDevice()
	{
		uint32_t count = 0;
		vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
		if (count == 0)
		{
			RV_CORE_ERROR("No Vulkan-capable physical devices found");
			throw std::runtime_error("no Vulkan devices");
		}

		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

		const std::vector<const char*> required = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		};

		VkPhysicalDevice best = VK_NULL_HANDLE;
		int bestScore = -1;

		for (VkPhysicalDevice device : devices)
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(device, &properties);

			if (properties.apiVersion < VK_API_VERSION_1_3)
			{
				RV_CORE_TRACE("Skipping {0}: reports Vulkan {1}.{2}, need 1.3", properties.deviceName,
							  VK_API_VERSION_MAJOR(properties.apiVersion),
							  VK_API_VERSION_MINOR(properties.apiVersion));
				continue;
			}

			if (!HasDeviceExtensions(device, required))
				continue;

			if (!FindQueueFamilies(device, m_Surface).IsComplete())
				continue;

			// Dynamic rendering and synchronization2 are the 1.3 features the
			// backend actually depends on.
			VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &features13;
			vkGetPhysicalDeviceFeatures2(device, &features2);

			if (!features13.dynamicRendering || !features13.synchronization2)
				continue;

			// Rank rather than taking the first discrete GPU: the original code
			// returned VK_NULL_HANDLE outright on integrated-only machines and
			// then used it.
			int score = 0;
			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score += 1000; break;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 500;  break;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score += 250;  break;
				default: break;
			}
			score += (int)(properties.limits.maxImageDimension2D / 1024);

			if (score > bestScore)
			{
				bestScore = score;
				best = device;
			}
		}

		if (best == VK_NULL_HANDLE)
		{
			RV_CORE_ERROR("No physical device supports Vulkan 1.3 with dynamic rendering and synchronization2");
			throw std::runtime_error("no suitable Vulkan device");
		}

		m_PhysicalDevice = best;
		m_QueueFamilies = FindQueueFamilies(m_PhysicalDevice, m_Surface);
	}

	void VulkanDevice::CreateLogicalDevice()
	{
		std::set<uint32_t> uniqueFamilies = {
			m_QueueFamilies.Graphics,
			m_QueueFamilies.Present,
		};

		const float priority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueInfos;
		queueInfos.reserve(uniqueFamilies.size());
		for (uint32_t family : uniqueFamilies)
		{
			VkDeviceQueueCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			info.queueFamilyIndex = family;
			info.queueCount = 1;
			info.pQueuePriorities = &priority;
			queueInfos.push_back(info);
		}

		VkPhysicalDeviceFeatures features{};
		VkPhysicalDeviceFeatures supported{};
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supported);
		features.samplerAnisotropy = supported.samplerAnisotropy;
		features.fillModeNonSolid  = supported.fillModeNonSolid;   // wireframe debug views
		features.depthClamp        = supported.depthClamp;         // shadow map depth clamping
		features.depthBiasClamp    = supported.depthBiasClamp;
		// Collider overlay lines thicker than one pixel. Optional in Vulkan;
		// a pipeline asking for a width the device cannot draw is a validation
		// error, so VulkanPipeline clamps to 1.0 when this is absent.
		features.wideLines         = supported.wideLines;
		m_WideLinesSupported       = supported.wideLines == VK_TRUE;
		// Indexing a sampler array with a non-constant expression, which the
		// batched quad shader does.
		features.shaderSampledImageArrayDynamicIndexing = supported.shaderSampledImageArrayDynamicIndexing;

		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.pNext = &features13;

		// dynamic_rendering is core in 1.3, but Dear ImGui's Vulkan backend
		// requires the extension to be enabled explicitly regardless.
		const std::vector<const char*> extensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		};

		VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.pNext = &features12;
		createInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
		createInfo.pQueueCreateInfos = queueInfos.data();
		createInfo.pEnabledFeatures = &features;
		createInfo.enabledExtensionCount = (uint32_t)extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();

		VK_CHECK(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device));
		// Loads device-level entry points directly, skipping the loader's
		// dispatch trampoline on every call.
		volkLoadDevice(m_Device);

		vkGetDeviceQueue(m_Device, m_QueueFamilies.Graphics, 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, m_QueueFamilies.Present, 0, &m_PresentQueue);

		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = m_QueueFamilies.Graphics;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_ImmediatePool));

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_ImmediateFence));
	}

	void VulkanDevice::CreateAllocator()
	{
		// volk resolves the entry points, so VMA has to be handed the table
		// rather than linking them statically.
		VmaVulkanFunctions functions{};
		functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo createInfo{};
		createInfo.physicalDevice = m_PhysicalDevice;
		createInfo.device = m_Device;
		createInfo.instance = m_Instance;
		createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		createInfo.pVulkanFunctions = &functions;

		VK_CHECK(vmaCreateAllocator(&createInfo, &m_Allocator));
	}

	void VulkanDevice::QueryCaps()
	{
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

		VkPhysicalDeviceMemoryProperties memory;
		vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memory);

		VkPhysicalDeviceFeatures features;
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &features);

		m_Caps.DeviceName = properties.deviceName;
		m_Caps.APIName = "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." +
								     std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + "." +
								     std::to_string(VK_API_VERSION_PATCH(properties.apiVersion));
		m_Caps.MaxTextureSlots = std::min(32u, properties.limits.maxPerStageDescriptorSampledImages);
		m_Caps.MaxTextureSize = properties.limits.maxImageDimension2D;
		m_Caps.MaxPushConstantSize = properties.limits.maxPushConstantsSize;
		m_Caps.UniformBufferAlignment = (uint32_t)properties.limits.minUniformBufferOffsetAlignment;
		m_Caps.SupportsAnisotropy = features.samplerAnisotropy == VK_TRUE;
		m_Caps.MaxAnisotropy = properties.limits.maxSamplerAnisotropy;
		m_Caps.SupportsDynamicRendering = true;
		m_Caps.SupportsTimestampQueries = properties.limits.timestampComputeAndGraphics == VK_TRUE;
		m_TimestampsSupported = m_Caps.SupportsTimestampQueries;
		// Zero means the device does not support them at all, whatever the
		// limit above said; a period of zero would turn every duration into
		// zero rather than into an error.
		m_TimestampPeriodNs = properties.limits.timestampPeriod > 0.0f
							? (double)properties.limits.timestampPeriod : 0.0;
		if (m_TimestampPeriodNs == 0.0)
			m_TimestampsSupported = false;

		for (uint32_t i = 0; i < memory.memoryHeapCount; i++)
		{
			if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				m_Caps.VideoMemoryBytes += memory.memoryHeaps[i].size;
		}
	}

	void VulkanDevice::CreateFrameContexts()
	{
		m_Deletion = std::make_shared<DeletionQueue>();
		m_Deletion->PerFrame.resize(m_FramesInFlight);

		m_Frames.resize(m_FramesInFlight);

		for (auto& frame : m_Frames)
		{
			VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			poolInfo.queueFamilyIndex = m_QueueFamilies.Graphics;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &frame.CommandPool));

			VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			allocInfo.commandPool = frame.CommandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;
			VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &frame.CommandBuffer));

			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &frame.ImageAvailable));

			// Created signalled so the very first BeginFrame does not block.
			VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.InFlight));
		}
	}

	VkDescriptorPool VulkanDevice::CreateDescriptorPoolBlock()
	{
		// One block. VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT lets
		// resource sets be released individually when they are destroyed.
		const VkDescriptorPoolSize sizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          500 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,           250 },
			// Dear ImGui's backend allocates these separately rather than as
			// combined image samplers.
			{ VK_DESCRIPTOR_TYPE_SAMPLER,                 250 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
		};

		VkDescriptorPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		createInfo.maxSets = kDescriptorSetsPerPool;
		createInfo.poolSizeCount = (uint32_t)std::size(sizes);
		createInfo.pPoolSizes = sizes;

		VkDescriptorPool pool = VK_NULL_HANDLE;
		VK_CHECK(vkCreateDescriptorPool(m_Device, &createInfo, nullptr, &pool));
		return pool;
	}

	void VulkanDevice::CreateDescriptorPool()
	{
		m_DescriptorPool = CreateDescriptorPoolBlock();
		m_DescriptorPools.push_back(m_DescriptorPool);

		// Separate, and deliberately not part of the chain. ImGui takes a pool
		// handle once and keeps it, so it cannot follow the chain to a new
		// block -- and if it is sharing the block the renderer fills, its
		// allocations start failing the moment a scene has enough materials.
		m_ImGuiPool = CreateDescriptorPoolBlock();
	}

	VkDescriptorPool VulkanDevice::AllocateDescriptorSets(const VkDescriptorSetLayout* layouts,
														  uint32_t count, VkDescriptorSet* out)
	{
		// A pool has a fixed capacity, so the only question a renderer can ask
		// is what happens when a scene needs more than one holds. Until a scene
		// of a thousand meshes was built, the answer here was that every
		// material past the ceiling failed to allocate and drew with no
		// descriptors -- reported once per material and then rendered wrong.
		//
		// A chain, allocated from the newest block and extended when that block
		// says it is full. Each set remembers which block it came from, because
		// vkFreeDescriptorSets must be given the pool that owns it.
		for (int attempt = 0; attempt < 2; attempt++)
		{
			VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = m_DescriptorPool;
			allocInfo.descriptorSetCount = count;
			allocInfo.pSetLayouts = layouts;

			const VkResult result = vkAllocateDescriptorSets(m_Device, &allocInfo, out);
			if (result == VK_SUCCESS)
				return m_DescriptorPool;

			// Both mean "this block cannot serve you", not "the device is out".
			// Anything else is a real failure and belongs in the log.
			if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
			{
				RV_CORE_ERROR("vkAllocateDescriptorSets failed: {0}", (int)result);
				return VK_NULL_HANDLE;
			}

			m_DescriptorPool = CreateDescriptorPoolBlock();
			m_DescriptorPools.push_back(m_DescriptorPool);

			RV_CORE_TRACE("Descriptor pool block {0} added ({1} sets each)",
						  m_DescriptorPools.size(), kDescriptorSetsPerPool);
		}

		// A freshly created block refused an allocation it has the capacity
		// for, which means the request is larger than a whole block.
		RV_CORE_ERROR("A descriptor set request of {0} does not fit an empty pool of {1}",
					  count, kDescriptorSetsPerPool);
		return VK_NULL_HANDLE;
	}

	void VulkanDevice::CreateTimestampPools()
	{
		if (!m_TimestampsSupported)
		{
			RV_CORE_WARN("This device does not support timestamp queries; GPU timings "
						 "will read zero");
			return;
		}

		const uint32_t frames = GetFramesInFlight();
		m_TimestampPools.resize(frames);

		for (uint32_t i = 0; i < frames; i++)
		{
			VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			info.queryType = VK_QUERY_TYPE_TIMESTAMP;
			info.queryCount = RHI::RHIDevice::kTimestampSlots;

			VK_CHECK(vkCreateQueryPool(m_Device, &info, nullptr, &m_TimestampPools[i]));
		}

		m_TimestampPoolUsed.assign(frames, 0);
		m_ResolvedTicks.assign(RHI::RHIDevice::kTimestampSlots, 0);
		m_ResolvedWritten.assign(RHI::RHIDevice::kTimestampSlots, 0);
		// Two values per query: the tick and whether it was written at all.
		m_TimestampScratch.assign((size_t)RHI::RHIDevice::kTimestampSlots * 2, 0);
	}

	void VulkanDevice::RecycleTimestampPool(VkCommandBuffer cmd)
	{
		if (m_TimestampPools.empty())
			return;

		VkQueryPool pool = m_TimestampPools[m_FrameIndex];

		// A pool that has never been reset holds nothing readable, and asking
		// is a validation error rather than an empty answer. The first pass
		// through each frame slot therefore resets without reading.
		const bool readable = m_TimestampPoolUsed[m_FrameIndex] != 0;

		// Read what this pool holds before resetting it. The fence for this
		// frame slot has already been waited on, so the GPU is finished with
		// everything that wrote into it -- which is what makes this free rather
		// than a stall.
		const VkResult result = readable
			? vkGetQueryPoolResults(
				m_Device, pool, 0, RHI::RHIDevice::kTimestampSlots,
				m_TimestampScratch.size() * sizeof(uint64_t), m_TimestampScratch.data(),
				sizeof(uint64_t) * 2,
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
			: VK_NOT_READY;

		if (readable && (result == VK_SUCCESS || result == VK_NOT_READY))
		{
			for (uint32_t i = 0; i < RHI::RHIDevice::kTimestampSlots; i++)
			{
				m_ResolvedTicks[i] = m_TimestampScratch[(size_t)i * 2];
				// Availability, not the tick: a slot nothing wrote reads zero
				// here, and a zero tick is a legal value.
				m_ResolvedWritten[i] = m_TimestampScratch[(size_t)i * 2 + 1] != 0 ? 1 : 0;
			}
		}

		vkCmdResetQueryPool(cmd, pool, 0, RHI::RHIDevice::kTimestampSlots);
		m_TimestampPoolUsed[m_FrameIndex] = 1;
	}

	VkFormat VulkanDevice::SelectDepthFormat() const
	{
		// D32_SFLOAT_S8_UINT first: stencil is wanted later for masking, and
		// D24_UNORM_S8_UINT is not universally supported (notably on some AMD).
		const VkFormat candidates[] = {
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
		};

		for (VkFormat format : candidates)
		{
			VkFormatProperties properties;
			vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &properties);
			if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
				return format;
		}

		return VK_FORMAT_D32_SFLOAT;
	}

	void VulkanDevice::CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR capabilities;
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities));

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

		VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
												    : formats[0];
		for (const auto& format : formats)
		{
			// UNORM rather than SRGB: the renderer writes already-encoded
			// colour, and ImGui's Vulkan backend assumes a linear-write target.
			if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosen = format;
				break;
			}
		}
		m_SwapchainFormat = chosen.format;
		m_SwapchainColorSpace = chosen.colorSpace;

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());

		m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;   // always available
		if (!m_VSync)
		{
			// IMMEDIATE first, MAILBOX only if there is no IMMEDIATE.
			//
			// MAILBOX is the nicer mode on paper -- unsynchronised without
			// tearing -- and it is what this used to prefer. It is also silently
			// wrong on this driver in the case that matters most: a swapchain
			// created in MAILBOX *as the replacement for a FIFO one* presents at
			// exactly the refresh rate anyway. Measured, four runs each:
			// created MAILBOX at startup, ~450 FPS; recreated MAILBOX after the
			// surface had been presenting FIFO, 240.0 FPS every single run.
			// IMMEDIATE does not care -- 442 FPS from the identical toggle --
			// and startup-with-vsync-off measures the same in either mode.
			//
			// Ruled out before landing on the mode itself: oldSwapchain (fully
			// destroying the old swapchain first changes nothing), image count,
			// and resizing the window so the compositor re-evaluates it. None
			// of the three moved the number off 240.0.
			//
			// Unchecking VSync in the editor is the case this has to serve, so
			// the mode that always works wins. Tearing is what vsync off means.
			bool hasImmediate = false;
			bool hasMailbox = false;
			for (VkPresentModeKHR mode : presentModes)
			{
				hasImmediate = hasImmediate || mode == VK_PRESENT_MODE_IMMEDIATE_KHR;
				hasMailbox = hasMailbox || mode == VK_PRESENT_MODE_MAILBOX_KHR;
			}

			if (hasImmediate)
				m_PresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			else if (hasMailbox)
				m_PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		}

		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			m_SwapchainExtent = capabilities.currentExtent;
		}
		else
		{
			int width = (int)m_PendingWidth;
			int height = (int)m_PendingHeight;
			glfwGetFramebufferSize(m_Window, &width, &height);
			// The original clamped width against maxImageExtent.height and
			// height against maxImageExtent.width.
			m_SwapchainExtent.width = std::clamp((uint32_t)width,
				capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			m_SwapchainExtent.height = std::clamp((uint32_t)height,
				capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}

		uint32_t imageCount = capabilities.minImageCount + 1;
		// maxImageCount == 0 means "no limit"; without this clamp the extra
		// image can exceed the surface's maximum.
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
			imageCount = capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		createInfo.surface = m_Surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = m_SwapchainFormat;
		createInfo.imageColorSpace = m_SwapchainColorSpace;
		createInfo.imageExtent = m_SwapchainExtent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = m_PresentMode;
		createInfo.clipped = VK_TRUE;
		// The swapchain being replaced, when there is one.
		//
		// Not VK_NULL_HANDLE, and the old one is not destroyed before this
		// point. vkDeviceWaitIdle waits on the *queues*; it says nothing about
		// the presentation engine, which may still be holding images of the old
		// swapchain and still waiting on their semaphores. Tearing it down
		// first and hoping is the classic intermittent crash on resize or on a
		// present-mode change -- it works every time until the one time the
		// compositor is a frame behind.
		//
		// Handing the old one over instead lets the driver retire it in order,
		// and lets images be reused rather than reallocated.
		createInfo.oldSwapchain = m_Swapchain;

		const uint32_t families[] = { m_QueueFamilies.Graphics, m_QueueFamilies.Present };
		if (m_QueueFamilies.Graphics != m_QueueFamilies.Present)
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = families;
		}
		else
		{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		VK_CHECK(vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain));

		uint32_t actualCount = 0;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualCount, nullptr);
		m_SwapchainImages.resize(actualCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualCount, m_SwapchainImages.data());

		m_SwapchainImageViews.resize(actualCount);
		for (uint32_t i = 0; i < actualCount; i++)
		{
			VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			viewInfo.image = m_SwapchainImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = m_SwapchainFormat;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 1;
			// The original never incremented its loop counter here, so every
			// view was written into slot 0 and the rest leaked.
			VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]));
		}

		m_RenderFinished.resize(actualCount);
		for (uint32_t i = 0; i < actualCount; i++)
		{
			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinished[i]));
		}

		CreateDepthResources();

		RV_CORE_INFO("Swapchain: {0}x{1}, {2} images, present mode {3}",
					 m_SwapchainExtent.width, m_SwapchainExtent.height, actualCount, (int)m_PresentMode);
	}

	void VulkanDevice::CreateDepthResources()
	{
		m_DepthFormat = SelectDepthFormat();

		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = m_DepthFormat;
		imageInfo.extent = { m_SwapchainExtent.width, m_SwapchainExtent.height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		VK_CHECK(vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.image = m_DepthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_DepthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (m_DepthFormat == VK_FORMAT_D24_UNORM_S8_UINT || m_DepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
			viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView));

		// Dynamic rendering does not perform the initial layout transition a
		// render pass would, so the image would still be UNDEFINED when
		// vkCmdBeginRendering declares it as DEPTH_ATTACHMENT_OPTIMAL. Nothing
		// ever moves it out of that layout afterwards, so once is enough.
		ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
								   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
									VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = DepthAttachmentLayout(m_DepthFormat);
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_DepthImage;
			barrier.subresourceRange = viewInfo.subresourceRange;

			VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dependency.imageMemoryBarrierCount = 1;
			dependency.pImageMemoryBarriers = &barrier;
			vkCmdPipelineBarrier2(cmd, &dependency);
		});
	}

	void VulkanDevice::DestroySwapchainResources()
	{
		// Everything this device owns *about* a swapchain, but not the
		// swapchain: the views and the depth buffer are ours and are safe to
		// destroy once the queues are idle.
		//
		// The per-image semaphores are not, and are handled separately -- a
		// pending present waits on one, and presentation is not covered by
		// vkDeviceWaitIdle.
		if (m_DepthImageView) { vkDestroyImageView(m_Device, m_DepthImageView, nullptr); m_DepthImageView = VK_NULL_HANDLE; }
		if (m_DepthImage)     { vmaDestroyImage(m_Allocator, m_DepthImage, m_DepthAllocation); m_DepthImage = VK_NULL_HANDLE; }

		for (VkImageView view : m_SwapchainImageViews)
			vkDestroyImageView(m_Device, view, nullptr);
		m_SwapchainImageViews.clear();
		m_SwapchainImages.clear();
	}

	void VulkanDevice::DestroySwapchain()
	{
		DestroySwapchainResources();

		for (VkSemaphore semaphore : m_RenderFinished)
			vkDestroySemaphore(m_Device, semaphore, nullptr);
		m_RenderFinished.clear();

		if (m_Swapchain)
		{
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}
	}

	void VulkanDevice::RecreateSwapchain()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (width == 0 || height == 0)
			return;   // minimised; try again next frame

		vkDeviceWaitIdle(m_Device);

		// Order matters here, and getting it wrong is an intermittent crash
		// rather than a reliable one.
		//
		// The views and depth buffer go first -- they are ours. The old
		// swapchain and its semaphores stay alive *through* the creation of the
		// new one, because the presentation engine may still be using them and
		// vkDeviceWaitIdle does not wait for it. CreateSwapchain hands the old
		// handle over as oldSwapchain, which is the point at which the driver
		// takes responsibility for retiring it in order.
		DestroySwapchainResources();

		const VkSwapchainKHR retired = m_Swapchain;
		std::vector<VkSemaphore> retiredSemaphores;
		retiredSemaphores.swap(m_RenderFinished);

		m_PendingWidth = (uint32_t)width;
		m_PendingHeight = (uint32_t)height;
		CreateSwapchain();

		// Now, and not before: the new swapchain exists, the old one has been
		// handed over, and nothing can still be waiting on these.
		if (retired != VK_NULL_HANDLE && retired != m_Swapchain)
			vkDestroySwapchainKHR(m_Device, retired, nullptr);

		for (VkSemaphore semaphore : retiredSemaphores)
			vkDestroySemaphore(m_Device, semaphore, nullptr);

		m_SwapchainDirty = false;
	}

	void VulkanDevice::OnResize(uint32_t width, uint32_t height)
	{
		m_PendingWidth = width;
		m_PendingHeight = height;
		m_SwapchainDirty = true;
	}

	void VulkanDevice::SetVSync(bool enabled)
	{
		if (m_VSync == enabled)
			return;
		m_VSync = enabled;
		m_SwapchainDirty = true;
	}

	RHI::RHICommandList* VulkanDevice::BeginFrame()
	{
		if (m_SwapchainDirty)
			RecreateSwapchain();

		if (m_Swapchain == VK_NULL_HANDLE || m_SwapchainExtent.width == 0 || m_SwapchainExtent.height == 0)
			return nullptr;

		FrameContext& frame = m_Frames[m_FrameIndex];

		// Waiting here is what makes the rest of this frame slot safe to touch:
		// its command buffer, its descriptor sets and anything queued for
		// deferred destruction.
		VK_CHECK(vkWaitForFences(m_Device, 1, &frame.InFlight, VK_TRUE, UINT64_MAX));

		const VkResult acquire = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX,
													   frame.ImageAvailable, VK_NULL_HANDLE, &m_ImageIndex);
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return nullptr;
		}
		if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
		{
			RV_CORE_ERROR("vkAcquireNextImageKHR failed: {0}", ResultToString(acquire));
			return nullptr;
		}

		// Only reset once the frame is definitely going ahead, otherwise an
		// early return above would leave the fence unsignalled and deadlock the
		// next pass through this slot.
		VK_CHECK(vkResetFences(m_Device, 1, &frame.InFlight));

		// Safe now that this slot's fence has been waited on.
		m_Deletion->Flush(m_FrameIndex);

		VK_CHECK(vkResetCommandPool(m_Device, frame.CommandPool, 0));
		m_CommandList->Begin(frame.CommandBuffer);

		// After Begin, because resetting a query pool is a recorded command.
		RecycleTimestampPool(frame.CommandBuffer);

		m_FrameActive = true;
		// From here until EndFrame, a destroyed resource may already be in
		// this frame's command buffer, and the queue slots it accordingly.
		m_Deletion->InFrame = true;

		return m_CommandList.get();
	}

	void VulkanDevice::EndFrame()
	{
		if (!m_FrameActive)
			return;

		FrameContext& frame = m_Frames[m_FrameIndex];
		m_CommandList->End();
		m_FrameActive = false;
		m_Deletion->InFrame = false;

		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &frame.ImageAvailable;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &frame.CommandBuffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &m_RenderFinished[m_ImageIndex];

		VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, frame.InFlight));

		// After the submit and before the present: the image holds the finished
		// frame at exactly this point, and reading it here means the capture is
		// of what is about to be shown rather than of an intermediate state.
		if (m_Capture)
			CaptureSwapchainImage();

		VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &m_RenderFinished[m_ImageIndex];
		present.swapchainCount = 1;
		present.pSwapchains = &m_Swapchain;
		present.pImageIndices = &m_ImageIndex;

		const VkResult result = vkQueuePresentKHR(m_PresentQueue, &present);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			m_SwapchainDirty = true;
		else if (result != VK_SUCCESS)
			RV_CORE_ERROR("vkQueuePresentKHR failed: {0}", ResultToString(result));

		m_FrameIndex = (m_FrameIndex + 1) % m_FramesInFlight;
		m_Deletion->FrameIndex = m_FrameIndex;
	}

	void VulkanDevice::WaitIdle()
	{
		if (m_Device)
			vkDeviceWaitIdle(m_Device);
	}

	void VulkanDevice::RequestCapture(CaptureCallback callback)
	{
		m_Capture = std::move(callback);
	}

	// Copies the presented image into host memory and hands it to the waiting
	// callback.
	//
	// Deliberately unsubtle: it waits for the frame's fence, does its own
	// one-shot submit, and allocates a staging buffer it immediately throws
	// away. Every one of those is wrong for something on the frame path and
	// right for a diagnostic that fires once.
	void VulkanDevice::CaptureSwapchainImage()
	{
		FrameContext& frame = m_Frames[m_FrameIndex];

		// The submit that drew this frame has to have finished before its
		// result can be read.
		vkWaitForFences(m_Device, 1, &frame.InFlight, VK_TRUE, UINT64_MAX);

		const uint32_t width = m_SwapchainExtent.width;
		const uint32_t height = m_SwapchainExtent.height;
		const VkDeviceSize size = (VkDeviceSize)width * height * 4;

		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
						  VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = nullptr;
		VmaAllocationInfo allocated{};

		if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer, &allocation,
							&allocated) != VK_SUCCESS)
		{
			RV_CORE_ERROR("Capture: could not allocate a staging buffer");
			m_Capture = nullptr;
			return;
		}

		const VkImage image = m_SwapchainImages[m_ImageIndex];

		ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			auto barrier = [&](VkImageLayout from, VkImageLayout to,
							   VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess,
							   VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage)
			{
				VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
				imageBarrier.srcStageMask = srcStage;
				imageBarrier.srcAccessMask = srcAccess;
				imageBarrier.dstStageMask = dstStage;
				imageBarrier.dstAccessMask = dstAccess;
				imageBarrier.oldLayout = from;
				imageBarrier.newLayout = to;
				imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageBarrier.image = image;
				imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBarrier.subresourceRange.levelCount = 1;
				imageBarrier.subresourceRange.layerCount = 1;

				VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &imageBarrier;
				vkCmdPipelineBarrier2(cmd, &dependency);
			};

			barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					0, VK_ACCESS_2_TRANSFER_READ_BIT,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { width, height, 1 };
			vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
								   buffer, 1, &region);

			// Back to PRESENT_SRC, because the present that follows this
			// expects to find it in exactly the layout it was left in.
			barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					VK_ACCESS_2_TRANSFER_READ_BIT, 0,
					VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		});

		std::vector<uint8_t> rgba((size_t)size);
		memcpy(rgba.data(), allocated.pMappedData, (size_t)size);

		// Swapchains on this platform are usually BGRA; the contract is RGBA,
		// so a caller never has to ask which.
		if (m_SwapchainFormat == VK_FORMAT_B8G8R8A8_UNORM ||
			m_SwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB)
		{
			for (size_t i = 0; i < rgba.size(); i += 4)
				std::swap(rgba[i], rgba[i + 2]);
		}

		vmaDestroyBuffer(m_Allocator, buffer, allocation);

		// Moved out before invoking, so a callback that requests another
		// capture arms the next frame rather than being cleared by this one.
		CaptureCallback callback;
		callback.swap(m_Capture);
		callback(rgba.data(), width, height);
	}

	void VulkanDevice::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record)
	{
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandPool = m_ImmediatePool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd));

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

		record(cmd);

		VK_CHECK(vkEndCommandBuffer(cmd));

		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;

		VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, m_ImmediateFence));
		VK_CHECK(vkWaitForFences(m_Device, 1, &m_ImmediateFence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(m_Device, 1, &m_ImmediateFence));

		vkFreeCommandBuffers(m_Device, m_ImmediatePool, 1, &cmd);
	}

	void VulkanDevice::DeferDestruction(std::function<void()> deleter)
	{
		m_Deletion->Push(std::move(deleter));
	}

	void VulkanDevice::SetDebugName(uint64_t handle, VkObjectType type, const char* name)
	{
		if (!m_HasDebugUtils || !name || !vkSetDebugUtilsObjectNameEXT)
			return;

		VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = type;
		info.objectHandle = handle;
		info.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(m_Device, &info);
	}

	// -------------------------------------------------------------------------
	// Resource factories
	// -------------------------------------------------------------------------
	RHI::Ref<RHI::RHIBuffer> VulkanDevice::CreateBuffer(const RHI::BufferDesc& desc)
	{
		return std::make_shared<VulkanBuffer>(*this, desc);
	}

	RHI::Ref<RHI::RHITexture> VulkanDevice::CreateTexture(const RHI::TextureDesc& desc)
	{
		return std::make_shared<VulkanTexture>(*this, desc);
	}

	RHI::Ref<RHI::RHISampler> VulkanDevice::CreateSampler(const RHI::SamplerDesc& desc)
	{
		return std::make_shared<VulkanSampler>(*this, desc);
	}

	RHI::Ref<RHI::RHIShader> VulkanDevice::CreateShader(const RHI::CompiledShader& compiled)
	{
		return std::make_shared<VulkanShader>(*this, compiled);
	}

	RHI::Ref<RHI::RHIPipeline> VulkanDevice::CreatePipeline(const RHI::GraphicsPipelineDesc& desc)
	{
		return std::make_shared<VulkanPipeline>(*this, desc);
	}

	RHI::Ref<RHI::RHIRenderTarget> VulkanDevice::CreateRenderTarget(const RHI::RenderTargetDesc& desc)
	{
		return std::make_shared<VulkanRenderTarget>(*this, desc);
	}

	RHI::Ref<RHI::RHIResourceSet> VulkanDevice::CreateResourceSet(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set)
	{
		return std::make_shared<VulkanResourceSet>(*this, std::static_pointer_cast<VulkanPipeline>(pipeline), set);
	}
}
