#pragma once
#include <string>
#include "glm/glm.hpp"

namespace RageV {

	class Shader {
	public:
		Shader(std::string& vertexSrc, std::string& fragmentSrc);
		~Shader();

		void Bind() const;
		void UnBind()  const;
		void SetUniformMat4(const std::string& name, const glm::mat4& matrix);
	private:
		unsigned int m_Program;
	};

}