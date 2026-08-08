#include <rvpch.h>
#include "RHIDevice.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/OpenGL/OpenGLRHI.h"

namespace RageV::RHI
{
	Scope<RHIDevice> RHIDevice::Create(const DeviceDesc& desc)
	{
		switch (desc.Backend)
		{
			case Backend::Vulkan:
			{
				try
				{
					return std::make_unique<Vk::VulkanDevice>(desc);
				}
				catch (const std::exception& e)
				{
					RV_CORE_ERROR("Vulkan device creation failed: {0}", e.what());
					return nullptr;
				}
			}
			case Backend::OpenGL:
			{
				try
				{
					return std::make_unique<GL::OpenGLDevice>(desc);
				}
				catch (const std::exception& e)
				{
					RV_CORE_ERROR("OpenGL device creation failed: {0}", e.what());
					return nullptr;
				}
			}
		}
		return nullptr;
	}
}
