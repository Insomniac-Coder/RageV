#include <rvpch.h>
#include "ComponentRegistry.h"
#include "Components.h"
#include "Entity.h"
#include "RageV/Renderer/Renderer.h"
#include "ScriptRegistry.h"

namespace RageV
{
		// PascalCase to a sentence.
	//
	// The rule is the usual one: a word ends where a lowercase letter is
	// followed by an uppercase one, and an acronym ends where an uppercase run
	// is followed by a lowercase one. Everything after the first word is
	// lowercased unless it is an acronym, so "PerspectiveFOV" reads
	// "Perspective FOV" and "OrthographicNearClip" reads "Orthographic near
	// clip".
	std::string HumanFieldName(const char* name)
	{
		if (!name || !*name)
			return {};

		const std::string source(name);
		std::vector<std::string> words;
		std::string current;

		for (size_t i = 0; i < source.size(); i++)
		{
			const char c = source[i];
			const bool upper = c >= 'A' && c <= 'Z';

			if (upper && !current.empty())
			{
				const char previous = source[i - 1];
				const bool previousLower = (previous >= 'a' && previous <= 'z') ||
										   (previous >= '0' && previous <= '9');
				const bool nextLower = i + 1 < source.size() &&
									   source[i + 1] >= 'a' && source[i + 1] <= 'z';

				if (previousLower || nextLower)
				{
					words.push_back(current);
					current.clear();
				}
			}

			current += c;
		}

		if (!current.empty())
			words.push_back(current);

		std::string result;
		for (size_t i = 0; i < words.size(); i++)
		{
			std::string word = words[i];

			// Acronyms keep their case; ordinary words after the first do not.
			const bool acronym = word.size() > 1 &&
								 std::all_of(word.begin(), word.end(),
											 [](unsigned char c) { return std::isupper(c) != 0; });

			if (i > 0 && !acronym)
				word[0] = (char)std::tolower((unsigned char)word[0]);

			if (i > 0)
				result += ' ';
			result += word;
		}

		return result;
	}

namespace
	{
		std::vector<ComponentDesc> s_Components;
		bool s_Initialised = false;

		const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };
		const char* kProbeUpdateNames[] = { "Baked", "Realtime" };
		const char* kProjectionNames[] = { "Perspective", "Orthographic" };
		const char* kBodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
		const char* kColliderShapeNames[] = { "Box", "Sphere", "Capsule" };
		const char* kParticleFacingNames[] = { "Billboard", "Flat" };
		const char* kParticleBlendNames[] = { "Alpha", "Additive", "WeightedBlended" };
		const char* kParticleSpaceNames[] = { "World", "Local" };
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

