#include <rvpch.h>
#include "OpenGLShader.h"
#include "glad/glad.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace RageV
{
	OpenGLShader::~OpenGLShader()
	{
		glDeleteProgram(m_Program);
	}

	OpenGLShader::OpenGLShader(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		// Create an empty vertex shader handle
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);

		// Send the vertex shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		const GLchar* source = (const GLchar*)vertexSrc.c_str();
		glShaderSource(vertexShader, 1, &source, 0);

		// Compile the vertex shader
		glCompileShader(vertexShader);

		GLint isCompiled = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(vertexShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(vertexShader);

			RV_CORE_ERROR(infoLog[0]);
			RV_ASSERT(false, "Vertex shader compliation failed!");
			return;
		}

		// Create an empty fragment shader handle
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

		// Send the fragment shader source code to GL
		// Note that std::string's .c_str is NULL character terminated.
		source = (const GLchar*)fragmentSrc.c_str();
		glShaderSource(fragmentShader, 1, &source, 0);

		// Compile the fragment shader
		glCompileShader(fragmentShader);

		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &isCompiled);
		if (isCompiled == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetShaderInfoLog(fragmentShader, maxLength, &maxLength, &infoLog[0]);

			// We don't need the shader anymore.
			glDeleteShader(fragmentShader);
			// Either of them. Don't leak shaders.
			glDeleteShader(vertexShader);

			RV_CORE_ERROR(infoLog[0]);
			RV_ASSERT(false, "Fragment shader compliation failed!");
			return;
		}

		// Vertex and fragment shaders are successfully compiled.
		// Now time to link them together into a program.
		// Get a program object.
		m_Program = glCreateProgram();

		// Attach our shaders to our program
		glAttachShader(m_Program, vertexShader);
		glAttachShader(m_Program, fragmentShader);

		// Link our program
		glLinkProgram(m_Program);

		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(m_Program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(m_Program, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(m_Program, maxLength, &maxLength, &infoLog[0]);

			// We don't need the program anymore.
			glDeleteProgram(m_Program);
			// Don't leak shaders either.
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			RV_CORE_ERROR(infoLog[0]);
			RV_ASSERT(false, "Shader program linking failed!");
			return;
		}

		// Always detach shaders after a successful link.
		glDetachShader(m_Program, vertexShader);
		glDetachShader(m_Program, fragmentShader);
	}

	void OpenGLShader::Bind() const
	{
		glUseProgram(m_Program);
	}

	void OpenGLShader::UnBind() const
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