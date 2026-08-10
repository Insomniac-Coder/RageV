#pragma once

// Shaders are authored once in Vulkan-flavoured GLSL, compiled to SPIR-V by
// glslang, and reflected with SPIRV-Cross. The Vulkan backend consumes the
// SPIR-V directly; the OpenGL backend cross-compiles it back to GLSL 4.50.

#include "RHITypes.h"
#include <unordered_map>

namespace RageV::RHI
{
	// Everything the RHI needs to know about a shader without parsing SPIR-V
	// again at pipeline-creation time.
	struct ShaderReflection
	{
		std::vector<ResourceSetLayoutDesc> Sets;
		std::vector<PushConstantRange>     PushConstants;
		VertexLayout                       VertexInput;   // derived from stage inputs

		// A compute shader's `layout(local_size_x = ...)`, recovered rather
		// than declared twice. A dispatch is in groups, so the caller divides
		// by this -- and taking it from the shader is what keeps a resized
		// work group from silently under-dispatching everywhere it is used.
		// All ones for a shader with no compute stage -- which is also what a
		// compute shader declaring local_size_x = 1 has, so this cannot be
		// used to tell the two apart. `Stages` is what answers that.
		uint32_t LocalSize[3] = { 1, 1, 1 };

		// Every stage this shader carries, or-ed together. Asked before
		// building a pipeline of a given kind, so "this file has no compute
		// stage" is an error at creation rather than a dispatch that does
		// nothing.
		ShaderStage Stages = ShaderStage::None;

		const ResourceSetLayoutDesc* FindSet(uint32_t set) const
		{
			for (const auto& s : Sets)
				if (s.Set == set)
					return &s;
			return nullptr;
		}
	};

	struct ShaderStageSource
	{
		ShaderStage Stage = ShaderStage::None;
		std::string Source;      // GLSL
		std::string EntryPoint = "main";
	};

	struct ShaderDesc
	{
		std::string                    Name;
		std::vector<ShaderStageSource> Stages;
	};

	class RHIShader
	{
	public:
		virtual ~RHIShader() = default;

		const std::string&     GetName()       const { return m_Name; }
		const ShaderReflection& GetReflection() const { return m_Reflection; }

	protected:
		RHIShader(std::string name, ShaderReflection reflection)
			: m_Name(std::move(name)), m_Reflection(std::move(reflection)) {}

		std::string      m_Name;
		ShaderReflection m_Reflection;
	};

	// SPIR-V for one stage, plus the reflection recovered from it.
	struct CompiledStage
	{
		ShaderStage           Stage = ShaderStage::None;
		std::vector<uint32_t> SpirV;
		std::string           EntryPoint = "main";
	};

	struct CompiledShader
	{
		std::string                Name;
		std::vector<CompiledStage> Stages;
		ShaderReflection           Reflection;
	};
}
