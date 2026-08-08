#pragma once
#include "ScriptableEntity.h"
#include <functional>
#include <string>
#include <vector>

namespace RageV
{
	// Maps a script's name to a way of constructing it.
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

		static void Register(const std::string& name, Factory factory);
		static ScriptableEntity* Create(const std::string& name);
		static bool IsRegistered(const std::string& name);

		// Sorted, for the inspector's dropdown.
		static std::vector<std::string> GetNames();
	};

	namespace Detail
	{
		// Registration runs before main, so a script is available without the
		// application having to remember to register it.
		struct ScriptRegistrar
		{
			ScriptRegistrar(const std::string& name, ScriptRegistry::Factory factory)
			{
				ScriptRegistry::Register(name, std::move(factory));
			}
		};
	}
}

// Put this at file scope in a script's .cpp:
//
//     RV_REGISTER_SCRIPT(Spinner);
//
// The name written to scene files is the type's own, so it must be unique
// across the project.
#define RV_REGISTER_SCRIPT(Type)                                              \
	static ::RageV::Detail::ScriptRegistrar s_Register##Type(                 \
		#Type, []() -> ::RageV::ScriptableEntity* { return new Type(); })
