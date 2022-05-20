#pragma once
#include <string>
#include "glm/glm.hpp"
#include "RageV/Renderer/Texture.h"
#include "RageV/Renderer/Cameranew.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"

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
		RageV::SceneCamera Camera;
		bool isPrimary = true;
		bool fixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
		//CameraComponent(const glm::mat4& projection): Camera(projection) { }
	};

	struct NativeScriptComponent
	{
		RageV::ScriptableEntity* m_ScriptableEntity = nullptr;

		std::function<void()> OnInstantiateFunction;
		std::function<void(ScriptableEntity*)> OnCreateFunction;
		std::function<void(ScriptableEntity*, Timestep)> OnUpdateFunction;
		std::function<void(ScriptableEntity*)> OnDestroyFunction;
		std::function<void()> DestroyInstanceFunction;

		template <typename T>
		void Bind()
		{
			OnInstantiateFunction = [&]() { m_ScriptableEntity = new T(); };
			OnCreateFunction = [](ScriptableEntity* instance) { ((T*)instance)->OnCreate(); };
			OnUpdateFunction = [](ScriptableEntity* instance, Timestep ts) { ((T*)instance)->OnUpdate(ts); };
			OnDestroyFunction = [](ScriptableEntity* instance) { ((T*)instance)->OnDestroy(); };
			DestroyInstanceFunction = [&]() { delete ((T*)m_ScriptableEntity); };
		}
	};

}