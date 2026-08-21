#include <rvpch.h>
#include "ECS.h"

#include <unordered_map>

namespace RageV::ECS
{
	// One numbering for the whole process, and the reason it lives in a .cpp.
	//
	// **Every module has to agree.** RageV.dll, the editor, the runtime, a
	// project's script module and the test tool all call
	// `Registry::Get<TransformComponent>`, and each one instantiates that
	// template itself. If the index came from a counter inside the template,
	// each module would start its own count and reach a different pool -- the
	// per-module-statics trap this codebase has already paid for once, with
	// ImGui.
	//
	// So the type's *hash* crosses the boundary (it is computed from a string
	// and is the same everywhere) and this function turns it into a dense index
	// that every module shares, because there is only one copy of this function
	// -- the engine library exports it and everything else links to it.
	//
	// The map is walked once per component type per module, at the first call.
	// After that each module's own static holds the answer, which is what keeps
	// the hot path down to a vector index.
	uint32_t RegisterComponentType(uint64_t hash)
	{
		static std::unordered_map<uint64_t, uint32_t> types;
		static uint32_t next = 0;

		auto found = types.find(hash);
		if (found != types.end())
			return found->second;

		const uint32_t index = next++;
		types.emplace(hash, index);
		return index;
	}
}
