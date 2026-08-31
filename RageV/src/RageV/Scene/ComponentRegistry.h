#pragma once
#include <string>
#include <vector>
#include <type_traits>
#include "RageV/Math/Math.h"
#include "RageV/Asset/Asset.h"
#include "RageV/Scene/EntityRef.h"
#include "yaml-cpp/yaml.h"

namespace RageV
{
	class Entity;

	// One description per component, read by three consumers: the serializer,
	// the inspector, and -- later -- the C# marshalling layer.
	//
	// The alternative was hand-writing each of those per component, which is
	// three places to forget when a field is added and the reason a light's
	// intensity could exist in the inspector and not on disk for months.
	//
	// A purpose-built registry rather than a general reflection system. The
	// reflection libraries that come with an ECS identify data members by
	// hashed id rather than by string, so a serializer needs the name stored
	// alongside as a property -- at which point the reflection is holding data
	// it was handed rather than providing any. A typed descriptor avoids the
	// type-erased round trips and is what the C# layer wants anyway.

	enum class FieldType
	{
		Bool, Int, Float, Vec2, Vec3, Vec4, String, Enum, Asset, Entity
	};

	// Decides whether a field applies in the component's current state.
	using FieldVisibility = bool (*)(const void*);

	// How the inspector should present a field. Serialization ignores all of
	// it: a hint changes the widget, never the stored value.
	struct FieldHint
	{
		enum class Widget { Default, Color, Slider, Drag, Degrees };

		Widget Kind = Widget::Default;
		float Min = 0.0f;
		float Max = 0.0f;
		float Speed = 0.1f;

		const char* const* EnumNames = nullptr;
		int EnumCount = 0;

		// A spelling an older file may carry, and what it means now. Renaming
		// an enumerator otherwise silently loses the field: the name no longer
		// matches, the ordinal parse fails on a word, and the value falls back
		// -- which is how "an enum a script set by name" became a bug in this
		// engine once already. One alias is enough for every rename so far;
		// the day a field needs two, this becomes a small table.
		const char* LegacyName = nullptr;
		int LegacyNameValue = 0;

		const char* Tooltip = nullptr;

		// Which kind of asset an Asset field accepts, so the content browser
		// can refuse a drop of the wrong type rather than storing a handle that
		// resolves to nothing.
		AssetType Accepts = AssetType::None;

		// For fields that only apply in some states -- cone angles on a spot
		// light, orthographic size on a perspective camera. Null means always.
		// The serializer deliberately ignores this: hiding a field must not
		// drop it from disk, or toggling a light's type would lose its cone.
		FieldVisibility VisibleIf = nullptr;
		// For fields something else has taken over -- the post profile's
		// screen-space reflections while the ray-traced ones run (ENGINE-NOTES
		// 7ao). Shown, greyed, with the note beneath: hidden means "does not
		// apply in this mode", disabled means "applies, and is being answered
		// elsewhere". The value is kept either way; the serializer ignores
		// this as it ignores VisibleIf.
		FieldVisibility DisabledIf = nullptr;
		const char* DisabledNote = nullptr;

		// Marks a String field as naming a *method*, on the entity referenced by
		// the sibling Entity field with this key.
		//
		// The inspector then offers a dropdown of what that entity's script
		// actually declares instead of a text box, which is the difference
		// between a binding you pick and a binding you spell correctly. Null
		// for every other string.
		//
		// A sibling *key* rather than a pointer or an index, because a
		// FieldDesc is built one at a time and cannot refer to a neighbour that
		// may not exist yet -- and because the key is the stable name the rest
		// of this file is already built on.
		const char* MethodsOn = nullptr;

		// Which enumerator a saved `true` means, for a field that used to be a
		// boolean.
		//
		// An enum reads from disk by name, so a file holding `true` matches
		// nothing and falls back to the default -- which for a field whose
		// default is Off is a silent downgrade of every existing project. The
		// mapping is stated per field rather than assumed, because it is not
		// the same one twice: ambient occlusion's `true` was half resolution.
		// -1 means the field was never a boolean.
		//
		// **Last on purpose.** Several fields in ComponentRegistry.cpp still
		// build a FieldHint positionally -- `FieldHint{ Widget::Default, 0, 0,
		// 0.1f, nullptr, 0, "tooltip" }` -- so a member inserted anywhere but
		// the end silently shifts what those arguments mean. Adding this one
		// after EnumCount put a tooltip into an int and stopped the build,
		// which is the good outcome; the bad one is a member whose type
		// happens to match.
		int LegacyTrue = -1;

