#include <rvpch.h>
#include "EngineConfig.h"

#include <fstream>
#include <algorithm>

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
			return ParseBool(value, config.EnableValidation);

		if (key == "audio")
			return ParseBool(value, config.EnableAudio);

		if (key == "project")
		{
			config.ProjectPath = value;
			return true;
		}

		if (key == "frames-in-flight" || key == "framesinflight")
		{
			try
			{
				const int parsed = std::stoi(value);
				// More than 3 adds latency without adding throughput.
				config.FramesInFlight = (uint32_t)std::clamp(parsed, 1, 3);
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
				const int parsed = std::clamp(std::stoi(value), 640, 16384);
				(key == "width" ? config.WindowWidth : config.WindowHeight) = (uint32_t)parsed;
				return true;
			}
			catch (const std::exception&)
			{
				RV_CORE_WARN("{0} expects an integer, got '{1}'", key, value);
				return false;
			}
		}

		if (key == "fixed-hz" || key == "fixedhz")
		{
			try
			{
				const int parsed = std::stoi(value);
				// Below 20 the simulation is visibly steppy and fast collisions
				// tunnel through thin geometry; above 240 it burns CPU for
				// nothing a display can show.
				config.FixedHz = (uint32_t)std::clamp(parsed, 20, 240);
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

		s_Config = config;
		s_Initialized = true;

		RV_CORE_INFO("Graphics backend: {0} (change with --rhi=vulkan|opengl, or ragev.ini)",
					 BackendName(config.Backend));
	}

	const EngineConfig& EngineConfig::Get()
	{
		if (!s_Initialized)
			RV_CORE_WARN("EngineConfig::Get called before Init; using defaults");
		return s_Config;
	}
}
