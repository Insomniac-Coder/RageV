#pragma once

#include "ScriptGraph.h"

#include <filesystem>
#include <string>
#include <vector>

namespace RageV::Assets
{
	// Turns a `.rvgraph` into ordinary C# (8.10, ENGINE-NOTES 7bh).
	//
	// **This is the whole reason there is no third scripting runtime.** The
	// output is a `.g.cs` in the project's `Scripts/Generated/`, which the
	// SDK-style csproj already globs, which `dotnet build` already compiles,
	// and which the collectible load context already hot-reloads. A graph is
	// attached exactly as a hand-written script is, because
	// `ManagedScriptComponent` names a type and the generated class is a type.
	//
	// Nothing here evaluates a graph. If this file ever grows an interpreter,
	// the design in 7bh has been lost.
	struct GraphGenerateResult
	{
		// False when the graph does not generate. **No file is written in that
		// case** -- an empty `class Foo : Script` compiles, attaches, runs and
		// does nothing, which is 8.13's black frames one layer up (7bf).
		bool Ok = false;

		// The C#, when Ok. Deterministic: two runs over an unchanged graph
		// produce byte-identical text, because everything is walked in node-id
		// order (7bh, trap 1). Generated code that churns is generated code
		// nobody reads.
		std::string Source;

		// Everything wrong, worst first: the graph's own Validate() plus
		// anything only generation can see.
		std::vector<GraphIssue> Issues;

		bool HasErrors() const;
	};

	class ScriptGraphGenerator
	{
	public:
		// `className` is the C# type the graph becomes, which is the asset's
		// file stem. Rejected if it is not a legal identifier: a graph called
		// "My Thing.rvgraph" would otherwise emit a class nothing can name.
		static GraphGenerateResult Generate(const ScriptGraph& graph,
											const std::string& className);

		// Generates and writes `<scriptsRoot>/Generated/<className>.g.cs`.
		//
		// Writes nothing and returns false when the graph does not generate,
		// and **deletes a stale file** if one is there: a graph that has become
		// invalid must not leave last-good code compiled in and running, which
		// would be the engine quietly disagreeing with what the canvas shows.
		static bool GenerateToFile(const ScriptGraph& graph,
								   const std::string& className,
								   const std::filesystem::path& scriptsRoot,
								   std::vector<GraphIssue>& outIssues);

		// Every `.rvgraph` in the project, generated before a script build.
		// Returns false if any of them failed, having done the rest -- one bad
		// graph must not stop the others compiling.
		static bool GenerateAll(const std::filesystem::path& assetsRoot,
								const std::filesystem::path& scriptsRoot);

		// Whether `name` can be a C# type name.
		static bool IsIdentifier(const std::string& name);
	};
}
