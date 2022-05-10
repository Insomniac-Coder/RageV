#pragma once
#include <string>

namespace RageV {

	class Shader {
	public:
		Shader(std::string& vertexSrc, std::string& fragmentSrc);
		~Shader();

		void Bind() const;
		void UnBind()  const;

	private:
		unsigned int m_Program;
	};

}