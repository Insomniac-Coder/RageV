#include <rvpch.h>
#include "ComponentRegistry.h"
#include "Components.h"
#include "Entity.h"
#include "RageV/Renderer/Renderer.h"

namespace RageV
{
	namespace
	{
		std::vector<ComponentDesc> s_Components;
		bool s_Initialised = false;

		const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };
		const char* kProjectionNames[] = { "Perspective", "Orthographic" };
		const char* kPrimitiveNames[] = { "Cube", "Sphere", "Plane", "Cylinder", "Quad" };

		// The type-erased component operations. Written once here rather than
		// per registration, since every component needs the same three.
		template<typename T>
		void Bind(ComponentDesc& desc)
		{
			desc.TryGet = [](Entity entity) -> void*
			{
				return entity.HasComponent<T>() ? (void*)&entity.GetComponent<T>() : nullptr;
			};
			desc.Add = [](Entity entity) -> void*
			{
				if (!entity.HasComponent<T>())
					entity.AddComponent<T>();
				return (void*)&entity.GetComponent<T>();
			};
			desc.Remove = [](Entity entity)
			{
				if (entity.HasComponent<T>())
					entity.RemoveComponent<T>();
			};
		}

		FieldHint Color()
		{
			FieldHint hint;
			hint.Kind = FieldHint::Widget::Color;
			return hint;
		}

		FieldHint Slider(float min, float max, const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.Kind = FieldHint::Widget::Slider;
			hint.Min = min;
			hint.Max = max;
			hint.Tooltip = tooltip;
			return hint;
		}

		FieldHint Drag(float speed, float min = 0.0f, float max = 0.0f, const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.Kind = FieldHint::Widget::Drag;
			hint.Speed = speed;
			hint.Min = min;
			hint.Max = max;
			hint.Tooltip = tooltip;
			return hint;
		}

		// Stored in radians, shown in degrees. Without this the inspector would
		// present 0.785 where the user means 45.
		FieldHint Degrees()
		{
			FieldHint hint;
			hint.Kind = FieldHint::Widget::Degrees;
			return hint;
		}

		template<size_t N>
		FieldHint Enum(const char* const (&names)[N], const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.EnumNames = names;
			hint.EnumCount = (int)N;
			hint.Tooltip = tooltip;
			return hint;
		}

		FieldHint OnlyWhen(FieldVisibility visible, FieldHint hint = {})
		{
			hint.VisibleIf = visible;
			return hint;
		}

		bool IsSpot(const void* component)
		{
			return static_cast<const LightComponent*>(component)->Light.Type == Light::LightType::Spot;
		}

		bool IsPositional(const void* component)
		{
			return static_cast<const LightComponent*>(component)->Light.Type != Light::LightType::Directional;
		}

		bool IsPerspective(const void* component)
		{
			return static_cast<const CameraComponent*>(component)->Camera.Projection
				 == SceneCamera::ProjectionType::Perspective;
		}

