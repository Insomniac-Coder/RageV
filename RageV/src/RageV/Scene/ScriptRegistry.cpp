#include <rvpch.h>
#include "ScriptRegistry.h"
#include "RageV/Core/Log.h"
#include <algorithm>
#include <map>

namespace RageV
{
	namespace
	{
		// Ordered, and a function-local static: registration happens from other
		// translation units before main, and a namespace-scope container may
		// not be constructed yet when the first one runs.
		std::map<std::string, ScriptRegistry::Factory>& Factories()
		{
			static std::map<std::string, ScriptRegistry::Factory> factories;
			return factories;
		}
	}

	void ScriptRegistry::Register(const std::string& name, Factory factory)
	{
		if (!factory)
			return;

		// Two scripts under one name would make which of them a scene loads
		// depend on link order.
		if (Factories().count(name))
		{
			RV_CORE_WARN("Script '{0}' is registered twice; the first registration wins", name);
			return;
		}

		Factories()[name] = std::move(factory);
	}

	ScriptableEntity* ScriptRegistry::Create(const std::string& name)
	{
		const auto it = Factories().find(name);
		if (it == Factories().end())
		{
			// A scene referring to a script that no longer exists loads with
			// that entity un-scripted rather than failing to load at all.
			RV_CORE_WARN("Scene references unknown script '{0}'", name);
			return nullptr;
		}

		return it->second();
	}

	bool ScriptRegistry::IsRegistered(const std::string& name)
	{
		return Factories().count(name) > 0;
	}

	std::vector<std::string> ScriptRegistry::GetNames()
	{
		std::vector<std::string> names;
		names.reserve(Factories().size());
		for (const auto& [name, factory] : Factories())
			names.push_back(name);
		return names;
	}
}
