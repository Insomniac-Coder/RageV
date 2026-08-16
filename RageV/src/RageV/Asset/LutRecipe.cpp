#include <rvpch.h>
#include "LutRecipe.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/FieldSerializer.h"

#include "yaml-cpp/yaml.h"

#include <cmath>
#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		// The key the file is identified by, so a `.rvlut` that is actually
		// something else fails to load rather than loading as a recipe of pure
		// defaults -- which would bake the identity and grade nothing, and be
		// wrong in a way nobody could see.
		constexpr const char* kRoot = "LutRecipe";
		constexpr int kVersion = 1;

		// Rec.709. The same weights the tonemap uses, because a saturation
		// that pivots on a different luma than the rest of the frame shifts
		// hue on every desaturated colour.
		constexpr float kLumaR = 0.2126f;
		constexpr float kLumaG = 0.7152f;
		constexpr float kLumaB = 0.0722f;

		// How far temperature and tint push a channel at full deflection.
		// Small, because these are a look tweak on already-displayed values
		// rather than a white balance on sensor data: at 1.0 the warm end
		// should read as warm, not as orange.
		constexpr float kBalanceRange = 0.20f;

		bool IsDefault(float value, float fallback)
		{
			return value == fallback;
		}

		bool IsDefault(const Vec3& value, float fallback)
		{
			return value.x == fallback && value.y == fallback && value.z == fallback;
		}
	}

	bool IsLutRecipePath(const std::filesystem::path& path)
	{
		std::string extension = path.extension().string();
		for (char& c : extension)
			c = (char)std::tolower((unsigned char)c);
		return extension == ".rvlut";
	}

	ColorLut BakeRecipe(const LutRecipe& recipe)
	{
		const uint32_t size = (uint32_t)Math::Clamp(recipe.Size, 2, 64);

		ColorLut lut;
		lut.Size = size;
		lut.Values.reserve((size_t)size * size * size);

		// Each stage is skipped rather than computed when it is at its
		// default. That is what makes a neutral recipe bake the identity
		// *exactly*, which is the property the check rests on. See the header.
		const bool balance   = !IsDefault(recipe.Temperature, 0.0f)
							|| !IsDefault(recipe.Tint, 0.0f);
		const bool lift      = !IsDefault(recipe.Lift, 0.0f);
		const bool gamma     = !IsDefault(recipe.Gamma, 1.0f);
		const bool gain      = !IsDefault(recipe.Gain, 1.0f);
		const bool contrast  = !IsDefault(recipe.Contrast, 1.0f);
		const bool saturate  = !IsDefault(recipe.Saturation, 1.0f);

		// Warm lifts red and drops blue; tint trades green against magenta.
		const Vec3 balanceScale(
			1.0f + (recipe.Temperature * kBalanceRange),
			1.0f - (recipe.Tint * kBalanceRange),
			1.0f - (recipe.Temperature * kBalanceRange));

		const float step = 1.0f / (float)(size - 1);

		// Red changes fastest, which is the `.cube` order every reader of this
		// table already assumes.
		for (uint32_t b = 0; b < size; b++)
		{
			for (uint32_t g = 0; g < size; g++)
			{
				for (uint32_t r = 0; r < size; r++)
				{
					Vec3 colour(r * step, g * step, b * step);

					if (balance)
					{
						colour.x *= balanceScale.x;
						colour.y *= balanceScale.y;
						colour.z *= balanceScale.z;
					}

					if (lift)
					{
						// Moves the black end without touching white: at
						// colour 1 the term is zero on every channel.
						colour.x += recipe.Lift.x * (1.0f - colour.x);
						colour.y += recipe.Lift.y * (1.0f - colour.y);
						colour.z += recipe.Lift.z * (1.0f - colour.z);
					}

					if (gain)
					{
						colour.x *= recipe.Gain.x;
						colour.y *= recipe.Gain.y;
						colour.z *= recipe.Gain.z;
					}

					if (gamma)
					{
						// Clamped before the power: a negative value from a
						// hard lift would come back NaN, and one NaN entry is
						// a black hole in the middle of a grade.
						colour.x = Math::Pow(Math::Max(colour.x, 0.0f), 1.0f / Math::Max(recipe.Gamma.x, 1e-3f));
						colour.y = Math::Pow(Math::Max(colour.y, 0.0f), 1.0f / Math::Max(recipe.Gamma.y, 1e-3f));
						colour.z = Math::Pow(Math::Max(colour.z, 0.0f), 1.0f / Math::Max(recipe.Gamma.z, 1e-3f));
					}

					if (contrast)
					{
						colour.x = (colour.x - 0.5f) * recipe.Contrast + 0.5f;
						colour.y = (colour.y - 0.5f) * recipe.Contrast + 0.5f;
						colour.z = (colour.z - 0.5f) * recipe.Contrast + 0.5f;
					}

					if (saturate)
					{
						const float luma = colour.x * kLumaR + colour.y * kLumaG + colour.z * kLumaB;
						colour.x = luma + (colour.x - luma) * recipe.Saturation;
						colour.y = luma + (colour.y - luma) * recipe.Saturation;
						colour.z = luma + (colour.z - luma) * recipe.Saturation;
					}

					// A display-referred table: anything outside [0,1] is a
					// colour the target cannot show, and leaving it there
					// would wrap rather than clip once it reaches 8 bits.
					lut.Values.emplace_back(Math::Clamp(colour.x, 0.0f, 1.0f),
											Math::Clamp(colour.y, 0.0f, 1.0f),
											Math::Clamp(colour.z, 0.0f, 1.0f),
											1.0f);
				}
			}
		}

		return lut;
	}

	bool LutRecipeSerializer::Save(const LutRecipe& recipe, const std::filesystem::path& path)
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << kRoot << YAML::Value << kVersion;

		WriteFields(emitter, LutRecipeRegistry::Fields(), const_cast<LutRecipe*>(&recipe));

		emitter << YAML::EndMap;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write LUT recipe {0}", path.string());
			return false;
		}

		file << emitter.c_str();
		return true;
	}

	bool LutRecipeSerializer::Load(LutRecipe& out, const std::filesystem::path& path)
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
			RV_CORE_ERROR("LUT recipe {0} is not valid YAML: {1}", path.string(), e.what());
			return false;
		}

		if (!root || !root[kRoot])
			return false;

		// Into a local seeded with the defaults, then handed over: a knob the
		// file does not carry keeps its default rather than becoming zero,
		// which for Gamma or Gain would be a black frame.
		LutRecipe loaded;
		ReadFields(root, LutRecipeRegistry::Fields(), &loaded);

		out = loaded;
		return true;
	}

	bool ExportCube(const ColorLut& lut, const std::filesystem::path& path)
	{
		if (!lut.IsValid())
		{
			RV_CORE_ERROR("Refusing to export an invalid LUT to {0}", path.string());
			return false;
		}

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write {0}", path.string());
			return false;
		}

		file << "# Baked by RageV from a .rvlut recipe\n";
		file << "LUT_3D_SIZE " << lut.Size << "\n";
		file << "DOMAIN_MIN 0.0 0.0 0.0\n";
		file << "DOMAIN_MAX 1.0 1.0 1.0\n";

		// The same order the table is stored in, which is the format's: red
		// changes fastest. Writing it in any other order produces a file that
		// loads and grades wrongly.
		file << std::fixed;
		file.precision(6);
		for (const Vec4& entry : lut.Values)
			file << entry.x << " " << entry.y << " " << entry.z << "\n";

		return (bool)file;
	}
}
