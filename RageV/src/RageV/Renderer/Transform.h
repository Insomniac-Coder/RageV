#pragma once
#include "glm/glm.hpp"

namespace RageV
{
	struct Transform 
	{
		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
	};

	struct Transform2D
	{
		glm::vec3 position = { 0.0f, 0.0f, 0.0f };
		float rotation = 0.0f;
		glm::vec2 scale = { 1.0f, 1.0f };
	};
}