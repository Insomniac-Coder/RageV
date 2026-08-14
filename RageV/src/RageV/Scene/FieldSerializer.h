#pragma once
#include "ComponentRegistry.h"
#include "RageV/Math/Math.h"
#include "yaml-cpp/yaml.h"
#include <vector>

namespace RageV
{
	// One `FieldDesc` to and from YAML, and a whole list of them to and from a
	// map.
	//
	// This used to live inside SceneSerializer.cpp, where it served exactly one
	// caller: a component. Then the settings that had been crammed into
	// `SceneEnvironment` split three ways -- cost onto the project, look onto a
	// `.rvpostprofile`, place left in the scene (ENGINE-NOTES 7s) -- and three
	// more file formats needed to write the same field types.
	//
	// **Hoisted rather than copied**, because the failure this repository has
	// already shipped is a hand-written list drifting from the registry that
	// describes it: `TemporalFeedback` was registered, inspectable and
	// serialized nowhere, so it reset on every load and three scene files that
	// set it rendered identically. Three copies of a writer is three chances at
	// that; one writer driven by the registry is none, because a field that is
	// registered is written and a field that is not registered does not exist
	// to any of these formats.
	//
	// Reading is sparse and forgiving by design: a key that is absent leaves
	// the destination alone, so a file written before a field existed loads
	// with that field at its default rather than at zero, and a value that will
	// not convert costs itself rather than the file.

	YAML::Emitter& EmitVec2(YAML::Emitter& emitter, const Vec2& v);
	YAML::Emitter& EmitVec3(YAML::Emitter& emitter, const Vec3& v);
	YAML::Emitter& EmitVec4(YAML::Emitter& emitter, const Vec4& v);

	Vec2 ReadVec2(const YAML::Node& node, const Vec2& fallback);
	Vec3 ReadVec3(const YAML::Node& node, const Vec3& fallback);
	Vec4 ReadVec4(const YAML::Node& node, const Vec4& fallback);

	// Enums go to disk by name. Reordering an enum should not silently turn
	// every saved cube into a sphere.
	void WriteEnum(YAML::Emitter& emitter, const FieldDesc& field, int value);
	int  ReadEnum(const YAML::Node& node, const FieldDesc& field, int fallback);

	// `block` is the struct the field's Access was generated against -- a
	// component, or one of the settings blocks.
	void WriteField(YAML::Emitter& emitter, const FieldDesc& field, void* block);
	void ReadField(const YAML::Node& node, const FieldDesc& field, void* block);

	// Every field in the list, as `Name: value` pairs. The caller opens and
	// closes the map, so a block can carry keys of its own beside these.
	void WriteFields(YAML::Emitter& emitter, const std::vector<FieldDesc>& fields, void* block);

	// The inverse. A node that is not a map is not an error -- an absent
	// section reads as "nothing set", which leaves every default in place.
	void ReadFields(const YAML::Node& map, const std::vector<FieldDesc>& fields, void* block);
}
