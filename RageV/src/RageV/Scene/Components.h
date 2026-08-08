#pragma once
#include <string>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "RageV/Renderer/Camera.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include <glm/gtx/quaternion.hpp>

namespace RageV
{
	struct TagComponent {
		std::string Name;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& name) { Name = name; }
	};

	struct TransformComponent
	{
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& position) { Position = position; }
		

		glm::mat4 GetTransform()
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
			return	glm::translate(glm::mat4(1.0f), Position) *
					rotation *
					glm::scale(glm::mat4(1.0f), Scale);
		}
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

	struct LightComponent
	{
		RageV::Light Light;

		LightComponent() = default;
		LightComponent(const LightComponent&) = default;
		LightComponent(const RageV::Light& light) : Light(light) {}
	};

	// 3D geometry. The mesh itself is resolved from Primitive on demand rather
	// than stored, so the component stays trivially serializable and a scene
	// file does not embed vertex data.
	struct MeshComponent
	{
		PrimitiveType Primitive = PrimitiveType::Cube;
		// Null means the renderer's shared default material.
		RHI::Ref<Material> Material;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
		MeshComponent(PrimitiveType primitive) : Primitive(primitive) {}
	};

	// Plain function pointers, not std::function, and captureless lambdas.
	//
	// These used to be `[&]` lambdas capturing the enclosing component's `this`.
	// EnTT relocates components when its storage grows, so the moment a second
	// entity got a script the earlier component's lambdas pointed at freed
	// memory. Capturing nothing makes that failure unrepresentable rather than
	// merely fixed.
	//
	// The per-type OnCreate/OnUpdate/OnDestroy thunks are gone too:
	// ScriptableEntity already declares them virtual, so the extra indirection
	// bought nothing and was three more places to get the cast wrong.
	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		ScriptableEntity* (*InstantiateScript)() = nullptr;
		void (*DestroyScript)(NativeScriptComponent*) = nullptr;

		template <typename T>
		void Bind()
		{
			static_assert(std::is_base_of_v<ScriptableEntity, T>,
						  "Bound script type must derive from ScriptableEntity");

			InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestroyScript = [](NativeScriptComponent* component)
			{
				delete static_cast<T*>(component->Instance);
				component->Instance = nullptr;
			};
		}
	};

}