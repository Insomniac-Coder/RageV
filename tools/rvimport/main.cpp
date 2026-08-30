// Instantiate a model into a scene file, without the editor.
//
//     rvimport <model> [--out=<file>] [--project=<path>] [--rhi=vulkan|opengl]
//
// <model> is asset-relative: `models/showroom/porsche_992_gt3_r.glb`.
//
// **Why this exists.** A model with more than one material is not one entity.
// The importer splits it into a primitive per (mesh, material) pair, and the
// asset manager turns that into a tree -- a hundred and fifty entities for a
// car, each with its own mesh handle and its own `.rmat`. A scene generator
// written in Python can build a room, a light rig and a camera, and it cannot
// build that: the material files do not exist until something imports the
// model, and their handles are minted when they are written.
//
// So the generated scene and the imported model meet here. This writes the
// model's subtree in the same document shape a scene uses, once, and the
// generator splices it in -- which also means regenerating the room does not
// renumber a hundred and fifty entities that did not change.
//
// A device, because meshes upload as they are built. A hidden window, because
// the device needs a surface and GLFW's state lives in the engine DLL rather
// than here -- see the note in tools/scenetest for what a window created in
// this executable would do instead.

#include <rvpch.h>
#include "RageV/Core/Log.h"
#include "RageV/Core/Window.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Project/Project.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace RageV;

namespace
{
	void PrintUsage()
	{
		std::cout <<
			"rvimport -- instantiate a model into a scene file\n"
			"\n"
			"  rvimport <model> [options]\n"
			"\n"
			"  <model>            asset-relative path, e.g. models/car.glb\n"
			"\n"
			"  --out=<file>       where the subtree goes; stdout otherwise\n"
			"  --project=<path>   a .rvproject, or a folder holding one\n"
			"  --rhi=vulkan|opengl\n"
			"\n"
			"Writes the model's entity tree in a scene document's shape, with\n"
			"its materials created beside the model as .rmat assets.\n";
	}
}

int main(int argc, char** argv)
{
	Log::Init();

	std::string model;
	std::filesystem::path output;

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--help" || argument == "-h")
		{
			PrintUsage();
			return 0;
		}

		if (argument.rfind("--out=", 0) == 0)
			output = argument.substr(6);
		else if (argument.rfind("--", 0) == 0)
			continue;   // --project and --rhi are EngineConfig's
		else if (model.empty())
			model = argument;
	}

	if (model.empty())
	{
		PrintUsage();
		return 1;
	}

	// Backslashes to forward, since the registry keys on the latter and a
	// path typed on Windows arrives with the wrong ones.
	for (char& c : model)
	{
		if (c == '\\')
			c = '/';
	}

	// --project= and --rhi= are read here, so this tool resolves a project the
	// same way the editor and the runtime do.
	EngineConfig::Init(argc, argv);
	const EngineConfig& config = EngineConfig::Get();

	WindowProps props("rvimport", 320, 240);
	props.Visible = false;

	std::unique_ptr<Window> window(Window::Create(props));
	if (!window)
	{
		RV_CORE_ERROR("window creation failed");
		return 1;
	}

	RHI::DeviceDesc deviceDesc;
	deviceDesc.Backend = config.Backend;
	deviceDesc.Window = window->GetNativeWindow();
	deviceDesc.Width = 320;
	deviceDesc.Height = 240;
	deviceDesc.VSync = true;
	deviceDesc.FramesInFlight = 2;

	auto device = RHI::RHIDevice::Create(deviceDesc);
	if (!device)
	{
		RV_CORE_ERROR("device creation failed");
		return 1;
	}

	if (!Project::OpenConfiguredOrDefault())
	{
		RV_CORE_ERROR("no project to open; pass --project=<path>");
		return 1;
	}

	Assets::Registry::Init(Project::AssetRoot());
	Assets::Manager::Init(*device);

	const AssetHandle handle = Assets::Registry::GetHandle(model);
	if (!handle.IsValid())
	{
		RV_CORE_ERROR("'{0}' is not an asset of this project", model);
		return 1;
	}

	auto scene = std::make_shared<Scene>();

	Entity root = Assets::Manager::InstantiateModel(*scene, handle);
	if (!root)
	{
		RV_CORE_ERROR("'{0}' produced nothing", model);
		return 1;
	}

	SceneSerializer serializer(scene);
	const std::string yaml = serializer.SerializeSubtree(root);

	size_t entities = 0;
	for (size_t at = yaml.find("- EntityID:"); at != std::string::npos;
		 at = yaml.find("- EntityID:", at + 1))
	{
		entities++;
	}

	if (output.empty())
	{
		std::cout << yaml;
	}
	else
	{
		std::error_code error;
		std::filesystem::create_directories(output.parent_path(), error);

		// Binary, so the LF the serializer writes is the LF that lands on disk
		// -- a generated file that changes line endings by which tool touched
		// it last shows up as a whole-file diff.
		std::ofstream file(output, std::ios::binary);
		if (!file)
		{
			RV_CORE_ERROR("cannot write '{0}'", output.string());
			return 1;
		}

		file << yaml;
	}

	RV_CORE_INFO("{0}: {1} entities{2}", model, entities,
				 output.empty() ? "" : (" -> " + output.string()));

	Assets::Manager::Shutdown();
	Assets::Registry::Shutdown();
	return 0;
}