		// Marks an Int field as **one of a fixed set of values**, labelled by
		// EnumNames, rather than a range.
		//
		// Distinct from FieldType::Enum, which stores an *index*: this stores
		// the value itself, so `MsaaSamples: 8` on disk still means eight
		// samples and `--msaa=8` still means what it says. Storing an index
		// would have quietly redefined every saved project and the command
		// line with it.
		//
		// It exists because a range is the wrong shape for some numbers. MSAA
		// sample counts are 2, 4 and 8 -- a bit flag in both graphics APIs,
		// not a count -- and offering a slider from 1 to 8 offered five, which
		// is two bits set, which lost the device. A widget that cannot express
		// the illegal value is a better guarantee than a check that rejects
		// it afterwards.
		const int* ChoiceValues = nullptr;
		int ChoiceCount = 0;

		// Marks an Int field as choosing an **animation clip of the mesh on
		// this same entity**, so the inspector offers their names instead of
		// asking for an index.
		//
		// The set is the model's own clips and not every animation in the
		// project, because a clip is authored against a skeleton: offering
		// another model's would be offering something that cannot play. -1 is
		// the bind pose and is always offered.
		//
		// A flag rather than a sibling key like MethodsOn, because the source
		// is not a sibling field -- it is the entity's MeshComponent, which
		// the inspector already has in hand.
		bool ClipList = false;

		// Overrides the label derived from the field's key.
		//
		// Derivation handles almost everything, but it can only work from the
		// key -- and the key is the serialized name, so fixing a label by
		// renaming it changes every scene file on disk. An acronym that is not
		// all-caps in the key reads as a word ("Fov" rather than "FOV"), and a
		// key kept for backward compatibility may not describe the field at
		// all. Null means derive it.
		const char* Label = nullptr;
	};

	// Reads better at a call site than a braced initialiser, and matches the
	// other hint helpers: Field<&C::M>("Fov", Named("Field of view"))
	inline FieldHint Named(const char* label, FieldHint hint = {})
	{
		hint.Label = label;
		return hint;
	}

	// The same shape, for a string field that names a method on the entity a
	// sibling field points at: BindsMethod("OnClickTarget", ...)
	inline FieldHint BindsMethod(const char* targetField, FieldHint hint = {})
	{
		hint.MethodsOn = targetField;
		return hint;
	}

	// And for an int field that indexes the entity's own animation clips.
	inline FieldHint PicksClip(FieldHint hint = {})
	{
		hint.ClipList = true;
		return hint;
	}

	// "CastShadows" to "Cast shadows".
	//
	// Derived rather than written out per field, because a second string per
	// field is a second thing to keep in step -- and the one that would rot is
	// the label, since nothing breaks when it is wrong.
	//
	// Runs of capitals are left alone, so PerspectiveFOV reads "Perspective
	// FOV" rather than "Perspective f o v".
	std::string HumanFieldName(const char* name);

	struct FieldDesc
	{
		// The serialized key. Never derived from the label and never changed:
		// this is what every saved scene on disk is keyed by.
		const char* Name = nullptr;

		// What the inspector shows. Filled from Name at registration.
		std::string DisplayName;

		FieldType Type = FieldType::Float;
		void* (*Access)(void*) = nullptr;
		FieldHint Hint;

		// sizeof the member this describes. Filled by Field<>, and checked by
		// the test suite against what each FieldType is read and written as --
		// which is what turns "an enum must be int-sized" from a rule people
		// have to know into one the build enforces. A descriptor assembled by
		// hand rather than through Field<> leaves this zero and is skipped.
		uint32_t Size = 0;
	};

	struct ComponentDesc
	{
		const char* Name = nullptr;         // "TransformComponent", the YAML key
		const char* DisplayName = nullptr;  // "Transform", the inspector header
		std::vector<FieldDesc> Fields;

		bool Removable = true;
		bool AddableFromMenu = true;

		// By value: an Entity is a handle pair, so taking it by reference only
		// stopped callers passing a temporary.
		void* (*TryGet)(Entity) = nullptr;
		void* (*Add)(Entity) = nullptr;
		void  (*Remove)(Entity) = nullptr;

		// Run after any field is written through reflection. Derived state that
		// used to hide behind a setter lives here -- the camera's cached
		// projection, for one.
		void (*OnChanged)(void*) = nullptr;

		// The narrow escape hatch, for data a field list cannot express. Only
		// MeshComponent uses it, for its material: materials become real assets
		// in phase 1 and this goes away with them.
		void (*SerializeExtra)(YAML::Emitter&, void*) = nullptr;
		void (*DeserializeExtra)(const YAML::Node&, void*) = nullptr;
	};

