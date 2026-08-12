#pragma once
#include "ScriptableEntity.h"
#include "RageV/Math/Math.h"
#include <functional>
#include <string>
#include <vector>

namespace RageV
{
	// What kind of thing an editable script field is.
	//
	// The same five kinds C# exposes, deliberately: the inspector draws one set
	// of widgets and the scene stores one set of values, whichever language the
	// script is written in.
	enum class ScriptFieldKind
	{
		Bool, Int, Float, Vec3, String
	};

	// One editable field on a native script.
	//
	// Read and written as text, matching the managed side. That looks lazy for
	// C++ -- the type is known at compile time -- and it is what keeps a scene
	// file and an inspector widget from needing two implementations, one per
	// language, for the same five kinds of value.
	struct ScriptField
	{
		std::string Name;
		ScriptFieldKind Kind = ScriptFieldKind::Float;

		// The default, as the freshly constructed script has it. Captured at
		// registration from a throwaway instance, so a field written
		// `float Speed = 1.2f;` shows 1.2 rather than zero.
		std::string Default;

		std::function<std::string(const ScriptableEntity*)> Get;
		std::function<void(ScriptableEntity*, const std::string&)> Set;
	};

	// One method a scene file may name -- what a UI button's OnClick binds to.
	//
	// **Registered explicitly, and only for C++.** C# needs nothing like this:
	// reflection can enumerate a type's methods and call one by name, so a
	// managed handler works the moment it is written. C++ has no reflection at
	// all, so the name has to be given to the engine from outside the class,
	// exactly as a field's is.
	//
	// That asymmetry is real and is worth stating in both guides rather than
	// letting somebody discover it when their C++ handler does not appear in
	// the dropdown.
	//
	// **`void()` and nothing else.** A handler that took arguments would need
	// the scene file to store them and the inspector to edit them, which is a
	// small expression language nobody asked for; a return value would have
	// nowhere to go. The method reads whatever it needs off its own entity,
	// which is what the equivalent Unity handler does in practice.
	struct ScriptMethod
	{
		std::string Name;
		std::function<void(ScriptableEntity*)> Invoke;
	};

	// Maps a script's name to a way of constructing it, and to its fields.
	//
	// Without this, a script can only be attached from C++ with a compile-time
	// type, which means it cannot be chosen in the inspector and cannot be
	// written to a scene file -- a scene with scripted behaviour would lose all
	// of it on save. The name is the durable reference, exactly as an asset
	// handle is for assets.
	//
	// Renaming a registered script breaks scenes that reference it, the same
	// way renaming a C# class does in Unity. Registered names are API.
	class ScriptRegistry
	{
	public:
		using Factory = std::function<ScriptableEntity* ()>;

		// Returned by Register so fields can be declared in the same statement.
		//
		// Holds the script's *name* rather than a pointer into the registry:
		// the alternative dangles the moment another script registers and the
		// storage moves, which would happen at static-initialisation time and
		// be entertaining to debug.
		class Registration
		{
		public:
			explicit Registration(std::string name) : m_Name(std::move(name)) {}

			// Declares a field the inspector may edit.
			//
			//     RV_REGISTER_SCRIPT(Spinner).Field<&Spinner::Speed>("Speed");
			//
			// The member must be **public**. C++ has no reflection, so the
			// registration has to name the member from outside the class, and
			// naming a private one is not something the language allows without
			// a friend declaration in every script. C# has the opposite rule --
			// private fields are editable there -- because reflection can reach
			// them and requiring `public` would be telling people to write worse
			// C# for the inspector's benefit.
			template<auto Member>
			Registration& Field(const char* name);

			// Declares a method a scene may call by name -- a button's
			// OnClick, and anything later that binds behaviour from data.
			//
			//     RV_REGISTER_SCRIPT(Menu).Method<&Menu::StartGame>("StartGame");
			//
			// Public, and `void()`. See ScriptMethod for why both.
			//
			// The registered name is what the scene file stores, so it is API
			// in exactly the way the script's own name is: changing it breaks
			// every button already bound to it. Keeping it the same as the C++
			// method's name is the convention, and is what makes the two
			// possible to keep in step by eye.
			template<auto Fn>
			Registration& Method(const char* name);

		private:
			std::string m_Name;
		};

		static Registration Register(const std::string& name, Factory factory);
		static ScriptableEntity* Create(const std::string& name);
		static bool IsRegistered(const std::string& name);

		// Sorted, for the inspector's dropdown.
		static std::vector<std::string> GetNames();

		// Module scoping: every registration that runs while a scope is open is
		// tagged with it, which is what lets a game module be *unloaded*.
		//
		// A module's scripts register themselves from static initialisers
		// during LoadLibrary -- there is no list of them anywhere, so the only
		// way to know what a module added is to bracket the load and watch.
		// GameModule opens a scope, loads the DLL, closes the scope; on unload
		// it calls UnregisterScope, and every factory the module registered
		// leaves the map *before* FreeLibrary frees the code they point into.
		//
		// The order is the entire point. A factory outliving its module is a
		// function pointer into unmapped memory behind a map that still looks
		// correct, and the crash arrives later, from somewhere unrelated.
		static int  BeginModuleScope();
		static void EndModuleScope();
		static size_t UnregisterScope(int scope);

		// What a scope registered, for the load-time log -- "Sample.dll: 3
		// script(s)" is the line that says the module actually arrived.
		static std::vector<std::string> NamesInScope(int scope);

