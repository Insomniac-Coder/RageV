#pragma once
#include "RageV/Renderer/GraphicsContext.h"

struct GLFWwindow;
namespace RageV
{

	class VulkanContext : public GraphicsContext
	{
	public:
		VulkanContext(GLFWwindow* windowHandle);
		virtual void SwapBuffers() override;
		virtual void Init() override;
		const GraphicsInfo& GetGraphicsInfo() const override;

	private:
		GLFWwindow* m_WindowHandle;

	};

}