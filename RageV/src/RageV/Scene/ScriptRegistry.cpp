#include <rvpch.h>
#include "ScriptRegistry.h"
#include "RageV/Core/Log.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>

namespace RageV
{
	// Defined in Scripts/BuiltinScripts.cpp. Declared here so that translation
	// unit is referenced, and therefore linked.
	void RegisterBuiltinScripts();

	namespace
	{
		// A factory and where it came from. Scope 0 is the engine itself --
		// everything compiled in -- and each loaded game module gets its own,
		// so unloading one can remove exactly its scripts.
		struct FactoryEntry
		{
			ScriptRegistry::Factory Make;
			int Scope = 0;
		};

		// Ordered, and a function-local static: registration happens from other
		// translation units before main, and a namespace-scope container may
		// not be constructed yet when the first one runs.
		std::map<std::string, FactoryEntry>& Factories()
		{
			static std::map<std::string, FactoryEntry> factories;
			return factories;
		}

		// The open scope, if any. Not a stack: one module loads at a time, on
		// one thread, and pretending otherwise would be complexity for a
		// situation the design rules out.
		int& CurrentScope()
		{
			static int scope = 0;
			return scope;
		}

		std::map<std::string, std::vector<ScriptField>>& Fields()
		{
			static std::map<std::string, std::vector<ScriptField>> fields;
			return fields;
		}

		std::map<std::string, std::vector<ScriptMethod>>& Methods()
		{
			static std::map<std::string, std::vector<ScriptMethod>> methods;
			return methods;
		}

		// On demand, so no caller has to remember it -- the same reason
		// ComponentRegistry initialises itself.
		void EnsureBuiltins()
		{
			static bool done = false;
			if (done)
				return;

			// Set first: RegisterBuiltinScripts calls Register, which comes
			// back through here.
			done = true;
			RegisterBuiltinScripts();
		}
	}

	ScriptRegistry::Registration ScriptRegistry::Register(const std::string& name, Factory factory)
	{
		if (!factory)
			return Registration(name);

		// Two scripts under one name would make which of them a scene loads
		// depend on link order.
		if (Factories().count(name))
		{
			RV_CORE_WARN("Script '{0}' is registered twice; the first registration wins", name);
			return Registration(name);
		}

		Factories()[name] = { std::move(factory), CurrentScope() };
		return Registration(name);
	}

	int ScriptRegistry::BeginModuleScope()
	{
		// Monotonic, never reused: a stale scope id held by anyone can then
		// only ever unregister nothing, rather than somebody else's scripts.
		static int nextScope = 0;
		CurrentScope() = ++nextScope;
		return CurrentScope();
	}

	void ScriptRegistry::EndModuleScope()
	{
		CurrentScope() = 0;
	}

	size_t ScriptRegistry::UnregisterScope(int scope)
	{
		if (scope == 0)
			return 0;   // the engine's own scripts are not a thing to unregister

		size_t removed = 0;
		for (auto it = Factories().begin(); it != Factories().end();)
		{
			if (it->second.Scope == scope)
			{
				// The fields and methods go with the factory: all of them hold
				// lambdas into the same module the factory points at, and one
				// left behind is a call into unmapped memory the next time
				// anything looks the script up.
				Fields().erase(it->first);
				Methods().erase(it->first);
				it = Factories().erase(it);
				removed++;
			}
			else
			{
				++it;
			}
		}
		return removed;
	}

	std::vector<std::string> ScriptRegistry::NamesInScope(int scope)
	{
		std::vector<std::string> names;
		for (const auto& [name, entry] : Factories())
		{
			if (entry.Scope == scope)
				names.push_back(name);
		}
		return names;
	}

	void ScriptRegistry::AddField(const std::string& script, ScriptField field)
	{
		std::vector<ScriptField>& fields = Fields()[script];

		// A field declared twice would appear twice in the inspector and the
		// second would win on load, which is the sort of thing that is obvious
		// once seen and invisible until then.
		const auto existing = std::find_if(fields.begin(), fields.end(),
			[&field](const ScriptField& seen) { return seen.Name == field.Name; });

		if (existing != fields.end())
		{
			RV_CORE_WARN("Script '{0}' declares field '{1}' twice", script, field.Name);
			return;
		}

		fields.push_back(std::move(field));
	}

	const std::vector<ScriptField>& ScriptRegistry::FieldsOf(const std::string& name)
	{
		EnsureBuiltins();

		static const std::vector<ScriptField> none;
		const auto it = Fields().find(name);
		return it == Fields().end() ? none : it->second;
	}

