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
		virtual void SetMat4(const std::string& name, const glm::mat4& mat4) = 0;
		virtual void SetMat3(const std::string& name, const glm::mat3& mat3) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& float4) = 0;
		virtual void SetFloat3(const std::string& name, const glm::vec3& float3) = 0;
		virtual void SetFloat2(const std::string& name, const glm::vec2& float2) = 0;
		virtual void SetFloat1(const std::string& name, const float& float1) = 0;
		virtual void SetIntArray(const std::string& name, const int* intarray, const unsigned int& count) = 0;
		virtual void SetInt4(const std::string& name, const int& int1, const int& int2, const int& int3, const int& int4) = 0;
		virtual void SetInt3(const std::string& name, const int& int1, const int& int2, const int& int3) = 0;
		virtual void SetInt2(const std::string& name, const int& int1, const int& int2) = 0;
		virtual void SetInt1(const std::string& name, const int& int1) = 0;
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