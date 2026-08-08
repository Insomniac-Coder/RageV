#pragma once
#include <string>
#include <vector>
#include <type_traits>
#include <glm/glm.hpp>
#include "RageV/Asset/Asset.h"
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
	// This is a purpose-built registry rather than entt::meta. EnTT identifies
	// data members by hashed id, not by string, so a serializer needs the name
	// stored alongside as a property -- at which point entt::meta is holding
	// data it was handed rather than providing any. A typed descriptor avoids
	// the meta_any round-trips and is what the C# layer will want anyway.

	enum class FieldType
	{
		Bool, Int, Float, Vec3, Vec4, String, Enum, Asset
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
	};

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
			if constexpr (std::is_same_v<T, AssetHandle>)      return FieldType::Asset;
			else if constexpr (std::is_same_v<T, bool>)        return FieldType::Bool;
			else if constexpr (std::is_enum_v<T>)              return FieldType::Enum;
			else if constexpr (std::is_same_v<T, int>)         return FieldType::Int;
			else if constexpr (std::is_same_v<T, float>)       return FieldType::Float;
			else if constexpr (std::is_same_v<T, glm::vec3>)   return FieldType::Vec3;
			else if constexpr (std::is_same_v<T, glm::vec4>)   return FieldType::Vec4;
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
		field.DisplayName = HumanFieldName(name);
		field.Type = Detail::TypeOf<typename Traits::Type>();
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
		field.DisplayName = HumanFieldName(name);
		field.Type = Detail::TypeOf<typename InnerTraits::Type>();
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
}