	// --- field construction --------------------------------------------------
	namespace Detail
	{
		template<typename T> struct MemberTraits;
		template<typename C, typename T> struct MemberTraits<T C::*>
		{
			using Class = C;
			using Type = T;
		};

		// Deduced from the member's actual type, so a float field cannot be
		// declared as a vec3 and quietly read four bytes past its end.
		template<typename T>
		constexpr FieldType TypeOf()
		{
			// EntityRef before AssetHandle would be equivalent -- they are
			// distinct types -- but the order is kept alphabetically unhelpful
			// and structurally deliberate: AssetHandle *is* UUID, so anything
			// that becomes an alias of UUID later lands in the Asset arm by
			// accident. EntityRef is a struct precisely so it cannot.
			if constexpr (std::is_same_v<T, AssetHandle>)      return FieldType::Asset;
			else if constexpr (std::is_same_v<T, EntityRef>)   return FieldType::Entity;
			else if constexpr (std::is_same_v<T, bool>)        return FieldType::Bool;
			else if constexpr (std::is_enum_v<T>)
			{
				// Every consumer of an Enum field -- the serializer, the undo
				// stack, the inspector's combo, the C# component bridge --
				// reads and writes it through an `int*`, because a field list
				// is type-erased and an enum's identity does not survive it.
				//
				// So a registered enum must be int-sized. Declare one
				// `: uint8_t` and it compiles, serializes, and writes three
				// bytes past its end into whatever member follows -- a
				// corruption with no error, in a component that looked fine
				// in the inspector. Caught here instead, at the registration
				// that would have caused it.
				//
				// The fix is always the same: drop the narrow underlying type.
				// A component's enum is one field on one instance, never a
				// packed array, so the three bytes buy nothing.
				static_assert(sizeof(T) == sizeof(int),
					"A registered enum field must be int-sized: reflection reads and "
					"writes it through an int*, so a narrower underlying type is a "
					"silent memory stomp. Remove the ': uint8_t' (or similar) from "
					"this enum's declaration.");
				return FieldType::Enum;
			}
			else if constexpr (std::is_same_v<T, int>)         return FieldType::Int;
			else if constexpr (std::is_same_v<T, float>)       return FieldType::Float;
			else if constexpr (std::is_same_v<T, Vec2>)   return FieldType::Vec2;
			else if constexpr (std::is_same_v<T, Vec3>)   return FieldType::Vec3;
			else if constexpr (std::is_same_v<T, Vec4>)   return FieldType::Vec4;
			else if constexpr (std::is_same_v<T, std::string>) return FieldType::String;
			else static_assert(sizeof(T) == 0, "No FieldType for this member type");
		}
	}

	// Plain member: Field<&TransformComponent::Position>("Position")
	//
	// A generated accessor rather than offsetof, which is only conditionally
	// supported once a component contains anything with a virtual destructor.
	template<auto Member>
	FieldDesc Field(const char* name, FieldHint hint = {})
	{
		using Traits = Detail::MemberTraits<decltype(Member)>;

		FieldDesc field;
		field.Name = name;
		field.DisplayName = hint.Label ? std::string(hint.Label) : HumanFieldName(name);
		field.Type = Detail::TypeOf<typename Traits::Type>();
		field.Size = (uint32_t)sizeof(typename Traits::Type);
		field.Access = [](void* component) -> void*
		{
			return &(static_cast<typename Traits::Class*>(component)->*Member);
		};
		field.Hint = hint;
		return field;
	}

	// Member of a member: Field<&LightComponent::Light, &Light::Type>("Type").
	// Components that wrap a data object -- LightComponent, CameraComponent --
	// would otherwise need their inner type flattened into them.
	template<auto Outer, auto Inner>
	FieldDesc Field(const char* name, FieldHint hint = {})
	{
		using OuterTraits = Detail::MemberTraits<decltype(Outer)>;
		using InnerTraits = Detail::MemberTraits<decltype(Inner)>;

		FieldDesc field;
		field.Name = name;
		field.DisplayName = hint.Label ? std::string(hint.Label) : HumanFieldName(name);
		field.Type = Detail::TypeOf<typename InnerTraits::Type>();
		field.Size = (uint32_t)sizeof(typename InnerTraits::Type);
		field.Access = [](void* component) -> void*
		{
			auto* owner = static_cast<typename OuterTraits::Class*>(component);
			return &((owner->*Outer).*Inner);
		};
		field.Hint = hint;
		return field;
	}

