#pragma once
#include "RageV/Renderer/Shader.h"
#include <unordered_map>

namespace RageV
{

	class OpenGLShader : public Shader {
	public:
		OpenGLShader(const std::string& vertexSrc, const std::string& fragmentSrc);
		virtual ~OpenGLShader();
		virtual void Bind() const override;
		virtual void UnBind()  const override;
		void SetUniform(const std::string& name, const glm::mat4& matrix4);
		void SetUniform(const std::string& name, const glm::mat3& matrix3);
		void SetUniform(const std::string& name, const glm::vec4& float4);
		void SetUniform(const std::string& name, const glm::vec3& float3);
		void SetUniform(const std::string& name, const glm::vec2& float2);
		void SetUniform(const std::string& name, const float& float1);
		void SetUniform(const std::string& name, const int& int1, const int& int2, const int& int3, const int& int4);
		void SetUniform(const std::string& name, const int& int1, const int& int2, const int& int3);
		void SetUniform(const std::string& name, const int& int1, const int& int2);
		void SetUniform(const std::string& name, const int& int1);
	private:
		int GetUniformLocation(const std::string& uniformName);
		unsigned int m_Program;
		std::unordered_map<std::string, int> m_ShaderCache;
	};

}