		// Empty for a script with no registered fields, which is most of them
		// and is not an error.
		static const std::vector<ScriptField>& FieldsOf(const std::string& name);

		// The same, for methods. Answers without an instance -- which is the
		// requirement, because the inspector fills its dropdown while the scene
		// is stopped and no script instance exists anywhere.
		static const std::vector<ScriptMethod>& MethodsOf(const std::string& name);

		// Used by Registration::Field and ::Method. Not meant to be called
		// directly.
		static void AddField(const std::string& script, ScriptField field);
		static void AddMethod(const std::string& script, ScriptMethod method);
	};

	namespace Detail
	{
		// Text conversion for the five supported kinds.
		//
		// Invariant formatting, for the same reason the managed side uses the
		// invariant culture: a scene written on a machine with a comma decimal
		// separator would otherwise load nowhere else.
		std::string ScriptFieldToText(bool value);
		std::string ScriptFieldToText(int value);
		std::string ScriptFieldToText(float value);
		std::string ScriptFieldToText(const Vec2& value);
		std::string ScriptFieldToText(const Vec3& value);
		std::string ScriptFieldToText(const std::string& value);

		void ScriptFieldFromText(const std::string& text, bool& out);
		void ScriptFieldFromText(const std::string& text, int& out);
		void ScriptFieldFromText(const std::string& text, float& out);
		void ScriptFieldFromText(const std::string& text, Vec2& out);
		void ScriptFieldFromText(const std::string& text, Vec3& out);
		void ScriptFieldFromText(const std::string& text, std::string& out);

		constexpr ScriptFieldKind KindOf(const bool*)        { return ScriptFieldKind::Bool; }
		constexpr ScriptFieldKind KindOf(const int*)         { return ScriptFieldKind::Int; }
		constexpr ScriptFieldKind KindOf(const float*)       { return ScriptFieldKind::Float; }
		constexpr ScriptFieldKind KindOf(const Vec3*)        { return ScriptFieldKind::Vec3; }
		constexpr ScriptFieldKind KindOf(const std::string*) { return ScriptFieldKind::String; }

		// Splits a pointer-to-member into the class and the member's type.
		template<typename T> struct ScriptMemberTraits;
		template<typename Class, typename Value>
		struct ScriptMemberTraits<Value Class::*>
		{
			using Owner = Class;
			using Type = Value;
		};

		// The same for a method, and specialised *only* for `void()`.
		//
		// That is the signature check, and it is deliberately done by having no
		// other specialisation to match: registering a method that takes an
		// argument or returns something fails to compile at the registration
		// line, naming the method. A runtime check would have reported it as a
		// button that quietly does nothing.
		template<typename T> struct ScriptMethodTraits;
		template<typename Class>
		struct ScriptMethodTraits<void (Class::*)()>
		{
			using Owner = Class;
		};

		// Registration runs before main, so a script is available without the
		// application having to remember to register it.
		struct ScriptRegistrar
		{
			template<typename Type>
			static ScriptRegistry::Registration Make(const std::string& name)
			{
				return ScriptRegistry::Register(
					name, []() -> ScriptableEntity* { return new Type(); });
			}
		};
	}

	template<auto Member>
	ScriptRegistry::Registration& ScriptRegistry::Registration::Field(const char* name)
	{
		using Traits = Detail::ScriptMemberTraits<decltype(Member)>;
		using Owner = typename Traits::Owner;
		using Value = typename Traits::Type;

		ScriptField field;
		field.Name = name;
		field.Kind = Detail::KindOf((const Value*)nullptr);

		field.Get = [](const ScriptableEntity* script) -> std::string
		{
			return Detail::ScriptFieldToText(static_cast<const Owner*>(script)->*Member);
		};

		field.Set = [](ScriptableEntity* script, const std::string& text)
		{
			Detail::ScriptFieldFromText(text, static_cast<Owner*>(script)->*Member);
		};

		// The default comes from a freshly constructed instance, so a field
		// written `float Speed = 1.2f;` shows 1.2 rather than whatever zero
		// happens to be. Reading it off the type would give zero and look
		// entirely plausible.
		{
			const Owner sample;
			field.Default = Detail::ScriptFieldToText(sample.*Member);
		}

		ScriptRegistry::AddField(m_Name, std::move(field));
		return *this;
	}

	template<auto Fn>
	ScriptRegistry::Registration& ScriptRegistry::Registration::Method(const char* name)
	{
		using Owner = typename Detail::ScriptMethodTraits<decltype(Fn)>::Owner;

		ScriptMethod method;
		method.Name = name;

		// The cast is safe because the only thing that ever calls this is the
		// registry, against an instance it created from this same script's
		// factory -- the same reasoning Field's accessors rest on.
		method.Invoke = [](ScriptableEntity* script)
		{
			(static_cast<Owner*>(script)->*Fn)();
		};

		ScriptRegistry::AddMethod(m_Name, std::move(method));
		return *this;
	}
}

// Put this at file scope in a script's .cpp:
//
//     RV_REGISTER_SCRIPT(Spinner);
//
// Or, to expose fields to the inspector:
//
//     RV_REGISTER_SCRIPT(Spinner).Field<&Spinner::Speed>("Speed");
//
// The name written to scene files is the type's own, so it must be unique
// across the project.
#define RV_REGISTER_SCRIPT(Type)                                              \
	static ::RageV::ScriptRegistry::Registration s_Register##Type =            \
		::RageV::Detail::ScriptRegistrar::Make<Type>(#Type)
