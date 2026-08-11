#include <rvpch.h>
#include "FontSerializer.h"
#include "RageV/Core/Log.h"

#include "yaml-cpp/yaml.h"

#include <fstream>

namespace RageV::Assets
{
	bool FontSerializer::Load(Font& out, const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file)
			return false;

		YAML::Node root;
		try
		{
			root = YAML::Load(file);
		}
		catch (const YAML::Exception& error)
		{
			RV_CORE_ERROR("Font {0} is not valid YAML: {1}", path.string(), error.what());
			return false;
		}

		if (!root["Glyphs"] || !root["Atlas"])
		{
			RV_CORE_ERROR("Font {0} has no glyph table or names no atlas", path.string());
			return false;
		}

		// Built into a local and moved out at the end, so a file that turns out
		// to be malformed halfway through leaves the caller's font alone rather
		// than half-replacing it.
		Font font;

		font.AtlasFile = root["Atlas"].as<std::string>("");
		font.AtlasWidth = root["AtlasWidth"].as<uint32_t>(0);
		font.AtlasHeight = root["AtlasHeight"].as<uint32_t>(0);
		font.EmSize = root["EmSize"].as<float>(0.0f);
		font.PxRange = root["PxRange"].as<float>(0.0f);
		font.Ascent = root["Ascent"].as<float>(0.0f);
		font.Descent = root["Descent"].as<float>(0.0f);
		font.LineHeight = root["LineHeight"].as<float>(0.0f);

		// Without these the shader cannot work out how sharp it may be and the
		// atlas rectangles cannot be turned into texture coordinates. A font
		// missing them would draw, badly, with no visible cause.
		if (font.AtlasWidth == 0 || font.AtlasHeight == 0 ||
			font.EmSize <= 0.0f || font.PxRange <= 0.0f)
		{
			RV_CORE_ERROR("Font {0} is missing its atlas size, em size or range",
						  path.string());
			return false;
		}

		for (const YAML::Node& node : root["Glyphs"])
		{
			Font::Glyph glyph;
			glyph.Codepoint = node["C"].as<uint32_t>(0);
			glyph.Advance = node["Advance"].as<float>(0.0f);

			// Absent for a glyph with no outline. A space is the common case
			// and it is not an error: it carries an advance and draws nothing.
			if (const YAML::Node rect = node["Rect"]; rect && rect.size() == 4)
			{
				glyph.X = rect[0].as<float>(0.0f);
				glyph.Y = rect[1].as<float>(0.0f);
				glyph.W = rect[2].as<float>(0.0f);
				glyph.H = rect[3].as<float>(0.0f);
			}

			if (const YAML::Node plane = node["Plane"]; plane && plane.size() == 4)
			{
				glyph.Left = plane[0].as<float>(0.0f);
				glyph.Bottom = plane[1].as<float>(0.0f);
				glyph.Right = plane[2].as<float>(0.0f);
				glyph.Top = plane[3].as<float>(0.0f);
			}

			font.AddGlyph(glyph);
		}

		if (font.IsEmpty())
		{
			RV_CORE_ERROR("Font {0} has an empty glyph table", path.string());
			return false;
		}

		if (const YAML::Node kerning = root["Kerning"])
		{
			for (const YAML::Node& pair : kerning)
			{
				if (pair.size() != 3)
					continue;

				font.SetKerning(pair[0].as<uint32_t>(0), pair[1].as<uint32_t>(0),
								pair[2].as<float>(0.0f));
			}
		}

		out = std::move(font);
		return true;
	}
}
