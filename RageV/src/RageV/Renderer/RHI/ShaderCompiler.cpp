#include <rvpch.h>
#include "ShaderCompiler.h"

#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <spirv_glsl.hpp>

#include <fstream>
#include <sstream>

namespace RageV::RHI
{
	namespace
	{
		bool s_Initialized = false;
		std::filesystem::path s_CacheDirectory;

		EShLanguage ToGlslangStage(ShaderStage stage)
		{
			switch (stage)
			{
				case ShaderStage::Vertex:   return EShLangVertex;
				case ShaderStage::Fragment: return EShLangFragment;
				case ShaderStage::Compute:  return EShLangCompute;
				default:                    return EShLangVertex;
			}
		}

		ShaderStage ParseStageName(std::string name)
		{
			// Trim, then accept the engine's existing spellings.
			while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ' || name.back() == '\t'))
				name.pop_back();
			size_t start = name.find_first_not_of(" \t");
			if (start != std::string::npos)
				name = name.substr(start);

			if (name == "vertex")                      return ShaderStage::Vertex;
			if (name == "fragment" || name == "pixel") return ShaderStage::Fragment;
			if (name == "compute")                     return ShaderStage::Compute;
			return ShaderStage::None;
		}

		Format FormatFromSpirvType(const spirv_cross::SPIRType& type)
		{
			using BT = spirv_cross::SPIRType::BaseType;
			switch (type.basetype)
			{
				case BT::Float:
					switch (type.vecsize)
					{
						case 1: return Format::R32_SFLOAT;
						case 2: return Format::R32G32_SFLOAT;
						case 3: return Format::R32G32B32_SFLOAT;
						case 4: return Format::R32G32B32A32_SFLOAT;
						default: break;
					}
					break;
				// Vector width matters here as much as it does for floats. It
				// used to be ignored, so a uvec4 of joint indices reflected as
				// one uint: four bytes where the buffer holds sixteen, every
				// later attribute at the wrong offset, and the mesh drawn as a
				// spray of triangles across the world.
				case BT::Int:
					switch (type.vecsize)
					{
						case 1: return Format::R32_SINT;
						case 2: return Format::R32G32_SINT;
						case 3: return Format::R32G32B32_SINT;
						case 4: return Format::R32G32B32A32_SINT;
						default: break;
					}
					break;
				case BT::UInt:
					switch (type.vecsize)
					{
						case 1: return Format::R32_UINT;
						case 2: return Format::R32G32_UINT;
						case 3: return Format::R32G32B32_UINT;
						case 4: return Format::R32G32B32A32_UINT;
						default: break;
					}
					break;
				default: break;
			}
			return Format::Undefined;
		}

		// Merges a binding into a set, OR-ing the stage mask when the same
		// binding is declared by more than one stage.
		void MergeBinding(ShaderReflection& reflection, uint32_t set, const ResourceBinding& binding)
		{
			ResourceSetLayoutDesc* layout = nullptr;
			for (auto& s : reflection.Sets)
			{
				if (s.Set == set)
				{
					layout = &s;
					break;
				}
			}
			if (!layout)
			{
				reflection.Sets.push_back(ResourceSetLayoutDesc{ set, {} });
				layout = &reflection.Sets.back();
			}

			for (auto& existing : layout->Bindings)
			{
				if (existing.Binding == binding.Binding)
				{
					existing.Stages = existing.Stages | binding.Stages;
					existing.Count = std::max(existing.Count, binding.Count);
					existing.BlockSize = std::max(existing.BlockSize, binding.BlockSize);
					return;
				}
			}
			layout->Bindings.push_back(binding);
		}

		uint64_t HashSource(const std::string& source, ShaderStage stage)
		{
			// FNV-1a. Only used as a cache key.
			uint64_t hash = 1469598103934665603ull;
			for (unsigned char c : source)
			{
				hash ^= c;
				hash *= 1099511628211ull;
			}
			hash ^= (uint64_t)stage;
			hash *= 1099511628211ull;
			return hash;
		}

		std::filesystem::path CachePathFor(uint64_t hash)
		{
			std::ostringstream name;
			name << std::hex << hash << ".spv";
			return s_CacheDirectory / name.str();
		}

