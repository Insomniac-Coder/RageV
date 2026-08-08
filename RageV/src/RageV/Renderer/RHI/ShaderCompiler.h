#pragma once

// Shaders have one source of truth: Vulkan-flavoured GLSL with explicit
// set/binding decorations. glslang lowers it to SPIR-V, SPIRV-Cross recovers
// the binding layout, and the OpenGL backend cross-compiles the same SPIR-V
// back to desktop GLSL. Nothing here needs an installed Vulkan SDK.

#include "RHITypes.h"
#include "RHIShader.h"
#include <optional>
#include <filesystem>

namespace RageV::RHI
{
	class ShaderCompiler
	{
	public:
		// glslang keeps process-wide state; these bracket its use.
		static void Init();
		static void Shutdown();

		// Reads a .glsl file split into stages by `#type vertex` / `#type
		// fragment` markers -- the same convention the engine already used.
		static std::optional<CompiledShader> CompileFromFile(const std::filesystem::path& path);

		static std::optional<CompiledShader> Compile(const ShaderDesc& desc);

		// SPIR-V -> GLSL for the OpenGL backend. Rewrites descriptor bindings
		// into the flat binding-point namespace GL uses.
		static std::optional<std::string> CrossCompileToGLSL(const CompiledStage& stage,
															uint32_t glslVersion = 450);

		// Compiled SPIR-V is cached here keyed by source hash. Empty disables
		// caching.
		static void SetCacheDirectory(const std::filesystem::path& directory);

	private:
		static std::optional<std::vector<uint32_t>> CompileStage(const std::string& source,
																 ShaderStage stage,
																 const std::string& debugName);
		static void ReflectStage(const CompiledStage& stage, ShaderReflection& outReflection);
	};
}
