#include <rvpch.h>
#include "ComponentRegistry.h"
#include "Components.h"
#include "Entity.h"
#include "RageV/Renderer/Renderer.h"
#include "ScriptRegistry.h"

namespace RageV
{
	namespace
	{
		std::vector<ComponentDesc> s_Components;
		bool s_Initialised = false;

		const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };
		const char* kProbeUpdateNames[] = { "Baked", "Realtime" };
		const char* kProjectionNames[] = { "Perspective", "Orthographic" };
		const char* kBodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
		const char* kColliderShapeNames[] = { "Box", "Sphere", "Capsule" };
		const char* kAudioBusNames[] = { "Master", "Music", "SFX", "UI" };

		FieldHint AssetRef(AssetType accepts, const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.Accepts = accepts;
			hint.Tooltip = tooltip;
			return hint;
		}

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

		bool IsRealtimeProbe(const void* component)
		{
			return static_cast<const ReflectionProbeComponent*>(component)->Update
				 == ProbeUpdate::Realtime;
		}

		bool IsDynamicBody(const void* component)
		{
			return static_cast<const RigidBodyComponent*>(component)->Type == BodyType::Dynamic;
		}

		bool IsBoxCollider(const void* component)
		{
			return static_cast<const ColliderComponent*>(component)->Shape == ColliderShape::Box;
		}

		bool IsCapsuleCollider(const void* component)
		{
			return static_cast<const ColliderComponent*>(component)->Shape == ColliderShape::Capsule;
		}

		// Sphere and capsule both have one.
		bool IsRoundCollider(const void* component)
		{
			return static_cast<const ColliderComponent*>(component)->Shape != ColliderShape::Box;
		}