	void ScriptRegistry::AddMethod(const std::string& script, ScriptMethod method)
	{
		std::vector<ScriptMethod>& methods = Methods()[script];

		const auto existing = std::find_if(methods.begin(), methods.end(),
			[&method](const ScriptMethod& seen) { return seen.Name == method.Name; });

		// Two methods under one name would make which one a button calls depend
		// on registration order -- the same reason a duplicate script name is
		// refused rather than silently resolved.
		if (existing != methods.end())
		{
			RV_CORE_WARN("Script '{0}' declares method '{1}' twice", script, method.Name);
			return;
		}

		methods.push_back(std::move(method));
	}

	const std::vector<ScriptMethod>& ScriptRegistry::MethodsOf(const std::string& name)
	{
		EnsureBuiltins();

		static const std::vector<ScriptMethod> none;
		const auto it = Methods().find(name);
		return it == Methods().end() ? none : it->second;
	}

	ScriptableEntity* ScriptRegistry::Create(const std::string& name)
	{
		EnsureBuiltins();
		const auto it = Factories().find(name);
		if (it == Factories().end())
		{
			// A scene referring to a script that no longer exists loads with
			// that entity un-scripted rather than failing to load at all.
			RV_CORE_WARN("Scene references unknown script '{0}'", name);
			return nullptr;
		}

		return it->second.Make();
	}

	bool ScriptRegistry::IsRegistered(const std::string& name)
	{
		EnsureBuiltins();
		return Factories().count(name) > 0;
	}

	std::vector<std::string> ScriptRegistry::GetNames()
	{
		EnsureBuiltins();
		std::vector<std::string> names;
		names.reserve(Factories().size());
		for (const auto& [name, entry] : Factories())
			names.push_back(name);
		return names;
	}

	namespace Detail
	{
		// Invariant formatting throughout, for the same reason the managed side
		// uses the invariant culture: a scene written on a machine with a comma
		// decimal separator would otherwise load nowhere else, and that is a bug
		// nobody sees until somebody else opens the project.
		//
		// std::ostringstream is locale-sensitive by default, so these use
		// to_chars and the classic locale rather than the stream's.
		std::string ScriptFieldToText(bool value)  { return value ? "true" : "false"; }
		std::string ScriptFieldToText(int value)   { return std::to_string(value); }

		std::string ScriptFieldToText(float value)
		{
			// Shortest round-trippable form. std::to_string gives six decimals
			// and turns 1.2 into "1.200000", which then lives in the scene file
			// and in the inspector forever.
			char buffer[32];
			const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
			return error == std::errc{} ? std::string(buffer, end) : "0";
		}

		std::string ScriptFieldToText(const Vec2& value)
		{
			return ScriptFieldToText(value.x) + " " + ScriptFieldToText(value.y);
		}

		std::string ScriptFieldToText(const Vec3& value)
		{
			return ScriptFieldToText(value.x) + " " +
				   ScriptFieldToText(value.y) + " " +
				   ScriptFieldToText(value.z);
		}

		std::string ScriptFieldToText(const std::string& value) { return value; }

		void ScriptFieldFromText(const std::string& text, bool& out)
		{
			out = (text == "true" || text == "1");
		}

		void ScriptFieldFromText(const std::string& text, int& out)
		{
			std::from_chars(text.data(), text.data() + text.size(), out);
		}

		void ScriptFieldFromText(const std::string& text, float& out)
		{
			std::from_chars(text.data(), text.data() + text.size(), out);
		}

		void ScriptFieldFromText(const std::string& text, Vec2& out)
		{
			// Same rule as the Vec3 below: a half-typed value leaves the other
			// component alone rather than zeroing it.
			Vec3 wide(out.x, out.y, 0.0f);
			ScriptFieldFromText(text + " 0", wide);
			out = Vec2(wide.x, wide.y);
		}

		void ScriptFieldFromText(const std::string& text, Vec3& out)
		{
			// A malformed value leaves the component alone rather than zeroing
			// it: a half-typed number in the inspector should not silently
			// discard the other two axes.
			Vec3 parsed = out;
			size_t start = 0;
			for (int axis = 0; axis < 3; axis++)
			{
				while (start < text.size() && text[start] == ' ')
					start++;

				size_t end = text.find(' ', start);
				if (end == std::string::npos)
					end = text.size();
				if (start >= end)
					return;

				float value = 0.0f;
				const auto result = std::from_chars(text.data() + start, text.data() + end, value);
				if (result.ec != std::errc{})
					return;

				parsed[axis] = value;
				start = end;
			}
			out = parsed;
		}

		void ScriptFieldFromText(const std::string& text, std::string& out) { out = text; }
	}
}
