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
		, m_FramesInFlight(Math::Max(1u, desc.FramesInFlight))
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

		CreateInstance(desc.EnableValidation, desc.GpuAssistedValidation);
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
		// After FlushAll above, so every pipeline that borrowed it and every
		// heap allocated from it has already gone.
		if (m_BindlessLayout) vkDestroyDescriptorSetLayout(m_Device, m_BindlessLayout, nullptr);
		m_BindlessLayout = VK_NULL_HANDLE;

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

	void VulkanDevice::CreateInstance(bool enableValidation, bool gpuAssisted)
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
		//
		// GPU-assisted validation is the other opt-in, and the only check that
		// sees an out-of-range index into the bindless heap: the layer cannot
		// know which element a shader will read, so it instruments the shader
		// to find out (ENGINE-NOTES 7al). It replaces sync validation for the
		// run rather than joining it -- both instrument, and together they
		// have not been a reliable pair.
		const VkBool32 enabled = VK_TRUE;
		const VkBool32 disabled = VK_FALSE;
		// Core checks go off with it too, on the layer's own advice: the two
		// together are "not recommended" and it says so on every run. The
		// ordinary --validation=on run is where core checks live.
		const VkLayerSettingEXT layerSettings[] = {
			{ validationLayer, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, gpuAssisted ? &disabled : &enabled },
			{ validationLayer, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, gpuAssisted ? &disabled : &enabled },
			{ validationLayer, "gpuav_enable",  VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, gpuAssisted ? &enabled : &disabled },
		};

		VkLayerSettingsCreateInfoEXT layerSettingsInfo{ VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT };
		layerSettingsInfo.settingCount = (uint32_t)std::size(layerSettings);
		layerSettingsInfo.pSettings = layerSettings;

		if (!layers.empty() && HasInstanceExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME, validationLayer))
		{
			extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
			// Two calls rather than one with a ternary: a format string is
			// checked against its arguments at compile time now, and a
			// runtime choice between two literals is not a constant the
			// checker can see.
			if (gpuAssisted)
				RV_CORE_INFO("Vulkan GPU-assisted validation enabled "
							 "(synchronization validation off for this run)");
			else
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

			// And imageCubeArray, which the lit shaders need to pick a
			// reflection probe per object. Rejected here rather than left to
			// fail later, because there is no fallback: without it the PBR
			// shader cannot be created, so a device that gets past this point
			// would draw nothing lit at all.
			if (!features2.features.imageCubeArray)
				continue;

			// BC, for cooked textures. One feature bit covers every BC format,
			// and every desktop GPU this engine can run on has it -- but the
			// imageCubeArray lesson stands: a requirement that is not stated
			// at selection is undefined behaviour on the machine that lacks it.
			if (!features2.features.textureCompressionBC)
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
		// Different blend state on different attachments of one pipeline.
		// Weighted-blended transparency is built on it: accumulation sums
		// while revealage multiplies down, in a single draw. Without it every
		// attachment must match attachment zero, which validation says
		// plainly -- and which is how this was found rather than shipped.
		features.independentBlend  = supported.independentBlend;
		m_IndependentBlendSupported = supported.independentBlend == VK_TRUE;
		// A fragment shader writing a storage image -- the voxeliser's
		// imageStore (ENGINE-NOTES 7bc). Optional in the spec and present on
		// every desktop part; asked for where it is, and reported in the caps
		// so the voxel pass can refuse where it is not.
		features.fragmentStoresAndAtomics = supported.fragmentStoresAndAtomics;
		m_FragmentStoresSupported = supported.fragmentStoresAndAtomics == VK_TRUE;
		// Indexing a sampler array with a non-constant expression, which the
		// batched quad shader does.
		features.shaderSampledImageArrayDynamicIndexing = supported.shaderSampledImageArrayDynamicIndexing;
		// samplerCubeArray, which the PBR shaders use to pick a reflection
		// probe per object. Both the SPIR-V capability and the cube-array image
		// view require it.
		//
		// Neither failed loudly without it. This driver created the view and
		// ran the shader regardless, and only the validation layer said the two
		// were undefined -- so the whole feature worked on this machine and was
		// undefined behaviour on any stricter one. Device selection above
		// rejects a physical device that lacks it, so this is not a probe: by
		// here it is known to be supported.
		features.imageCubeArray = VK_TRUE;
		// Selection above rejected any device without it; see the note there.
		features.textureCompressionBC = VK_TRUE;

		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;
		// Mandatory in 1.3, and declared by glslang for a `discard` in a
		// fragment stage that also does image stores (the voxeliser, 7bc):
		// the capability the SPIR-V asks for has to be enabled or the layer
		// objects at module creation.
		features13.shaderDemoteToHelperInvocation = VK_TRUE;
		// Mandatory in 1.3, and the mesh shader's SPIR-V needs it: glslang
		// spells the work-group size as LocalSizeId at SPIR-V 1.6, and the
		// spec ties that spelling to this feature bit.
		features13.maintenance4 = VK_TRUE;

		// Descriptor indexing, for the bindless texture heap (ENGINE-NOTES
		// 7al). Optional: every 1.2 feature bit is, so they are asked for and
		// enabled only where present, and the caps say which way it went. Six
		// bits, each load-bearing: runtime-sized arrays in the shader,
		// non-uniform indexing of them, a set that may be partially written
		// and rewritten while bound, and a set allocated with fewer
		// descriptors than its layout's maximum.
		VkPhysicalDeviceVulkan12Features supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceFeatures2 supported2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		supported2.pNext = &supported12;
		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported2);

		// Ray queries (ENGINE-NOTES 7am): three extensions and three features,
		// enabled only when every one is present. Deferred host operations is
		// a dependency of acceleration structures the spec insists on, though
		// nothing here defers anything.
		const std::vector<const char*> rayExtensions = {
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_RAY_QUERY_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		};
		VkPhysicalDeviceRayQueryFeaturesKHR supportedRayQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAcceleration{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		supportedAcceleration.pNext = &supportedRayQuery;
		const bool rayExtensionsPresent = HasDeviceExtensions(m_PhysicalDevice, rayExtensions);
		if (rayExtensionsPresent)
		{
			// Chained behind the 1.2 query above only if the extensions exist:
			// asking about a feature struct of an absent extension is itself
			// a validation error.
			supported12.pNext = &supportedAcceleration;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported2);
			supported12.pNext = nullptr;
		}
		m_RayQuerySupported = rayExtensionsPresent &&
							  supported12.bufferDeviceAddress &&
							  supportedAcceleration.accelerationStructure &&
							  supportedRayQuery.rayQuery;

		VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		rayQueryFeatures.rayQuery = VK_TRUE;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		accelerationFeatures.accelerationStructure = VK_TRUE;
		accelerationFeatures.pNext = &rayQueryFeatures;

		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.pNext = &features13;
		if (m_RayQuerySupported)
		{
			features12.bufferDeviceAddress = VK_TRUE;
			features13.pNext = &accelerationFeatures;
		}
		m_DescriptorIndexingSupported =
			supported12.descriptorIndexing &&
			supported12.runtimeDescriptorArray &&
			supported12.shaderSampledImageArrayNonUniformIndexing &&
			supported12.descriptorBindingPartiallyBound &&
			supported12.descriptorBindingSampledImageUpdateAfterBind &&
			supported12.descriptorBindingVariableDescriptorCount;
		if (m_DescriptorIndexingSupported)
		{
			features12.descriptorIndexing = VK_TRUE;
			features12.runtimeDescriptorArray = VK_TRUE;
			features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			features12.descriptorBindingPartiallyBound = VK_TRUE;
			features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
			features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
		}

		// dynamic_rendering is core in 1.3, but Dear ImGui's Vulkan backend
		// requires the extension to be enabled explicitly regardless.
		std::vector<const char*> extensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		};
		if (m_RayQuerySupported)
			extensions.insert(extensions.end(), rayExtensions.begin(), rayExtensions.end());

		// Mesh shading (roadmap 8.3's second half). One extension, one feature
		// bit, both optional: the meshlet depth path asks the caps and the
		// classic vertex path is what runs where the answer is no. The task
		// stage is deliberately *not* enabled -- the meshlet path culls in the
		// mesh stage itself, and a feature nobody calls is a feature the
		// validation layers make everyone carry.
		VkPhysicalDeviceMeshShaderFeaturesEXT supportedMesh{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
		if (HasDeviceExtensions(m_PhysicalDevice, { VK_EXT_MESH_SHADER_EXTENSION_NAME }))
		{
			VkPhysicalDeviceFeatures2 meshQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			meshQuery.pNext = &supportedMesh;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &meshQuery);
		}
		m_MeshShadingSupported = supportedMesh.meshShader == VK_TRUE;

		VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
		meshFeatures.meshShader = VK_TRUE;
		if (m_MeshShadingSupported)
		{
			extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
			meshFeatures.pNext = features12.pNext;
			features12.pNext = &meshFeatures;
		}

		// The post-mortem pair, both optional and both free when idle.
		// Checkpoints (NVIDIA) let the queue answer "which breadcrumb did the
		// GPU reach" after a loss; device fault (cross-vendor) reports what
		// kind of fault and at what address. Neither changes rendering.
		m_CheckpointsSupported = HasDeviceExtensions(
			m_PhysicalDevice, { VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME });
		if (m_CheckpointsSupported)
			extensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

		VkPhysicalDeviceFaultFeaturesEXT supportedFault{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT };
		if (HasDeviceExtensions(m_PhysicalDevice, { VK_EXT_DEVICE_FAULT_EXTENSION_NAME }))
		{
			VkPhysicalDeviceFeatures2 faultQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			faultQuery.pNext = &supportedFault;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &faultQuery);
		}
		m_DeviceFaultSupported = supportedFault.deviceFault == VK_TRUE;

		VkPhysicalDeviceFaultFeaturesEXT faultFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT };
		faultFeatures.deviceFault = VK_TRUE;
		if (m_DeviceFaultSupported)
		{
			extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
			faultFeatures.pNext = &features12;
		}

		VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		createInfo.pNext = m_DeviceFaultSupported ? (void*)&faultFeatures
												  : (void*)&features12;
		createInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
		createInfo.pQueueCreateInfos = queueInfos.data();
		createInfo.pEnabledFeatures = &features;
		createInfo.enabledExtensionCount = (uint32_t)extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();

		VK_CHECK(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device));
		// Loads device-level entry points directly, skipping the loader's
		// dispatch trampoline on every call.
		volkLoadDevice(m_Device);

		// Said once, because "no checkpoints reached" means two very different
		// things depending on this and a post-mortem that cannot tell them
		// apart is worse than none.
		RV_CORE_INFO("GPU post-mortem: checkpoints {0}, device fault {1}",
					 m_CheckpointsSupported ? "available" : "UNAVAILABLE",
					 m_DeviceFaultSupported ? "available" : "UNAVAILABLE");

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
		// Acceleration structures and their inputs are reached by address, and
		// VMA has to know the feature is on to allocate memory that can be.
		if (m_RayQuerySupported)
			createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

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
		m_Caps.MaxTextureSlots = Math::Min(32u, properties.limits.maxPerStageDescriptorSampledImages);
		m_Caps.MaxTextureSize = properties.limits.maxImageDimension2D;
		m_Caps.MaxPushConstantSize = properties.limits.maxPushConstantsSize;
		m_Caps.UniformBufferAlignment = (uint32_t)properties.limits.minUniformBufferOffsetAlignment;
		// **All four limits, intersected.** The scene target is a colour
		// attachment, a depth attachment, and sampled by depth of field and
		// the composite -- so a count is only usable if every one of those
		// offers it. framebufferColorSampleCounts alone is the limit people
		// reach for and the one that reports 8 on hardware whose
		// sampledImageDepthSampleCounts stops at 4.
		{
			const VkSampleCountFlags usable =
				properties.limits.framebufferColorSampleCounts
				& properties.limits.framebufferDepthSampleCounts
				& properties.limits.sampledImageColorSampleCounts
				& properties.limits.sampledImageDepthSampleCounts;

			// The flag bit for a count *is* the count, so this walks the enum
			// upward and keeps the last one that is present.
			m_Caps.MaxSampleCount = 1;
			for (uint32_t count : { 2u, 4u, 8u, 16u, 32u, 64u })
			{
				if (usable & (VkSampleCountFlags)count)
					m_Caps.MaxSampleCount = count;
			}
		}

		m_Caps.SupportsAnisotropy = features.samplerAnisotropy == VK_TRUE;
		m_Caps.MaxAnisotropy = properties.limits.maxSamplerAnisotropy;
		m_Caps.SupportsDynamicRendering = true;
		m_Caps.SupportsRayQuery = m_RayQuerySupported;
		m_Caps.SupportsMeshShading = m_MeshShadingSupported;
		m_Caps.SupportsTimestampQueries = properties.limits.timestampComputeAndGraphics == VK_TRUE;

		// The heap's capacity is what one update-after-bind set may hold, which
		// is a separate, usually far larger limit than the ordinary one. A
		// device whose limit is under the floor reports the feature absent: a
		// heap of a hundred textures is not the feature, it is a smaller version
		// of the problem it exists to remove.
		if (m_DescriptorIndexingSupported)
		{
			VkPhysicalDeviceVulkan12Properties properties12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
			VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			properties2.pNext = &properties12;
			vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &properties2);

			const uint32_t limit = Math::Min(properties12.maxDescriptorSetUpdateAfterBindSampledImages,
											 properties12.maxPerStageDescriptorUpdateAfterBindSampledImages);
			if (limit >= kBindlessFloor)
			{
				m_Caps.SupportsDescriptorIndexing = true;
				m_Caps.MaxBindlessTextures = Math::Min(kBindlessCapacity, limit);
			}
			else
			{
				RV_CORE_WARN("[Vulkan] descriptor indexing present but the update-after-bind "
							 "sampled-image limit is {0}; the bindless heap is disabled", limit);
				m_DescriptorIndexingSupported = false;
			}
		}
		// Core since Vulkan 1.0, and the graphics queue this engine uses is
		// required to support it -- so this is reporting rather than probing.
		m_Caps.SupportsCompute = true;
		m_Caps.MaxComputeWorkGroupSize = properties.limits.maxComputeWorkGroupSize[0];
		m_Caps.MaxComputeWorkGroupCount = properties.limits.maxComputeWorkGroupCount[0];
		m_Caps.MaxStorageBufferBytes = properties.limits.maxStorageBufferRange;
		m_Caps.SupportsFragmentStores = m_FragmentStoresSupported;
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
		std::vector<VkDescriptorPoolSize> sizes = {
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
		// A type from an extension the device did not enable is a creation
		// error, so the scene sets' TLAS binding is only provided for where it
		// can exist (ENGINE-NOTES 7am).
		if (m_RayQuerySupported)
			sizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 200 });

		VkDescriptorPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		createInfo.maxSets = kDescriptorSetsPerPool;
		createInfo.poolSizeCount = (uint32_t)sizes.size();
		createInfo.pPoolSizes = sizes.data();

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
			m_SwapchainExtent.width = Math::Clamp((uint32_t)width,
				capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			m_SwapchainExtent.height = Math::Clamp((uint32_t)height,
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

		// Nothing is recorded or submitted after the device is gone. Without
		// this the frame proceeds, fails at the wait, fails again at the
		// submit, and does it again next frame -- which is how one fault
		// becomes a log with nothing in it but the fault's own echo.
		if (Vk::DeviceLost())
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

		// The bound-set tracker's lifetime is one command buffer *recording*,
		// which starts here -- not a present cycle. A set bound in a previous
		// recording is fair game to rewrite once its fence has been waited on,
		// and that wait is exactly what precedes this line.
		m_BoundThisFrame.clear();

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

		// The same point in the frame, for the same reason: this submit is what
		// wrote the target, and the capture waits on its fence.
		if (m_TextureCapture && m_CaptureTexture)
			CaptureTextureImage();

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

	void VulkanDevice::NoteSetBound(VkDescriptorSet set, VkCommandBuffer cmd)
	{
		// Under validation only, like the tripwire it feeds: it is a
		// diagnostic, and a hash insert on every descriptor bind is not
		// something to pay for in a build that will never read it.
		if (m_ValidationEnabled)
			m_BoundThisFrame[set] = cmd;
	}

	bool VulkanDevice::WasSetBoundThisFrame(VkDescriptorSet set) const
	{
		return m_ValidationEnabled && m_BoundThisFrame.count(set) != 0;
	}

	void VulkanDevice::RetireBinds(VkCommandBuffer cmd)
	{
		for (auto it = m_BoundThisFrame.begin(); it != m_BoundThisFrame.end(); )
			it = it->second == cmd ? m_BoundThisFrame.erase(it) : std::next(it);
	}

	bool VulkanDevice::IsDeviceLost() const
	{
		// The first caller to learn the device is gone triggers the
		// post-mortem. Here rather than at the VK_CHECK that latched it,
		// because the report needs the device and the queue, and the latch
		// site is a free function that has neither.
		if (Vk::DeviceLost() && !m_CrashDetailsReported)
		{
			m_CrashDetailsReported = true;
			ReportGpuCrashDetails();
		}
		return Vk::DeviceLost();
	}

	void VulkanDevice::SetCheckpoint(VkCommandBuffer cmd, const char* name)
	{
		if (!m_CheckpointsSupported || !name)
			return;

		// The driver stores the pointer, not the characters, and answers with
		// it after the crash -- so the string is interned for the device's
		// lifetime. A node-based set keeps every address stable as it grows.
		const auto& interned = *m_CheckpointNames.insert(name).first;
		vkCmdSetCheckpointNV(cmd, interned.c_str());
	}

	namespace
	{
		const char* FaultAddressTypeToString(VkDeviceFaultAddressTypeEXT type)
		{
			switch (type)
			{
				case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:                        return "none";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:                return "INVALID READ";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:               return "INVALID WRITE";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:             return "INVALID EXECUTE";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "shader instruction pointer (unknown)";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "shader instruction pointer (invalid)";
				case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:   return "shader instruction pointer (faulted here)";
				default:                                                           return "?";
			}
		}
	}

	void VulkanDevice::ReportGpuCrashDetails() const
	{
		if (m_CheckpointsSupported)
		{
			uint32_t count = 0;
			vkGetQueueCheckpointDataNV(m_GraphicsQueue, &count, nullptr);
			std::vector<VkCheckpointDataNV> data(count);
			for (auto& entry : data)
				entry.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
			vkGetQueueCheckpointDataNV(m_GraphicsQueue, &count, data.data());

			if (count == 0)
				RV_CORE_ERROR("  GPU checkpoints: none reached -- the fault "
							  "precedes the first breadcrumb");
			for (const VkCheckpointDataNV& entry : data)
			{
				// Top-of-pipe means the marker's pass *started*; bottom means
				// it finished. The started-but-not-finished one is the killer.
				const char* stage =
					entry.stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ? "started"
					: entry.stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT ? "finished"
					: "reached";
				RV_CORE_ERROR("  GPU checkpoint ({0}): {1}", stage,
							  entry.pCheckpointMarker
								  ? (const char*)entry.pCheckpointMarker
								  : "<null>");
			}
		}

		if (m_DeviceFaultSupported)
		{
			VkDeviceFaultCountsEXT counts{ VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT };
			if (vkGetDeviceFaultInfoEXT(m_Device, &counts, nullptr) == VK_SUCCESS)
			{
				std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
				std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);
				VkDeviceFaultInfoEXT info{ VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT };
				info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
				info.pVendorInfos = vendor.empty() ? nullptr : vendor.data();
				counts.vendorBinarySize = 0;

				if (vkGetDeviceFaultInfoEXT(m_Device, &counts, &info) == VK_SUCCESS)
				{
					RV_CORE_ERROR("  Device fault: {0}", info.description);
					for (const VkDeviceFaultAddressInfoEXT& address : addresses)
					{
						// The type is the half that says what to do with the
						// number. An invalid access is an address in *our*
						// space and the registry can name it; an instruction
						// pointer is somewhere in a shader's code and the
						// registry would only mis-attribute it to whichever
						// buffer happened to be below.
						const bool access =
							address.addressType == VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT ||
							address.addressType == VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT ||
							address.addressType == VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT;

						RV_CORE_ERROR("    address {0:#x} (+/- {1:#x}), {2}",
									  (uint64_t)address.reportedAddress,
									  (uint64_t)address.addressPrecision,
									  FaultAddressTypeToString(address.addressType));
						if (access)
							Vk::DescribeGpuAddress((uint64_t)address.reportedAddress);
					}
					for (const VkDeviceFaultVendorInfoEXT& entry : vendor)
					{
						RV_CORE_ERROR("    vendor: {0} (code {1:#x}, data {2:#x})",
									  entry.description,
									  (uint64_t)entry.vendorFaultCode,
									  (uint64_t)entry.vendorFaultData);
					}
				}
			}
		}

		if (!m_CheckpointsSupported && !m_DeviceFaultSupported)
			RV_CORE_ERROR("  No post-mortem extensions on this device; a "
						  "graphics debugger is the remaining tool");
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

	bool VulkanDevice::ReadTexture(const RHI::Ref<RHI::RHITexture>& texture,
								   std::vector<uint8_t>& out)
	{
		if (!texture)
			return false;

		VulkanTexture* image = static_cast<VulkanTexture*>(texture.get());
		const RHI::TextureDesc& desc = image->GetDesc();

		// A volume's slices are its depth; everything else counts layers. Cubes
		// are six of those, which EffectiveLayers already knows.
		const bool volume = desc.Type == RHI::TextureType::Texture3D;
		const uint32_t depth = volume ? Math::Max(desc.Depth, 1u) : 1u;
		const uint32_t layers = volume ? 1u : RHI::EffectiveLayers(desc);
		const uint64_t slice = RHI::TextureDataSize(desc.Format, desc.Width, desc.Height);
		const uint64_t size = slice * depth * layers;
		if (size == 0)
			return false;

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
			RV_CORE_ERROR("ReadTexture: could not allocate {0} bytes to read '{1}' into",
						  size, desc.DebugName);
			return false;
		}

		// **Put it back in the layout it was in.** A field is read between
		// frames and goes on being sampled afterwards; a storage image that came
		// back in TRANSFER_SRC would be a layout mismatch on every later read,
		// which is the same trap the uploads settle for.
		const VkImageLayout restore = image->GetLayout();

		ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			image->TransitionTo(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = layers;
			region.imageExtent = { desc.Width, desc.Height, depth };
			vkCmdCopyImageToBuffer(cmd, image->GetImage(),
								   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
								   buffer, 1, &region);

			image->TransitionTo(cmd, restore);
		});

		out.resize((size_t)size);
		memcpy(out.data(), allocated.pMappedData, (size_t)size);
		vmaDestroyBuffer(m_Allocator, buffer, allocation);
		return true;
	}

	void VulkanDevice::RequestTextureCapture(const RHI::Ref<RHI::RHITexture>& texture,
											 CaptureCallback callback)
	{
		// A null texture disarms rather than falling back to the swapchain --
		// see the contract on the interface. Silently photographing the whole
		// editor window because a render target was missing is the kind of
		// wrong answer that looks like a working feature.
		if (!texture)
			return;

		m_CaptureTexture = texture;
		m_TextureCapture = std::move(callback);
	}

	// The same readback CaptureSwapchainImage does, on an image the caller
	// named. Deliberately unsubtle for the same reasons: it waits, it does its
	// own one-shot submit, and it throws the staging buffer away.
	void VulkanDevice::CaptureTextureImage()
	{
		FrameContext& frame = m_Frames[m_FrameIndex];
		vkWaitForFences(m_Device, 1, &frame.InFlight, VK_TRUE, UINT64_MAX);

		VulkanTexture* texture = static_cast<VulkanTexture*>(m_CaptureTexture.get());
		const RHI::TextureDesc& desc = texture->GetDesc();

		// The contract says RGBA8, and this is where a caller that ignored it
		// is caught. Copying a float target into an RGBA8 buffer would
		// reinterpret the bits and produce a picture of noise, which is a
		// worse outcome than no picture.
		if (desc.Format != RHI::Format::R8G8B8A8_UNORM)
		{
			RV_CORE_ERROR("Texture capture: '{0}' is not R8G8B8A8_UNORM, so it cannot "
						  "be read back", desc.DebugName);
			m_CaptureTexture.reset();
			m_TextureCapture = nullptr;
			return;
		}

		const uint32_t width = desc.Width;
		const uint32_t height = desc.Height;
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
			RV_CORE_ERROR("Texture capture: could not allocate a staging buffer");
			m_CaptureTexture.reset();
			m_TextureCapture = nullptr;
			return;
		}

		// Whatever layout it was left in, and back to it afterwards: this runs
		// between the frame's work and the present, and the panel that samples
		// this texture next frame expects to find it as the graph left it.
		const VkImageLayout restore = texture->GetLayout();

		ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			texture->TransitionTo(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = { width, height, 1 };
			vkCmdCopyImageToBuffer(cmd, texture->GetImage(),
								   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
								   buffer, 1, &region);

			texture->TransitionTo(cmd, restore);
		});

		std::vector<uint8_t> rgba((size_t)size);
		memcpy(rgba.data(), allocated.pMappedData, (size_t)size);
		vmaDestroyBuffer(m_Allocator, buffer, allocation);

		// Moved out before invoking, so a callback that asks for another
		// capture arms the next frame rather than being cleared by this one.
		CaptureCallback callback;
		callback.swap(m_TextureCapture);
		m_CaptureTexture.reset();
		callback(rgba.data(), width, height);
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

		// The same bookends as the frame's list. An immediate submit carries
		// acceleration-structure builds and staging copies, which is exactly
		// the work that would fault without any render-graph pass having run.
		SetCheckpoint(cmd, "immediate/begin");
		record(cmd);
		SetCheckpoint(cmd, "immediate/end");

		VK_CHECK(vkEndCommandBuffer(cmd));

		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;

		VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submit, m_ImmediateFence));
		VK_CHECK(vkWaitForFences(m_Device, 1, &m_ImmediateFence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(m_Device, 1, &m_ImmediateFence));

		vkFreeCommandBuffers(m_Device, m_ImmediatePool, 1, &cmd);
	}

	void VulkanDevice::ExecuteImmediate(const std::function<void(RHI::RHICommandList&)>& record)
	{
		if (!record)
			return;

		VkCommandBuffer retired = VK_NULL_HANDLE;
		ImmediateSubmit([&](VkCommandBuffer buffer)
		{
			// Adopted rather than begun: ImmediateSubmit owns the begin and
			// the end, and this list only records between them.
			VulkanCommandList list(*this);
			list.Adopt(buffer);
			record(list);
			retired = buffer;
		});

		// ImmediateSubmit has waited for completion, so every bind *this*
		// recording noted is finished with -- and only those.
		//
		// It used to clear wholesale, which also forgot the frame command
		// buffer's binds whenever this ran mid-frame. That was written as an
		// acceptable imprecision in a diagnostic, and it stopped being
		// acceptable the moment the same record started deciding whether a
		// descriptor set is safe to rewrite: an asset upload between two
		// scene renders would blind the check for the rest of the frame,
		// which is exactly when the hazard happens.
		RetireBinds(retired);
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
		auto concrete = std::static_pointer_cast<VulkanPipeline>(pipeline);
		return std::make_shared<VulkanResourceSet>(*this, concrete.get(), concrete, set);
	}

	RHI::Ref<RHI::RHIResourceSet> VulkanDevice::CreateResourceSet(
		const RHI::Ref<RHI::RHIComputePipeline>& pipeline, uint32_t set)
	{
		auto concrete = std::static_pointer_cast<VulkanComputePipeline>(pipeline);
		return std::make_shared<VulkanResourceSet>(*this, concrete.get(), concrete, set);
	}

	VkDescriptorSetLayout VulkanDevice::GetBindlessTextureLayout()
	{
		if (!m_DescriptorIndexingSupported)
			return VK_NULL_HANDLE;
		if (m_BindlessLayout != VK_NULL_HANDLE)
			return m_BindlessLayout;

		// One binding, up to MaxBindlessTextures descriptors, and the three
		// flags the heap is built on: PARTIALLY_BOUND so a slot never written
		// is not an error, UPDATE_AFTER_BIND so a slot can be written while the
		// set is bound to a pending command buffer, and VARIABLE_DESCRIPTOR_COUNT
		// so a heap is allocated with exactly the capacity asked for and an
		// index past it is out of range rather than an unwritten slot. The
		// layout carries the matching pool flag or the second is refused at
		// creation.
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = m_Caps.MaxBindlessTextures;
		binding.stageFlags = VK_SHADER_STAGE_ALL;

		const VkDescriptorBindingFlags flags =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		flagsInfo.bindingCount = 1;
		flagsInfo.pBindingFlags = &flags;

		VkDescriptorSetLayoutCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		createInfo.pNext = &flagsInfo;
		createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		createInfo.bindingCount = 1;
		createInfo.pBindings = &binding;

		VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &createInfo, nullptr, &m_BindlessLayout));
		SetDebugName((uint64_t)m_BindlessLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "bindless textures");
		return m_BindlessLayout;
	}

	RHI::Ref<RHI::RHIAccelerationStructure> VulkanDevice::CreateBottomLevelAS(
		const RHI::AccelerationGeometryDesc& geometry)
	{
		if (!m_RayQuerySupported)
			return nullptr;
		auto structure = std::make_shared<VulkanAccelerationStructure>(*this, geometry);
		// A build that could not start leaves no handle behind; hand back null
		// rather than an object that traces into nothing.
		if (structure->GetHandle() == VK_NULL_HANDLE)
			return nullptr;
		return structure;
	}

	RHI::Ref<RHI::RHIAccelerationStructure> VulkanDevice::CreateTopLevelAS(uint32_t maxInstances)
	{
		if (!m_RayQuerySupported)
			return nullptr;
		return std::make_shared<VulkanAccelerationStructure>(*this, Math::Max(maxInstances, 1u));
	}

	RHI::Ref<RHI::RHIResourceSet> VulkanDevice::CreateBindlessTextureSet(uint32_t capacity)
	{
		if (!m_DescriptorIndexingSupported)
		{
			RV_CORE_INFO("[Vulkan] bindless texture heap unavailable on this device; "
						 "the bound path is used");
			return nullptr;
		}
		if (capacity == 0 || capacity > m_Caps.MaxBindlessTextures)
		{
			RV_CORE_ERROR("[Vulkan] bindless heap capacity {0} is outside 1..{1}",
						  capacity, m_Caps.MaxBindlessTextures);
			return nullptr;
		}
		return std::make_shared<VulkanBindlessSet>(*this, capacity);
	}

	RHI::Ref<RHI::RHIComputePipeline> VulkanDevice::CreateComputePipeline(
		const RHI::ComputePipelineDesc& desc)
	{
		if (!desc.Shader)
			return nullptr;

		if (!HasFlag(desc.Shader->GetReflection().Stages, RHI::ShaderStage::Compute))
		{
			RV_CORE_ERROR("'{0}' has no compute stage; no compute pipeline was created",
						  desc.Name);
			return nullptr;
		}

		auto pipeline = std::make_shared<VulkanComputePipeline>(*this, desc);

		// The constructor logs why. Returning null rather than a pipeline that
		// dispatches nothing is what lets a caller fall back to its CPU path
		// instead of quietly rendering nothing.
		return pipeline->GetHandle() ? pipeline : nullptr;
	}
}
