#include <rvpch.h>
#include "FieldSerializer.h"
#include "RageV/Scene/EntityRef.h"

namespace RageV
{
	YAML::Emitter& EmitVec2(YAML::Emitter& emitter, const Vec2& v)
	{
		emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
		return emitter;
	}

	YAML::Emitter& EmitVec3(YAML::Emitter& emitter, const Vec3& v)
	{
		emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
		return emitter;
	}

	YAML::Emitter& EmitVec4(YAML::Emitter& emitter, const Vec4& v)
	{
		emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
		return emitter;
	}

	Vec2 ReadVec2(const YAML::Node& node, const Vec2& fallback)
	{
		if (!node || !node.IsSequence() || node.size() != 2)
			return fallback;
		return { node[0].as<float>(), node[1].as<float>() };
	}

	Vec3 ReadVec3(const YAML::Node& node, const Vec3& fallback)
	{
		if (!node || !node.IsSequence() || node.size() != 3)
			return fallback;
		return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
	}

	Vec4 ReadVec4(const YAML::Node& node, const Vec4& fallback)
	{
		if (!node || !node.IsSequence() || node.size() != 4)
			return fallback;
		return { node[0].as<float>(), node[1].as<float>(),
				 node[2].as<float>(), node[3].as<float>() };
	}

	// Enums go to disk by name. Reordering an enum should not silently turn
	// every saved cube into a sphere -- that was already true of primitives
	// and is now true of light types, projections and anti-aliasing modes too.
	void WriteEnum(YAML::Emitter& emitter, const FieldDesc& field, int value)
	{
		if (field.Hint.EnumNames && value >= 0 && value < field.Hint.EnumCount)
			emitter << field.Hint.EnumNames[value];
		else
			emitter << value;
	}

	int ReadEnum(const YAML::Node& node, const FieldDesc& field, int fallback)
	{
		if (!node)
			return fallback;

		const std::string text = node.as<std::string>();

		// A field that used to be a boolean. `true` and `false` match no
		// enumerator's name, so without this every project saving the old
		// spelling would quietly load as the enum's default -- which is Off
		// for all of these, and so would turn a feature off rather than
		// migrate it. FieldHint::LegacyTrue says which enumerator the `true`
		// meant; `false` is always the first, because Off is.
		if (field.Hint.LegacyTrue >= 0 && (text == "true" || text == "false"))
			return text == "true" ? field.Hint.LegacyTrue : 0;

		if (field.Hint.EnumNames)
		{
			for (int i = 0; i < field.Hint.EnumCount; i++)
			{
				if (text == field.Hint.EnumNames[i])
					return i;
			}
		}

		// Version 1 and 2 wrote enums as indices, so those still load.
		try { return std::stoi(text); }
		catch (...) { return fallback; }
	}

	void WriteField(YAML::Emitter& emitter, const FieldDesc& field, void* block)
	{
		void* value = field.Access(block);
		emitter << YAML::Key << field.Name << YAML::Value;

		switch (field.Type)
		{
			case FieldType::Bool:   emitter << *(bool*)value; break;
			case FieldType::Int:    emitter << *(int*)value; break;
			case FieldType::Enum:   WriteEnum(emitter, field, *(int*)value); break;
			case FieldType::Float:  emitter << *(float*)value; break;
			case FieldType::Vec2:   EmitVec2(emitter, *(Vec2*)value); break;
			case FieldType::Vec3:   EmitVec3(emitter, *(Vec3*)value); break;
			case FieldType::Vec4:   EmitVec4(emitter, *(Vec4*)value); break;
			case FieldType::String: emitter << *(std::string*)value; break;
			case FieldType::Asset:  emitter << (uint64_t)*(AssetHandle*)value; break;
			// The UUID, exactly as an asset reference is written -- and for
			// the same reason: it is the only name for the target that
			// survives this file being closed.
			case FieldType::Entity: emitter << (uint64_t)*(EntityRef*)value; break;
		}
	}

	void ReadField(const YAML::Node& node, const FieldDesc& field, void* block)
	{
		if (!node)
			return;

		// A map where a handle should be is not malformed data -- it is a
		// version-5 scene, whose MeshComponent stored the material as a
		// nested block of scalars. The component's DeserializeExtra reads
		// that block into per-entity overrides; this field keeping its
		// default (no asset) is exactly the right outcome. Warning about it
		// made every legacy scene open under a wall of red that looked like
		// breakage and described none.
		if (field.Type == FieldType::Asset && node.IsMap())
			return;

		void* value = field.Access(block);

		// A field whose text does not convert leaves its default rather
		// than taking down the load. One malformed value in a hand-edited
		// scene should cost that value, not the file.
		try
		{
			switch (field.Type)
			{
				case FieldType::Bool:   *(bool*)value = node.as<bool>(); break;
				case FieldType::Int:    *(int*)value = node.as<int>(); break;
				case FieldType::Enum:   *(int*)value = ReadEnum(node, field, *(int*)value); break;
				case FieldType::Float:  *(float*)value = node.as<float>(); break;
				case FieldType::Vec2:   *(Vec2*)value = ReadVec2(node, *(Vec2*)value); break;
				case FieldType::Vec3:   *(Vec3*)value = ReadVec3(node, *(Vec3*)value); break;
				case FieldType::Vec4:   *(Vec4*)value = ReadVec4(node, *(Vec4*)value); break;
				case FieldType::String: *(std::string*)value = node.as<std::string>(); break;
				case FieldType::Asset:  *(AssetHandle*)value = AssetHandle(node.as<uint64_t>()); break;
				case FieldType::Entity: *(EntityRef*)value = EntityRef(UUID(node.as<uint64_t>())); break;
			}
		}
		catch (const YAML::Exception& e)
		{
			RV_CORE_WARN("Field '{0}' could not be read ({1}); left at its default",
						 field.Name, e.what());
		}
	}

	void WriteFields(YAML::Emitter& emitter, const std::vector<FieldDesc>& fields, void* block)
	{
		for (const FieldDesc& field : fields)
			WriteField(emitter, field, block);
	}

	void ReadFields(const YAML::Node& map, const std::vector<FieldDesc>& fields, void* block)
	{
		if (!map || !map.IsMap())
			return;

		for (const FieldDesc& field : fields)
			ReadField(map[field.Name], field, block);
	}
}
