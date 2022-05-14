#include <rvpch.h>
#include "OpenGLShader.h"
#include "glad/glad.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include <fstream>

namespace RageV
{
	static GLenum StringtoGLenum(const std::string& code)
	{
		if (code == "vertex")
			return GL_VERTEX_SHADER;
		if (code == "pixel" || code == "fragment")
			return GL_FRAGMENT_SHADER;
		RV_CORE_ERROR("Invalid code provided!");
		return 0;
	}

	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_Program);
	}

	OpenGLShader::OpenGLShader(const std::string& shaderPath)
	{
		std::unordered_map<GLenum, std::string> sources = ReadFileAndSeparate(shaderPath);
		Compile(sources);
	}

	OpenGLShader::OpenGLShader(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		std::unordered_map<GLenum, std::string> sources;
		sources[GL_VERTEX_SHADER] = vertexSrc;
		sources[GL_FRAGMENT_SHADER] = fragmentSrc;
		Compile(sources);
	}

	std::unordered_map<GLenum, std::string> OpenGLShader::ReadFileAndSeparate(const std::string& shaderPath)
	{
		std::ifstream file(shaderPath);
		std::string line;
		std::string keyword = "#type ";
		std::string type = "";
		std::string content = "";
		std::unordered_map<GLenum, std::string> sources;
		while (std::getline(file, line))
		{
			if (int pos = line.find(keyword) != std::string::npos)
			{
				if (type != "")
					sources[StringtoGLenum(type)] = content;
				type = line.substr(keyword.size(), line.find_first_of("\r\n") - keyword.size());
				content = "";
			}
			else
			{
				content += (line + "\n");
			}
		}
		sources[StringtoGLenum(type)] = content;

		return sources;
	}

	void OpenGLShader::Compile(std::unordered_map<GLenum, std::string>& sources)
	{
		unsigned int t_Program = glCreateProgram();
		std::vector<GLuint> shaderIds;

		for (auto it = sources.begin(); it != sources.end(); it++)
		{
			GLuint shader = glCreateShader(it->first);
			const GLchar* source = (const GLchar*)it->second.c_str();

			glShaderSource(shader, 1, &source, 0);
			glCompileShader(shader);

			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE)
			{
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

				std::vector<GLchar> infoLog(maxLength);
				glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

				glDeleteShader(shader);

				RV_CORE_ERROR(infoLog[0]);
				RV_ASSERT(false, "Shader compliation failed!");
				break;
			}
			shaderIds.push_back(shader);
			glAttachShader(t_Program, shader);
		}

		glLinkProgram(t_Program);
		GLint isLinked = 0;
		glGetProgramiv(t_Program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(t_Program, GL_INFO_LOG_LENGTH, &maxLength);
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(t_Program, maxLength, &maxLength, &infoLog[0]);
			glDeleteProgram(t_Program);
			
			for(auto item: shaderIds)
				glDeleteShader(item);

			RV_CORE_ERROR(std::string(infoLog.begin(), infoLog.end()));
			RV_ASSERT(false, "Shader program linking failed!");
			return;
		}

		for(auto item : shaderIds)
			glDetachShader(t_Program, item);
		m_Program = t_Program; //hand over the value after the entire process is successful
	}

	void OpenGLShader::Bind() const
	{
		glUseProgram(m_Program);
	}

	void OpenGLShader::SetUniform(const std::string& name, const glm::mat4& matrix4)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniformMatrix4fv(loc, 1, false, glm::value_ptr(matrix4));
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const glm::mat3& matrix3)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniformMatrix3fv(loc, 1, false, glm::value_ptr(matrix3));
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const glm::vec4& float4)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform4f(loc, float4.x, float4.y, float4.z, float4.w);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const glm::vec3& float3)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform3f(loc, float3.x, float3.y, float3.z);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const glm::vec2& float2)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform2f(loc, float2.x, float2.y);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const float& float1)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform1f(loc, float1);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const int& int1, const int& int2, const int& int3, const int& int4)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform4i(loc, int1, int2, int3, int4);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const int& int1, const int& int2, const int& int3)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform3i(loc, int1, int2, int3);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const int& int1, const int& int2)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform2i(loc, int1, int2);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}
	void OpenGLShader::SetUniform(const std::string& name, const int& int1)
	{
		glUseProgram(m_Program);
		int loc = GetUniformLocation(name);
		if (loc != -1)
			glUniform1i(loc, int1);
		else
			RV_CORE_ERROR("Uniform {0} not found!", name);
	}

	int OpenGLShader::GetUniformLocation(const std::string& uniformName)
	{
		auto it = m_ShaderCache.find(uniformName);
		if (it != m_ShaderCache.end())
			return it->second;
		else
		{
			int loc = glGetUniformLocation(m_Program, uniformName.c_str());
			if (loc != -1)
				m_ShaderCache[uniformName] = loc;
			return loc;

		}
		return -1;
	}

}