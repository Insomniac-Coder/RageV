#pragma once
#include <string>
#include "glm/glm.hpp"

namespace RageV {

	class Shader {
	public:
		virtual ~Shader() {};

		virtual void Bind() const = 0;
		static Shader* Create(const std::string& shaderPath);
		static Shader* Create(const std::string& vertexSrc, const std::string& fragmentSrc);
	};

}