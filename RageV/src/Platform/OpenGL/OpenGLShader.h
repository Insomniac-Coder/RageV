#pragma once
#include "RageV/Renderer/Shader.h"
#include <unordered_map>

typedef unsigned int GLenum;
namespace RageV
{

	class OpenGLShader : public Shader {
	public:
		OpenGLShader(const std::string& shaderPath);
		OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
		virtual ~OpenGLShader();
		
		virtual void SetMat4(const std::string& name, const glm::mat4& mat4) override;
		virtual void SetMat3(const std::string& name, const glm::mat3& mat3) override;
		virtual void SetFloat4(const std::string& name, const glm::vec4& float4) override;
		virtual void SetFloat3(const std::string& name, const glm::vec3& float3) override;
		virtual void SetFloat2(const std::string& name, const glm::vec2& float2) override;
		virtual void SetFloat1(const std::string& name, const float& float1) override;
		virtual void SetInt4(const std::string& name, const int& int1, const int& int2, const int& int3, const int& int4) override;
		virtual void SetInt3(const std::string& name, const int& int1, const int& int2, const int& int3) override;
		virtual void SetInt2(const std::string& name, const int& int1, const int& int2) override;
		virtual void SetInt1(const std::string& name, const int& int1) override;

		//Shader specific functions
		virtual void Bind() const override;
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
		//Helper functions
		void Compile(std::unordered_map<GLenum, std::string>& sources);
		std::unordered_map<GLenum, std::string> ReadFileAndSeparate(const std::string& shaderPath);
	private:
		int GetUniformLocation(const std::string& uniformName);
		unsigned int m_Program;
		std::unordered_map<std::string, int> m_ShaderCache;
	};

}