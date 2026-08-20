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
	// What to do about a node this build cannot represent.
	//
	// `Strict` is the default and the answer for every automatic caller: the
	// generator, the asset cache, anything that runs without somebody
	// watching. `DropUnknown` exists for the *one* caller that has a person in
	// front of it and has told them what it costs -- the editor's "open
	// without it". A silent drop is what 10.10 removed; a chosen one is a
	// different thing.
	enum class GraphLoadMode
	{
		Strict,
		DropUnknown,
	};

	class ScriptGraphSerializer
	{
	public:
		static bool Save(const ScriptGraph& graph, const std::filesystem::path& path);

		// False leaves `out` untouched, so a caller that pre-filled a default
		// keeps it rather than being handed an empty graph it has to detect.
		//
		// **A graph is loaded whole or not at all** (10.10, ENGINE-NOTES 7bi).
		// A node whose type this build does not know used to be dropped with a
		// warning and the rest returned as if nothing had happened -- so the
		// graph generated *silently emptied*, and a save wrote the loss back
		// to disk. Partial is the one answer this cannot give: the file is the
		// user's work, and half of it is not a smaller version of it.
		//
		// `outError`, when given, receives the same sentence the log gets, for
		// a caller that has to put it in front of somebody. Nobody who just
		// double-clicked a file is reading the console.
		// `outMessage` carries why it was refused under Strict, and what was
		// dropped under DropUnknown -- in both cases the sentence a caller
		// puts in front of somebody.
		static bool Load(ScriptGraph& out, const std::filesystem::path& path,
						 std::string* outMessage = nullptr,
						 GraphLoadMode mode = GraphLoadMode::Strict);
	};
}