		bool ReadCache(const std::filesystem::path& path, std::vector<uint32_t>& outSpirV)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamsize size = file.tellg();
			if (size <= 0 || (size % sizeof(uint32_t)) != 0)
				return false;

			file.seekg(0);
			outSpirV.resize((size_t)size / sizeof(uint32_t));
			return (bool)file.read((char*)outSpirV.data(), size);
		}

		void WriteCache(const std::filesystem::path& path, const std::vector<uint32_t>& spirv)
		{
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary);
			if (file)
				file.write((const char*)spirv.data(), (std::streamsize)(spirv.size() * sizeof(uint32_t)));
		}
	}

	void ShaderCompiler::Init()
	{
		if (s_Initialized)
			return;
		glslang::InitializeProcess();
		s_Initialized = true;
	}

	void ShaderCompiler::Shutdown()
	{
		if (!s_Initialized)
			return;
		glslang::FinalizeProcess();
		s_Initialized = false;
	}

	void ShaderCompiler::SetCacheDirectory(const std::filesystem::path& directory)
	{
		s_CacheDirectory = directory;
	}

	namespace
	{
		// Splices `#include "file"` in place, relative to the including file.
		//
		// Added for skinning, which needs a second variant of the PBR shader
		// differing only in how it computes a vertex position. Without this the
		// choice is to copy six hundred lines of lighting and let the two
		// drift, which is the mistake `BuildFrame` exists to prevent one level
		// up -- a frame described twice ended up with two transfer functions in
		// one image, and a surface shaded twice would go the same way.
		//
		// `seen` guards against a cycle. A shader that includes itself is a
		// hang otherwise, and a compiler that hangs is much harder to diagnose
		// than one that complains.
		bool SpliceIncludes(const std::filesystem::path& path, std::string& out,
							std::vector<std::filesystem::path>& seen, int depth)
		{
			if (depth > 16)
			{
				RV_CORE_ERROR("Shader includes nested more than 16 deep at {0}", path.string());
				return false;
			}

			std::error_code error;
			const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
			const std::filesystem::path key = error ? path : resolved;

			for (const std::filesystem::path& already : seen)
			{
				if (already == key)
				{
					RV_CORE_ERROR("Shader include cycle at {0}", path.string());
					return false;
				}
			}
			seen.push_back(key);

			std::ifstream file(path);
			if (!file)
			{
				RV_CORE_ERROR("Shader file not found: {0}", path.string());
				return false;
			}

			const std::string marker = "#include";
			std::string line;

			while (std::getline(file, line))
			{
				const size_t pos = line.find(marker);
				const size_t first = line.find_first_not_of(" \t");

				// Only a line whose first token is the directive. A `#include`
				// inside a comment is not one, and neither is the word in prose.
				if (pos == std::string::npos || first != pos)
				{
					out += line;
					out += '\n';
					continue;
				}

				const size_t open = line.find('"', pos);
				const size_t close = open == std::string::npos
								   ? std::string::npos : line.find('"', open + 1);
				if (open == std::string::npos || close == std::string::npos)
				{
					RV_CORE_ERROR("Malformed #include in {0}: {1}", path.string(), line);
					return false;
				}

				const std::string name = line.substr(open + 1, close - open - 1);

				// Relative to the including file, which is what every other
				// language means by it and what lets a shader move with its
				// includes.
				if (!SpliceIncludes(path.parent_path() / name, out, seen, depth + 1))
					return false;
			}

			seen.pop_back();
			return true;
		}
	}

	std::optional<CompiledShader> ShaderCompiler::CompileFromFile(const std::filesystem::path& path)
	{
		std::string source;
		std::vector<std::filesystem::path> seen;
		if (!SpliceIncludes(path, source, seen, 0))
			return std::nullopt;

		std::istringstream file(source);

		ShaderDesc desc;
		desc.Name = path.stem().string();

		// Split on `#type <stage>` markers.
		const std::string marker = "#type ";
		ShaderStage current = ShaderStage::None;
		std::string body;
		std::string line;

		auto flush = [&]()
		{
			if (current != ShaderStage::None && !body.empty())
				desc.Stages.push_back(ShaderStageSource{ current, body, "main" });
			body.clear();
		};

		while (std::getline(file, line))
		{
			const size_t pos = line.find(marker);
			if (pos != std::string::npos)
			{
				flush();
				current = ParseStageName(line.substr(pos + marker.size()));
				if (current == ShaderStage::None)
					RV_CORE_WARN("Unknown shader stage in {0}: {1}", path.string(), line);
			}
			else
			{
				body += line;
				body += '\n';
			}
		}
		flush();

		if (desc.Stages.empty())
		{
			RV_CORE_ERROR("Shader {0} contains no #type sections", path.string());
			return std::nullopt;
		}

		return Compile(desc);
	}

	std::optional<CompiledShader> ShaderCompiler::Compile(const ShaderDesc& desc)
	{
		Init();

		CompiledShader result;
		result.Name = desc.Name;

		for (const auto& stageSource : desc.Stages)
		{
			const uint64_t hash = HashSource(stageSource.Source, stageSource.Stage);

			std::vector<uint32_t> spirv;
			bool fromCache = false;
			if (!s_CacheDirectory.empty() && ReadCache(CachePathFor(hash), spirv))
				fromCache = true;

			if (!fromCache)
			{
				auto compiled = CompileStage(stageSource.Source, stageSource.Stage,
											 desc.Name + ":" + ShaderStageName(stageSource.Stage));
				if (!compiled)
					return std::nullopt;

				spirv = std::move(*compiled);
				if (!s_CacheDirectory.empty())
					WriteCache(CachePathFor(hash), spirv);
			}

			CompiledStage stage;
			stage.Stage = stageSource.Stage;
			stage.SpirV = std::move(spirv);
			stage.EntryPoint = stageSource.EntryPoint;

			ReflectStage(stage, result.Reflection);
			result.Stages.push_back(std::move(stage));
		}

		return result;
	}

	std::optional<std::vector<uint32_t>> ShaderCompiler::CompileStage(const std::string& source,
																	  ShaderStage stage,
																	  const std::string& debugName)
	{
		const EShLanguage language = ToGlslangStage(stage);
		glslang::TShader shader(language);

		const char* sources[] = { source.c_str() };
		shader.setStrings(sources, 1);
		shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
		shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
		shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

		const EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
		const int defaultVersion = 450;

		if (!shader.parse(GetDefaultResources(), defaultVersion, false, messages))
		{
			RV_CORE_ERROR("Shader compilation failed [{0}]:\n{1}", debugName, shader.getInfoLog());
			return std::nullopt;
		}

		glslang::TProgram program;
		program.addShader(&shader);
		if (!program.link(messages))
		{
			RV_CORE_ERROR("Shader linking failed [{0}]:\n{1}", debugName, program.getInfoLog());
			return std::nullopt;
		}

		std::vector<uint32_t> spirv;
		spv::SpvBuildLogger logger;
		glslang::SpvOptions options;
#ifdef RV_DEBUG
		options.generateDebugInfo = true;
		options.disableOptimizer = true;
#else
		options.disableOptimizer = true;   // spirv-opt is not built in (ENABLE_OPT=OFF)
		options.stripDebugInfo = true;
#endif
		glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &logger, &options);

		const std::string messagesText = logger.getAllMessages();
		if (!messagesText.empty())
			RV_CORE_TRACE("SPIR-V generation [{0}]: {1}", debugName, messagesText);

		if (spirv.empty())
		{
			RV_CORE_ERROR("SPIR-V generation produced nothing for {0}", debugName);
			return std::nullopt;
		}

		return spirv;
	}

	void ShaderCompiler::ReflectStage(const CompiledStage& stage, ShaderReflection& outReflection)
	{
		spirv_cross::Compiler compiler(stage.SpirV);
		const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		auto decorate = [&](const spirv_cross::Resource& resource, ResourceType type, uint32_t blockSize)
		{
			ResourceBinding binding;
			binding.Binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			binding.Type = type;
			binding.Stages = stage.Stage;
			binding.Name = resource.name;
			binding.BlockSize = blockSize;

			const spirv_cross::SPIRType& resourceType = compiler.get_type(resource.type_id);
			binding.Count = resourceType.array.empty() ? 1u : resourceType.array[0];

			const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
			MergeBinding(outReflection, set, binding);
		};

		for (const auto& ubo : resources.uniform_buffers)
		{
			const auto& type = compiler.get_type(ubo.base_type_id);
			decorate(ubo, ResourceType::UniformBuffer, (uint32_t)compiler.get_declared_struct_size(type));
		}

		for (const auto& ssbo : resources.storage_buffers)
		{
			const auto& type = compiler.get_type(ssbo.base_type_id);
			decorate(ssbo, ResourceType::StorageBuffer, (uint32_t)compiler.get_declared_struct_size(type));
		}

		for (const auto& image : resources.sampled_images)
			decorate(image, ResourceType::CombinedImageSampler, 0);

		for (const auto& image : resources.storage_images)
			decorate(image, ResourceType::StorageImage, 0);

		for (const auto& pushConstant : resources.push_constant_buffers)
		{
			const auto& type = compiler.get_type(pushConstant.base_type_id);

			PushConstantRange range;
			range.Offset = 0;
			range.Size = (uint32_t)compiler.get_declared_struct_size(type);
			range.Stages = stage.Stage;

			bool merged = false;
			for (auto& existing : outReflection.PushConstants)
			{
				if (existing.Offset == range.Offset && existing.Size == range.Size)
				{
					existing.Stages = existing.Stages | range.Stages;
					merged = true;
					break;
				}
			}
			if (!merged)
				outReflection.PushConstants.push_back(range);
		}

		// Vertex inputs only come from the vertex stage. SPIR-V carries
		// locations and types but not offsets or stride -- those are the
		// application's choice -- so assume one interleaved binding packed in
		// ascending location order, which is what the engine's vertex structs
		// do.
		if (stage.Stage == ShaderStage::Vertex && !resources.stage_inputs.empty())
		{
			struct Input { uint32_t Location; Format Format; uint32_t Size; };
			std::vector<Input> inputs;
			inputs.reserve(resources.stage_inputs.size());

			for (const auto& input : resources.stage_inputs)
			{
				const spirv_cross::SPIRType& type = compiler.get_type(input.base_type_id);
				const Format format = FormatFromSpirvType(type);
				if (format == Format::Undefined)
				{
					RV_CORE_WARN("Unsupported vertex input type for '{0}'", input.name);
					continue;
				}
				inputs.push_back({ compiler.get_decoration(input.id, spv::DecorationLocation),
								   format, FormatSize(format) });
			}

			std::sort(inputs.begin(), inputs.end(),
					  [](const Input& a, const Input& b) { return a.Location < b.Location; });

			VertexLayout layout;
			uint32_t offset = 0;
			for (const auto& input : inputs)
			{
				layout.Attributes.push_back(VertexAttribute{ input.Location, 0, input.Format, offset });
				offset += input.Size;
			}
			layout.Bindings.push_back(VertexBinding{ 0, offset, false });

			outReflection.VertexInput = std::move(layout);
		}
	}

	uint32_t FlatBindingMap::LookupUniformBuffer(uint32_t set, uint32_t binding) const
	{
		const auto it = UniformBuffers.find(Key(set, binding));
		return it == UniformBuffers.end() ? UINT32_MAX : it->second;
	}

	uint32_t FlatBindingMap::LookupStorageBuffer(uint32_t set, uint32_t binding) const
	{
		const auto it = StorageBuffers.find(Key(set, binding));
		return it == StorageBuffers.end() ? UINT32_MAX : it->second;
	}

	uint32_t FlatBindingMap::LookupTexture(uint32_t set, uint32_t binding) const
	{
		const auto it = Textures.find(Key(set, binding));
		return it == Textures.end() ? UINT32_MAX : it->second;
	}

	FlatBindingMap ShaderCompiler::BuildFlatBindingMap(const ShaderReflection& reflection)
	{
		FlatBindingMap map;

		// Walk sets and bindings in ascending order so the assignment is
		// deterministic and identical everywhere it is recomputed.
		std::vector<const ResourceSetLayoutDesc*> sets;
		sets.reserve(reflection.Sets.size());
		for (const auto& set : reflection.Sets)
			sets.push_back(&set);
		std::sort(sets.begin(), sets.end(),
				  [](const ResourceSetLayoutDesc* a, const ResourceSetLayoutDesc* b) { return a->Set < b->Set; });

		uint32_t nextUniformBuffer = 0;
		uint32_t nextStorageBuffer = 0;
		uint32_t nextTextureUnit = 0;

		for (const ResourceSetLayoutDesc* set : sets)
		{
			std::vector<const ResourceBinding*> bindings;
			bindings.reserve(set->Bindings.size());
			for (const auto& binding : set->Bindings)
				bindings.push_back(&binding);
			std::sort(bindings.begin(), bindings.end(),
					  [](const ResourceBinding* a, const ResourceBinding* b) { return a->Binding < b->Binding; });

			for (const ResourceBinding* binding : bindings)
			{
				const uint32_t key = FlatBindingMap::Key(set->Set, binding->Binding);
				switch (binding->Type)
				{
					case ResourceType::UniformBuffer:
						map.UniformBuffers[key] = nextUniformBuffer++;
						break;
					case ResourceType::StorageBuffer:
						map.StorageBuffers[key] = nextStorageBuffer++;
						break;
					case ResourceType::CombinedImageSampler:
					case ResourceType::StorageImage:
						map.Textures[key] = nextTextureUnit;
						// An array consumes one unit per element.
						nextTextureUnit += std::max(1u, binding->Count);
						break;
				}
			}
		}

		// Push constants become a uniform buffer on GL, so they need a binding
		// point of their own after the real ones.
		if (!reflection.PushConstants.empty())
		{
			map.PushConstantBinding = nextUniformBuffer++;
			for (const auto& range : reflection.PushConstants)
				map.PushConstantSize = std::max(map.PushConstantSize, range.Offset + range.Size);
		}

		map.TextureUnitCount = nextTextureUnit;
		return map;
	}

	std::optional<std::string> ShaderCompiler::CrossCompileToGLSL(const CompiledStage& stage,
																  const FlatBindingMap& bindings,
																  uint32_t glslVersion)
	{
		try
		{
			spirv_cross::CompilerGLSL compiler(stage.SpirV);

			spirv_cross::CompilerGLSL::Options options;
			options.version = glslVersion;
			options.es = false;
			// Keeps the GLSL close to the SPIR-V so uniform block names and
			// binding points survive.
			options.enable_420pack_extension = true;
			options.emit_uniform_buffer_as_plain_uniforms = false;
			// GL has no push-constant concept, so the block is re-emitted as a
			// uniform buffer the backend fills per draw.
			options.emit_push_constant_as_uniform_buffer = true;
			compiler.set_common_options(options);

			const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

			auto rewrite = [&](const spirv_cross::Resource& resource, uint32_t assigned)
			{
				if (assigned == UINT32_MAX)
				{
					RV_CORE_WARN("No flat binding assigned for '{0}'", resource.name);
					return;
				}
				compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
				compiler.set_decoration(resource.id, spv::DecorationBinding, assigned);
			};

			auto setAndBinding = [&](const spirv_cross::Resource& resource)
			{
				return std::pair<uint32_t, uint32_t>{
					compiler.get_decoration(resource.id, spv::DecorationDescriptorSet),
					compiler.get_decoration(resource.id, spv::DecorationBinding)
				};
			};

			for (const auto& resource : resources.uniform_buffers)
			{
				const auto [set, binding] = setAndBinding(resource);
				rewrite(resource, bindings.LookupUniformBuffer(set, binding));
			}
			for (const auto& resource : resources.storage_buffers)
			{
				const auto [set, binding] = setAndBinding(resource);
				rewrite(resource, bindings.LookupStorageBuffer(set, binding));
			}
			for (const auto& resource : resources.sampled_images)
			{
				const auto [set, binding] = setAndBinding(resource);
				rewrite(resource, bindings.LookupTexture(set, binding));
			}
			for (const auto& resource : resources.storage_images)
			{
				const auto [set, binding] = setAndBinding(resource);
				rewrite(resource, bindings.LookupTexture(set, binding));
			}

			for (const auto& resource : resources.push_constant_buffers)
			{
				compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
				compiler.set_decoration(resource.id, spv::DecorationBinding, bindings.PushConstantBinding);
			}

			return compiler.compile();
		}
		catch (const std::exception& e)
		{
			RV_CORE_ERROR("SPIR-V -> GLSL cross-compilation failed: {0}", e.what());
			return std::nullopt;
		}
	}
}
