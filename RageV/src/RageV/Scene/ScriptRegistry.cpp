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
		// Ordered, and a function-local static: registration happens from other
		// translation units before main, and a namespace-scope container may
		// not be constructed yet when the first one runs.
		std::map<std::string, ScriptRegistry::Factory>& Factories()
		{
			static std::map<std::string, ScriptRegistry::Factory> factories;
			return factories;
		}

		std::map<std::string, std::vector<ScriptField>>& Fields()
		{
			static std::map<std::string, std::vector<ScriptField>> fields;
			return fields;
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

		Factories()[name] = std::move(factory);
		return Registration(name);
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

		return it->second();
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
		for (const auto& [name, factory] : Factories())
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
