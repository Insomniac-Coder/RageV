#include <rvpch.h>
#include "Texture.h"
#include "Renderer.h"
#include "Platform/OpenGL/OpenGLTexture2D.h"

namespace RageV {
	std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::None: RV_ASSERT(false, "None API is not supported!"); return nullptr;
			case RenderAPI::API::OpenGL: return std::make_shared<OpenGLTexture2D>(path);
		}
		return nullptr;
	}

	std::shared_ptr<Texture2D> Texture2D::Create(const unsigned int& width, const unsigned int& height)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::None: RV_ASSERT(false, "None API is not supported!"); return nullptr;
			case RenderAPI::API::OpenGL: return std::make_shared<OpenGLTexture2D>(width, height);
		}
		return nullptr;
	}

}