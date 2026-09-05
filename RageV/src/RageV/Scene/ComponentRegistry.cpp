#include <rvpch.h>
#include "ComponentRegistry.h"
#include "RageV/Asset/MaterialSerializer.h"
#include "Components.h"
#include "Entity.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Renderer/RayShadows.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Project/Project.h"
#include "RageV/Asset/LutRecipe.h"
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
		// In LightMobility order; serialised by these names.
		const char* kLightMobilityNames[] = { "Realtime", "Half bake", "Full bake", "Hybrid Full Bake" };
		// "Baked" was this list's first entry and meant "captured once at
		// runtime"; it is read as Cached for files written before the rename.
		const char* kProbeUpdateNames[] = { "Cached", "Realtime" };
		const char* kProbeRateNames[] = { "15Hz", "30Hz", "45Hz", "60Hz", "PerFrame" };
		const char* kProjectionNames[] = { "Perspective", "Orthographic" };
		const char* kBodyTypeNames[] = { "Static", "Kinematic", "Dynamic" };
		const char* kColliderShapeNames[] = { "Box", "Sphere", "Capsule" };
		const char* kCanvasScaleNames[] = { "ConstantPixels", "ScaleWithScreen" };
		const char* kTextAlignNames[] = { "Left", "Center", "Right" };
		const char* kBillboardNames[] = { "None", "Full", "Upright" };
		const char* kParticleFacingNames[] = { "Billboard", "Flat" };
		const char* kParticleBlendNames[] = { "Alpha", "Additive", "WeightedBlended" };
		const char* kParticleSpaceNames[] = { "World", "Local" };
		const char* kEmitterShapeNames[] = { "Point", "Box" };
		const char* kParticleMotionNames[] = { "Directional", "Stationary" };
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

		// The same, with the leading `first` values kept out of the editor's
		// dropdown. They remain valid everywhere else -- see EnumFirst.
		template<size_t N>
		FieldHint EnumFrom(int first, const char* const (&names)[N],
						   const char* tooltip = nullptr)
		{
			FieldHint hint = Enum(names, tooltip);
			hint.EnumFirst = first;
			return hint;
		}

		// The same, for an enum whose file spelling has changed. `legacy` is
		// still read; `names` is what gets written.
		template<size_t N>
		FieldHint EnumRenamed(const char* const (&names)[N], const char* legacy,
							  int legacyValue, const char* tooltip = nullptr)
		{
			FieldHint hint = Enum(names, tooltip);
			hint.LegacyName = legacy;
			hint.LegacyNameValue = legacyValue;
			return hint;
		}

		// An int field that is one of a fixed set of values rather than a
		// range. `values` and `names` are parallel, and the compiler checks
		// they are the same length.
		template<size_t N>
		FieldHint Choice(const int (&values)[N], const char* const (&names)[N],
						 const char* tooltip = nullptr)
		{
			FieldHint hint;
			hint.ChoiceValues = values;
			hint.ChoiceCount = (int)N;
			hint.EnumNames = names;
			hint.EnumCount = (int)N;
			hint.Tooltip = tooltip;
			return hint;
		}

		// What a file's `true` meant, for a field that used to be a boolean.
		// See FieldHint::LegacyTrue -- the mapping differs per field, so it is
		// written at the field rather than guessed by the serializer.
		FieldHint WasBool(int trueValue, FieldHint hint)
		{
			hint.LegacyTrue = trueValue;
			return hint;
		}

		FieldHint OnlyWhen(FieldVisibility visible, FieldHint hint = {})
		{
			hint.VisibleIf = visible;
			return hint;
		}

		// Greyed with a note while `disabled` holds; see FieldHint::DisabledIf.
		FieldHint DisabledWhen(FieldVisibility disabled, const char* note, FieldHint hint = {})
		{
			hint.DisabledIf = disabled;
			hint.DisabledNote = note;
			return hint;
		}

		bool IsSpot(const void* component)
		{
			return static_cast<const LightComponent*>(component)->Light.Type == Light::LightType::Spot;
		}

		bool NotAutoFit(const void* component)
		{
			return !static_cast<const IrradianceVolumeComponent*>(component)->AutoFit;
		}

		bool EmitsFromBox(const void* component)
		{
			return static_cast<const ParticleEmitterComponent*>(component)->Shape
				== EmitterShape::Box;
		}

		bool ParticlesStandStill(const void* component)
		{
			return static_cast<const ParticleEmitterComponent*>(component)->Motion
				== ParticleMotion::Stationary;
		}

		bool IsPositional(const void* component)
		{
			return static_cast<const LightComponent*>(component)->Light.Type != Light::LightType::Directional;
		}

		bool IsHybridBake(const void* component)
		{
			return static_cast<const LightComponent*>(component)->Light.Mobility
				== LightMobility::HybridFullBake;
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

				// Last, and deliberately: it is the one field here that opens
				// a second block of settings underneath itself, so anything
				// after it would read as belonging to the profile.
				Field<&CameraComponent::PostProfile>("PostProfile",
					AssetRef(AssetType::PostProfile,
						"How this camera grades its frame -- exposure, bloom, and "
						"everything phase 9 adds. Optional: no profile renders the "
						"neutral grade, which is what every scene did before "
						"profiles existed.\n\n"
						"It is an asset, so two cameras can share one and a look "
						"can be changed in one place. Editing the fields below "
						"edits that file, for every camera using it.")),
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
				// The static/moving split (ENGINE-NOTES 7cx): a checkbox, off
				// by default. See MeshComponent::Static for what it decides.
				Field<&MeshComponent::Static>("Static",
					Tip("This object never moves. Static objects are what the bake "
						"sees -- they shadow and bounce light in the baked field -- "
						"and they read fully baked lights from the field instead of "
						"lighting them live. Leave it off for anything that moves: a "
						"moving object is lit live by every light, casts a live "
						"shadow, and is left out of the bake so its shadow is not "
						"painted where it used to stand. Skinned meshes are always "
						"moving.")),

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

		// --- Terrain (ENGINE-NOTES 7ap) --------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "TerrainComponent";
			desc.DisplayName = "Terrain";
			desc.Fields = {
				Field<&TerrainComponent::Terrain>("Terrain",
					AssetRef(AssetType::Terrain,
							 "The heights: an .rvterrain, a square grid of 16-bit samples "
							 "made by tools/scripts/make_terrain.py from a 16-bit PNG or "
							 "from noise. Empty draws nothing.")),
				Field<&TerrainComponent::Size>("Size",
					Drag(0.5f, 1.0f, 16384.0f,
						 "Metres a side. The terrain is centred on this entity: it "
						 "reaches half of this each way in X and Z.")),
				Field<&TerrainComponent::Height>("Height",
					Drag(0.1f, 0.0f, 4000.0f,
						 "Metres the highest possible sample stands above the base. "
						 "The asset's heights are fractions of this.")),
				// The key stays "Material" -- the one material a stage-1 terrain
				// had, and layer 0 of a painted one -- so a scene saved before
				// layers existed reads unchanged. The label says what it is now.
				Field<&TerrainComponent::Material>("Material",
					Named("Layer 0", AssetRef(AssetType::Material,
							 "The base layer: everywhere nothing else is painted, "
							 "and the material of a terrain with no paint at all. "
							 "Empty means the renderer's default."))),
				Field<&TerrainComponent::Layer1>("Layer1",
					Named("Layer 1", AssetRef(AssetType::Material,
							 "A material blended in where the asset's weight map "
							 "paints this layer. Empty means the layer is not there."))),
				Field<&TerrainComponent::Layer2>("Layer2",
					Named("Layer 2", AssetRef(AssetType::Material,
							 "As layer 1, from the weight map's third channel."))),
				Field<&TerrainComponent::Layer3>("Layer3",
					Named("Layer 3", AssetRef(AssetType::Material,
							 "As layer 1, from the weight map's fourth channel."))),
				Field<&TerrainComponent::TextureScale>("TextureScale",
					Named("Texture scale", Drag(0.05f, 0.05f, 1000.0f,
						 "Metres per repeat of the layers' textures. Each "
						 "layer's own tiling multiplies on top."))),
				Field<&TerrainComponent::Collision>("Collision",
					Tip("A static collider under the drawn surface, exact to the "
						"triangle. The terrain is its own collider: no RigidBody or "
						"Collider component is needed, and one on this entity is "
						"ignored.")),
				Field<&TerrainComponent::Static>("Static",
					Tip("This terrain never moves. The same switch a mesh has: on, "
						"the bake sees it and it reads fully baked lights from the "
						"field; off, it is lit live and left out of the bake.")),
			};

			desc.OnChanged = [](void* component)
			{
				auto* terrain = static_cast<TerrainComponent*>(component);
				terrain->Size = Math::Max(terrain->Size, 1.0f);
				terrain->Height = Math::Max(terrain->Height, 0.0f);
				terrain->TextureScale = Math::Max(terrain->TextureScale, 0.05f);
			};

			Bind<TerrainComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Water -----------------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "WaterComponent";
			desc.DisplayName = "Water";
			desc.Fields = {
				Field<&WaterComponent::Width>("Width",
					Drag(0.5f, 0.0f, 20000.0f,
						 "Metres across this entity's X. The body is centred on "
						 "the entity: it reaches half of this each way.")),
				Field<&WaterComponent::Length>("Length",
					Drag(0.5f, 0.0f, 20000.0f,
						 "Metres along this entity's Z, centred the same way.")),
				Field<&WaterComponent::Spacing>("Spacing",
					Drag(0.05f, 0.05f, 500.0f,
						 "Metres between grid vertices. Waves that move the "
						 "surface can only move what is there, so this is the "
						 "finest shape the water will ever be able to take. "
						 "Divisions are capped at 1024 per axis: a coarser "
						 "spacing is used rather than a mesh nobody can draw.")),
				Field<&WaterComponent::TextureScale>("TextureScale",
					Named("Texture scale", Drag(0.05f, 0.05f, 1000.0f,
						 "Metres per repeat of the surface detail, taken off the "
						 "position rather than across the body -- so resizing adds "
						 "surface instead of magnifying what is on it."))),

				Field<&WaterComponent::ShallowColor>("ShallowColor",
					Named("Shallow colour", Color(
						 "What the water reads as where the bottom is close. "
						 "Water is a gradient, not a colour: a single base tint "
						 "cannot say that the bar is green and the channel beside "
						 "it is nearly black."))),
				Field<&WaterComponent::DeepColor>("DeepColor",
					Named("Deep colour", Color(
						 "The open-water colour. Very dark is correct -- almost "
						 "everything seen in deep water is the sky reflected in "
						 "it, and a tint bright enough to notice washes that out."))),
				Field<&WaterComponent::GradientDepth>("GradientDepth",
					Named("Gradient depth", Drag(0.1f, 0.01f, 500.0f,
						 "Metres of depth it takes to go from the shallow colour "
						 "to the deep one."))),

				Field<&WaterComponent::WaveHeight>("WaveHeight",
					Named("Wave height", Drag(0.01f, 0.0f, 30.0f,
						 "Crest to trough, in metres. Bounded by Spacing: a wave "
						 "shorter than two grid quads cannot be drawn, only "
						 "aliased, so a tall wave on a coarse grid is a request "
						 "the geometry cannot honour."))),
				Field<&WaterComponent::WaveLength>("WaveLength",
					Named("Wave length", Drag(0.1f, 0.1f, 2000.0f,
						 "Metres from one crest to the next, for the largest wave "
						 "in the sum."))),
				Field<&WaterComponent::Choppiness>("Choppiness",
					Drag(0.01f, 0.0f, 2.0f,
						 "How far the crests lean into peaks rather than staying "
						 "sinusoidal. 0 is a swell, 1 is a chopped sea, and past "
						 "1 the crests fold through themselves -- which is what a "
						 "breaking wave is, and the only thing foam is computed "
						 "from. A sea held under 1 can never have a whitecap.")),
				Field<&WaterComponent::WaveSpeed>("WaveSpeed",
					Named("Wave speed", Drag(0.01f, 0.0f, 50.0f,
						 "Metres a second the pattern travels."))),
				Field<&WaterComponent::WaveDirection>("WaveDirection",
					Named("Wave direction", Drag(1.0f, 0.0f, 360.0f,
						 "Degrees the waves run towards. A sea running across the "
						 "shot reads very differently from one running away."))),
				Field<&WaterComponent::Foam>("Foam",
					Drag(0.01f, 0.0f, 1.0f,
						 "How white the crests go where the surface stretches.")),
			};

			desc.OnChanged = [](void* component)
			{
				auto* water = static_cast<WaterComponent*>(component);
				water->Width = Math::Max(water->Width, 0.0f);
				water->Length = Math::Max(water->Length, 0.0f);
				water->Spacing = Math::Max(water->Spacing, 0.05f);
				water->TextureScale = Math::Max(water->TextureScale, 0.05f);
				water->GradientDepth = Math::Max(water->GradientDepth, 0.01f);
				water->WaveHeight = Math::Max(water->WaveHeight, 0.0f);
				water->WaveLength = Math::Max(water->WaveLength, 0.1f);
				water->WaveSpeed = Math::Max(water->WaveSpeed, 0.0f);

				// **Clamped, not trusted.** Past 1 a Gerstner wave folds through
				// itself and the surface self-intersects -- which does not read
				// as a rough sea, it reads as torn geometry.
				water->Choppiness = Math::Clamp(water->Choppiness, 0.0f, 2.0f);
				water->Foam = Math::Clamp(water->Foam, 0.0f, 1.0f);
			};

			Bind<WaterComponent>(desc);
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
				// Metres of emitter radius; 0 is a point. What it changes is
				// the specular image: a sphere's reflection on water is the
				// streak, a point's is a blinking speck. See Light::SourceRadius.
				Field<&LightComponent::Light, &Light::SourceRadius>("SourceRadius",
					OnlyWhen(IsPositional, Drag(0.05f, 0.0f, 10.0f))),
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
				Field<&LightComponent::Light, &Light::Mobility>("Mobility",
					Named("Mobility",
						Enum(kLightMobilityNames,
							"How much of this light the bake owns.\n\n"
							"Realtime: nothing. Direct light and shadows render live, "
							"scripts can switch it freely without invalidating any bake, "
							"and its bounce is absent from baked GI -- headlights, "
							"flashlights, anything that toggles.\n\n"
							"Half bake: its bounce is in the bake, its direct light and "
							"shadows stay live. Right for a lit scene with things moving "
							"through it; changing the light means re-baking.\n\n"
							"Full bake: its direct light is solved into the field too, "
							"with a shadow ray per cell, and static objects read it "
							"from there instead of lighting it live -- the cheapest "
							"form at runtime. Moving objects are still lit live by it, "
							"shadows included, and still shadow the static floor from "
							"it. Lit live everywhere where no field is bound.\n\n"
							"Hybrid Full Bake: Half bake within the radius below, Full "
							"bake beyond it. The bright spot under the lamp, and the glow "
							"it makes, stay live and exact; the far light, which is most "
							"of the cost, comes from the field. Changing the radius means "
							"re-baking."))),
				Field<&LightComponent::Light, &Light::HybridRadius>("HybridRadius",
					OnlyWhen(IsHybridBake, Named("Half bake radius",
						Slider(1.0f, 200.0f,
							   "Metres from the lamp within which it is half baked -- lit "
							   "live, with its bright spot and its glow. Beyond this the "
							   "field supplies its light. Two metres keeps a lamp post's top "
							   "and its glow live (owner's default); the bake stores only "
							   "what lies past it, so a new radius is a new bake.")))),
			};
			// Scenes from before the enum stored a bool. `IsBaked: false` was a
			// realtime light; true, or absent, was what is now Half bake.
			desc.DeserializeExtra = [](const YAML::Node& node, void* component)
			{
				if (node["Mobility"])
					return;
				if (const YAML::Node baked = node["IsBaked"])
					static_cast<LightComponent*>(component)->Light.Mobility =
						baked.as<bool>() ? LightMobility::HalfBake : LightMobility::Realtime;
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
					EnumRenamed(kProbeUpdateNames, "Baked", (int)ProbeUpdate::Cached,
						"Cached captures once and costs nothing after "
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
				Field<&ReflectionProbeComponent::Rate>("Rate",
					OnlyWhen(IsRealtimeProbe,
						Enum(kProbeRateNames,
							 "How often the probe takes its next capture step. A "
							 "reflection is seen through a rough or curved surface, "
							 "so 15 Hz usually looks identical to per-frame and "
							 "costs a fraction -- per-frame capture is also where "
							 "a realtime probe's frame spikes come from."))),
				Field<&ReflectionProbeComponent::FacesPerFrame>("FacesPerFrame",
					OnlyWhen(IsRealtimeProbe,
						Drag(1.0f, 1.0f, 6.0f, "Six in one frame is a visible hitch; one "
											   "per frame is a sixth of the cost and a "
											   "sixth of a second of latency."))),
				Field<&ReflectionProbeComponent::Recapture>("Recapture",
					Named("Re-capture now",
						Tip("Take a fresh capture on the next frame. Moving the probe, "
						"resizing its influence or clips, or changing the sky already "
						"do this by themselves; moving geometry or changing a light "
						"do not, because finding those precisely costs more than it "
						"saves. Use this after either."))),
			};
			Bind<ReflectionProbeComponent>(desc);
			s_Components.push_back(std::move(desc));
		}

		// --- Irradiance volume ------------------------------------------------
		{
			ComponentDesc desc;
			desc.Name = "IrradianceVolumeComponent";
			desc.DisplayName = "Irradiance Volume";
			desc.Fields = {
				Field<&IrradianceVolumeComponent::AutoFit>("AutoFit",
					Named("Auto Fit",
						Tip("Fit the box to the scene's static meshes instead of "
							"authoring it, snapped outward to the cell grid -- so "
							"nothing visible can sit outside the baked light. While "
							"on, the transform and Extents are ignored. Skinned "
							"meshes and terrain are not part of the fit; scenes "
							"built around either should author the box by hand, "
							"which is why this is a choice and not the default."))),
				Field<&IrradianceVolumeComponent::Extents>("Extents",
					OnlyWhen(NotAutoFit,
						Drag(0.1f, 0.05f, 500.0f,
							 "Half the box, in metres, so 5 covers ten metres. The "
							 "transform places and turns the volume and its scale is "
							 "ignored -- a volume has no mesh for a scale to multiply, "
							 "and one parented to something scaled used to change size "
							 "without anyone touching it."))),
				Field<&IrradianceVolumeComponent::Spacing>("Spacing",
					Drag(0.1f, 0.25f, 16.0f,
						 "Metres between stored samples. The grid follows the box's "
						 "size, so a larger room costs more cells rather than coarser "
						 "ones. Tighter resolves a doorway or a corner; looser is "
						 "cheaper and smoother.")),
				Field<&IrradianceVolumeComponent::MaxResolution>("MaxResolution",
					Drag(1.0f, 4.0f, 128.0f,
						 "A ceiling on cells per axis, whatever the spacing asks for. "
						 "A volume dragged across a level would otherwise request a "
						 "grid no memory budget survives, and would fail as an "
						 "allocation error rather than as a coarser answer.")),
				Field<&IrradianceVolumeComponent::Passes>("Passes",
					Drag(1.0f, 1.0f, 64.0f,
						 "How many times the solve goes over the grid -- and every "
						 "pass past the first is a bounce: its rays read what the "
						 "previous pass stored, so the answer converges on full "
						 "multi-bounce light while the passes average out the "
						 "mottling. Eight is visually converged for ordinary rooms; "
						 "more costs bake time and nothing else.")),
				Field<&IrradianceVolumeComponent::RaysPerCell>("RaysPerCell",
					Named("Rays per cell",
						Drag(64.0f, 64.0f, 4096.0f,
							 "What each cell spends deciding what reaches it. The "
							 "error this leaves is not noise but mottling -- "
							 "interpolation spreads a cell's error over a cell's "
							 "width, so it reads as patches of a wall lighter than "
							 "their neighbours. Variance falls as one over this, and "
							 "every ray is spent once, in the baker."))),
				Field<&IrradianceVolumeComponent::Recapture>("Recapture",
					Named("Solve again",
						Tip("Solve the field again on the next frame. Moving or "
							"resizing the volume already does this by itself, and so "
							"does a change to the scene's lights -- but moving "
							"geometry or editing a material does not, because "
							"finding those precisely costs more than it saves."))),
			};
			Bind<IrradianceVolumeComponent>(desc);
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
				Field<&ParticleEmitterComponent::Shape>("Shape",
					Enum(kEmitterShapeNames,
						 "Where a particle is born. Point is the emitter's own "
						 "position -- a chimney, a muzzle. Box spawns anywhere "
						 "inside a volume, which is what anything spread over an "
						 "area needs: fireflies over a clearing, dust in a shaft "
						 "of light. A cone leaving one point reads as a fountain "
						 "however slowly it moves, because the tell is the empty "
						 "space at the origin rather than the speed.")),
				Field<&ParticleEmitterComponent::BoxSize>("BoxSize",
					Named("Box size",
						DisabledWhen([](const void* c) { return !EmitsFromBox(c); },
									 "Only a Box emitter has a volume to spawn in.",
							FieldHint{ FieldHint::Widget::Default, 0, 0, 0.05f, nullptr, 0,
									   "Full extents in metres, in the emitter's own "
									   "frame. Its own size rather than the entity's "
									   "scale, as a collider's is -- so an emitter "
									   "parented to something scaled is not scaled "
									   "twice. Shown in the viewport under "
									   "Window > Show Emitters, or F4." }))),
				Field<&ParticleEmitterComponent::Motion>("Motion",
					Enum(kParticleMotionNames,
						 "Whether a particle goes anywhere once it is born. "
						 "Directional is the cone below, then gravity and drag. "
						 "Stationary gives it no velocity and applies neither, so "
						 "it appears, lives out its lifetime and goes -- which "
						 "with a Box is a field of things blinking on and off in "
						 "place. Speed at zero is not the same thing: gravity "
						 "still takes it.")),

				Field<&ParticleEmitterComponent::Direction>("Direction",
					DisabledWhen(ParticlesStandStill,
								 "A stationary particle has no direction.",
						FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
								   "The cone's axis, in the emitter's own frame." })),
				Field<&ParticleEmitterComponent::Spread>("Spread",
					DisabledWhen(ParticlesStandStill,
								 "A stationary particle has no direction.",
						Slider(0.0f, 180.0f,
							   "Half-angle of the cone, degrees. 180 is a sphere, "
							   "which is how a box emitter drifts every way at "
							   "once."))),
				Field<&ParticleEmitterComponent::Speed>("Speed",
					DisabledWhen(ParticlesStandStill,
								 "A stationary particle does not move.",
								 Drag(0.1f, 0.0f, 1000.0f))),
				Field<&ParticleEmitterComponent::SpeedJitter>("SpeedJitter",
					DisabledWhen(ParticlesStandStill,
								 "A stationary particle does not move.",
								 Slider(0.0f, 1.0f))),
				Field<&ParticleEmitterComponent::Gravity>("Gravity",
					DisabledWhen(ParticlesStandStill,
								 "Nothing pulls on a stationary particle -- that is "
								 "what makes it stationary rather than slow.",
						FieldHint{ FieldHint::Widget::Default, 0, 0, 0.1f, nullptr, 0,
								   "This emitter's own, not the physics world's: snow "
								   "drifts, sparks plunge, neither is a rigid body." })),
				Field<&ParticleEmitterComponent::Drag>("Drag",
					DisabledWhen(ParticlesStandStill,
								 "Nothing pulls on a stationary particle.",
						Slider(0.0f, 10.0f, "Fraction of velocity lost per second."))),
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

	// --- the three settings blocks --------------------------------------------

	namespace
	{
		// The names a script writes, and the same order the enum declares.
		const char* const kGiQualityNames[] = { "Low", "Medium", "High" };
		const char* const kGiSourceNames[] = { "Realtime", "Baked" };
		// Off, and then the resolution the occlusion is computed at. `false`
		// and `true` in an older file are Off and Half, which is what they did.
		// Off, then the ladder: each rung is a resolution and a ray count.
		// `false` and `true` in an older file are Off and Half, which is what
		// they did.
		const char* const kAoDetailNames[] = { "Off", "Quarter", "Half", "Full" };
		// Off, then the two the rasterised dial uses -- rays, then resolution.
		const char* const kRayDetailNames[] = { "Off", "Low", "Medium", "High" };
		const char* const kRayBudgetNames[] = { "Off", "Absolute ray time", "Fractional" };
		// WR-17, in RenderSettings::RayOptimisation order.
		const char* const kRayOptimisationNames[] = { "Off", "Quality", "Balanced", "Performance" };

		const char* const kFocusModeNames[] = { "Manual", "Target" };
		const char* const kMsaaCountNames[] = { "2x", "4x", "8x" };

		const char* const kAntiAliasingNames[] = { "None", "FXAA", "SMAA", "SSAA",
												   "MSAA", "TAA" };
		const char* const kSkyNames[] = { "Color", "Gradient", "Cubemap" };

		// Which rows apply to the mode that is selected.
		//
		// Same rule the components follow: a hidden field is hidden, never
		// dropped, so toggling anti-aliasing cannot lose the sample count it
		// was set to. Showing all three at once is what the panel used to do
		// before it was registry-driven, and two of them did nothing.
		bool UsesMsaa(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->AA == AntiAliasing::MSAA;
		}
		bool UsesSsaa(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->AA == AntiAliasing::SSAA;
		}
		bool UsesTaa(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->AA == AntiAliasing::TAA;
		}
		bool CastsShadows(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->ShadowsEnabled;
		}
		// The ray-tracing block (7am-7ao) is offered only where it can run:
		// on a device with ray queries -- Vulkan on hardware that traces, never
		// OpenGL -- and under Shadows, whose pass builds the structure the rays
		// trace into. Elsewhere the rows are absent, not greyed: there is no
		// "on" for them to be. The values are kept in the project either way,
		// so a project authored beside an RTX and opened on OpenGL keeps its
		// choices for the next time it is opened where they apply.
		bool OffersRayTracing(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->ShadowsEnabled && RayShadows::IsAvailable();
		}
		bool RayTracingOn(const void* block)
		{
			return OffersRayTracing(block) && static_cast<const RenderSettings*>(block)->RayTracing;
		}
		// The two ray-budget parameters, each shown only under the mode that
		// reads it -- a millisecond ceiling means nothing under Fractional and
		// a share means nothing under Absolute.
		bool UsesFog(const void* block)
		{
			return static_cast<const PostSettings*>(block)->Fog;
		}

		// Reflections shade a hit through the bindless heap, so they are
		// offered only where materials are bindless as well.
		bool OffersRayReflections(const void* block)
		{
			return RayTracingOn(block) && Renderer3D::IsBindless();
		}
		// The cascade dials mean nothing to a traced shadow (7an): with ray
		// tracing on, no map of any kind is rendered. Asked of what runs, not
		// of the checkbox alone: a ticked box on OpenGL renders maps.
		bool UsesCascades(const void* block)
		{
			const auto* render = static_cast<const RenderSettings*>(block);
			return render->ShadowsEnabled && !RayTracingOn(block);
		}

		bool HasColorLut(const void* block)
		{
			return static_cast<const PostSettings*>(block)->ColorLut.IsValid();
		}

		// Whether the ray-traced twin has taken a screen-space effect over
		// (ENGINE-NOTES 7ao). Asked of the *resolved* state -- the project's
		// checkboxes, the command line, and what this device can -- rather
		// than of the profile block, because that is what actually runs: a
		// profile authored beside an RTX and opened on OpenGL shows its SSAO
		// row live, which is what runs there.
		bool RayReflectionsTakeOver(const void*)
		{
			return ResolveRayTracedReflections(Project::Render()) != RayDetail::Off;
		}
		bool RayOcclusionTakesOver(const void*)
		{
			return ResolveRayTracedAmbientOcclusion(Project::Render()) != AoDetail::Off;
		}
		bool RayGiTakesOver(const void*)
		{
			return ResolveRayTracedGlobalIllumination(Project::Render()) != RayDetail::Off;
		}
		// The voxel form (8.1, ENGINE-NOTES 7bc): the profile's choice, where
		// rays do not win and the device can.
		//
		// Both read `block` rather than a global, which is only legal because
		// every row that reaches them is a *post* row -- including GI bounces,
		// which is why that one had to move too (10.6, 7bg). A render row
		// reaching either of these would cast the wrong struct.
		bool VoxelGiTakesOver(const void* block)
		{
			return !RayGiTakesOver(block)
				&& ResolveVoxelGlobalIllumination(*static_cast<const PostSettings*>(block));
		}
		bool VoxelGiOn(const void* block)
		{
			return static_cast<const PostSettings*>(block)->VoxelGlobalIllumination;
		}
		// The radius is the screen-space gather's alone: a traced ray runs
		// until it hits something, and a cone to the cascade's edge.
		bool GiScreenSpaceRuns(const void* block)
		{
			return static_cast<const PostSettings*>(block)->GlobalIllumination
				&& !RayGiTakesOver(block) && !VoxelGiTakesOver(block);
		}
		// The gather's alone again. For a while the traced form obeyed it too,
		// and that conflation is what put a dull patch on gi_corner's red wall:
		// a dial tuned as a 2 m bleed width was deciding how far light may
		// travel. The traced form now has GiReach, a row of its own below.
		bool GiRadiusApplies(const void* block)
		{
			return GiScreenSpaceRuns(block);
		}
		// The quality dial serves the screen gather and the voxel gather --
		// both run at the resolution it picks -- and not the traced form.
		// Whether the traced bounce is what answers GI, which is when asking
		// where *its* light comes from is a question worth showing.
		// **Nothing is being computed, so nothing about how is worth setting.**
		// Baked reads a stored answer: the taps, the radius and the denoise all
		// describe a gather that is not running.
		bool GiIsBaked(const void* block)
		{
			return static_cast<const PostSettings*>(block)->GiSource == GiSource::Baked;
		}

		bool RayGiIsBaked(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->RayTracedGiSource
				== GiSource::Baked;
		}

		bool TracesGi(const void* block)
		{
			return static_cast<const RenderSettings*>(block)->RayTracedGlobalIllumination
				!= RayDetail::Off;
		}

		bool GiGatherRuns(const void* block)
		{
			return static_cast<const PostSettings*>(block)->GlobalIllumination && !RayGiTakesOver(block);
		}
		// Either gather, or the voxel grid's second bounce: what GI bounces
		// is offered for (7ax, 7bc).
		bool OffersGiBounces(const void* block)
		{
			return RayGiTakesOver(block) || VoxelGiTakesOver(block);
		}
		// The intensity serves both forms, so it shows while either runs --
		// the rule the AO dials already follow.
		bool GiDialsApply(const void* block)
		{
			return static_cast<const PostSettings*>(block)->GlobalIllumination || RayGiTakesOver(block);
		}
		// The traced bounce shades a hit through the heap, so it is offered
		// exactly where reflections are.
		bool OffersRayGi(const void* block)
		{
			return OffersRayReflections(block);
		}
		// The AO dials serve both forms, so they show while either runs.
		bool AoDialsApply(const void* block)
		{
			return static_cast<const PostSettings*>(block)->AmbientOcclusion != AoDetail::Off
				|| RayOcclusionTakesOver(block);
		}

		bool BloomOn(const void* block)
		{
			return static_cast<const PostSettings*>(block)->BloomEnabled;
		}

		bool LightGlowOn(const void* block)
		{
			return static_cast<const PostSettings*>(block)->LightGlow;
		}

		bool SkyIsGradient(const void* block)
		{
			return static_cast<const SceneEnvironment*>(block)->Sky == SkyType::Gradient;
		}
		bool SkyIsCubemap(const void* block)
		{
			return static_cast<const SceneEnvironment*>(block)->Sky == SkyType::Cubemap;
		}
		bool SkyIsDrawn(const void* block)
		{
			return static_cast<const SceneEnvironment*>(block)->Sky != SkyType::Color;
		}

		std::vector<FieldDesc> BuildRenderSettings()
		{
			return {
				Field<&RenderSettings::AA>("AntiAliasing",
					Named("Anti-aliasing", Enum(kAntiAliasingNames,
						 "FXAA guesses at an edge from one pixel's neighbourhood. "
						 "SMAA reconstructs it and is about five times more accurate "
						 "for three times the cost. SSAA and MSAA resolve in linear "
						 "light, before the tone curve. TAA accumulates a jittered "
						 "frame over time and beats all of them standing still; "
						 "under motion it is roughly a wash, which is what the "
						 "Feedback dial trades."))),

				Field<&RenderSettings::MsaaSamples>("MsaaSamples",
					Named("Samples", OnlyWhen(UsesMsaa,
						Choice(MsaaCounts, kMsaaCountNames,
							"Coverage samples per pixel under MSAA. Costs bandwidth "
							"and a little rasterizer work rather than shading, so 4 "
							"is an ordinary choice where supersampling at 4 is a "
							"statement.\n\n"
							"A list rather than a slider because these are the only "
							"three that exist: a sample count is a bit flag in both "
							"graphics APIs, and asking for five asks for two of them "
							"at once.")))),

				Field<&RenderSettings::SupersampleFactor>("SupersampleFactor",
					Named("Supersample", OnlyWhen(UsesSsaa,
						Drag(0.05f, 1, 4,
							"How many times larger SSAA draws each axis. Cost is the "
							"square of it: two is four times the pixels shaded, four "
							"is sixteen.")))),

				Field<&RenderSettings::TemporalFeedback>("TemporalFeedback",
					Named("Feedback", OnlyWhen(UsesTaa,
						Drag(0.005f, 0.0f, 0.98f,
							"How much of TAA's accumulated image survives each frame. "
							"This is the ghosting-versus-flicker dial and there is no "
							"correct value: higher converges cleaner and holds stale "
							"history longer, lower is sharper under motion and noisier "
							"standing still. Measured against a supersampled "
							"reference, 0.6 is within a fraction of no filter under "
							"motion and three times better than it standing still.")))),

				Field<&RenderSettings::TemporalJitterScale>("TemporalJitterScale",
					Named("Jitter width", OnlyWhen(UsesTaa,
						Drag(0.01f, 0.0f, 2.0f,
							"How far TAA's per-frame offset reaches, in pixels. This "
							"is the filter's width, not how much history it keeps: "
							"wider covers more of the pixel and softens, narrower is "
							"sharper and leaves more of the pixel unsampled. 1 is the "
							"pixel's own area, which is what converges on the "
							"supersampled image; 0 stops jittering, which leaves the "
							"accumulation averaging the same sample forever.")))),

				Field<&RenderSettings::TemporalJitterPhase>("TemporalJitterPhase",
					Named("Jitter samples", OnlyWhen(UsesTaa,
						Drag(0.1f, 1, 16,
							"How many frames the offset sequence runs before it "
							"repeats. More converges on a finer image and takes "
							"longer to recover after a cut; past sixteen the "
							"difference stops being visible while the recovery still "
							"costs.")))),

				Field<&RenderSettings::ShadowsEnabled>("ShadowsEnabled", Named("Shadows")),

				Field<&RenderSettings::RayTracing>("RayTracing",
					Named("Ray tracing", OnlyWhen(OffersRayTracing, Tip(
						"Trace rays instead of rendering shadow maps: one ray per "
						"pixel toward every casting light, with no acne, no "
						"detachment, no distance limit and no cap on how many "
						"lights cast; skinned casters cast their pose. The edge is "
						"hard. Offered only on a device with ray queries (Vulkan on "
						"hardware that traces); elsewhere this row is absent and "
						"the maps are used. Applies at once -- no restart. The two "
						"options below add ray-traced reflections and ambient "
						"occlusion on the same structures.")))),

				Field<&RenderSettings::RayTracedReflections>("RayTracedReflections",
					Named("RT reflections", OnlyWhen(OffersRayReflections,
						WasBool((int)RayDetail::High, Enum(kRayDetailNames,
						"Trace the mirror ray from every glossy surface and shade "
						"what it hits, instead of walking the screen: reflections "
						"of things off-screen and behind other things, correct "
						"parallax. Rough surfaces keep the probe. Offered only "
						"where materials are bindless as well. Where this is not "
						"Off, the post profile's Screen-space reflections are not "
						"used and its rows say so." "\n\n"
						"The level is how glossy a surface has to be to earn a "
						"ray -- High traces up to roughness 0.6, Medium to 0.4, "
						"Low only near-mirrors. Not a resolution, unlike the other "
						"two: the ray is cast inside the lit shader so its answer "
						"can be mixed into the specular term the same frame, which "
						"is the whole of what it has over the screen-space walk."))))),

				Field<&RenderSettings::RayTracedWaterRefraction>("RayTracedWaterRefraction",
					Named("RT water refraction", OnlyWhen(OffersRayReflections, Tip(
						"Bend the view ray at the water's surface and trace it "
						"into the scene, so what shows through the swell is the "
						"actual geometry below at its actual refracted position "
						"-- on screen or not -- with absorption measured along "
						"the true underwater path. Off, the water refracts "
						"through a copy of the opaque frame instead, which is "
						"the raster form and most of the look. Offered only "
						"where materials are bindless as well, because a "
						"refracted hit is shaded through the material heap.")))),

				Field<&RenderSettings::RayTracedSkyVisibility>("RayTracedSkyVisibility",
					Named("RT sky visibility", OnlyWhen(RayTracingOn,
						Enum(kAoDetailNames,
						"How much sky a surface can actually see, traced per "
						"pixel instead of read from a baked irradiance volume. "
						"The baked fraction only exists where a volume reaches, "
						"so without this the sky arrives unoccluded under the "
						"deck, inside the truss, beside a cliff, and on anything "
						"that moved since the bake. It scales the sky term only "
						"-- never the bounced light, which was already occluded "
						"on its way in. Quarter, Half and Full are 2, 4 and 8 "
						"rays, the same currency the traced occlusion below "
						"spends; four is what it was measured at.")))),

				Field<&RenderSettings::RayTracedAmbientOcclusion>("RayTracedAmbientOcclusion",
					Named("RT ambient occlusion", OnlyWhen(RayTracingOn,
						WasBool((int)AoDetail::Half, Enum(kAoDetailNames,
						"Cast SSAO's taps as short rays into the scene instead of "
						"probing the depth buffer: no halos, off-screen occluders "
						"count, and no reconstruction to wobble. Uses the post "
						"profile's AO radius and intensity; where this is not Off "
						"the profile's own Ambient occlusion is not used and its "
						"row says so." "\n\n"
						"Half and Full are the resolution the occlusion is "
						"computed at, the same two the profile's row offers."))))),

				Field<&RenderSettings::RayTracedGlobalIllumination>("RayTracedGlobalIllumination",
					Named("RT global illumination", OnlyWhen(OffersRayGi,
						DisabledWhen(RayGiIsBaked,
							"The source below is Baked, so no rays are cast and the "
							"rung means nothing. Off still turns the traced form off.",
						WasBool((int)RayDetail::High, Enum(kRayDetailNames,
						"Cast the bounce as rays from every surface instead of "
						"gathering it off the screen: light arrives from what is "
						"behind the camera and behind other things, and lands on "
						"the surface's own albedo rather than on the lit colour "
						"standing in for it. Offered only where materials are "
						"bindless as well. Where this is not Off, the post "
						"profile's Global illumination is not used and its row "
						"says so; its intensity still applies." "\n\n"
						"Low and Medium cast two rays and four; High casts four "
						"at the frame's own resolution rather than half of it, "
						"which is four times the rays and most of the cost. Half "
						"resolution is not the compromise it sounds like -- "
						"indirect light is the lowest-frequency thing in the "
						"picture -- and it is what ambient occlusion has always "
						"done.")))))),

				// **And where that bounce comes from**, on the same rule the
				// rasterised form follows: shown only while the dial above is
				// asking for rays at all.
				Field<&RenderSettings::RayTracedGiSource>("RayTracedGiSource",
					Named("RT GI source", OnlyWhen(TracesGi,
						Enum(kGiSourceNames,
							"Realtime traces one bounce from scratch every frame. "
							"Baked keeps that bounce and terminates its rays in the "
							"field stored beside the scene, so each hit inherits a "
							"stored bounce and the frame carries two bounces of light "
							"for one bounce of rays -- measured at 0.73 error against "
							"a two-bounce reference where Realtime scores 1.48, at "
							"the same cost. Needs an Irradiance Volume and a bake; "
							"without either it renders Realtime and says so.")))),



				// WR-17: RT optimisation, one preset for every light.
				Field<&RenderSettings::RtOptimisation>("RtOptimisation",
					Named("RT optimisation", OnlyWhen(RayTracingOn,
						EnumFrom(1, kRayOptimisationNames,
							"How far the traced shadows are allowed to economise. Every "
							"casting light costs every pixel one ray, and a sea under a "
							"hundred lamps pays for all of them; the farther a light the "
							"less it contributes, so the farther it is the more of its "
							"rays a pixel may skip, borrowing the visibility of a far "
							"ray it did trace. Measured on the night scene at 1440p "
							"against 114 ms with everything traced.\n\n"
							"Off traces every ray and gives every light its range.\n\n"
							"Quality thins rays from 300 to 600 m and keeps half at 450: "
							"100 ms, and nothing the eye finds.\n\n"
							"Balanced thins from 150 to 300 m and keeps one ray in eight "
							"past it: 96 ms, with speckle on the dark water and the "
							"glitter band.\n\n"
							"Performance lets no light reach past 300 m: 50 ms, and the "
							"far glitter dims.")))),

				// WR-16 S0: the calibrated cost of a ray, by kind. Milliseconds
				// per million rays, from the isolation protocol on the target
				// machine; zero is uncalibrated. Saved with the project so the
				// controller (S3) reads a measured number, never a guess.

				Field<&RenderSettings::ShadowDistance>("ShadowDistance",
					Named("Distance", OnlyWhen(UsesCascades,
						Drag(0.5f, 1.0f, 500.0f,
							"How far from the camera shadows are drawn at all. Not "
							"the far plane: past this distance the texels are so "
							"large the shadow is worse than none.")))),

				Field<&RenderSettings::ShadowSplitLambda>("ShadowSplitLambda",
					Named("Split lambda", OnlyWhen(UsesCascades,
						Slider(0.0f, 1.0f,
							"Blend between a logarithmic cascade split, which "
							"distributes texels correctly and starves the far "
							"cascades, and a uniform one, which does the reverse.")))),

				Field<&RenderSettings::ShadowNormalOffset>("ShadowNormalOffset",
					Named("Normal offset", OnlyWhen(UsesCascades,
						Drag(0.05f, 0.0f, 8.0f,
							"How far along the surface normal a sample is pushed, in "
							"shadow texels. Raising it removes acne and starts "
							"detaching shadows from their casters; no value has "
							"neither.")))),

				// These reallocate render targets when they change, so they are
				// absent from the script surface -- but they are in this list,
				// because the list is also what writes the project file, and a
				// setting the panel edits and nothing saves is the exact bug
				// this registry exists to prevent.
				Field<&RenderSettings::ShadowCascades>("ShadowCascades",
					Named("Cascades", OnlyWhen(UsesCascades,
						Drag(0.05f, 1, 4,
							"More cascades means better texel density near the "
							"camera and more scene renders.")))),

				Field<&RenderSettings::ShadowResolution>("ShadowResolution",
					Named("Resolution", OnlyWhen(CastsShadows,
						Drag(8.0f, 256, 8192,
							"Per cascade, square. The single biggest lever on both "
							"quality and cost: four 2048 maps is 64 MB of depth.")))),
			};
		}

		// True when the vignette is doing anything, so its falloff row is
		// hidden while it is not. Same rule the AA parameters follow: a
		// control that cannot change the picture is worse than an absent one.
		bool VignetteOn(const void* block)
		{
			return ((const PostSettings*)block)->VignetteIntensity > 0.0f;
		}

		bool GrainOn(const void* block)
		{
			return ((const PostSettings*)block)->FilmGrain > 0.0f;
		}

		bool AutoExposureOn(const void* block)
		{
			return ((const PostSettings*)block)->AutoExposure;
		}

		bool DepthOfFieldOn(const void* block)
		{
			return ((const PostSettings*)block)->DepthOfField;
		}

		bool FocusIsTarget(const void* block)
		{
			return ((const PostSettings*)block)->Focus == FocusMode::Target;
		}

		// Greyed only while something is genuinely answering them. A profile
		// shared with another scene names a target this one does not have, and
		// a row greyed with "the focus target is answering this" beside a slot
		// drawn in red as Missing entity is the panel contradicting itself --
		// with the two controls that would fix the shot switched off.
		bool FocusSolveAnswers(const void* block)
		{
			const PostSettings* post = (const PostSettings*)block;
			return post->Focus == FocusMode::Target && post->TargetResolved;
		}

		// The subject rows: present only when there is a subject to describe.
		// Hidden rather than greyed, which is the distinction FieldHint draws
		// -- these do not *apply* in Manual mode, as opposed to applying and
		// being answered elsewhere, which is what the two rows below them do.
		bool FocusTargetFields(const void* block)
		{
			return DepthOfFieldOn(block) && FocusIsTarget(block);
		}

		bool MotionBlurOn(const void* block)
		{
			return ((const PostSettings*)block)->MotionBlur;
		}

		bool AmbientOcclusionOn(const void* block)
		{
			return ((const PostSettings*)block)->AmbientOcclusion != AoDetail::Off;
		}

		bool ScreenSpaceReflectionsOn(const void* block)
		{
			return ((const PostSettings*)block)->ScreenSpaceReflections;
		}

		std::vector<FieldDesc> BuildPostSettings()
		{
			return {
				Field<&PostSettings::Fog>("Fog",
					Named("Fog", Tip(
						"Exponential height fog: density falls off with world "
						"height, so the air is thick at ground level and thin "
						"above it. One fullscreen pass over the depth buffer, "
						"applied on the linear image before depth of field and "
						"bloom -- fog is light, not a filter over a finished "
						"frame, so a fogged lamp blooms as the fog's colour "
						"rather than having grey painted over its bloom."))),

				Field<&PostSettings::FogColor>("FogColor",
					Named("Fog colour", OnlyWhen(UsesFog, Color(
						"What the fog is, in linear HDR. Distant geometry tends "
						"to this and so does the sky, so it is effectively the "
						"horizon colour.")))),

				Field<&PostSettings::FogDensity>("FogDensity",
					Named("Density", OnlyWhen(UsesFog,
						Drag(0.001f, 0.0f, 1.0f,
							"Density at the reference height, per metre. 0.005 "
							"is a clear evening; 0.05 is weather.")))),

				Field<&PostSettings::FogHeightFalloff>("FogHeightFalloff",
					Named("Height falloff", OnlyWhen(UsesFog,
						Drag(0.005f, 0.0f, 2.0f,
							"How fast density falls with height, per metre. Zero "
							"is a uniform volume -- plain distance fog, as a "
							"special case rather than a second mode.")))),

				Field<&PostSettings::FogHeight>("FogHeight",
					Named("Layer height", OnlyWhen(UsesFog,
						Drag(0.1f, -1000.0f, 1000.0f,
							"The world height the density is quoted at. Put it at "
							"the water, not at the origin, or a scene built above "
							"zero starts in clear air.")))),

				Field<&PostSettings::FogStartDistance>("FogStartDistance",
					Named("Start distance", OnlyWhen(UsesFog,
						Drag(0.05f, 0.0f, 500.0f,
							"How far a ray travels before any fog accumulates. "
							"Physically zero; in practice a bonnet or a cockpit a "
							"metre from the camera should not haze.")))),

				Field<&PostSettings::FogFloor>("FogFloor",
					Named("Fog floor",
						Drag(0.5f, -100000.0f, 100000.0f,
							"The world height below which there is no air to "
							"fog. Set it to the water plane in a scene that has "
							"one: fog integrated through water hazes the seabed, "
							"and hazes it hardest because the height falloff "
							"makes the layer densest at the bottom."))),
				Field<&PostSettings::FogMaxOpacity>("FogMaxOpacity",
					Named("Max opacity", OnlyWhen(UsesFog,
						Drag(0.01f, 0.0f, 1.0f,
							"The most the fog may take. Not physical -- it is how "
							"a scene keeps a landmark readable through air thick "
							"enough to sell the distance in front of it.")))),

				Field<&PostSettings::FogSkyAffect>("FogSkyAffect",
					Named("Sky affect", OnlyWhen(UsesFog,
						Drag(0.01f, 0.0f, 1.0f,
							"How much of the fog's colour comes from the sky in "
							"each view direction instead of from the fog colour "
							"above. One constant colour is what makes a night "
							"haze read as grey paint: real air is amber toward a "
							"city and near-black toward open water, because fog "
							"glows with the sky it sits under. Sampled from the "
							"same cube the scene reflects, so a gradient sky and "
							"an environment map both work. Zero is the flat "
							"colour this pass had before the dial existed.")))),

				Field<&PostSettings::FogSkyOcclusion>("FogSkyOcclusion",
					Named("Sky occlusion", OnlyWhen(UsesFog,
						Drag(0.01f, 0.0f, 1.0f,
							"How much the fog eats the sky behind it. Once the "
							"fog's colour is the sky's, a pixel showing only sky "
							"mixes the sky toward itself and comes out unchanged "
							"however thick the air gets -- so a fog bank in front "
							"of a city glow could brighten the skyline but never "
							"swallow it. This dims the background by the fog "
							"amount, which is what makes a bank read as a "
							"silhouette. Sky pixels only: geometry is already "
							"attenuated by the fog itself.")))),

				Field<&PostSettings::LightGlow>("LightGlow",
					Named("Light glow", Tip(
						"Every light with a source size is drawn as a soft disc a "
						"few pixels across whatever its distance, carrying the "
						"light's own intensity. It exists because a lamp's lens is "
						"geometry, and geometry smaller than a pixel blinks under "
						"every anti-aliasing mode the moment anything moves; the "
						"disc covers whole pixels in every frame and cannot. It "
						"fades out where the lens is bigger on screen than the "
						"disc, so close-ups keep the authored look."))),

				Field<&PostSettings::LightGlowPixels>("LightGlowPixels",
					Named("Glow size", OnlyWhen(LightGlowOn,
						Drag(0.1f, 1.0f, 32.0f,
							"Diameter of the disc in pixels. Larger is softer and "
							"dimmer per pixel; the total light is the same.")))),

				Field<&PostSettings::LightGlowIntensity>("LightGlowIntensity",
					Named("Glow intensity", OnlyWhen(LightGlowOn,
						Drag(0.01f, 0.0f, 10.0f,
							"A multiplier on the physical brightness. One is the "
							"light's own intensity; below it the lamps read dimmer "
							"than they are.")))),

				Field<&PostSettings::LightFlare>("LightFlare",
					Named("Flare", OnlyWhen(LightGlowOn,
						Drag(0.01f, 0.0f, 1.0f,
							"How much of each light's glow goes into a wider, "
							"fainter halo -- the glare a lens or an eye adds to a "
							"bright point at night. Moves energy rather than adding "
							"it. Zero is a plain disc.")))),

				Field<&PostSettings::LightFlareSize>("LightFlareSize",
					Named("Flare size", OnlyWhen(LightGlowOn,
						Drag(0.5f, 2.0f, 256.0f,
							"Diameter of the halo in pixels.")))),

				Field<&PostSettings::LightFlareRays>("LightFlareRays",
					Named("Flare rays", OnlyWhen(LightGlowOn,
						Drag(1.0f, 0.0f, 12.0f,
							"How many rays the halo carries. Zero is a plain halo; "
							"six is what a lens with a six-blade iris draws.")))),

				Field<&PostSettings::Exposure>("Exposure",
					Drag(0.01f, 0.01f, 16.0f,
						"Applied before the tone curve, which is what makes this "
						"an exposure control rather than a brightness one: it "
						"slides the scene along the response curve instead of "
						"scaling the result of it. With auto exposure on it "
						"becomes exposure compensation -- a multiplier on what "
						"the metering worked out.")),

				// --- auto exposure. ENGINE-NOTES 7y ---------------------------
				Field<&PostSettings::AutoExposure>("AutoExposure",
					Named("Auto exposure", FieldHint{ .Tooltip =
						"Meters the scene on the GPU and moves the exposure "
						"toward it, the way an eye adjusts on stepping outside. "
						"Off by default, and off exactly: no compute runs and "
						"the exposure above is used unchanged." })),

				Field<&PostSettings::AutoExposureKey>("AutoExposureKey",
					Named("Target grey", OnlyWhen(AutoExposureOn,
						Drag(0.002f, 0.01f, 1.0f,
							"What the metered average is exposed to. 0.18 is "
							"middle grey; higher is a brighter picture.")))),

				Field<&PostSettings::AutoExposureSpeed>("AutoExposureSpeed",
					Named("Adaptation speed", OnlyWhen(AutoExposureOn,
						Drag(0.02f, 0.05f, 20.0f,
							"How fast it moves, roughly in stops per second. "
							"Framerate independent: ten frames of 10 ms land "
							"where one of 100 ms does.")))),

				Field<&PostSettings::AutoExposureLowPercent>("AutoExposureLowPercent",
					Named("Ignore darkest", OnlyWhen(AutoExposureOn,
						Slider(0.0f, 0.9f,
							"The fraction of the darkest pixels to leave out of "
							"the average. This is why a histogram is kept "
							"rather than a plain average: shadow carries no "
							"information about how the shot is exposed.")))),

				Field<&PostSettings::AutoExposureHighPercent>("AutoExposureHighPercent",
					Named("Keep up to", OnlyWhen(AutoExposureOn,
						Slider(0.1f, 1.0f,
							"Where the bright end of the window sits. Below 1 "
							"the sun and the specular hits stop dragging the "
							"exposure every time the camera turns past them.")))),

				Field<&PostSettings::AutoExposureMin>("AutoExposureMin",
					Named("Min exposure", OnlyWhen(AutoExposureOn,
						Drag(0.005f, 0.001f, 4.0f,
							"Floor on the result, for a scene with nothing in "
							"it to meter.")))),

				Field<&PostSettings::AutoExposureMax>("AutoExposureMax",
					Named("Max exposure", OnlyWhen(AutoExposureOn,
						Drag(0.05f, 0.1f, 128.0f,
							"Ceiling on the result. A nearly black scene would "
							"otherwise be lifted until its noise is the "
							"picture.")))),

				Field<&PostSettings::AutoExposureMinLog>("AutoExposureMinLog",
					Named("Range low", OnlyWhen(AutoExposureOn,
						Drag(0.05f, -16.0f, 0.0f,
							"The darkest stop the histogram spans, as log2 "
							"luminance. Anything below is discarded rather than "
							"counted, so a night scene does not meter its own "
							"blackness.")))),

				Field<&PostSettings::AutoExposureMaxLog>("AutoExposureMaxLog",
					Named("Range high", OnlyWhen(AutoExposureOn,
						Drag(0.05f, 0.0f, 16.0f,
							"The brightest stop it spans. Anything above lands "
							"in the top bin.")))),

				Field<&PostSettings::BloomEnabled>("BloomEnabled", Named("Bloom")),

				Field<&PostSettings::BloomThreshold>("BloomThreshold",
					Named("Threshold", OnlyWhen(BloomOn,
						Drag(0.01f, 0.0f, 16.0f,
							"Brightness at which a pixel starts to bleed. Above 1, "
							"only things genuinely brighter than white glow.")))),

				Field<&PostSettings::BloomKnee>("BloomKnee",
					Named("Knee", OnlyWhen(BloomOn,
						Drag(0.01f, 0.0f, 4.0f,
							"Width of the ramp around the threshold. Zero is a hard "
							"cut, which pops as something crosses it and reads as "
							"flickering.")))),

				Field<&PostSettings::BloomIntensity>("BloomIntensity",
					Named("Intensity", OnlyWhen(BloomOn, Drag(0.002f, 0.0f, 2.0f)))),

				Field<&PostSettings::ColorLut>("ColorLut",
					Named("Colour LUT",
						AssetRef(AssetType::ColorLut,
							"A .cube lookup table, applied after the tone curve -- "
							"which is where a LUT exported from a grading tool "
							"expects to be, so one does here what it did there. It "
							"cannot recover highlight detail the tone curve has "
							"already compressed; that is a different feature."))),

				Field<&PostSettings::ColorLutStrength>("ColorLutStrength",
					Named("LUT strength",
						OnlyWhen(HasColorLut, Slider(0.0f, 1.0f,
							"How much of the graded result is used, against the "
							"ungraded one. A look is rarely wanted at full "
							"strength on the first try.")))),

				Field<&PostSettings::BloomClamp>("BloomClamp",
					Named("Clamp", OnlyWhen(BloomOn,
						Drag(0.25f, 0.0f, 256.0f,
							"Ceiling on what one pixel may contribute to bloom. "
							"Without it, anything very bright and very small -- the "
							"sun in curved metal -- survives as an isolated blob "
							"floating near the surface that produced it.")))),

				// --- depth of field. ENGINE-NOTES 7z ---------------------------
				Field<&PostSettings::DepthOfField>("DepthOfField",
					Named("Depth of field", FieldHint{ .Tooltip =
						"A real lens rather than a blur slider: the circle of "
						"confusion comes from the thin-lens equation, so f/1.4 "
						"does what f/1.4 does. Runs after anti-aliasing and "
						"before bloom, so an out-of-focus highlight glows as "
						"the disc it has become." })),

				// **The mode first, because it decides what the rows under it
				// mean.** Manual is every profile written before this existed,
				// and is what one loads as.
				Field<&PostSettings::Focus>("Focus",
					Named("Focus", OnlyWhen(DepthOfFieldOn,
						Enum(kFocusModeNames,
							"Manual sets the focus distance and the aperture by "
							"hand, which is right for a shot that does not move.\n\n"
							"Target names an object instead and solves both from "
							"it every frame -- the distance from where it is, the "
							"aperture from how deep it is. A camera that travels "
							"keeps its subject sharp without anybody animating a "
							"slider.")))),

				Field<&PostSettings::FocusTarget>("FocusTarget",
					Named("Focus target", OnlyWhen(FocusTargetFields, FieldHint{ .Tooltip =
						"The object the shot is focused on. Pick it from the list "
						"or drag it in from the Hierarchy.\n\n"
						"Its world bounds are what the solve reads, so a parent "
						"covering a whole model is the right thing to name -- "
						"naming one wheel focuses on that wheel." }))),

				Field<&PostSettings::SubjectCoverage>("SubjectCoverage",
					Named("Subject coverage", OnlyWhen(FocusTargetFields,
						Slider(0.0f, 1.0f,
							"How much of the subject has to be sharp, as a "
							"fraction of its depth. 1 keeps all of it sharp, "
							"which is the product-photograph answer. Lower opens "
							"the aperture and throws the far end away: 0.2 is a "
							"portrait, with the near fifth sharp.")))),

				Field<&PostSettings::FocusDistance>("FocusDistance",
					Named("Focus distance", OnlyWhen(DepthOfFieldOn,
						DisabledWhen(FocusSolveAnswers,
							"The focus target is answering this, from where it is.",
						Drag(0.05f, 0.05f, 500.0f,
							"Where the plane of sharp focus is, in metres."))))),

				Field<&PostSettings::FocalLength>("FocalLength",
					Named("Focal length", OnlyWhen(DepthOfFieldOn,
						Drag(0.5f, 8.0f, 400.0f,
							"In millimetres, the way lenses are sold. 50 is "
							"normal on a 35 mm sensor; longer is both narrower "
							"and shallower.")))),

				Field<&PostSettings::Aperture>("Aperture",
					Named("Aperture (f)", OnlyWhen(DepthOfFieldOn,
						DisabledWhen(FocusSolveAnswers,
							"The focus target is answering this, from how deep "
							"the subject is and the coverage above.",
						Drag(0.02f, 0.7f, 32.0f,
							"The f-number. Small is a wide aperture and a "
							"shallow field: f/1.4 throws a background away, "
							"f/16 keeps most of a scene sharp."))))),

				Field<&PostSettings::MaxBokehRadius>("MaxBokehRadius",
					Named("Max blur", OnlyWhen(DepthOfFieldOn,
						Drag(0.25f, 2.0f, 64.0f,
							"Ceiling on the blur radius in pixels. Let it grow "
							"without bound and the disc of samples thins into "
							"a ring of separate dots.")))),

				// --- SSR. ENGINE-NOTES 7ad ------------------------------------
				Field<&PostSettings::ScreenSpaceReflections>("ScreenSpaceReflections",
					Named("Screen-space reflections", DisabledWhen(RayReflectionsTakeOver,
						"Ray-traced reflections are on in Render Settings and are used instead.",
						Tip(
						"Reflections traced through what is already on screen, "
						"so a floor shows the crate standing on it -- which a "
						"probe, photographed from one point, cannot. Where the "
						"trace has no answer (off screen, behind the camera) the "
						"probe stays. Follows the normal map; rough surfaces get "
						"a blurred reflection or none.")))),

				Field<&PostSettings::SsrMaxDistance>("SsrMaxDistance",
					Named("SSR distance", OnlyWhen(ScreenSpaceReflectionsOn,
						DisabledWhen(RayReflectionsTakeOver, nullptr,
						Drag(0.1f, 1.0f, 200.0f,
							"How far a reflected ray travels before giving up, "
							"in metres. Longer finds more and costs more."))))),

				Field<&PostSettings::SsrThickness>("SsrThickness",
					Named("SSR thickness", OnlyWhen(ScreenSpaceReflectionsOn,
						DisabledWhen(RayReflectionsTakeOver, nullptr,
						Drag(0.01f, 0.02f, 5.0f,
							"How far behind a surface a ray may land and still "
							"count as hitting it. Small for thin railings; too "
							"large and rays hit walls through them."))))),

				Field<&PostSettings::SsrIntensity>("SsrIntensity",
					Named("SSR intensity", OnlyWhen(ScreenSpaceReflectionsOn,
						DisabledWhen(RayReflectionsTakeOver, nullptr,
						Slider(0.0f, 2.0f,
							"A scale on the traced reflection's share of the "
							"pixel; 1 is what the material implies."))))),

				// --- global illumination. ENGINE-NOTES 7at --------------------
				Field<&PostSettings::GlobalIllumination>("GlobalIllumination",
					Named("Global illumination", DisabledWhen(RayGiTakesOver,
						"Ray-traced global illumination is on in Render Settings and is used "
						"instead; the intensity below still applies.",
						Tip(
						"One bounce of diffuse light gathered from what is on "
						"the screen: a red wall throws red onto the white floor "
						"beside it. Light only bounces from what is in frame and "
						"in front of the camera -- turn away from the wall and "
						"its colour goes with it, which is what the ray-traced "
						"and voxel forms fix. With Voxel global illumination "
						"below on, the bounce is gathered from a voxelised "
						"scene instead, and sees off screen.")))),

				Field<&PostSettings::VoxelGlobalIllumination>("VoxelGlobalIllumination",
					Named("Voxel global illumination", DisabledWhen(RayGiTakesOver,
						"Ray-traced global illumination is on in Render Settings and wins: "
						"the voxel grid is not built at all while it runs.",
						Tip(
						"Where a camera's profile asks for global illumination, "
						"gather it from a voxelised scene instead of from the "
						"screen: the scene is rasterised into a grid around the "
						"camera each frame, lit from the shadow cascades, and "
						"cone-traced from every pixel. Light arrives from behind "
						"the camera, behind other things and off every edge of "
						"the frame, on both backends, with no ray hardware. The "
						"profile's Global illumination stays the on switch; "
						"ray-traced GI wins where it runs. A wall thinner than "
						"a voxel leaks a little light through itself.")))),

				Field<&PostSettings::VoxelGiResolution>("VoxelGiResolution",
					Named("Voxel resolution", OnlyWhen(VoxelGiOn,
						DisabledWhen(RayGiTakesOver, nullptr,
						Drag(0.5f, 32, 128,
							"Voxels along each cascade's side; rounded to 32, 64 "
							"or 128. Memory and the voxelisation cost go with the "
							"cube of it."))))),

				Field<&PostSettings::VoxelGiCascades>("VoxelGiCascades",
					Named("Voxel cascades", OnlyWhen(VoxelGiOn,
						DisabledWhen(RayGiTakesOver, nullptr,
						Drag(0.02f, 1, 4,
							"How many nested grids, each covering twice the "
							"distance of the last at half the detail. Three at "
							"the default voxel size reaches 64 metres."))))),

				Field<&PostSettings::VoxelGiVoxelSize>("VoxelGiVoxelSize",
					Named("Voxel size", OnlyWhen(VoxelGiOn,
						DisabledWhen(RayGiTakesOver, nullptr,
						Drag(0.01f, 0.05f, 4.0f,
							"The finest cascade's voxel, in metres. Smaller "
							"resolves thinner walls and a smaller room; larger "
							"reaches further for the same grid."))))),

				Field<&PostSettings::GiRadius>("GiRadius",
					Named("GI radius", OnlyWhen(GiRadiusApplies,
						Drag(0.05f, 0.1f, 20.0f,
							"World metres the screen-space gather searches. "
							"Small is colour bleeding in corners; large is "
							"room-scale. The traced form has its own reach, "
							"and the voxel form cones to the edge of its "
							"grid.")))),

				// **Where the rasterised form's indirect light comes from.**
				// Shown only while the gather is what answers GI, for the same
				// reason its quality dial is: a control for a path that is not
				// running is a control that teaches the wrong thing.
				Field<&PostSettings::GiSource>("GiSource",
					Named("Source", OnlyWhen(GiGatherRuns,
						Enum(kGiSourceNames,
							"Realtime gathers indirect light from the screen every "
							"frame, and can only find what is on screen. Baked "
							"replaces that gather with the field stored beside the "
							"scene -- cheaper, and it knows about surfaces the "
							"screen cannot see, though it stays coarser than the "
							"traced form at walls and contacts. Needs an Irradiance "
							"Volume and a bake; without either it renders Realtime "
							"and says so.")))),
				Field<&PostSettings::GiQuality>("GiQuality",
					Named("Quality", OnlyWhen(GiGatherRuns, DisabledWhen(GiIsBaked,
						"The source is Baked, so nothing is gathered and this "
						"describes a pass that is not running.",
						Enum(kGiQualityNames,
							"How finely the bounce is gathered. Low and Medium "
							"differ only in how many taps each pixel takes; High "
							"also gathers at full resolution, which is where most "
							"of its cost is and most of its sharpness. The blur "
							"after it narrows to match, so the detail survives. "
							"Not read by the ray-traced form -- its cost is rays."))))),

				Field<&PostSettings::GiIntensity>("GiIntensity",
					Named("GI intensity", OnlyWhen(GiDialsApply,
						Slider(0.0f, 4.0f,
							"How much of the gathered light is added. 0 is "
							"exactly the image without it. Read by both forms, "
							"so a scene tuned under one is not re-tuned under "
							"the other.")))),
				Field<&PostSettings::ReflectionFloor>("ReflectionFloor",
					Named("Reflection floor",
						Slider(0.0f, 64.0f,
							"The floor under the traced reflection's firefly "
							"clamp. That clamp bounds a mirror ray against the "
							"prefiltered probe, which is right by day and wrong "
							"at night: the probe goes black, the bound collapses "
							"onto this, and a real highlight -- a lamp reflected "
							"in water -- is crushed with the fireflies. 0.05 is "
							"what it was as a constant; a night scene raises it "
							"towards the brightness of the sources whose "
							"reflections have to survive."))),

				Field<&PostSettings::GiBounces>("GiBounces",
					Named("GI bounces", OnlyWhen(OffersGiBounces,
						Drag(0.02f, 1, 2,
							"How many times light bounces before the reflection "
							"probe answers for the rest. Two lets light reach a "
							"surface that can see nothing directly lit -- the "
							"inside of a doorway, the shaded side of a pillar -- "
							"which one bounce leaves as dark as the probe's "
							"average. Ray-traced: one extra ray per bounce ray, "
							"and the temporal filter absorbs its noise. Voxel: "
							"the grid is also lit from last frame's grid, one "
							"bounce more each frame, converging on every bounce "
							"on a still scene.")))),

				Field<&PostSettings::GiDenoise>("GiDenoise",
					Named("GI denoise", OnlyWhen(GiDialsApply,
						Slider(0.0f, 0.98f,
							"How much of last frame's bounce survives into "
							"this one, converging the traced form's four rays "
							"a pixel; 0 turns the accumulation off exactly. "
							"Reprojected through the motion vectors, so it "
							"works under every anti-aliasing mode rather than "
							"only under TAA. Ray-traced GI only: the "
							"screen-space gather reads the lit image and "
							"would compound its own output.")))),

				// --- SSAO. ENGINE-NOTES 7ac -----------------------------------
				Field<&PostSettings::AmbientOcclusion>("AmbientOcclusion",
					Named("Ambient occlusion", DisabledWhen(RayOcclusionTakesOver,
						"Ray-traced ambient occlusion is on in Render Settings and is used instead; "
						"the radius and intensity below still apply.",
						WasBool((int)AoDetail::Half, Enum(kAoDetailNames,
						"Contact shadowing from the depth buffer: creases, "
						"corners and the seam where things meet the ground "
						"darken. Applied over the lit image, so treat it as "
						"shadowing and keep the intensity restrained rather "
						"than expecting global illumination." "\n\n"
						"Half computes it at half the frame's resolution and "
						"upsamples, which is what this did when it was a "
						"checkbox and is right for almost everything: occlusion "
						"is low frequency. Full resolves creases a pixel wide, "
						"and costs about four times as much."))))),

				Field<&PostSettings::AoRadius>("AoRadius",
					Named("AO radius", OnlyWhen(AoDialsApply,
						Drag(0.02f, 0.05f, 4.0f,
							"World metres the occlusion hemisphere reaches. "
							"Small is crease darkening; large is soft "
							"room-scale shading. Under ray-traced occlusion, "
							"how far each ray goes.")))),

				Field<&PostSettings::AoIntensity>("AoIntensity",
					Named("AO intensity", OnlyWhen(AoDialsApply,
						Slider(0.0f, 4.0f,
							"An exponent on the occlusion: open surfaces stay "
							"untouched at any setting, and only how dark the "
							"dark end goes changes.")))),

				// --- motion blur. ENGINE-NOTES 7ab ----------------------------
				Field<&PostSettings::MotionBlur>("MotionBlur",
					Named("Motion blur", FieldHint{ .Tooltip =
						"Smears along the motion vectors the scene already "
						"writes for TAA. Objects smear over what they pass -- "
						"the blur happens where the motion lands, not only "
						"inside the mover -- and the camera's own turn smears "
						"the sky. Skinned meshes smear by the object's motion, "
						"not the limb's." })),

				Field<&PostSettings::MotionBlurShutter>("MotionBlurShutter",
					Named("Shutter", OnlyWhen(MotionBlurOn,
						Slider(0.0f, 1.0f,
							"Fraction of the frame the virtual shutter is open. "
							"0.5 is the 180-degree default every film camera "
							"also defaults to; 1.0 smears each frame into the "
							"next with nothing crisp between.")))),

				Field<&PostSettings::MotionBlurMaxRadius>("MotionBlurMaxRadius",
					Named("Max smear", OnlyWhen(MotionBlurOn,
						Drag(0.25f, 4.0f, 64.0f,
							"Ceiling on the smear in pixels, and also the tile "
							"size the dominant motion is tracked at -- a blur "
							"that reaches further than its tiles can see tears "
							"at their boundaries.")))),

				// --- lens and film, in the order they run ---------------------
				Field<&PostSettings::ChromaticAberration>("ChromaticAberration",
					Named("Aberration", Drag(0.0002f, 0.0f, 0.05f,
						"Lateral dispersion, as a fraction of the frame's width. "
						"Three taps of the scene at three offsets, on linear "
						"light, because a lens disperses before the sensor sees "
						"anything. The bloom is deliberately not dispersed -- it "
						"is blurred wider than any sane offset."))),

				Field<&PostSettings::VignetteIntensity>("VignetteIntensity",
					Named("Vignette", Slider(0.0f, 1.0f,
						"How dark the corners go. Applied *before* the tone "
						"curve, so it behaves like less light reaching the "
						"corner rather than a shadow painted over the result."))),

				Field<&PostSettings::VignetteSmoothness>("VignetteSmoothness",
					Named("Vignette falloff", OnlyWhen(VignetteOn,
						Slider(0.05f, 1.0f,
							"How gradually it arrives. Low is a hard circle; high "
							"is a slow darkening that reaches most of the frame.")))),

				Field<&PostSettings::FilmGrain>("FilmGrain",
					Named("Film grain", Slider(0.0f, 1.0f,
						"Applied last, after the tone curve and after the LUT: "
						"grain is the texture of the recording medium, not a "
						"colour anybody graded. Two octaves of value noise, so "
						"it is round clumps rather than a grid of squares, and "
						"it peaks in the midtones the way a stock does. "
						"Animated, and seeded from the frame number rather "
						"than a clock, so a screenshot of frame 30 is the same "
						"picture every time."))),

				Field<&PostSettings::FilmGrainSize>("FilmGrainSize",
					Named("Grain size", OnlyWhen(GrainOn,
						Drag(0.02f, 1.0f, 8.0f,
							"Pixels per speck -- the period of the noise "
							"lattice. Larger reads as a faster stock. Below "
							"about 2 the finest octave is past what the pixel "
							"grid can resolve, so it sharpens into noise "
							"instead of showing specks.")))),
			};
		}

		// The knobs, in the order they are applied. The inspector draws a
		// registry in order, so the panel reads as the pipeline runs --
		// somebody chasing an unexpected result can follow the rows downward.
		// ENGINE-NOTES 7v.
		std::vector<FieldDesc> BuildMaterial()
		{
			// Shown only once the feature is on: three numbers that mean
			// nothing while it is off is exactly the clutter OnlyWhen exists
			// for.
			auto tilingOn = [](const void* block)
			{
				return static_cast<const Assets::MaterialDesc*>(block)->StochasticTiling;
			};

			return {
				// --- the extended lobes ---------------------------------
				//
				// Nested, through MaterialDesc::Params: these are GPU block
				// fields, but they are also the four decisions that say what
				// kind of surface this is, so they belong in front of whoever
				// is choosing.
				Field<&Assets::MaterialDesc::Params, &MaterialParams::Clearcoat>("Clearcoat",
					Named("Clearcoat", Slider(0.0f, 1.0f,
						"A thin smooth layer over the base, with its own fixed "
						"4% reflectance: car lacquer, varnish, a wet surface. "
						"You get two highlights -- a sharp one from the coat and "
						"a broader one from what is under it. The base is "
						"attenuated by what the coat reflects, so this "
						"redistributes light rather than adding it."))),

				Field<&Assets::MaterialDesc::Params, &MaterialParams::ClearcoatRoughness>("ClearcoatRoughness",
					Named("Clearcoat roughness", Slider(0.0f, 1.0f,
						"How polished the coat is, independently of the surface "
						"beneath. Weathered lacquer over smooth paint is the "
						"case this exists for."))),

				Field<&Assets::MaterialDesc::Params, &MaterialParams::Anisotropy>("Anisotropy",
					Named("Anisotropy", Slider(-1.0f, 1.0f,
						"Stretches the highlight along the surface's u "
						"direction, or across it below zero. Brushed metal, "
						"hair, a vinyl record. Zero is the ordinary round "
						"highlight. Water has its own anisotropic lobe and does "
						"not use this one."))),

				Field<&Assets::MaterialDesc::Params, &MaterialParams::Subsurface>("Subsurface",
					Named("Subsurface", Slider(0.0f, 1.0f,
						"Lets the diffuse term reach past the terminator, which "
						"is the soft edge skin, marble, wax and leaves have. "
						"**A wrap, not a diffusion profile**: it will not carry "
						"light through a thin object, so it gets the soft "
						"terminator and not the glow of a hand over a torch."))),

				Field<&Assets::MaterialDesc::Params, &MaterialParams::SheenColor>("SheenColor",
					Named("Sheen colour", Color(
						"The retroreflective rim cloth has at grazing angles: "
						"velvet, felt, dust lying on a surface. Uses the Charlie "
						"distribution, which rises at the silhouette where GGX "
						"falls to nothing -- which is why velvet is brightest at "
						"its edge. Black is off."))),

				Field<&Assets::MaterialDesc::Params, &MaterialParams::SheenRoughness>("SheenRoughness",
					Named("Sheen roughness", Slider(0.05f, 1.0f,
						"How spread the fuzz is. Low is a tight rim, high is a "
						"broad bloom across the whole surface."))),

				Field<&Assets::MaterialDesc::StochasticTiling>("StochasticTiling",
					Named("Stochastic tiling",
						Tip("Synthesise a larger, non-repeating tile from this "
							"material's maps. A tiling texture repeats, and past "
							"a certain range the eye reads the repeat rather "
							"than the tile: the same features in the same "
							"places, over and over. Macro variation modulates "
							"that and cannot remove it. This replaces the maps "
							"with a synthesised tile that does not repeat "
							"within itself. Cached on disk, so it costs nothing "
							"at draw time. Divide Tiling by the scale as well, "
							"or the bigger tile is only a sharper one repeating "
							"just as often."))),

				Field<&Assets::MaterialDesc::TilingScale>("TilingScale",
					Named("Tile scale", OnlyWhen(tilingOn,
						Slider(2, 8,
							"How many times larger the synthesised tile is, per "
							"axis. 4 turns a 1K source into 4K: sixteen times "
							"the memory, and the repeat four times further "
							"away.")))),

				Field<&Assets::MaterialDesc::TilingCells>("TilingCells",
					Named("Lattice cells", OnlyWhen(tilingOn,
						Slider(2, 32,
							"Lattice cells across the output. The random "
							"offsets wrap at this, which is what keeps the "
							"result seamless; more cells blends smaller pieces "
							"of the source more often.")))),

				Field<&Assets::MaterialDesc::RegenerateTiling>("RegenerateTiling",
					Named("Regenerate", OnlyWhen(tilingOn,
						Tip("Synthesise again. The result is cached against the "
							"source maps and the numbers above, so it runs once "
							"and a changed source invalidates it by itself. "
							"This is for the case a stored answer cannot "
							"detect.")))),
			};
		}

		std::vector<FieldDesc> BuildLutRecipe()
		{
			return {
				Field<&Assets::LutRecipe::Temperature>("Temperature",
					Drag(0.005f, -1.0f, 1.0f,
						"Warm above zero, cool below. A look tweak on already "
						"displayed colours rather than a white balance on sensor "
						"data, so the range is deliberately gentle.")),

				Field<&Assets::LutRecipe::Tint>("Tint",
					Drag(0.005f, -1.0f, 1.0f,
						"Magenta above zero, green below. The other axis of white "
						"balance, and the one that rescues a grade that has gone "
						"subtly sickly rather than subtly wrong.")),

				Field<&Assets::LutRecipe::Lift>("Lift",
					Named("Lift", Drag(0.005f, -1.0f, 1.0f,
						"Moves the black end without touching white: at full white "
						"the term is zero on every channel. Raising it is what "
						"gives a grade the washed, filmic shadow."))),

				Field<&Assets::LutRecipe::Gamma>("Gamma",
					Named("Gamma", Drag(0.01f, 0.05f, 4.0f,
						"Bends everything between black and white, leaving both "
						"ends where they are. Per channel, so it is also the "
						"finest control over a colour cast in the midtones."))),

				Field<&Assets::LutRecipe::Gain>("Gain",
					Named("Gain", Drag(0.01f, 0.0f, 4.0f,
						"Scales the highlights. Per channel, and the one to reach "
						"for when the whites are the wrong colour."))),

				Field<&Assets::LutRecipe::Contrast>("Contrast",
					Drag(0.005f, 0.0f, 4.0f,
						"About a 0.5 pivot, which is mid-grey in the encoded values "
						"this operates on. Applied after gain and before "
						"saturation -- that order is what makes these behave the "
						"way a colourist expects.")),

				Field<&Assets::LutRecipe::Saturation>("Saturation",
					Drag(0.005f, 0.0f, 4.0f,
						"Toward Rec.709 luma: 0 is monochrome, 1 untouched. The "
						"same weights the tone curve uses, so a desaturated colour "
						"does not shift hue on its way to grey.")),

				Field<&Assets::LutRecipe::Size>("Size",
					Named("Table size", Drag(0.2f, 2, 64,
						"Entries per axis in the baked table. 33 is what grading "
						"tools emit most often and is odd, so the middle of the "
						"range lands on an entry rather than between two. Larger "
						"is a bigger texture for a difference nobody has "
						"measured."))),
			};
		}

		std::vector<FieldDesc> BuildSceneEnvironment()
		{
			return {
				Field<&SceneEnvironment::AmbientColor>("AmbientColor", Named("Ambient colour", Color())),
				Field<&SceneEnvironment::AmbientIntensity>("AmbientIntensity",
					Drag(0.005f, 0.0f, 4.0f,
						"A single colour arriving from every direction. It cannot "
						"vary with view angle or roughness the way a real "
						"environment does -- image-based lighting replaces it, and "
						"falls back to it for scenes with no environment map. Set "
						"it to 0 for pure direct lighting.")),

				Field<&SceneEnvironment::Sky>("Sky",
					Named("Background", Enum(kSkyNames,
						 "Colour draws nothing and leaves the clear colour, which is "
						 "what a 2D or UI-only scene wants. Gradient costs no asset. "
						 "An environment map is a panorama -- .hdr for values "
						 "brighter than white -- or one face of a six-file set."))),

				Field<&SceneEnvironment::SkyHorizon>("SkyHorizon",
					Named("Horizon", OnlyWhen(SkyIsGradient, Color()))),
				Field<&SceneEnvironment::SkyZenith>("SkyZenith",
					Named("Zenith", OnlyWhen(SkyIsGradient, Color()))),
				Field<&SceneEnvironment::SkyGround>("SkyGround",
					Named("Ground", OnlyWhen(SkyIsGradient, Color()))),

				Field<&SceneEnvironment::SkyIntensity>("SkyIntensity",
					Named("Intensity", OnlyWhen(SkyIsDrawn, Drag(0.01f, 0.0f, 16.0f)))),

				Field<&SceneEnvironment::SkyRotation>("SkyRotation",
					Named("Rotation", OnlyWhen(SkyIsCubemap,
						Tip("A panorama points wherever it was shot, and the scene "
							"was not built to match it.", Degrees())))),

				// Registered, unlike before, so the scene's own writer is
				// registry-driven all the way through rather than
				// registry-driven except for the one field it wrote by hand.
				// A script setting it by number would be setting a handle,
				// which is why the interop layer refuses Asset fields rather
				// than this list omitting it.
				Field<&SceneEnvironment::SkyTexture>("SkyTexture",
					Named("Environment map",
						OnlyWhen(SkyIsCubemap, AssetRef(AssetType::Texture)))),

				// --- shaped sky (WR-1) ------------------------------------------
				Field<&SceneEnvironment::SkyCurve>("SkyCurve",
					Named("Sky curve", OnlyWhen(SkyIsGradient, Drag(0.01f, 0.05f, 2.0f,
						"How fast the gradient crowds toward the horizon. 1 is a "
						"straight ramp; under 1 holds a bright band low in the sky "
						"instead of spreading it evenly to the zenith -- what a "
						"real night sky's glow looks like. 0.45 is the curve every "
						"scene used before this field existed.")))),

				Field<&SceneEnvironment::CityGlowColor>("CityGlowColor",
					Named("City glow colour", OnlyWhen(SkyIsGradient, Color(
						"An additive lobe low in the sky, toward CityGlowBearing -- "
						"a city's light pollution. Black adds nothing.")))),
				Field<&SceneEnvironment::CityGlowBearing>("CityGlowBearing",
					Named("City glow bearing", OnlyWhen(SkyIsGradient, Drag(1.0f, 0.0f, 360.0f,
						"Degrees, this scene's compass: 0 is south (+z), 90 is "
						"west (+x) -- sun_rotation's convention.")))),
				Field<&SceneEnvironment::CityAzimuthK>("CityAzimuthK",
					Named("City glow width", OnlyWhen(SkyIsGradient, Drag(0.1f, 1.0f, 32.0f,
						"How tightly the glow hugs its bearing. Higher is "
						"narrower.")))),
				Field<&SceneEnvironment::CityElevationK>("CityElevationK",
					Named("City glow height", OnlyWhen(SkyIsGradient, Drag(0.1f, 1.0f, 32.0f,
						"How fast the glow fades with elevation. Higher stays "
						"lower in the sky.")))),

				Field<&SceneEnvironment::MoonColor>("MoonColor",
					Named("Moon colour", OnlyWhen(SkyIsGradient, Color(
						"The disc's own colour, pre-exposure -- set it high enough "
						"and it clips into bloom. Black draws no moon.")))),
				Field<&SceneEnvironment::MoonElevation>("MoonElevation",
					Named("Moon elevation", OnlyWhen(SkyIsGradient, Drag(0.5f, -90.0f, 90.0f,
						"Degrees over the horizon. This struct cannot see the "
						"scene's entities, so it does not know a moon light's own "
						"rotation -- set this to match it if the scene has one.")))),
				Field<&SceneEnvironment::MoonBearing>("MoonBearing",
					Named("Moon bearing", OnlyWhen(SkyIsGradient, Drag(1.0f, 0.0f, 360.0f,
						"Degrees, CityGlowBearing's compass.")))),
				Field<&SceneEnvironment::MoonDisc>("MoonDisc",
					Named("Moon angular size", OnlyWhen(SkyIsGradient, Drag(0.0001f, 0.0f, 0.05f,
						"Angular radius in radians. The real moon's is about "
						"0.0046.")))),

				Field<&SceneEnvironment::MoonTexture>("MoonTexture",
					Named("Moon texture", OnlyWhen(SkyIsGradient,
						Tip("An image of the moon's face, disc filling the "
							"frame with alpha outside the limb. Unset, the "
							"disc is drawn analytically instead -- which "
							"reads as a sun, not a moon.",
							AssetRef(AssetType::Texture))))),

				Field<&SceneEnvironment::SkyDither>("SkyDither",
					Named("Sky dither", OnlyWhen(SkyIsGradient,
						FieldHint{ .Tooltip =
							"Triangular noise on the gradient to hide banding. Off "
							"by default: a first version dithered before Exposure "
							"and the tone curve, which amplify a night sky's dark "
							"values hardest -- measured near 10x too strong there. "
							"On compensates for a typical night sky's brightness, "
							"not any brightness; judge it by eye against banding, "
							"not by whether the number feels right." }))),
			};
		}

		// Built on first use, so none of them depends on the order two
		// translation units' globals happen to initialise in.
		const FieldDesc* FindIn(const std::vector<FieldDesc>& fields, const std::string& name)
		{
			for (const auto& field : fields)
			{
				if (name == field.Name)
					return &field;
			}
			return nullptr;
		}
	}

	const std::vector<FieldDesc>& RenderSettingsRegistry::Fields()
	{
		static const std::vector<FieldDesc> fields = BuildRenderSettings();
		return fields;
	}

	const FieldDesc* RenderSettingsRegistry::Find(const std::string& name)
	{
		return FindIn(Fields(), name);
	}

	const std::vector<FieldDesc>& PostSettingsRegistry::Fields()
	{
		static const std::vector<FieldDesc> fields = BuildPostSettings();
		return fields;
	}

	const FieldDesc* PostSettingsRegistry::Find(const std::string& name)
	{
		return FindIn(Fields(), name);
	}

	const std::vector<FieldDesc>& MaterialRegistry::Fields()
	{
		static const std::vector<FieldDesc> fields = BuildMaterial();
		return fields;
	}

	const FieldDesc* MaterialRegistry::Find(const std::string& name)
	{
		return FindIn(Fields(), name);
	}

	const std::vector<FieldDesc>& LutRecipeRegistry::Fields()
	{
		static const std::vector<FieldDesc> fields = BuildLutRecipe();
		return fields;
	}

	const FieldDesc* LutRecipeRegistry::Find(const std::string& name)
	{
		return FindIn(Fields(), name);
	}

	const std::vector<FieldDesc>& SceneEnvironmentRegistry::Fields()
	{
		static const std::vector<FieldDesc> fields = BuildSceneEnvironment();
		return fields;
	}

	const FieldDesc* SceneEnvironmentRegistry::Find(const std::string& name)
	{
		return FindIn(Fields(), name);
	}

	SettingsLookup FindSetting(const std::string& name)
	{
		if (const FieldDesc* field = RenderSettingsRegistry::Find(name))
			return { SettingsBlock::Render, field };
		if (const FieldDesc* field = PostSettingsRegistry::Find(name))
			return { SettingsBlock::Post, field };
		if (const FieldDesc* field = SceneEnvironmentRegistry::Find(name))
			return { SettingsBlock::SceneEnvironment, field };
		return {};
	}
}
