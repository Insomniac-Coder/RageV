#include <rvpch.h>
#include "Shader.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "Renderer.h"

namespace RageV
{
	Shader* Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::OpenGL: return new OpenGLShader(vertexSrc, fragmentSrc);
			case RenderAPI::API::None: RV_CORE_ASSERT(false, "This Render API is not supported!"); return nullptr;
		}

		return nullptr;
	}

}