		FieldHint Color(const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.Kind = FieldHint::Widget::Color;
			hint.Tooltip = tooltip;
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
				camera->ViewRank = Math::Clamp(camera->ViewRank, 0, 99);
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
			// The key is "ColorValue" because that is what is on disk; the label
			// is not, because "Color value" inside a component already called
			// Color says the word twice.
			desc.Fields = { Field<&ColorComponent::Color>("ColorValue", Named("Color", Color())) };
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
				auto readVec4 = [](const YAML::Node& n, Vec4 fallback)
				{
					if (!n || !n.IsSequence() || n.size() != 4)
						return fallback;
					return Vec4(n[0].as<float>(), n[1].as<float>(),
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
				Field<&ReflectionProbeComponent::NearClip>("NearClip",
					Named("Near clip", Drag(0.01f, 0.001f, 10.0f))),
				Field<&ReflectionProbeComponent::FarClip>("FarClip",
					Named("Far clip", Drag(1.0f, 0.1f, 10000.0f))),
				Field<&ReflectionProbeComponent::FacesPerFrame>("FacesPerFrame",
					OnlyWhen(IsRealtimeProbe,
						Drag(1.0f, 1.0f, 6.0f, "Six in one frame is a visible hitch; one "
											   "per frame is a sixth of the cost and a "
											   "sixth of a second of latency."))),
			};
			Bind<ReflectionProbeComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Animator ---------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "AnimatorComponent";
			desc.DisplayName = "Animator";
			desc.Fields = {
				Field<&AnimatorComponent::Clip>("Clip",
					Drag(1.0f, -1.0f, 64.0f, "Which clip of the model's own list. "
											 "-1 holds the bind pose.")),
				Field<&AnimatorComponent::Playing>("Playing"),
				Field<&AnimatorComponent::Loop>("Loop"),
				Field<&AnimatorComponent::Speed>("Speed",
					Drag(0.05f, -4.0f, 4.0f, "Negative plays the clip backwards.")),
			};
			Bind<AnimatorComponent>(desc);
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
				source->MinDistance = Math::Max(source->MinDistance, 0.01f);
				source->MaxDistance = Math::Max(source->MaxDistance, source->MinDistance + 0.01f);
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
				listener->ListenerRank = Math::Clamp(listener->ListenerRank, 0, 99);
			};
			Bind<AudioListenerComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Particle emitter ------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "ParticleEmitterComponent";
			desc.DisplayName = "Particle Emitter";
			desc.Fields = {
				Field<&ParticleEmitterComponent::Emit>("Emit",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "Continuous emission. Off with a Burst is an explosion: "
							   "one bang, nothing after." }),
				Field<&ParticleEmitterComponent::Rate>("Rate",
					Drag(1.0f, 0.0f, 10000.0f, "Particles per second.")),
				Field<&ParticleEmitterComponent::Burst>("Burst",
					Drag(1.0f, 0.0f, 16384.0f,
						 "Fired once on the first step after Play, then consumed. "
						 "Scripts write it to fire again.")),
				Field<&ParticleEmitterComponent::Lifetime>("Lifetime",
					Drag(0.05f, 0.05f, 60.0f, "Seconds each particle lives.")),
				Field<&ParticleEmitterComponent::LifetimeJitter>("LifetimeJitter",
					Slider(0.0f, 1.0f, "Random spread as a fraction of Lifetime.")),
				Field<&ParticleEmitterComponent::Direction>("Direction",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "The cone's axis, in the emitter's own frame." }),
				Field<&ParticleEmitterComponent::Spread>("Spread",
					Slider(0.0f, 180.0f, "Half-angle of the cone, degrees. 180 is a sphere.")),
				Field<&ParticleEmitterComponent::Speed>("Speed",
					Drag(0.1f, 0.0f, 1000.0f)),
				Field<&ParticleEmitterComponent::SpeedJitter>("SpeedJitter",
					Slider(0.0f, 1.0f)),
				Field<&ParticleEmitterComponent::Gravity>("Gravity",
					FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
							   "This emitter's own, not the physics world's: snow "
							   "drifts, sparks plunge, neither is a rigid body." }),
				Field<&ParticleEmitterComponent::Drag>("Drag",
					Slider(0.0f, 10.0f, "Fraction of velocity lost per second.")),
				Field<&ParticleEmitterComponent::SizeStart>("SizeStart",
					Drag(0.01f, 0.0f, 100.0f)),
				Field<&ParticleEmitterComponent::SizeEnd>("SizeEnd",
					Drag(0.01f, 0.0f, 100.0f)),
				Field<&ParticleEmitterComponent::ColorStart>("ColorStart", Color()),
				Field<&ParticleEmitterComponent::ColorEnd>("ColorEnd",
					Color("Faded to over each particle's life. An alpha of zero here "
						  "is what makes smoke thin out instead of popping off.")),
				Field<&ParticleEmitterComponent::SizeCurve>("SizeCurve",
					Named("Size curve",
						  AssetRef(AssetType::Curve,
								   "A shape for size over life, instead of the straight "
								   "line from SizeStart to SizeEnd. Leave empty to keep "
								   "the pair."))),
				Field<&ParticleEmitterComponent::ColorGradient>("ColorGradient",
					Named("Colour gradient",
						  AssetRef(AssetType::Curve,
								   "Colour over life as a gradient, instead of ColorStart "
								   "to ColorEnd. Three channels; opacity is the alpha "
								   "curve's job."))),
				Field<&ParticleEmitterComponent::AlphaCurve>("AlphaCurve",
					Named("Alpha curve",
						  AssetRef(AssetType::Curve,
								   "Opacity over life on its own, so a particle can fade "
								   "in and out. Overrides the alpha of the colour pair."))),
				Field<&ParticleEmitterComponent::Spin>("Spin",
					Drag(1.0f, 0.0f, 3600.0f, "Max degrees per second, signed at random "
											  "per particle.")),
				Field<&ParticleEmitterComponent::Facing>("Facing",
					Enum(kParticleFacingNames, "Billboard turns to the camera -- a 3D "
											   "scene's smoke and sparks. Flat lies in the "
											   "XY plane, which is what a 2D game wants.")),
				Field<&ParticleEmitterComponent::Blend>("Blend",
					Enum(kParticleBlendNames,
						 "Alpha reads as matter and is depth-sorted -- but only "
						 "within a CPU emitter; a GPU one cannot sort itself. "
						 "Additive reads as light and sums in any order. "
						 "WeightedBlended is alpha without the sort, resolved in "
						 "a second pass: correct from any angle, and what a GPU "
						 "emitter of smoke wants.")),
				Field<&ParticleEmitterComponent::Space>("Space",
					Enum(kParticleSpaceNames, "World leaves particles where they were "
											  "born; Local carries them with the emitter.")),
				Field<&ParticleEmitterComponent::Texture>("Texture",
					AssetRef(AssetType::Texture, "Optional sprite. A plain white quad "
												 "without one.")),
				Field<&ParticleEmitterComponent::MaxParticles>("MaxParticles",
					Drag(16.0f, 1.0f, 16384.0f)),
				Field<&ParticleEmitterComponent::SimulateOnGpu>("SimulateOnGpu",
					Named("Simulate on GPU",
						FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
								   "The pool lives in a storage buffer and a compute pass "
								   "integrates it; the CPU never touches a particle. Same "
								   "look, different payer." })),
			};

			desc.OnChanged = [](void* component)
			{
				auto* emitter = static_cast<ParticleEmitterComponent*>(component);
				emitter->MaxParticles = Math::Clamp(emitter->MaxParticles, 1, 16384);
				emitter->Burst = Math::Clamp(emitter->Burst, 0, 16384);
				emitter->Lifetime = Math::Max(emitter->Lifetime, 0.05f);
			};

			Bind<ParticleEmitterComponent>(desc);
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
			// No generic field list. The script name needs a dropdown of what is
			// actually registered, and the component draws that itself -- listing
			// it here as well produced two "Script" rows, one of which was a
			// plain text box that could name a script that does not exist.
			//
			// It is still serialized, below, under the same key the generic path
			// used, so scenes written before this change still load.
			desc.Fields = {};

			// The same escape hatch the managed component uses, and for the same
			// reason: a list of name/value pairs whose shape depends on the
			// script is not something a static field list can express.
			desc.SerializeExtra = [](YAML::Emitter& out, void* component)
			{
				auto* script = static_cast<NativeScriptComponent*>(component);

				out << YAML::Key << "Script" << YAML::Value << script->ScriptName;

				// Omitted when unset, so a scene written before labels existed
				// still round-trips to the same bytes.
				if (!script->Label.empty())
					out << YAML::Key << "Name" << YAML::Value << script->Label;

				if (script->Fields.Empty())
					return;

				out << YAML::Key << "Fields" << YAML::Value << YAML::BeginMap;
				for (const auto& entry : script->Fields.Values)
					out << YAML::Key << entry.Name << YAML::Value << entry.Value;
				out << YAML::EndMap;
			};

			desc.DeserializeExtra = [](const YAML::Node& node, void* component)
			{
				auto* script = static_cast<NativeScriptComponent*>(component);
				script->Fields.Values.clear();

				const YAML::Node& constNode = node;
				if (const YAML::Node name = constNode["Script"])
					script->ScriptName = name.as<std::string>(std::string{});
				if (const YAML::Node label = constNode["Name"])
					script->Label = label.as<std::string>(std::string{});

				const YAML::Node fields = constNode["Fields"];
				if (!fields || !fields.IsMap())
					return;

				for (const auto& entry : fields)
				{
					script->Fields.Values.push_back({ entry.first.as<std::string>(),
													  entry.second.as<std::string>() });
				}
			};

			Bind<NativeScriptComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Managed (C#) script ---------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "ManagedScriptComponent";
			desc.DisplayName = "Script";

			// Not in Add Component. There is one Script component as far as
			// anyone using the editor is concerned, and its Language row is what
			// converts between the two -- offering both here meant an entity
			// could end up with a C++ script and a C# script at once, which the
			// inspector then drew as two identical-looking components.
			//
			// Still a distinct component underneath: see the comment on
			// ManagedScriptComponent for why they are not one type.
			desc.AddableFromMenu = false;

			// The type name is the durable reference, the way the registered
			// name is for a native script. Presented as a dropdown of what the
			// loaded assemblies actually contain, so a typo cannot produce an
			// entity that silently does nothing.
			// Drawn by the component itself, for the same reason the native one is.
			desc.Fields = {};

			// The field overrides need the escape hatch: they are a list of
			// name/value pairs whose shape depends on a C# type the engine
			// cannot see at compile time, which is exactly what a static field
			// list cannot express.
			//
			// Written as a map rather than a sequence of pairs, so a scene file
			// reads `Speed: 2.5` and a person editing one by hand is not
			// counting list entries.
			desc.SerializeExtra = [](YAML::Emitter& out, void* component)
			{
				auto* script = static_cast<ManagedScriptComponent*>(component);

				out << YAML::Key << "Script" << YAML::Value << script->ScriptName;

				if (!script->Label.empty())
					out << YAML::Key << "Name" << YAML::Value << script->Label;

				if (script->Fields.Empty())
					return;

				out << YAML::Key << "Fields" << YAML::Value << YAML::BeginMap;
				for (const auto& entry : script->Fields.Values)
					out << YAML::Key << entry.Name << YAML::Value << entry.Value;
				out << YAML::EndMap;
			};

			desc.DeserializeExtra = [](const YAML::Node& node, void* component)
			{
				auto* script = static_cast<ManagedScriptComponent*>(component);
				script->Fields.Values.clear();

				const YAML::Node& constNode = node;
				if (const YAML::Node name = constNode["Script"])
					script->ScriptName = name.as<std::string>(std::string{});
				if (const YAML::Node label = constNode["Name"])
					script->Label = label.as<std::string>(std::string{});

				const YAML::Node fields = constNode["Fields"];
				if (!fields || !fields.IsMap())
					return;

				for (const auto& entry : fields)
				{
					script->Fields.Values.push_back({ entry.first.as<std::string>(),
													  entry.second.as<std::string>() });
				}
			};

			Bind<ManagedScriptComponent>(desc);
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
