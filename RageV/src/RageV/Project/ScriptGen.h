#pragma once

// Declaration-site script registration: the scanner behind rvgen.
//
// A native script used to end with a registration block --
//
//     RV_REGISTER_SCRIPT(Bell)
//         .Field<&Bell::Swing>("Swing")
//         .Method<&Bell::Ring>("Ring");
//
// -- which repeats every name twice and is exactly the kind of trailing
// ceremony that drifts from the class above it. The wanted shape is a marker
// on the declaration itself and nothing anywhere else:
//
//     RVShowInEditor
//     float Swing = 0.34f;
//
// C++ cannot do that in the language: a macro sitting in a class body does
// not know the class it is in, and the preprocessor cannot find out. So the
// markers expand to nothing (ScriptableEntity.h defines them empty) and this
// scanner reads the source *text*, finds the markers, and emits the
// registration code a person would have written. rvgen is the command-line
// tool over it; a project's Source/CMakeLists.txt runs it at configure time.
//
// **The emitted registration lives in a wrapper TU that #includes the marked
// source file.** Not a style choice: scripts here are ordinarily whole classes
// in a .cpp (Bell and Anvil are), and no separate file can name the members
// of a class it cannot see. The wrapper compiles the source file and appends
// the registrations, and the build compiles the file only through its
// wrapper. One wrapper per marked file, so file-local statics stay
// file-local.
//
// **This is a scanner, not a compiler.** It strips comments, strings and
// preprocessor lines, then tracks braces, namespaces, classes and access
// levels -- enough to attribute a marker to its class correctly, and to
// refuse loudly (file and line, MSVC diagnostic format) anything it does not
// understand. Refusing is load-bearing: a generator that guesses wrong or
// silently skips a field produces an inspector with a field missing and no
// error anywhere, which is the worst outcome available.
//
// Lives in the engine rather than in the tool so scenetest can drive it
// in-process -- the same arrangement ModuleBuild::ParseDiagnostic has, and
// for the same reason: the parsing is the only part that can quietly stop
// matching reality.

#include <filesystem>
#include <string>
#include <vector>

namespace RageV
{
	class ScriptGen
	{
	public:
		// One marked field. The type is kept as written for error messages;
		// registration itself never needs it -- the member pointer in the
		// emitted code carries the real type, so a lie here cannot miscompile.
		struct Field
		{
			std::string Name;
			std::string Type;
			int Line = 0;
		};

		struct Method
		{
			std::string Name;
			int Line = 0;
		};

		// One class that will be registered.
		struct Script
		{
			std::string Name;        // registered name -- the class's own, scene-file API
			std::string Qualified;   // ::Ns::Class, for the emitted code
			int Line = 0;
			std::vector<Field> Fields;
			std::vector<Method> Methods;
		};

		// An RV_REGISTER_SCRIPT(Name) seen while scanning. Collected so the
		// tool can refuse a class registered both ways: the registry would
		// warn at load and first-wins, but a build-time error names the line.
		struct LegacyRegistration
		{
			std::string Name;
			int Line = 0;
		};

		struct FileScan
		{
			std::filesystem::path Path;
			std::vector<Script> Scripts;
			std::vector<LegacyRegistration> Legacy;
		};

		// A refusal, already located. Format() renders it as an MSVC
		// diagnostic -- `path(line): error RVGEN1: message` -- which is the
		// one shape ModuleBuild::ParseDiagnostic already lands in the
		// editor's build panel, so a generator error reads like any other
		// compile error.
		struct Error
		{
			std::filesystem::path File;
			int Line = 0;
			std::string Message;

			std::string Format() const;
		};

		// Scans one file's text. Errors accumulate; a file that errors still
		// returns whatever it understood, so one pass can report several
		// problems rather than the first.
		static FileScan Scan(const std::filesystem::path& file,
							 const std::string& text,
							 std::vector<Error>& errors);

		// The wrapper TU for one scanned file, or empty when it marked
		// nothing. Deterministic: same scan, same bytes.
		static std::string Emit(const FileScan& scan);

		// Whether the text mentions a marker at all, comments and strings
		// excluded. Cheap enough for ModuleBuild to run over every source on
		// every build: it is how a project whose CMakeLists predates rvgen
		// gets a warning instead of fields that silently never appear.
		static bool HasMarkers(const std::string& text);
	};
}
