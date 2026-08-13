#include <rvpch.h>
#include "CurveSerializer.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include "yaml-cpp/yaml.h"

#include <fstream>

namespace RageV::Assets
{
	bool CurveSerializer::Save(const Curve& curve, const std::filesystem::path& path)
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Curve" << YAML::Value << (uint32_t)curve.GetChannels();
		emitter << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;

		for (const Curve::Key& key : curve.GetKeys())
		{
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "T" << YAML::Value << key.Time;

			// Only the channels this curve actually uses. Writing all four
			// would put two meaningless numbers beside every colour stop and
			// invite somebody to edit one.
			emitter << YAML::Key << "V" << YAML::Value << YAML::Flow << YAML::BeginSeq;
			for (uint32_t channel = 0; channel < curve.GetChannels(); channel++)
				emitter << key.Value[channel];
			emitter << YAML::EndSeq;

			emitter << YAML::EndMap;
		}

		emitter << YAML::EndSeq;
		emitter << YAML::EndMap;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write curve {0}", path.string());
			return false;
		}

		file << emitter.c_str();
		return true;
	}

	bool CurveSerializer::Load(Curve& out, const std::filesystem::path& path)
	{
		std::string text;
		if (!IO::VFS::ReadText(path, text))
			return false;

		YAML::Node root;
		try
		{
			root = YAML::Load(text);
		}
		catch (const YAML::Exception& e)
		{
			// A malformed curve is somebody's hand edit, so say which file and
			// what the parser objected to rather than "load failed".
			RV_CORE_ERROR("Curve {0} is not valid YAML: {1}", path.string(), e.what());
			return false;
		}

		if (!root || !root["Curve"])
			return false;

		// Built into a local and moved in at the end, so a file that parses
		// halfway cannot leave the caller with a curve that is part old and
		// part new.
		Curve loaded((uint32_t)root["Curve"].as<int>(1));

		if (const YAML::Node keys = root["Keys"])
		{
			for (const YAML::Node& node : keys)
			{
				const YAML::Node values = node["V"];
				if (!values)
					continue;

				Vec4 value(0.0f, 0.0f, 0.0f, 0.0f);
				const size_t count = values.size() < Curve::kMaxChannels
								   ? values.size() : Curve::kMaxChannels;
				for (size_t channel = 0; channel < count; channel++)
					value[(int)channel] = values[channel].as<float>(0.0f);

				// Through AddKey rather than pushed at the back: a hand-edited
				// file with its keys out of order must load as the curve it
				// describes, not as one that evaluates backwards in the middle.
				loaded.AddKey(node["T"].as<float>(0.0f), value);
			}
		}

		out = std::move(loaded);
		return true;
	}
}
