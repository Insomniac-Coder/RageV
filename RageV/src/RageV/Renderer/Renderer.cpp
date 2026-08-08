#include <rvpch.h>
#include "Renderer.h"
#include "Renderer2D.h"

namespace RageV
{
	namespace
	{
		RHI::RHIDevice*      s_Device = nullptr;
		RHI::RHICommandList* s_CommandList = nullptr;
	}

	void Renderer::Init(RHI::RHIDevice& device)
	{
		s_Device = &device;
		Renderer2D::Init(device);
	}

	void Renderer::Shutdown()
	{
		Renderer2D::Shutdown();
		s_CommandList = nullptr;
		s_Device = nullptr;
	}

	void Renderer::BeginFrame(RHI::RHICommandList* commandList)
	{
		s_CommandList = commandList;
	}

	void Renderer::EndFrame()
	{
		s_CommandList = nullptr;
	}

	RHI::RHICommandList* Renderer::GetCommandList()
	{
		return s_CommandList;
	}

	RHI::RHIDevice& Renderer::GetDevice()
	{
		RV_CORE_ASSERT(s_Device, "Renderer has no device");
		return *s_Device;
	}

	bool Renderer::HasDevice()
	{
		return s_Device != nullptr;
	}

	void Renderer::OnWindowResize(unsigned int width, unsigned int height)
	{
		if (s_Device)
			s_Device->OnResize(width, height);
	}
}
