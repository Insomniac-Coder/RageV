#pragma once

#include "ScriptGraph.h"

#include <filesystem>

namespace RageV::Assets
{
	// Reads and writes `.rvgraph`, the file behind AssetType::ScriptGraph.
	//
	// YAML like everything else authored here. It matters more than usual for
	// this one: a graph *generates source*, so the graph and the C# it produced
	// are both in the diff, and a reviewer can see that one explains the other.
	// That is half the argument for generating C# rather than running a graph
	// directly (ENGINE-NOTES 7bh).
	//
	// **Written in id order, always**, for the same reason the generator emits
	// in id order: two saves of an unchanged graph must produce identical text,
	// or every save churns the file and the diff stops being worth reading.
	class ScriptGraphSerializer
	{
	public:
		static bool Save(const ScriptGraph& graph, const std::filesystem::path& path);

		// False leaves `out` untouched, so a caller that pre-filled a default
		// keeps it rather than being handed an empty graph it has to detect.
		static bool Load(ScriptGraph& out, const std::filesystem::path& path);
	};
}