		bool IsOrthographic(const void* component)
		{
			return !IsPerspective(component);
		}
	}

	void ComponentRegistry::Init()
	{
		if (s_Initialised)
			return;
		s_Initialised = true;

		// Order is the serialization order and the inspector order, so it is
		// fixed here rather than emerging from a hash map. Changing it changes
		// every scene file's byte layout, which the round-trip test will catch.

		// --- Tag ------------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "TagComponent";
			desc.DisplayName = "Tag";
			desc.Removable = false;
			desc.AddableFromMenu = false;
			// Keyed "Tag" rather than "Name" to keep existing files readable.
			desc.Fields = { Field<&TagComponent::Name>("Tag") };
			Bind<TagComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Transform -------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "TransformComponent";
			desc.DisplayName = "Transform";
			desc.Removable = false;
			desc.AddableFromMenu = false;
			desc.Fields = {
				Field<&TransformComponent::Position>("Position"),
				Field<&TransformComponent::Rotation>("Rotation", Degrees()),
				Field<&TransformComponent::Scale>("Scale"),
			};
			Bind<TransformComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Camera ----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "CameraComponent";
			desc.DisplayName = "Camera";
			desc.Fields = {
				Field<&CameraComponent::isPrimary>("isPrimary",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "The camera the runtime renders through. Only one applies." }),
				Field<&CameraComponent::fixedAspectRatio>("FixedAspectRatio"),
				Field<&CameraComponent::Camera, &SceneCamera::Projection>("ProjectionType",
					Enum(kProjectionNames, "Perspective is 3D, orthographic is 2D.")),

				Field<&CameraComponent::Camera, &SceneCamera::PerspectiveFOV>("PerspectiveFOV",
					OnlyWhen(IsPerspective, Drag(0.5f, 1.0f, 179.0f))),
				Field<&CameraComponent::Camera, &SceneCamera::PerspectiveNear>("PerspectiveNearClip",
					OnlyWhen(IsPerspective, Drag(0.01f, 0.001f, 100.0f))),
				Field<&CameraComponent::Camera, &SceneCamera::PerspectiveFar>("PerspectiveFarClip",
					OnlyWhen(IsPerspective, Drag(1.0f, 0.1f, 100000.0f))),

				Field<&CameraComponent::Camera, &SceneCamera::OrthographicSize>("OrthographicScale",
					OnlyWhen(IsOrthographic, Drag(0.1f, 0.01f, 1000.0f))),
				Field<&CameraComponent::Camera, &SceneCamera::OrthographicNear>("OrthographicNearClip",
					OnlyWhen(IsOrthographic, Drag(0.1f))),
				Field<&CameraComponent::Camera, &SceneCamera::OrthographicFar>("OrthographicFarClip",
					OnlyWhen(IsOrthographic, Drag(0.1f))),
			};
			desc.OnChanged = [](void* component)
			{
				static_cast<CameraComponent*>(component)->Camera.Recalculate();
			};
			Bind<CameraComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Color -----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "ColorComponent";
			desc.DisplayName = "Color";
			desc.Fields = { Field<&ColorComponent::Color>("ColorValue", Color()) };
			Bind<ColorComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Mesh ------------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "MeshComponent";
			desc.DisplayName = "Mesh";
			desc.Fields = {
				Field<&MeshComponent::Primitive>("Primitive", Enum(kPrimitiveNames)),
			};

			// Materials are the one thing a field list cannot express yet: the
			// component holds a Ref, not a value. They become real assets in
			// phase 1 and this hook goes away with them.
			desc.SerializeExtra = [](YAML::Emitter& emitter, void* component)
			{
				auto* mesh = static_cast<MeshComponent*>(component);
				if (!mesh->Material)
					return;

				const auto& params = mesh->Material->GetParams();
				emitter << YAML::Key << "Material";
				emitter << YAML::BeginMap;
				emitter << YAML::Key << "BaseColor" << YAML::Value << YAML::Flow
						<< YAML::BeginSeq << params.BaseColor.r << params.BaseColor.g
						<< params.BaseColor.b << params.BaseColor.a << YAML::EndSeq;
				emitter << YAML::Key << "Emissive" << YAML::Value << YAML::Flow
						<< YAML::BeginSeq << params.EmissiveColor.r << params.EmissiveColor.g
						<< params.EmissiveColor.b << params.EmissiveColor.a << YAML::EndSeq;
				emitter << YAML::Key << "Metallic" << YAML::Value << params.Metallic;
				emitter << YAML::Key << "Roughness" << YAML::Value << params.Roughness;
				emitter << YAML::Key << "Occlusion" << YAML::Value << params.Occlusion;
				emitter << YAML::EndMap;
			};
			desc.DeserializeExtra = [](const YAML::Node& node, void* component)
			{
				auto material = node["Material"];
				if (!material || !Renderer::HasDevice())
					return;

				auto* mesh = static_cast<MeshComponent*>(component);
				mesh->Material = std::make_shared<Material>(Renderer::GetDevice(), "Material");

				auto& params = mesh->Material->GetParams();
				auto readVec4 = [](const YAML::Node& n, glm::vec4 fallback)
				{
					if (!n || !n.IsSequence() || n.size() != 4)
						return fallback;
					return glm::vec4(n[0].as<float>(), n[1].as<float>(),
									 n[2].as<float>(), n[3].as<float>());
				};

				params.BaseColor = readVec4(material["BaseColor"], params.BaseColor);
				params.EmissiveColor = readVec4(material["Emissive"], params.EmissiveColor);
				if (material["Metallic"])  params.Metallic = material["Metallic"].as<float>();
				if (material["Roughness"]) params.Roughness = material["Roughness"].as<float>();
				if (material["Occlusion"]) params.Occlusion = material["Occlusion"].as<float>();

				mesh->Material->Invalidate();
			};

			Bind<MeshComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Light -----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "LightComponent";
			desc.DisplayName = "Light";
			desc.Fields = {
				Field<&LightComponent::Light, &Light::Type>("Type", Enum(kLightTypeNames)),
				Field<&LightComponent::Light, &Light::Color>("Color", Color()),
				// Positional lights fall off with inverse square, so useful
				// values run into the hundreds; a 0-1 slider would be useless.
				Field<&LightComponent::Light, &Light::Intensity>("Intensity",
					Drag(1.0f, 0.0f, 1000.0f)),
				Field<&LightComponent::Light, &Light::Range>("Range",
					OnlyWhen(IsPositional, Drag(0.25f, 0.01f, 500.0f))),
				Field<&LightComponent::Light, &Light::InnerCone>("InnerCone",
					OnlyWhen(IsSpot, Slider(0.0f, 89.0f))),
				Field<&LightComponent::Light, &Light::OuterCone>("OuterCone",
					OnlyWhen(IsSpot, Slider(0.0f, 89.0f))),
			};
			Bind<LightComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		RV_CORE_INFO("Component registry: {0} components described", s_Components.size());
	}

	// Both accessors initialise on demand. Requiring an explicit Init call
	// would mean every tool and test had to remember it, and forgetting would
	// present as a scene that silently serializes nothing.
	const std::vector<ComponentDesc>& ComponentRegistry::All()
	{
		Init();
		return s_Components;
	}

	const ComponentDesc* ComponentRegistry::Find(const std::string& name)
	{
		Init();
		for (const auto& desc : s_Components)
		{
			if (name == desc.Name)
				return &desc;
		}
		return nullptr;
	}
}
