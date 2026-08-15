// rvgen -- generates script registrations from declaration-site markers.
//
//     rvgen --source <project>/Source --out <module build dir>/rvgen
//
// A project's Source/CMakeLists.txt runs this at configure time. It scans the
// flat Source/ directory (the same *.cpp and *.h the build globs), and for
// every file that marks something it writes `<file>.rvgen.cpp` -- a wrapper
// that #includes the file and appends the registrations -- plus
// `rvgen.manifest.cmake`, which tells CMake which sources now compile through
// their wrappers and which generated files to add.
//
// **Failure is loud and total.** Any file the scanner refuses means nothing
// is written and the exit code fails the configure: a generator that shipped
// what it understood and skipped the rest would produce an inspector with a
// field quietly missing, which is worse than any error message. Diagnostics
// are MSVC-shaped so the editor's build panel parses them like compile
// errors.
//
// **Unchanged output is not rewritten.** This runs on every configure, and a
// rewritten-but-identical wrapper is a touched mtime, which is every wrapped
// script recompiling on every build for no reason. Content is compared first;
// stale wrappers whose markers went away are deleted.
//
// The scanning itself is RageV::ScriptGen, in the engine, where scenetest
// exercises it in-process. This file is only the directory walk and the
// manifest.

#include "RageV/Project/ScriptGen.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	std::string ReadFile(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream text;
		text << in.rdbuf();
		return text.str();
	}

	// Write only when the bytes differ -- see the header comment for why.
	bool WriteIfChanged(const fs::path& path, const std::string& content)
	{
		std::error_code ec;
		if (fs::exists(path, ec) && ReadFile(path) == content)
			return false;

		std::ofstream out(path, std::ios::binary);
		out << content;
		return true;
	}

	std::string CMakePath(const fs::path& path)
	{
		return path.generic_string();
	}
}

int main(int argc, char** argv)
{
	fs::path source;
	fs::path out;

	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (arg == "--source" && i + 1 < argc)       source = argv[++i];
		else if (arg == "--out" && i + 1 < argc)     out = argv[++i];
		else
		{
			std::cerr << "rvgen: unknown argument '" << arg << "'\n"
					  << "usage: rvgen --source <dir> --out <dir>\n";
			return 1;
		}
	}

	std::error_code ec;
	if (source.empty() || out.empty() || !fs::is_directory(source, ec))
	{
		std::cerr << "usage: rvgen --source <dir> --out <dir>\n";
		return 1;
	}

	// The same files the module's CMakeLists globs: flat, .cpp and .h. Sorted,
	// because directory order is the filesystem's mood and the manifest and
	// wrappers should not churn with it.
	std::vector<fs::path> files;
	for (const auto& entry : fs::directory_iterator(source, ec))
	{
		if (!entry.is_regular_file(ec))
			continue;
		const fs::path& path = entry.path();
		const std::string extension = path.extension().string();
		if (extension != ".cpp" && extension != ".h")
			continue;
		// Defensive: never scan our own output, wherever somebody put it.
		if (path.filename().string().find(".rvgen.") != std::string::npos)
			continue;
		files.push_back(fs::absolute(path));
	}
	std::sort(files.begin(), files.end());

	std::vector<RageV::ScriptGen::Error> errors;
	std::vector<RageV::ScriptGen::FileScan> scans;
	for (const fs::path& file : files)
		scans.push_back(RageV::ScriptGen::Scan(file, ReadFile(file), errors));

	// The cross-file versions of Scan's own checks: registered names are what
	// scene files store, so uniqueness is per project, not per file.
	for (size_t a = 0; a < scans.size(); a++)
	{
		for (const RageV::ScriptGen::Script& script : scans[a].Scripts)
		{
			for (size_t b = 0; b < scans.size(); b++)
			{
				if (a == b)
					continue;
				for (const RageV::ScriptGen::Script& other : scans[b].Scripts)
					if (other.Name == script.Name && a < b)
						errors.push_back({ scans[b].Path, other.Line,
							"two script classes are both named '" + script.Name
							+ "' (the other is in " + scans[a].Path.filename().string()
							+ "); registered names are what scene files store, "
							"so they must be unique" });
				for (const RageV::ScriptGen::LegacyRegistration& legacy : scans[b].Legacy)
					if (legacy.Name == script.Name)
						errors.push_back({ scans[a].Path, script.Line,
							"'" + script.Name + "' uses declaration-site markers, and "
							+ scans[b].Path.filename().string() + " line "
							+ std::to_string(legacy.Line) + " registers it with "
							"RV_REGISTER_SCRIPT; remove one -- the markers "
							"replace the block" });
			}
		}
	}

	if (!errors.empty())
	{
		for (const RageV::ScriptGen::Error& error : errors)
			std::cerr << error.Format() << "\n";
		return 1;
	}

	fs::create_directories(out, ec);

	std::vector<fs::path> wrapped;      // marked .cpps: compile through the wrapper
	std::vector<fs::path> generated;    // every wrapper TU
	size_t scripts = 0;

	for (const RageV::ScriptGen::FileScan& scan : scans)
	{
		const std::string emitted = RageV::ScriptGen::Emit(scan);
		if (emitted.empty())
			continue;

		const fs::path wrapper = out / (scan.Path.filename().string() + ".rvgen.cpp");
		WriteIfChanged(wrapper, emitted);
		generated.push_back(wrapper);

		// A marked header stays in the glob -- it is listed, not compiled --
		// but a marked .cpp compiling both directly and through its wrapper
		// would define everything in it twice.
		if (scan.Path.extension() == ".cpp")
			wrapped.push_back(scan.Path);

		scripts += scan.Scripts.size();
	}

	// Wrappers whose markers went away. An orphan is not harmless clutter: it
	// still #includes its source file, and anything that ever compiled it
	// again would register scripts the project deleted.
	for (const auto& entry : fs::directory_iterator(out, ec))
	{
		const fs::path& path = entry.path();
		if (path.filename().string().find(".rvgen.cpp") == std::string::npos)
			continue;
		if (std::find(generated.begin(), generated.end(), path) == generated.end())
			fs::remove(path, ec);
	}

	std::ostringstream manifest;
	manifest << "# Generated by rvgen -- which sources compile through wrapper TUs,\n";
	manifest << "# and which wrappers to compile. Regenerated on every configure.\n";
	manifest << "set(RVGEN_WRAPPED_SOURCES\n";
	for (const fs::path& path : wrapped)
		manifest << "    \"" << CMakePath(path) << "\"\n";
	manifest << ")\n";
	manifest << "set(RVGEN_GENERATED\n";
	for (const fs::path& path : generated)
		manifest << "    \"" << CMakePath(path) << "\"\n";
	manifest << ")\n";
	WriteIfChanged(out / "rvgen.manifest.cmake", manifest.str());

	std::cout << "rvgen: " << scripts << " script(s) across "
			  << generated.size() << " file(s), " << files.size() << " scanned\n";
	return 0;
}
