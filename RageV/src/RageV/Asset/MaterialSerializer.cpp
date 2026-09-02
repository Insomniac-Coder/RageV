#include <rvpch.h>
#include "MaterialSerializer.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include "yaml-cpp/yaml.h"

#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		void EmitColor(YAML::Emitter& emitter, const char* key, const Vec4& value)
		{
			emitter << YAML::Key << key << YAML::Value << YAML::Flow
					<< YAML::BeginSeq << value.r << value.g << value.b << value.a
					<< YAML::EndSeq;
		}

		Vec4 ReadColor(const YAML::Node& node, Vec4 fallback)
		{
			if (!node || !node.IsSequence() || node.size() != 4)
				return fallback;

			return Vec4(node[0].as<float>(), node[1].as<float>(),
						node[2].as<float>(), node[3].as<float>());
		}

		// A handle is written only when it points at something. An explicit
		// zero would read the same on load, but it puts five "nothing here"
		// lines in every material anybody opens.
		void EmitMap(YAML::Emitter& emitter, const char* key, AssetHandle handle)
		{
			if (handle.IsValid())
				emitter << YAML::Key << key << YAML::Value << (uint64_t)handle;
		}

		AssetHandle ReadMap(const YAML::Node& node)
		{
			return node ? AssetHandle(node.as<uint64_t>()) : AssetHandle(0);
		}
	}

	bool MaterialSerializer::Save(const MaterialDesc& material, const std::filesystem::path& path)
	{
		const MaterialParams& params = material.Params;

		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Material" << YAML::Value << material.Name;

		EmitColor(emitter, "BaseColor", params.BaseColor);
		EmitColor(emitter, "Emissive", params.EmissiveColor);

		emitter << YAML::Key << "Metallic"    << YAML::Value << params.Metallic;
		emitter << YAML::Key << "Roughness"   << YAML::Value << params.Roughness;
		emitter << YAML::Key << "Occlusion"   << YAML::Value << params.Occlusion;
		emitter << YAML::Key << "NormalScale" << YAML::Value << params.NormalScale;
		emitter << YAML::Key << "Specular"    << YAML::Value << params.Specular;
		emitter << YAML::Key << "HeightScale" << YAML::Value << params.HeightScale;
		// **Two named scalars, not a vector**, because that is what a person
		// authoring a material thinks in: metres per cycle, and how much. The
		// pair rides in a vec4 only because MaterialParams had no spare words
		// left, and the file has no reason to know that.
		// Written only when on, so no material authored before the extended
		// lobes existed gains a line.
		if (params.Clearcoat > 0.0f)
		{
			emitter << YAML::Key << "Clearcoat" << YAML::Value << params.Clearcoat;
			emitter << YAML::Key << "ClearcoatRoughness" << YAML::Value << params.ClearcoatRoughness;
		}
		if (params.Anisotropy != 0.0f)
			emitter << YAML::Key << "Anisotropy" << YAML::Value << params.Anisotropy;
		if (params.Subsurface > 0.0f)
			emitter << YAML::Key << "Subsurface" << YAML::Value << params.Subsurface;
		if (params.SheenColor.x > 0.0f || params.SheenColor.y > 0.0f || params.SheenColor.z > 0.0f)
		{
			emitter << YAML::Key << "SheenColor" << YAML::Value << YAML::Flow
					<< YAML::BeginSeq << params.SheenColor.x << params.SheenColor.y
					<< params.SheenColor.z << YAML::EndSeq;
			emitter << YAML::Key << "SheenRoughness" << YAML::Value << params.SheenRoughness;
		}

		if (material.StochasticTiling)
		{
			emitter << YAML::Key << "StochasticTiling" << YAML::Value << true;
			emitter << YAML::Key << "TilingScale" << YAML::Value << material.TilingScale;
			emitter << YAML::Key << "TilingCells" << YAML::Value << material.TilingCells;
		}
		if (params.Macro.x != 0.0f || params.Macro.y != 0.0f)
		{
			emitter << YAML::Key << "MacroScale" << YAML::Value << params.Macro.x;
			emitter << YAML::Key << "MacroStrength" << YAML::Value << params.Macro.y;
		}

		// Only when it is not the default, so every material written before
		// transparency existed still round-trips byte for byte -- which is what
		// keeps a regenerated scene's diff to what actually changed.
		if (material.Blend != BlendMode::Opaque)
		{
			emitter << YAML::Key << "Blend" << YAML::Value
					<< (material.Blend == BlendMode::Masked ? "Masked" : "Blend");
		}

		// Beside the mode and only with it, for the same reason: a material
		// that never asked to be cut out gains no key.
		if (material.Blend == BlendMode::Masked)
			emitter << YAML::Key << "AlphaCutoff" << YAML::Value << params.AlphaCutoff;

		// The thin-member fade (MaterialParams::Macro.zw), only when set.
		if (params.Macro.w > 0.0f)
		{
			emitter << YAML::Key << "FadeStart" << YAML::Value << params.Macro.z;
			emitter << YAML::Key << "FadeEnd" << YAML::Value << params.Macro.w;
		}

		// Written as a pair of pairs rather than a raw vec4, because "Tiling:
		// [8, 8]" is what somebody editing this by hand is looking for.
		emitter << YAML::Key << "Tiling" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq << params.UvTransform.x << params.UvTransform.y << YAML::EndSeq;
		emitter << YAML::Key << "UvOffset" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq << params.UvTransform.z << params.UvTransform.w << YAML::EndSeq;

		// MapFlags is deliberately absent.
		//
		// It is derived from which handles are here, every time this is loaded.
		// Storing it would let a file say "there is a normal map" while
		// carrying no handle for one -- and the flags are what the shader
		// branches on, so the file would win and the surface would sample a
		// texture nobody assigned.
		const bool anyMap = material.BaseColorMap.IsValid() || material.NormalMap.IsValid() ||
							material.OcclusionMap.IsValid() || material.EmissiveMap.IsValid() ||
							material.RoughnessMap.IsValid() || material.MetallicMap.IsValid() ||
							material.SpecularMap.IsValid() || material.HeightMap.IsValid();

		if (anyMap)
		{
			emitter << YAML::Key << "Maps" << YAML::Value << YAML::BeginMap;
			EmitMap(emitter, "BaseColor", material.BaseColorMap);
			EmitMap(emitter, "Normal", material.NormalMap);
			EmitMap(emitter, "Occlusion", material.OcclusionMap);
			EmitMap(emitter, "Emissive", material.EmissiveMap);
			EmitMap(emitter, "Roughness", material.RoughnessMap);
			EmitMap(emitter, "Metallic", material.MetallicMap);
			EmitMap(emitter, "Specular", material.SpecularMap);
			EmitMap(emitter, "Height", material.HeightMap);
			emitter << YAML::EndMap;
		}

		emitter << YAML::EndMap;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write material '{0}'", path.string());
			return false;
		}

		file << emitter.c_str();
		return true;
	}

	bool MaterialSerializer::Load(MaterialDesc& out, const std::filesystem::path& path)
	{
		std::string text;
		if (!IO::VFS::ReadText(path, text))
			return false;

		YAML::Node root;
		try
		{
			root = YAML::Load(text);
		}
		catch (const YAML::Exception& exception)
		{
			RV_CORE_ERROR("Material '{0}' will not parse: {1}", path.string(), exception.what());
			return false;
		}

		if (!root || !root["Material"])
		{
			RV_CORE_ERROR("'{0}' is not a material", path.string());
			return false;
		}

		MaterialDesc material;
		material.Name = root["Material"].as<std::string>("Material");

		MaterialParams& params = material.Params;
		params.BaseColor = ReadColor(root["BaseColor"], params.BaseColor);
		params.EmissiveColor = ReadColor(root["Emissive"], params.EmissiveColor);

		if (root["Metallic"])    params.Metallic = root["Metallic"].as<float>();
		if (root["Roughness"])   params.Roughness = root["Roughness"].as<float>();
		if (root["Occlusion"])   params.Occlusion = root["Occlusion"].as<float>();
		if (root["NormalScale"]) params.NormalScale = root["NormalScale"].as<float>();
		if (root["Specular"])    params.Specular = root["Specular"].as<float>();
		if (root["HeightScale"]) params.HeightScale = root["HeightScale"].as<float>();
		if (root["Clearcoat"])          params.Clearcoat = root["Clearcoat"].as<float>();
		if (root["ClearcoatRoughness"]) params.ClearcoatRoughness = root["ClearcoatRoughness"].as<float>();
		if (root["Anisotropy"])         params.Anisotropy = root["Anisotropy"].as<float>();
		if (root["Subsurface"])         params.Subsurface = root["Subsurface"].as<float>();
		if (root["SheenRoughness"])     params.SheenRoughness = root["SheenRoughness"].as<float>();
		if (root["SheenColor"])
		{
			const auto sheen = root["SheenColor"];
			if (sheen.size() >= 3)
			{
				params.SheenColor.x = sheen[0].as<float>();
				params.SheenColor.y = sheen[1].as<float>();
				params.SheenColor.z = sheen[2].as<float>();
			}
		}
		if (root["StochasticTiling"]) material.StochasticTiling = root["StochasticTiling"].as<bool>();
		if (root["TilingScale"])      material.TilingScale = root["TilingScale"].as<int>();
		if (root["TilingCells"])      material.TilingCells = root["TilingCells"].as<int>();
		if (root["MacroScale"])    params.Macro.x = root["MacroScale"].as<float>();
		if (root["MacroStrength"]) params.Macro.y = root["MacroStrength"].as<float>();

		if (const YAML::Node blend = root["Blend"])
		{
			// An unrecognised value reads as Opaque rather than throwing: a
			// material written by a newer build should lose its cutout, not
			// stop the project from opening.
			const std::string mode = blend.as<std::string>();
			material.Blend = mode == "Blend"  ? BlendMode::Blend
						   : mode == "Masked" ? BlendMode::Masked
											  : BlendMode::Opaque;
		}
		if (root["AlphaCutoff"]) params.AlphaCutoff = root["AlphaCutoff"].as<float>();
		if (root["FadeStart"])   params.Macro.z = root["FadeStart"].as<float>();
		if (root["FadeEnd"])     params.Macro.w = root["FadeEnd"].as<float>();

		auto readPair = [](const YAML::Node& node, float& x, float& y)
		{
			if (node && node.IsSequence() && node.size() == 2)
			{
				x = node[0].as<float>();
				y = node[1].as<float>();
			}
		};

		readPair(root["Tiling"], params.UvTransform.x, params.UvTransform.y);
		readPair(root["UvOffset"], params.UvTransform.z, params.UvTransform.w);

		if (const YAML::Node maps = root["Maps"])
		{
			material.BaseColorMap = ReadMap(maps["BaseColor"]);
			material.NormalMap = ReadMap(maps["Normal"]);
			material.OcclusionMap = ReadMap(maps["Occlusion"]);
			material.EmissiveMap = ReadMap(maps["Emissive"]);
			material.RoughnessMap = ReadMap(maps["Roughness"]);
			material.MetallicMap = ReadMap(maps["Metallic"]);
			material.SpecularMap = ReadMap(maps["Specular"]);
			material.HeightMap = ReadMap(maps["Height"]);
		}

		// Whatever the file said, the flags come from the handles. See Save.
		params.MapFlags = 0;

		out = std::move(material);
		return true;
	}
}