		bool IsSpatialSource(const void* component)
		{
			return static_cast<const AudioSourceComponent*>(component)->Spatial;
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
				Field<&CameraComponent::ViewRank>("ViewRank",
					Drag(0.25f, 0, 99, "Which camera the game view uses: lowest rank wins, "
									   "0 highest priority through 99 lowest. Ties break on "
									   "entity id, so the choice is stable between runs.")),
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
			// Version 4 and earlier stored a boolean. A camera that was primary
			// becomes rank 0 and one that was not becomes 50, which preserves
			// which camera wins without inventing an ordering among the rest.
			desc.DeserializeExtra = [](const YAML::Node& node, void* component)
			{
				if (node["ViewRank"])
					return;

				if (const YAML::Node primary = node["isPrimary"])
					static_cast<CameraComponent*>(component)->ViewRank = primary.as<bool>() ? 0 : 50;
			};

			desc.OnChanged = [](void* component)
			{
				auto* camera = static_cast<CameraComponent*>(component);
				camera->ViewRank = glm::clamp(camera->ViewRank, 0, 99);
				camera->Camera.Recalculate();
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
				Field<&MeshComponent::Mesh>("Mesh",
					AssetRef(AssetType::Mesh, "A built-in primitive or an imported model.")),
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
				auto* mesh = static_cast<MeshComponent*>(component);

				// Version 3 and earlier stored a primitive name instead of a
				// handle. Primitives are virtual assets now, so the name maps
				// straight onto one.
				if (const YAML::Node primitive = node["Primitive"]; primitive && !node["Mesh"])
				{
					PrimitiveType type = PrimitiveType::Cube;
					if (PrimitiveTypeFromName(primitive.as<std::string>(), type))
						mesh->Mesh = PrimitiveHandle(type);
				}

				auto material = node["Material"];
				if (!material || !Renderer::HasDevice())
					return;

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
				// Tooltip only; there is no widget choice to make for a checkbox.
				Field<&LightComponent::Light, &Light::CastShadows>("CastShadows",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "Whether this light casts shadows. Every type can, and each "
							   "costs scene renders: a directional light four, a spot one, "
							   "a point six. Only the first directional light to ask gets "
							   "cascades; four spot and four point lights cast at once." }),
			};
			Bind<LightComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Reflection probe -------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "ReflectionProbeComponent";
			desc.DisplayName = "Reflection Probe";
			desc.Fields = {
				Field<&ReflectionProbeComponent::Update>("Update",
					Enum(kProbeUpdateNames, "Baked captures once and costs nothing after "
											"that. Realtime re-captures continuously and "
											"shows moving objects in reflections.")),
				Field<&ReflectionProbeComponent::Resolution>("Resolution",
					Drag(8.0f, 16.0f, 1024.0f, "Per face. Reflections are seen through "
											   "rough or curved surfaces, so this can be "
											   "far lower than it feels like it should.")),
				Field<&ReflectionProbeComponent::Influence>("Influence",
					Drag(0.5f, 0.5f, 500.0f, "How far from the probe its capture is still "
											 "a reasonable answer. Beyond this the sky is "
											 "the better lie.")),
				Field<&ReflectionProbeComponent::NearClip>("NearClip", Drag(0.01f, 0.001f, 10.0f)),
				Field<&ReflectionProbeComponent::FarClip>("FarClip", Drag(1.0f, 0.1f, 10000.0f)),
				Field<&ReflectionProbeComponent::FacesPerFrame>("FacesPerFrame",
					OnlyWhen(IsRealtimeProbe,
						Drag(1.0f, 1.0f, 6.0f, "Six in one frame is a visible hitch; one "
											   "per frame is a sixth of the cost and a "
											   "sixth of a second of latency."))),
			};
			Bind<ReflectionProbeComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Rigid body ------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "RigidBodyComponent";
			desc.DisplayName = "Rigid Body";
			desc.Fields = {
				Field<&RigidBodyComponent::Type>("Type",
					Enum(kBodyTypeNames, "Static never moves and is cheapest. Kinematic is "
										 "moved by code and pushes dynamic bodies. Dynamic is "
										 "moved by the solver.")),
				Field<&RigidBodyComponent::Mass>("Mass",
					OnlyWhen(IsDynamicBody, Drag(0.1f, 0.001f, 10000.0f))),
				Field<&RigidBodyComponent::Friction>("Friction", Slider(0.0f, 2.0f)),
				Field<&RigidBodyComponent::Restitution>("Restitution",
					Slider(0.0f, 1.0f, "Bounciness. Above about 0.95 a stack gains energy "
										"and never settles.")),
				Field<&RigidBodyComponent::LinearDamping>("LinearDamping",
					OnlyWhen(IsDynamicBody, Slider(0.0f, 1.0f))),
				Field<&RigidBodyComponent::AngularDamping>("AngularDamping",
					OnlyWhen(IsDynamicBody, Slider(0.0f, 1.0f))),
				Field<&RigidBodyComponent::GravityFactor>("GravityFactor",
					OnlyWhen(IsDynamicBody, Drag(0.05f, -10.0f, 10.0f,
						"Scales gravity for this body alone: 0 floats, negative rises."))),
				Field<&RigidBodyComponent::FreezeRotation>("FreezeRotation",
					OnlyWhen(IsDynamicBody, FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f,
						nullptr, 0, "What a character wants: a capsule that tips over stops "
									"being a character." })),
			};
			Bind<RigidBodyComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Collider --------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "ColliderComponent";
			desc.DisplayName = "Collider";
			desc.Fields = {
				Field<&ColliderComponent::Shape>("Shape", Enum(kColliderShapeNames)),
				Field<&ColliderComponent::HalfExtents>("HalfExtents", OnlyWhen(IsBoxCollider)),
				Field<&ColliderComponent::Radius>("Radius",
					OnlyWhen(IsRoundCollider, Drag(0.05f, 0.01f, 1000.0f))),
				Field<&ColliderComponent::Height>("Height",
					OnlyWhen(IsCapsuleCollider, Drag(0.05f, 0.01f, 1000.0f))),
				Field<&ColliderComponent::Offset>("Offset"),
				Field<&ColliderComponent::IsTrigger>("IsTrigger",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "Reports overlaps without resisting them." }),
			};
			Bind<ColliderComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Audio source ----------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "AudioSourceComponent";
			desc.DisplayName = "Audio Source";
			desc.Fields = {
				Field<&AudioSourceComponent::Clip>("Clip",
					AssetRef(AssetType::Audio, "A .wav, .mp3 or .flac in the assets folder.")),
				Field<&AudioSourceComponent::Bus>("Bus",
					Enum(kAudioBusNames, "Which mix this sound belongs to, so music and "
										 "effects can be turned down separately.")),
				Field<&AudioSourceComponent::Volume>("Volume", Slider(0.0f, 2.0f)),
				Field<&AudioSourceComponent::Pitch>("Pitch",
					Drag(0.01f, 0.01f, 4.0f, "Also changes speed: 2 is an octave up and "
											 "half as long.")),
				Field<&AudioSourceComponent::Loop>("Loop"),
				Field<&AudioSourceComponent::PlayOnAwake>("PlayOnAwake",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "Starts when the scene starts playing." }),
				Field<&AudioSourceComponent::Stream>("Stream",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "Decode while playing rather than up front. For music." }),
				Field<&AudioSourceComponent::Spatial>("Spatial",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "Positioned in the world, so it fades with distance. Off for "
							   "music and UI." }),
				Field<&AudioSourceComponent::MinDistance>("MinDistance",
					OnlyWhen(IsSpatialSource, Drag(0.1f, 0.01f, 1000.0f,
						"Full volume within this distance."))),
				Field<&AudioSourceComponent::MaxDistance>("MaxDistance",
					OnlyWhen(IsSpatialSource, Drag(0.5f, 0.02f, 10000.0f))),
			};

			desc.OnChanged = [](void* component)
			{
				auto* source = static_cast<AudioSourceComponent*>(component);
				// A max below the min inverts the falloff curve, which is
				// audible as a sound that gets louder as it recedes.
				source->MinDistance = glm::max(source->MinDistance, 0.01f);
				source->MaxDistance = glm::max(source->MaxDistance, source->MinDistance + 0.01f);
			};

			Bind<AudioSourceComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Audio listener --------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "AudioListenerComponent";
			desc.DisplayName = "Audio Listener";
			desc.Fields = {
				Field<&AudioListenerComponent::ListenerRank>("ListenerRank",
					Drag(0.25f, 0, 99, "Which listener the scene is heard from: lowest rank "
									   "wins. With no listener at all the primary camera is "
									   "used.")),
			};
			desc.OnChanged = [](void* component)
			{
				auto* listener = static_cast<AudioListenerComponent*>(component);
				listener->ListenerRank = glm::clamp(listener->ListenerRank, 0, 99);
			};
			Bind<AudioListenerComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Native script ---------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "NativeScriptComponent";
			desc.DisplayName = "Script";
			// The registered name is the durable reference, the way an asset
			// handle is for assets. Presented as a dropdown of what is actually
			// registered rather than a free-text field, so a typo cannot
			// produce an entity that silently does nothing.
			desc.Fields = { Field<&NativeScriptComponent::ScriptName>("Script") };
			Bind<NativeScriptComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Prefab ----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "PrefabComponent";
			desc.DisplayName = "Prefab Instance";
			// Not offered in the Add menu: it is created by instantiating a
			// prefab, and adding it by hand would claim a provenance the
			// entity does not have.
			desc.AddableFromMenu = false;
			desc.Fields = {
				Field<&PrefabComponent::Source>("Source",
					AssetRef(AssetType::Prefab, "The prefab this tree was stamped from. "
											    "Editing a prefab does not yet update "
											    "instances already placed.")),
			};
			Bind<PrefabComponent>(desc);
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
