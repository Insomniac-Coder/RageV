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
		const char* kCanvasScaleNames[] = { "ConstantPixels", "ScaleWithScreen" };
		const char* kTextAlignNames[] = { "Left", "Center", "Right" };
		const char* kBillboardNames[] = { "None", "Full", "Upright" };
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

		// A tooltip and nothing else, for widgets that need no range.
		FieldHint Tip(const char* tooltip, FieldHint hint = {})
		{
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

		// One per override switch. Free functions rather than one parameterised
		// helper because FieldVisibility is a plain function pointer -- which
		// is deliberate, since a FieldDesc is a static description and must not
		// own state.
		bool OverridesBaseColor(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideBaseColor;
		}

		bool OverridesEmissive(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideEmissive;
		}

		bool OverridesMetallic(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideMetallic;
		}

		bool OverridesRoughness(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideRoughness;
		}

		bool OverridesOcclusion(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideOcclusion;
		}

		bool OverridesNormalScale(const void* component)
		{
			return static_cast<const MeshComponent*>(component)->OverrideNormalScale;
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

		// --- Mesh ------------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "MeshComponent";
			desc.DisplayName = "Mesh";
			desc.Fields = {
				Field<&MeshComponent::Mesh>("Mesh",
					AssetRef(AssetType::Mesh, "A built-in primitive or an imported model.")),
				Field<&MeshComponent::Material>("Material",
					AssetRef(AssetType::Material,
							 "Shared. The texture maps live here; leave it empty for the "
							 "renderer's default.")),

				// The scalars, each behind its own switch. Ordinary reflected
				// fields now -- the hook that used to write this by hand is
				// gone, and with it the reason texture maps could not be saved.
				//
				// A value row appears only when its switch is on. Visibility is
				// ignored by the serializer on purpose, so turning an override
				// off keeps the value it had rather than dropping it: switch it
				// back on and the colour is still there.
				// The switch carries the property's name and the value row is
				// just "Value". "Override Base Colour" does not fit the label
				// column at *any* UI scale -- it rendered as "Override Bas" --
				// and a value row only ever appears directly beneath its own
				// ticked switch, so there is nothing for the short name to be
				// confused with.
				Field<&MeshComponent::OverrideBaseColor>("OverrideBaseColor",
					Named("Base Colour", FieldHint{ .Tooltip =
						"Replace the material's base colour for this entity only." })),
				Field<&MeshComponent::BaseColor>("BaseColor",
					OnlyWhen(OverridesBaseColor, Named("Value", Color()))),

				Field<&MeshComponent::OverrideEmissive>("OverrideEmissive",
					Named("Emissive", FieldHint{ .Tooltip =
						"Light the surface gives off. It does not light anything else -- "
						"there is no emissive global illumination -- but it does feed bloom." })),
				Field<&MeshComponent::EmissiveColor>("EmissiveColor",
					OnlyWhen(OverridesEmissive, Named("Value", Color()))),

				Field<&MeshComponent::OverrideMetallic>("OverrideMetallic",
					Named("Metallic", FieldHint{ .Tooltip =
						"Metal or not. Real materials are one or the other; the values in "
						"between are for a surface that is partly covered, like dusty chrome." })),
				Field<&MeshComponent::Metallic>("Metallic",
					OnlyWhen(OverridesMetallic, Named("Value", Slider(0.0f, 1.0f)))),

				Field<&MeshComponent::OverrideRoughness>("OverrideRoughness",
					Named("Roughness", FieldHint{ .Tooltip =
						"How scattered the reflection is. 0 is a mirror, 1 is chalk." })),
				Field<&MeshComponent::Roughness>("Roughness",
					OnlyWhen(OverridesRoughness, Named("Value", Slider(0.0f, 1.0f)))),

				Field<&MeshComponent::OverrideOcclusion>("OverrideOcclusion",
					Named("Occlusion", FieldHint{ .Tooltip =
						"How much ambient light reaches the surface. Lower is more shadowed." })),
				Field<&MeshComponent::Occlusion>("Occlusion",
					OnlyWhen(OverridesOcclusion, Named("Value", Slider(0.0f, 1.0f)))),

				// Up to 4, not 1: the useful direction for this one is usually
				// *more*. A tiling material authored for a wall reads flat on
				// something the camera gets close to, and exaggerating is the
				// fix. 0 turns the map off without unassigning it, which is how
				// you find out whether it was the normal map all along.
				Field<&MeshComponent::OverrideNormalScale>("OverrideNormalScale",
					Named("Normal Scale", FieldHint{ .Tooltip =
						"How strongly the material's normal map bends the surface. 1 is the "
						"map as authored, 0 ignores it, above 1 exaggerates the relief." })),
				Field<&MeshComponent::NormalScale>("NormalScale",
					OnlyWhen(OverridesNormalScale, Named("Value", Slider(0.0f, 4.0f)))),
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

				// Version 5 and earlier wrote the material inline, as a nested
				// map of scalars -- and *only* scalars, because a Ref cannot be
				// written to a file. Scalars are exactly what an override is,
				// so an old material converts into overrides losslessly: the
				// scene renders identically and nothing needs migrating.
				//
				// This reads it and nothing writes it. An old scene saved from
				// the editor comes back out in the new shape.
				const YAML::Node material = node["Material"];
				if (!material || !material.IsMap())
					return;

				auto readVec4 = [](const YAML::Node& n, Vec4 fallback)
				{
					if (!n || !n.IsSequence() || n.size() != 4)
						return fallback;
					return Vec4(n[0].as<float>(), n[1].as<float>(),
									 n[2].as<float>(), n[3].as<float>());
				};

				if (material["BaseColor"])
				{
					mesh->OverrideBaseColor = true;
					mesh->BaseColor = readVec4(material["BaseColor"], mesh->BaseColor);
				}
				if (material["Emissive"])
				{
					mesh->OverrideEmissive = true;
					mesh->EmissiveColor = readVec4(material["Emissive"], mesh->EmissiveColor);
				}
				if (material["Metallic"])
				{
					mesh->OverrideMetallic = true;
					mesh->Metallic = material["Metallic"].as<float>();
				}
				if (material["Roughness"])
				{
					mesh->OverrideRoughness = true;
					mesh->Roughness = material["Roughness"].as<float>();
				}
				if (material["Occlusion"])
				{
					mesh->OverrideOcclusion = true;
					mesh->Occlusion = material["Occlusion"].as<float>();
				}
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
					PicksClip(Tip("Which of this model's animations to play. "
								  "The bind pose is the shape it was modelled in."))),
				Field<&AnimatorComponent::Playing>("Playing"),
				Field<&AnimatorComponent::Loop>("Loop"),
				Field<&AnimatorComponent::RunInEditor>("RunInEditor",
					Named("Run in editor",
						Tip("Preview this animation while the scene is only being "
							"edited.\n\nOff by default: animation belongs to the "
							"running game, and an editor whose characters are all "
							"mid-stride has nothing holding still to build "
							"against. Play ignores this and animates regardless."))),
				Field<&AnimatorComponent::Speed>("Speed",
					Drag(0.05f, -4.0f, 4.0f, "Negative plays the clip backwards.")),
				Field<&AnimatorComponent::BlendTime>("Blend Time",
					Drag(0.01f, 0.0f, 2.0f, "Seconds to cross-fade when the clip "
											"changes. Zero snaps.")),
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

		// --- UI canvas ---------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "UICanvasComponent";
			desc.DisplayName = "UI Canvas";
			desc.Fields = {
				Field<&UICanvasComponent::ScaleMode>("ScaleMode",
					Enum(kCanvasScaleNames, "How canvas units become pixels. Scale With "
										    "Screen keeps a layout the same relative size "
										    "on every display; Constant Pixels does not, "
										    "and is for debug overlays.")),
				Field<&UICanvasComponent::ReferenceResolution>("ReferenceResolution",
					Drag(1.0f, 1.0f, 16384.0f, "The size this UI was laid out against.")),
				Field<&UICanvasComponent::MatchWidthOrHeight>("MatchWidthOrHeight",
					Slider(0.0f, 1.0f, "Which axis the scale follows: 0 the width, 1 the "
									   "height. Halfway is safest -- following the width "
									   "alone makes a HUD grow on a window that is wider "
									   "and shorter, and walk off the bottom.")),
				Field<&UICanvasComponent::SortOrder>("SortOrder",
					Drag(0.25f, -999.0f, 999.0f, "Between canvases. Within one, each element's "
										   "own sort order decides.")),
			};
			Bind<UICanvasComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- UI rect -----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "UIRectComponent";
			desc.DisplayName = "UI Rect";
			desc.Fields = {
				Field<&UIRectComponent::AnchorMin>("AnchorMin",
					Drag(0.01f, 0.0f, 1.0f, "Top-left corner, as a fraction of the parent. "
											"Equal to AnchorMax pins a point; apart from it "
											"stretches.")),
				Field<&UIRectComponent::AnchorMax>("AnchorMax",
					Drag(0.01f, 0.0f, 1.0f, "Bottom-right corner, as a fraction of the "
											"parent.")),
				Field<&UIRectComponent::OffsetMin>("OffsetMin",
					Drag(1.0f, -16384.0f, 16384.0f,
						 "Left and top, in canvas units from the anchor.")),
				Field<&UIRectComponent::OffsetMax>("OffsetMax",
					Drag(1.0f, -16384.0f, 16384.0f,
						 "Right and bottom, in canvas units from the anchor.")),
				Field<&UIRectComponent::SortOrder>("SortOrder",
					Drag(0.25f, -999.0f, 999.0f, "Higher draws on top. Ties keep hierarchy "
										   "order.")),
				Field<&UIRectComponent::Visible>("Visible",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "Hides this element. Its children still lay out against it, "
							   "so hiding a panel does not move the label on it." }),
				Field<&UIRectComponent::BlocksPointer>("BlocksPointer",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "Stops the pointer here instead of letting it reach the "
							   "game. Off for labels and decoration, which is why a HUD "
							   "does not eat clicks; on for the backdrop of a modal. A UI "
							   "Button always blocks, whatever this says." }),
			};
			Bind<UIRectComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- UI image ----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "UIImageComponent";
			desc.DisplayName = "UI Image";
			desc.Fields = {
				Field<&UIImageComponent::Texture>("Texture",
					AssetRef(AssetType::Texture, "Leave empty for a plain filled rectangle.")),
				Field<&UIImageComponent::Color>("Color", Color()),
			};
			Bind<UIImageComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- UI text -----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "UITextComponent";
			desc.DisplayName = "UI Text";
			desc.Fields = {
				Field<&UITextComponent::Text>("Text"),
				Field<&UITextComponent::Font>("Font",
					AssetRef(AssetType::Font, "A .rvfont baked by tools/rvfont. A .ttf is "
											 "that tool's input and cannot be used here.")),
				Field<&UITextComponent::Size>("Size",
					Drag(0.5f, 1.0f, 512.0f, "Em size in canvas units. Below the atlas's "
											 "own smallest sharp size the antialiasing "
											 "softens -- rvfont prints that number when it "
											 "bakes a font.")),
				Field<&UITextComponent::Color>("Color", Color()),
				Field<&UITextComponent::Align>("Align", Enum(kTextAlignNames)),
				Field<&UITextComponent::Wrap>("Wrap",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "Break lines to fit the rectangle's width. Off means only "
							   "an explicit newline starts a line." }),
				Field<&UITextComponent::LineSpacing>("LineSpacing",
					Drag(0.01f, 0.1f, 4.0f, "Multiplier on the font's own line height.")),
			};
			Bind<UITextComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- world text --------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "WorldTextComponent";
			desc.DisplayName = "World Text";
			desc.Fields = {
				Field<&WorldTextComponent::Text>("Text"),
				Field<&WorldTextComponent::Font>("Font",
					AssetRef(AssetType::Font, "A .rvfont baked by tools/rvfont -- the same "
											  "asset a UI Text uses.")),
				Field<&WorldTextComponent::Size>("Size",
					Drag(0.01f, 0.01f, 100.0f, "Em height in world units, before this "
											   "entity's scale. Roughly the height of a "
											   "capital letter in metres.")),
				Field<&WorldTextComponent::Color>("Color", Color()),
				Field<&WorldTextComponent::Align>("Align", Enum(kTextAlignNames)),
				Field<&WorldTextComponent::WrapWidth>("WrapWidth",
					Drag(0.05f, 0.0f, 1000.0f, "Wrap at this width, in world units. Zero "
											   "never wraps, which is what a nameplate "
											   "wants.")),
				Field<&WorldTextComponent::LineSpacing>("LineSpacing",
					Drag(0.01f, 0.1f, 4.0f, "Multiplier on the font's own line height.")),
				Field<&WorldTextComponent::Billboard>("Billboard",
					Enum(kBillboardNames, "How it faces the camera. Upright turns to the "
										  "viewer but stays level -- Full tips the text "
										  "back when the camera looks down, which reads "
										  "as a bug on a row of nameplates.")),
			};
			Bind<WorldTextComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- UI button ---------------------------------------------------------
		//
		// Hovered, Pressed and Clicked are deliberately absent. They are what
		// the pointer did this frame, not what the author chose, and a field
		// registered here is a field written to the scene file -- a click that
		// survived a save and a reload would be a click nobody made.
		{
			ComponentDesc desc;
			desc.Name = "UIButtonComponent";
			desc.DisplayName = "UI Button";
			desc.Fields = {
				Field<&UIButtonComponent::Interactable>("Interactable",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "Off draws it at the normal tint and lets the pointer "
							   "through -- a greyed-out button." }),
				Field<&UIButtonComponent::NormalColor>("NormalColor",
					Color("At rest. Multiplied into the UI Image on this entity, so "
						  "white leaves it exactly as authored -- and the default sits "
						  "below white so that hovering has room to brighten.")),
				Field<&UIButtonComponent::HoverColor>("HoverColor",
					Color("Under the pointer.")),
				Field<&UIButtonComponent::PressedColor>("PressedColor",
					Color("Held down on this button.")),
				Field<&UIButtonComponent::OnClickTarget>("OnClickTarget",
					FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
							   "The entity whose script handles the click. Drag one from "
							   "the Hierarchy. Leave it empty for this entity, which is "
							   "what a script on the button itself wants." }),
				Field<&UIButtonComponent::OnClickMethod>("OnClickMethod",
					BindsMethod("OnClickTarget",
						FieldHint{ FieldHint::Widget::Default, 0.0f, 0.0f, 0.1f, nullptr, 0,
								   "The method to call. C++ scripts must register it with "
								   ".Method<>(); C# ones need no registration. Empty means "
								   "nothing is called and the click is read by polling "
								   "instead." })),
			};
			Bind<UIButtonComponent>(desc);
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

	// --- the scene's render settings ------------------------------------------

	namespace
	{
		// The names a script writes, and the same order the enum declares.
		// MSAA is deliberately absent while FrameGraphBuilder refuses it. A name
		// here is a mode a script can select and an inspector can show, and one
		// that quietly does nothing is worse than one that is not offered.
		const char* const kAntiAliasingNames[] = { "None", "FXAA", "SMAA", "SSAA" };
		const char* const kSkyNames[] = { "Color", "Gradient", "Cubemap" };

		std::vector<FieldDesc> BuildRenderSettings()
		{
			return {
				Field<&SceneEnvironment::AA>("AntiAliasing",
					Enum(kAntiAliasingNames,
						 "FXAA guesses at an edge from one pixel's neighbourhood. "
						 "SMAA reconstructs it and is about five times more accurate "
						 "for three times the cost.")),

				Field<&SceneEnvironment::SupersampleFactor>("SupersampleFactor",
					Tip("How many times larger SSAA draws each axis. Cost is the "
						"square of it: two is four times the pixels shaded, four "
						"is sixteen.")),

				Field<&SceneEnvironment::Exposure>("Exposure"),

				Field<&SceneEnvironment::BloomEnabled>("BloomEnabled"),
				Field<&SceneEnvironment::BloomThreshold>("BloomThreshold"),
				Field<&SceneEnvironment::BloomKnee>("BloomKnee"),
				Field<&SceneEnvironment::BloomIntensity>("BloomIntensity"),
				Field<&SceneEnvironment::BloomClamp>("BloomClamp"),

				Field<&SceneEnvironment::AmbientColor>("AmbientColor", Color()),
				Field<&SceneEnvironment::AmbientIntensity>("AmbientIntensity"),

				Field<&SceneEnvironment::Sky>("Sky", Enum(kSkyNames)),
				Field<&SceneEnvironment::SkyHorizon>("SkyHorizon", Color()),
				Field<&SceneEnvironment::SkyZenith>("SkyZenith", Color()),
				Field<&SceneEnvironment::SkyGround>("SkyGround", Color()),
				Field<&SceneEnvironment::SkyIntensity>("SkyIntensity"),
				Field<&SceneEnvironment::SkyRotation>("SkyRotation", Degrees()),

				Field<&SceneEnvironment::ShadowsEnabled>("ShadowsEnabled"),
				Field<&SceneEnvironment::ShadowDistance>("ShadowDistance"),
				Field<&SceneEnvironment::ShadowSplitLambda>("ShadowSplitLambda"),
				Field<&SceneEnvironment::ShadowNormalOffset>("ShadowNormalOffset"),
			};
		}
	}

	const std::vector<FieldDesc>& RenderSettingsRegistry::Fields()
	{
		// Function-local, so it is built on first use and cannot depend on the
		// order two translation units' globals happen to initialise in.
		static const std::vector<FieldDesc> fields = BuildRenderSettings();
		return fields;
	}

	const FieldDesc* RenderSettingsRegistry::Find(const std::string& name)
	{
		for (const auto& field : Fields())
		{
			if (name == field.Name)
				return &field;
		}
		return nullptr;
	}
}
