#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include "glm/glm.hpp"

namespace RageV {

	class Shader {
	public:
		virtual ~Shader() {};

		virtual void Bind() const = 0;
		static std::shared_ptr<Shader> Create(const std::string& shaderPath);
		static std::shared_ptr<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
	};

	class ShaderManager {
	public:
		void LoadShader(const std::string& shaderPath);
		void LoadShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);

		void AddShader(const std::string& name, const std::shared_ptr<Shader>& shader);

		std::shared_ptr<Shader> GetShader(const std::string& shaderName);
	private:
		std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
	};

}