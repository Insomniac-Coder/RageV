#include <rvpch.h>
#include "Shader.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "Renderer.h"

namespace RageV
{
	std::shared_ptr<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::OpenGL: return std::make_shared<OpenGLShader>(name, vertexSrc, fragmentSrc);
			case RenderAPI::API::None: RV_CORE_ASSERT(false, "This Render API is not supported!"); return nullptr;
		}

		return nullptr;
	}

	std::shared_ptr<Shader> Shader::Create(const std::string& shaderPath)
	{
		switch (Renderer::GetAPI())
		{
			case RenderAPI::API::OpenGL: return std::make_shared<OpenGLShader>(shaderPath);
			case RenderAPI::API::None: RV_CORE_ASSERT(false, "This Render API is not supported!"); return nullptr;
		}

		return nullptr;
	}

	void ShaderManager::LoadShader(const std::string& shaderPath)
	{
		std::shared_ptr<Shader> shader = Shader::Create(shaderPath);

		int lastSlashPos = shaderPath.find_last_of("/\\");
		lastSlashPos = lastSlashPos == std::string::npos ? 0 : lastSlashPos + 1;

		int lastDot = shaderPath.rfind('.');
		lastDot = lastDot == std::string::npos ? shaderPath.size() : lastDot;

		m_Shaders[shaderPath.substr(lastSlashPos, lastDot - lastSlashPos)] = shader;
	}

	void ShaderManager::LoadShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		std::shared_ptr<Shader> shader = Shader::Create( name, vertexSrc, fragmentSrc);

		m_Shaders[name] = shader;
	}

	void ShaderManager::AddShader(const std::string& name, const std::shared_ptr<Shader>& shader)
	{
		m_Shaders[name] = shader;
	}

	std::shared_ptr<Shader> ShaderManager::GetShader(const std::string& shaderName)
	{
		auto it = m_Shaders.find(shaderName);
		RV_CORE_ASSERT(it != m_Shaders.end(), "Couldn't find the requested shader");
		return it->second;
	}


}