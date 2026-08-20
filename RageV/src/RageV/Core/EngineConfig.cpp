#include <rvpch.h>
#include "EngineConfig.h"

#include <fstream>
#include <algorithm>
#include <cstdlib>

namespace RageV
{
	namespace
	{
		EngineConfig s_Config;
		bool s_Initialized = false;

		std::string Trim(std::string value)
		{
			const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
			value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
			return value;
		}

		std::string ToLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
			return value;
		}

		bool ParseBool(const std::string& value, bool& out)
		{
			const std::string lowered = ToLower(value);
			if (lowered == "1" || lowered == "on" || lowered == "true" || lowered == "yes")  { out = true;  return true; }
			if (lowered == "0" || lowered == "off" || lowered == "false" || lowered == "no") { out = false; return true; }
			return false;
		}
	}

	const char* EngineConfig::BackendName(RHI::Backend backend)
	{
		switch (backend)
		{
			case RHI::Backend::Vulkan: return "Vulkan";
			case RHI::Backend::OpenGL: return "OpenGL";
		}
		return "unknown";
	}

	bool EngineConfig::ApplyKeyValue(EngineConfig& config, std::string key, std::string value)
	{
		key = ToLower(Trim(key));
		value = Trim(value);

		if (key == "rhi" || key == "backend" || key == "api")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "vulkan" || lowered == "vk")
			{
				config.Backend = RHI::Backend::Vulkan;
				return true;
			}
			if (lowered == "opengl" || lowered == "gl")
			{
				config.Backend = RHI::Backend::OpenGL;
				return true;
			}
			RV_CORE_WARN("Unknown graphics backend '{0}'; expected 'vulkan' or 'opengl'", value);
			return false;
		}

		if (key == "vsync")
			return ParseBool(value, config.VSync);

		if (key == "validation")
		{
			// "gpu" is the layers plus GPU-assisted validation: the only thing
			// that can catch an out-of-range index into a bindless array,
			// which ordinary validation cannot see (ENGINE-NOTES 7al).
			if (value == "gpu")
			{
				config.EnableValidation = true;
				config.ValidationGpuAssisted = true;
				return true;
			}
			config.ValidationGpuAssisted = false;
			return ParseBool(value, config.EnableValidation);
		}

		if (key == "audio")
			return ParseBool(value, config.EnableAudio);

		if (key == "import-cache" || key == "importcache")
			return ParseBool(value, config.UseImportCache);

		if (key == "depth-sort" || key == "depthsort")
			return ParseBool(value, config.DepthSortOpaque);

		if (key == "bindless")
			return ParseBool(value, config.Bindless);

		if (key == "raytracing" || key == "rt")
		{
			config.HasRayTracingOverride = true;
			return ParseBool(value, config.RayTracingOverride);
		}

		if (key == "rt-reflections" || key == "rtreflections")
		{
			config.HasRayReflectionsOverride = true;
			return ParseBool(value, config.RayReflectionsOverride);
		}

		if (key == "rt-ao" || key == "rtao")
		{
			config.HasRayAoOverride = true;
			return ParseBool(value, config.RayAoOverride);
		}

		if (key == "rt-gi" || key == "rtgi" || key == "raygi")
		{
			config.HasRayGiOverride = true;
			return ParseBool(value, config.RayGiOverride);
		}

		if (key == "voxel-gi" || key == "voxelgi")
		{
			config.HasVoxelGiOverride = true;
			return ParseBool(value, config.VoxelGiOverride);
		}

		if (key == "render-defaults" || key == "renderdefaults")
		{
			config.HasRenderDefaults = true;
			return ParseBool(value, config.RenderDefaults);
		}

		if (key == "gi-bounces" || key == "gibounces")
		{
			config.GiBouncesOverride = std::atoi(value.c_str());
			// Rejected rather than clamped: `--gi-bounces=4` is a person
			// asking for something this does not do, and silently giving them
			// two would have them measure two and write down four.
			return config.GiBouncesOverride == 1 || config.GiBouncesOverride == 2;
		}

		if (key == "msaa" || key == "samples")
		{
			config.MsaaOverride = std::atoi(value.c_str());
			return config.MsaaOverride > 0;
		}

		if (key == "ssaa" || key == "supersample")
		{
			config.SupersampleOverride = std::atoi(value.c_str());
			return config.SupersampleOverride > 0;
		}

		if (key == "aa" || key == "anti-aliasing" || key == "antialiasing")
		{
			const std::string lowered = ToLower(value);
			if (lowered == "none" || lowered == "off")
				config.AAOverride = AntiAliasing::None;
			else if (lowered == "fxaa")
				config.AAOverride = AntiAliasing::FXAA;
			else if (lowered == "smaa")
				config.AAOverride = AntiAliasing::SMAA;
			else if (lowered == "ssaa")
				config.AAOverride = AntiAliasing::SSAA;
			else if (lowered == "msaa")
				config.AAOverride = AntiAliasing::MSAA;
			else if (lowered == "taa")
				config.AAOverride = AntiAliasing::TAA;
			else
			{
				RV_CORE_WARN("Unknown anti-aliasing mode '{0}'; expected 'none', 'fxaa', "
							 "'smaa', 'ssaa', 'msaa' or 'taa'",
							 value);
				return false;
			}
			config.HasAAOverride = true;
			return true;
		}

		if (key == "project")
		{
			config.ProjectPath = value;
			return true;
		}

		if (key == "play")
			return ParseBool(value, config.StartPlaying);

		if (key == "screenshot")
		{
			config.ScreenshotPath = value;
			return true;
		}

		if (key == "loading-screenshot" || key == "loadingscreenshot")
		{
			config.LoadingScreenshotPath = value;
			return true;
		}

		if (key == "ui-scale" || key == "uiscale")
		{
			if (ToLower(value) == "auto")
			{
				config.UIScale = 0.0f;   // resolved against the monitor later
				return true;
			}

			try
			{
				config.UIScale = Math::Clamp(std::stof(value), 0.5f, 4.0f);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("ui-scale expects a number or 'auto', got '{0}'", value);
				return false;
			}
		}

		if (key == "generate-graphs" || key == "generategraphs")
			return ParseBool(value, config.GenerateGraphs);

		if (key == "graph-drop-unknown" || key == "graphdropunknown")
			return ParseBool(value, config.GraphDropUnknown);

		if (key == "graph-zoom" || key == "graphzoom")
		{
			try
			{
				config.GraphZoom = std::stof(value);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("graph-zoom expects a number, got '{0}'", value);
				return false;
			}
		}

		if (key == "graph")
		{
			config.GraphPath = value;
			return true;
		}

		if (key == "scene")
		{
			config.ScenePath = value;
			return true;
		}

		if (key == "select")
		{
			config.SelectEntity = value;
			return true;
		}

		if (key == "brush")
		{
			config.BrushScript = value;
			return true;
		}

		if (key == "camera")
		{
			// x,y,z,distance,yaw,pitch -- the editor camera's whole state, which
			// is a focal point and an orbit around it rather than a position.
			float parts[6] = { 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f };
			size_t count = 0;
			size_t start = 0;

			while (count < 6 && start <= value.size())
			{
				const size_t comma = value.find(',', start);
				const std::string piece =
					value.substr(start, comma == std::string::npos ? std::string::npos
																   : comma - start);
				try
				{
					parts[count++] = std::stof(piece);
				}
				catch (const std::exception&)
				{
					RV_CORE_WARN("camera expects six numbers "
								 "x,y,z,distance,yaw,pitch; got '{0}'", value);
					return false;
				}

				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}

			if (count != 6)
			{
				RV_CORE_WARN("camera expects six numbers x,y,z,distance,yaw,pitch; "
							 "got {0} in '{1}'", count, value);
				return false;
			}

			config.CameraFocus = Vec3(parts[0], parts[1], parts[2]);
			config.CameraDistance = parts[3];
			config.CameraYaw = parts[4];
			config.CameraPitch = parts[5];
			config.HasCameraPose = true;
			return true;
		}

		if (key == "theme")
		{
			const std::string wanted = ToLower(value);
			if (wanted != "dark" && wanted != "light")
			{
				RV_CORE_WARN("theme expects 'dark' or 'light', got '{0}'", value);
				return false;
			}
			config.Theme = wanted;
			return true;
		}

		if (key == "screenshot-frame" || key == "screenshotframe")
		{
			try
			{
				config.ScreenshotFrame = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("screenshot-frame expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "screenshot-count" || key == "screenshotcount")
		{
			try
			{
				config.ScreenshotCount = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("screenshot-count expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "benchmark")
		{
			try
			{
				config.BenchmarkFrames = (uint32_t)Math::Max(std::stoi(value), 1);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("benchmark expects a frame count, got '{0}'", value);
				return false;
			}
		}

		if (key == "frames-in-flight" || key == "framesinflight")
		{
			try
			{
				const int parsed = std::stoi(value);
				// More than 3 adds latency without adding throughput.
				config.FramesInFlight = (uint32_t)Math::Clamp(parsed, 1, 3);
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("frames-in-flight expects an integer, got '{0}'", value);
				return false;
			}
		}

		if (key == "width" || key == "height")
		{
			try
			{
				const int parsed = Math::Clamp(std::stoi(value), 640, 16384);
				(key == "width" ? config.WindowWidth : config.WindowHeight) = (uint32_t)parsed;
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("{0} expects an integer, got '{1}'", key, value);
				return false;
			}
		}

		if (key == "frame-time" || key == "frametime")
		{
			try
			{
				const float seconds = std::stof(value);
				config.FrameTime = seconds > 0.0f ? seconds : 0.0f;
			}
			catch (...)
			{
				RV_CORE_WARN("frame-time expects seconds, got '{0}'", value);
				return false;
			}
			return true;
		}

		if (key == "fixed-hz" || key == "fixedhz")
		{
			try
			{
				const int parsed = std::stoi(value);
				// Below 20 the simulation is visibly steppy and fast collisions
				// tunnel through thin geometry; above 240 it burns CPU for
				// nothing a display can show.
				config.FixedHz = (uint32_t)Math::Clamp(parsed, 20, 240);
				config.FixedHzExplicit = true;
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("fixed-hz expects an integer, got '{0}'", value);
				return false;
			}
		}

		RV_CORE_WARN("Unknown config key '{0}'", key);
		return false;
	}

	void EngineConfig::LoadFile(EngineConfig& config, const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file)
			return;

		std::string line;
		while (std::getline(file, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
				continue;

			const size_t separator = line.find('=');
			if (separator == std::string::npos)
				continue;

			ApplyKeyValue(config, line.substr(0, separator), line.substr(separator + 1));
		}

		RV_CORE_INFO("Loaded config from {0}", path.string());
	}

	void EngineConfig::LoadCommandLine(EngineConfig& config, int argc, char** argv)
	{
		for (int i = 1; i < argc; i++)
		{
			std::string argument = argv[i];
			if (argument.rfind("--", 0) != 0)
				continue;
			argument = argument.substr(2);

			const size_t separator = argument.find('=');
			if (separator != std::string::npos)
			{
				ApplyKeyValue(config, argument.substr(0, separator), argument.substr(separator + 1));
			}
			else if (i + 1 < argc && argv[i + 1][0] != '-')
			{
				// Also accept "--rhi vulkan".
				ApplyKeyValue(config, argument, argv[++i]);
			}
			else
			{
				RV_CORE_WARN("Config flag '--{0}' has no value", argument);
			}
		}
	}

	void EngineConfig::Init(int argc, char** argv)
	{
		if (s_Initialized)
		{
			RV_CORE_WARN("EngineConfig::Init called more than once; ignoring");
			return;
		}

		EngineConfig config;

		std::error_code ec;
		const std::filesystem::path iniPath = std::filesystem::current_path(ec) / "ragev.ini";
		if (!ec)
			LoadFile(config, iniPath);

		LoadCommandLine(config, argc, argv);

		// A run that writes a screenshot is measuring, and a measurement must
		// not depend on what the owner last saved in the project. This cost a
		// session: `SampleProject.rvproject` had an uncommitted
		// `RayTracing: true`, the probe scripts did not pass
		// `--render-defaults`, and on a ray-capable device that silently
		// selected the traced form -- so every "voxel" number they printed was
		// the ray-traced one, and a shader edit that changed nothing looked
		// like a backend defect. Refusing is better than defaulting either
		// way: `on` would surprise anyone screenshotting their own game with
		// its own look, and `off` is exactly the trap. So the run has to say.
		if (!config.ScreenshotPath.empty() && !config.HasRenderDefaults)
		{
			RV_CORE_ERROR("--screenshot is a measurement, so it needs "
						  "--render-defaults=on|off stated explicitly.");
			RV_CORE_ERROR("  on  = ignore the project's Render Settings, which is what every "
						  "check script wants: the run then depends only on its command line.");
			RV_CORE_ERROR("  off = use the project's saved Render Settings, for screenshotting "
						  "a project as it is actually configured.");
			RV_CORE_ERROR("Without it a saved setting -- RayTracing above all -- silently "
						  "decides which form of an effect you measured.");
			std::exit(2);
		}

		s_Config = config;
		s_Initialized = true;

		RV_CORE_INFO("Graphics backend: {0} (change with --rhi=vulkan|opengl, or ragev.ini)",
					 BackendName(config.Backend));
	}

	bool EngineConfig::SaveBackendPreference(RHI::Backend backend)
	{
		return SaveSetting("rhi", backend == RHI::Backend::Vulkan ? "vulkan" : "opengl");
	}

	bool EngineConfig::SaveVSyncPreference(bool enabled)
	{
		return SaveSetting("vsync", enabled ? "on" : "off");
	}

	void EngineConfig::SetAntiAliasingOverride(AntiAliasing aa)
	{
		// The storage directly, rather than casting the const off what Get
		// hands out. Same object either way; this one does not require the
		// reader to check whether it is defined behaviour.
		s_Config.AAOverride = aa;
		s_Config.HasAAOverride = true;
	}

	bool EngineConfig::SaveAntiAliasingPreference(AntiAliasing aa)
	{
		// The same names --aa= takes, so the file and the flag are one
		// vocabulary rather than two that have to be kept in step.
		const char* name = "fxaa";
		switch (aa)
		{
			case AntiAliasing::None: name = "none"; break;
			case AntiAliasing::FXAA: name = "fxaa"; break;
			case AntiAliasing::SMAA: name = "smaa"; break;
			case AntiAliasing::SSAA: name = "ssaa"; break;
			case AntiAliasing::MSAA: name = "msaa"; break;
			case AntiAliasing::TAA:  name = "taa";  break;
			default: return false;   // a mode from a later version
		}

		return SaveSetting("aa", name);
	}

	bool EngineConfig::SaveWindowSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return false;

		// Both, or neither: a file carrying a new width beside an old height
		// would open at an aspect ratio nothing was ever left at.
		return SaveSetting("width", std::to_string(width)) &&
			   SaveSetting("height", std::to_string(height));
	}

	bool EngineConfig::SaveSetting(const std::string& key, const std::string& value)
	{
		std::error_code ec;
		const std::filesystem::path path = std::filesystem::current_path(ec) / "ragev.ini";
		if (ec)
		{
			RV_CORE_ERROR("Could not resolve the working directory; {0} not saved", key);
			return false;
		}

		// Read, rewrite, replace. Every other setting in the file is somebody
		// else's and has to survive -- a setting that silently discarded the
		// audio or window settings would be a worse bug than the one it fixes.
		std::vector<std::string> lines;
		bool replaced = false;

		if (std::ifstream existing(path); existing)
		{
			std::string line;
			while (std::getline(existing, line))
			{
				const std::string trimmed = Trim(line);
				const size_t equals = trimmed.find('=');

				if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';' &&
					trimmed[0] != '[' && equals != std::string::npos &&
					ToLower(Trim(trimmed.substr(0, equals))) == key)
				{
					lines.push_back(key + "=" + value);
					replaced = true;
					continue;
				}

				lines.push_back(line);
			}
		}

		if (!replaced)
			lines.push_back(key + "=" + value);

		std::ofstream out(path, std::ios::trunc);
		if (!out)
		{
			RV_CORE_ERROR("Could not write {0}; {1} not saved", path.string(), key);
			return false;
		}

		for (const std::string& line : lines)
			out << line << '\n';

		RV_CORE_INFO("Saved to ragev.ini: {0}={1}", key, value);
		return true;
	}

	const EngineConfig& EngineConfig::Get()
	{
		if (!s_Initialized)
			RV_CORE_WARN("EngineConfig::Get called before Init; using defaults");
		return s_Config;
	}
}
