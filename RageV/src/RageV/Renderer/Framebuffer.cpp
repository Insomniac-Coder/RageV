#include <rvpch.h>
#include "Framebuffer.h"
#include "RageV/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLFrameBuffer.h"

namespace RageV {
	std::shared_ptr<FrameBuffer> FrameBuffer::Create(const FrameBufferData& framebufferdata)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::None:
			{
				RV_ASSERT(false, "No API was chosen!");
				return nullptr;
			}
			case RenderAPI::API::OpenGL:
			{
				return std::make_shared<OpenGLFrameBuffer>(framebufferdata);
			}
		}

		return nullptr;
	}
}