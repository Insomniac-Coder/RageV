#include <rvpch.h>
#include "RHIDevice.h"
#include "Platform/Vulkan/VulkanDevice.h"

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
				RV_CORE_ERROR("The OpenGL RHI backend is not implemented yet; "
							  "the legacy OpenGL renderer is still in use for that path");
				return nullptr;
			}
		}
		return nullptr;
	}
}
