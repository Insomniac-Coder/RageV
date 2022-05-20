#pragma once
#include <string>
#include "glm/glm.hpp"
#include "RageV/Renderer/Texture.h"
#include "RageV/Renderer/Cameranew.h"

namespace RageV
{
	struct TagComponent {
		std::string Name;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& name) { Name = name; }
		operator std::string() { return Name; }
		operator const std::string() const { return Name; }
	};

	struct TransformComponent
	{
		glm::mat4 Transform{1.0f};

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::mat4& transform) { Transform = transform; }
		operator glm::mat4&() { return Transform; }
		operator const glm::mat4&() const { return Transform; }
	};

	struct ColorComponent
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		ColorComponent() = default;
		ColorComponent(const ColorComponent&) = default;
		ColorComponent(const glm::vec4& color) { Color = color; }
	};

	struct CameraComponent
	{
		RageV::Cameranew Camera;
		bool isPrimary = true;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
		CameraComponent(const glm::mat4& projection): Camera(projection) { }
	};

}