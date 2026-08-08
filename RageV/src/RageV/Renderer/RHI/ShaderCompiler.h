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
	// Vulkan addresses resources by (set, binding); OpenGL has one flat
	// namespace per resource type. Collapsing them densely from 0 per type
	// matters rather than being cosmetic: GL_MAX_TEXTURE_IMAGE_UNITS is
	// commonly 32, so any scheme that spaces sets apart -- set * 16 + binding,
	// say -- pushes the 32-element sampler array in the quad shader past the
	// limit and the shader fails to link.
	struct FlatBindingMap
	{
		static uint32_t Key(uint32_t set, uint32_t binding) { return (set << 16) | binding; }

		std::unordered_map<uint32_t, uint32_t> UniformBuffers;
		std::unordered_map<uint32_t, uint32_t> StorageBuffers;
		// Value is the *first* unit; an array of N samplers occupies N of them.
		std::unordered_map<uint32_t, uint32_t> Textures;

		uint32_t LookupUniformBuffer(uint32_t set, uint32_t binding) const;
		uint32_t LookupStorageBuffer(uint32_t set, uint32_t binding) const;
		uint32_t LookupTexture(uint32_t set, uint32_t binding) const;

		uint32_t TextureUnitCount = 0;

		// GL has no push constants. SPIRV-Cross re-emits the block as a uniform
		// buffer and the backend keeps a small buffer behind it, so this is the
		// binding point that buffer occupies. UINT32_MAX when the shader has no
		// push constants.
		uint32_t PushConstantBinding = UINT32_MAX;
		uint32_t PushConstantSize = 0;
	};

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

		// SPIR-V -> GLSL for the OpenGL backend, with descriptor bindings
		// rewritten into GL's flat binding namespace.
		static std::optional<std::string> CrossCompileToGLSL(const CompiledStage& stage,
															const FlatBindingMap& bindings,
															uint32_t glslVersion = 450);

		// Assigns the flat binding points. Must be computed once per shader and
		// shared by the GLSL generation and the resource-set binding code, or
		// the two disagree about where a resource lives.
		static FlatBindingMap BuildFlatBindingMap(const ShaderReflection& reflection);

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