	class ComponentRegistry
	{
	public:
		// Idempotent, and called on demand by both accessors -- no caller has
		// to remember it.
		static void Init();

		static const std::vector<ComponentDesc>& All();
		static const ComponentDesc* Find(const std::string& name);
	};

	// --- the three settings blocks ------------------------------------------
	//
	// None of these is a component -- none belongs to an entity -- but each is
	// described exactly the way a component is, and for three separate
	// payoffs that all come from the one list.
	//
	// **Serialization.** `RenderSettings` goes into the `.rvproject`,
	// `PostSettings` into a `.rvpostprofile`, and `SceneEnvironment` into the
	// `.rage`. All three are written and read by `FieldSerializer`, so none of
	// them has a hand-written list that can drift from the struct. That
	// drift is not hypothetical: `TemporalFeedback` was registered,
	// inspectable, and in nobody's serializer, so it reset on every load and
	// three scene files that set it produced identical pictures.
	//
	// **The inspector**, for the post profile -- which is drawn from its
	// registry in two places at once, the camera that names it and the
	// Properties panel when the asset itself is selected. One list, so those
	// cannot disagree.
	//
	// **Scripts.** A C++ script can say `Project::Render().AA = ...`; C# has
	// no equivalent, because a struct cannot cross the interop boundary but a
	// name and a piece of text can. `FindSetting` below is what the bridge
	// asks, and a setting added to any of these three lists reaches C# with
	// no binding written. ENGINE-NOTES 7s.
	//
	// **Not everything in every struct is here.** The shadow map's resolution
	// and cascade count are absent from the render list: they reallocate
	// render targets, which is not a thing to do from a per-frame hook. They
	// are edited in the panel and stored in the project by their own path.

	// What the frame costs. Lives on the project.
	class RenderSettingsRegistry
	{
	public:
		static const std::vector<FieldDesc>& Fields();
		static const FieldDesc* Find(const std::string& name);
	};

	// What the frame looks like. Lives on a `.rvpostprofile` asset that a
	// camera points at, and is where every item in roadmap phase 9 lands.
	class PostSettingsRegistry
	{
	public:
		static const std::vector<FieldDesc>& Fields();
		static const FieldDesc* Find(const std::string& name);
	};

	// A surface. Lives on a `.rmat` asset that a mesh or a terrain layer points
	// at.
	//
	// **Only the fields that are not shading maths.** MaterialParams is a GPU
	// block, edited by whoever authors the texture set; what is here is the
	// handful of decisions a person makes *about* a material -- chiefly whether
	// its maps get synthesised into a larger non-repeating tile, which is a
	// property of the asset and not of any scene using it.
	class MaterialRegistry
	{
	public:
		static const std::vector<FieldDesc>& Fields();
		static const FieldDesc* Find(const std::string& name);
	};

	// How a look is *made*. Lives on a `.rvlut` asset, which a post profile's
	// Colour LUT row points at.
	//
	// Registered for the same three reasons the blocks above are -- one list
	// drives the inspector, the file and the script bridge -- but deliberately
	// **not** part of `FindSetting`. Those three are reachable from "the scene
	// and its primary camera", which is what a script has; a LUT recipe is
	// reachable only by handle, and offering it under a flat name would mean
	// answering "which one" with a guess.
	class LutRecipeRegistry
	{
	public:
		static const std::vector<FieldDesc>& Fields();
		static const FieldDesc* Find(const std::string& name);
	};

	// Where the frame is. Lives on the scene.
	class SceneEnvironmentRegistry
	{
	public:
		static const std::vector<FieldDesc>& Fields();
		static const FieldDesc* Find(const std::string& name);
	};

	// Which block owns a setting, for a caller that has a name and no idea
	// where it went.
	//
	// The C# surface is one flat namespace of setting names and stays that
	// way: a script asking for "Exposure" should not have to know that
	// exposure moved from the scene to a profile asset, any more than it has
	// to know the scene format's version number. The three registries share
	// one namespace, and a check asserts they do not collide -- two blocks
	// with the same key would make this function's answer depend on the order
	// it happens to look.
	enum class SettingsBlock { None, Render, Post, SceneEnvironment };

	struct SettingsLookup
	{
		SettingsBlock Block = SettingsBlock::None;
		const FieldDesc* Field = nullptr;

		explicit operator bool() const { return Field != nullptr; }
	};

	SettingsLookup FindSetting(const std::string& name);
}
