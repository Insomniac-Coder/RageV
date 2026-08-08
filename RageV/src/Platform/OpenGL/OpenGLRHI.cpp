#include <rvpch.h>
#include "OpenGLRHI.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace RageV::GL
{
	namespace
	{
		struct GLFormat
		{
			GLenum Internal = 0;   // sized internal format for glTextureStorage
			GLenum Format   = 0;   // pixel format for glTextureSubImage
			GLenum Type     = 0;
		};

		GLFormat ToGLFormat(Format format)
		{
			switch (format)
			{
				case Format::R8_UNORM:            return { GL_R8,      GL_RED,  GL_UNSIGNED_BYTE };
				case Format::R8G8_UNORM:          return { GL_RG8,     GL_RG,   GL_UNSIGNED_BYTE };
				case Format::R8G8B8A8_UNORM:      return { GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE };
				case Format::R8G8B8A8_SRGB:       return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE };
				// GL has no BGRA sized internal format; store RGBA8 and let the
				// pixel-transfer format do the swizzle on upload.
				case Format::B8G8R8A8_UNORM:      return { GL_RGBA8,   GL_BGRA, GL_UNSIGNED_BYTE };
				case Format::B8G8R8A8_SRGB:       return { GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE };
				case Format::R16_SFLOAT:          return { GL_R16F,    GL_RED,  GL_HALF_FLOAT };
				case Format::R16G16_SFLOAT:       return { GL_RG16F,   GL_RG,   GL_HALF_FLOAT };
				case Format::R16G16B16A16_SFLOAT: return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
				case Format::B10G11R11_UFLOAT:    return { GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT };
				case Format::R9G9B9E5_UFLOAT:     return { GL_RGB9_E5, GL_RGB,  GL_FLOAT };
				case Format::R32_SFLOAT:          return { GL_R32F,    GL_RED,  GL_FLOAT };
				case Format::R32G32_SFLOAT:       return { GL_RG32F,   GL_RG,   GL_FLOAT };
				case Format::R32G32B32_SFLOAT:    return { GL_RGB32F,  GL_RGB,  GL_FLOAT };
				case Format::R32G32B32A32_SFLOAT: return { GL_RGBA32F, GL_RGBA, GL_FLOAT };
				case Format::R32_UINT:            return { GL_R32UI,   GL_RED_INTEGER, GL_UNSIGNED_INT };
				case Format::R32_SINT:            return { GL_R32I,    GL_RED_INTEGER, GL_INT };
				case Format::D16_UNORM:           return { GL_DEPTH_COMPONENT16,  GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT };
				case Format::D32_SFLOAT:          return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT };
				case Format::D24_UNORM_S8_UINT:   return { GL_DEPTH24_STENCIL8,   GL_DEPTH_STENCIL,   GL_UNSIGNED_INT_24_8 };
				case Format::D32_SFLOAT_S8_UINT:  return { GL_DEPTH32F_STENCIL8,  GL_DEPTH_STENCIL,   GL_FLOAT_32_UNSIGNED_INT_24_8_REV };
				case Format::Undefined:           return {};
			}
			return {};
		}

		GLenum ToGLTopology(PrimitiveTopology topology)
		{
			switch (topology)
			{
				case PrimitiveTopology::TriangleList:  return GL_TRIANGLES;
				case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
				case PrimitiveTopology::LineList:      return GL_LINES;
				case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
				case PrimitiveTopology::PointList:     return GL_POINTS;
			}
			return GL_TRIANGLES;
		}

		GLenum ToGLCompare(CompareOp op)
		{
			switch (op)
			{
				case CompareOp::Never:          return GL_NEVER;
				case CompareOp::Less:           return GL_LESS;
				case CompareOp::Equal:          return GL_EQUAL;
				case CompareOp::LessOrEqual:    return GL_LEQUAL;
				case CompareOp::Greater:        return GL_GREATER;
				case CompareOp::NotEqual:       return GL_NOTEQUAL;
				case CompareOp::GreaterOrEqual: return GL_GEQUAL;
				case CompareOp::Always:         return GL_ALWAYS;
			}
			return GL_LEQUAL;
		}

		GLenum ToGLWrap(WrapMode wrap)
		{
			switch (wrap)
			{
				case WrapMode::Repeat:         return GL_REPEAT;
				case WrapMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
				case WrapMode::ClampToEdge:    return GL_CLAMP_TO_EDGE;
				case WrapMode::ClampToBorder:  return GL_CLAMP_TO_BORDER;
			}
			return GL_REPEAT;
		}

		GLenum ToGLMinFilter(FilterMode filter, MipmapMode mipmap, bool hasMips)
		{
			if (!hasMips)
				return filter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;

			if (filter == FilterMode::Nearest)
				return mipmap == MipmapMode::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_LINEAR;
			return mipmap == MipmapMode::Nearest ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
		}

		GLenum ToGLTarget(TextureType type)
		{
			switch (type)
			{
				case TextureType::Texture2D:        return GL_TEXTURE_2D;
				case TextureType::Texture2DArray:   return GL_TEXTURE_2D_ARRAY;
				case TextureType::TextureCube:      return GL_TEXTURE_CUBE_MAP;
				case TextureType::TextureCubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
			}
			return GL_TEXTURE_2D;
		}

		uint32_t ComponentCount(Format format)
		{
			switch (format)
			{
				case Format::R32_SFLOAT:
				case Format::R32_UINT:
				case Format::R32_SINT:            return 1;
				case Format::R32G32_SFLOAT:       return 2;
				case Format::R32G32B32_SFLOAT:    return 3;
				case Format::R32G32B32A32_SFLOAT: return 4;
				default:                          return 4;
			}
		}

		bool IsIntegerFormat(Format format)
		{
			return format == Format::R32_UINT || format == Format::R32_SINT;
		}

		GLenum AttributeType(Format format)
		{
			switch (format)
			{
				case Format::R32_UINT: return GL_UNSIGNED_INT;
				case Format::R32_SINT: return GL_INT;
				default:               return GL_FLOAT;
			}
		}
	}

	// -------------------------------------------------------------------------
	// Buffer
	// -------------------------------------------------------------------------
	OpenGLBufferRHI::OpenGLBufferRHI(OpenGLDevice&, const BufferDesc& desc)
		: RHIBuffer(desc)
	{
		glCreateBuffers(1, &m_Handle);

		if (desc.Memory == MemoryDomain::HostVisible)
		{
			// Persistent coherent mapping mirrors the Vulkan HostVisible path,
			// so per-frame streaming costs a memcpy on both backends rather
			// than a driver-managed orphaning dance.
			const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
			glNamedBufferStorage(m_Handle, (GLsizeiptr)desc.Size, nullptr, flags | GL_DYNAMIC_STORAGE_BIT);
			m_Mapped = glMapNamedBufferRange(m_Handle, 0, (GLsizeiptr)desc.Size, flags);
		}
		else
		{
			glNamedBufferStorage(m_Handle, (GLsizeiptr)desc.Size, nullptr, GL_DYNAMIC_STORAGE_BIT);
		}

		if (!desc.DebugName.empty())
			glObjectLabel(GL_BUFFER, m_Handle, -1, desc.DebugName.c_str());
	}

	OpenGLBufferRHI::~OpenGLBufferRHI()
	{
		if (m_Mapped)
			glUnmapNamedBuffer(m_Handle);
		glDeleteBuffers(1, &m_Handle);
	}

	void OpenGLBufferRHI::Upload(const void* data, uint64_t size, uint64_t offset)
	{
		if (size == 0)
			return;

		if (m_Mapped)
			memcpy((uint8_t*)m_Mapped + offset, data, (size_t)size);
		else
			glNamedBufferSubData(m_Handle, (GLintptr)offset, (GLsizeiptr)size, data);
	}

	// -------------------------------------------------------------------------
	// Sampler
	// -------------------------------------------------------------------------
	OpenGLSamplerRHI::OpenGLSamplerRHI(const SamplerDesc& desc)
		: RHISampler(desc)
	{
		glCreateSamplers(1, &m_Handle);

		// MaxLod defaults high; treat anything above 0 as "mips may exist".
		const bool hasMips = desc.MaxLod > 0.0f;
		glSamplerParameteri(m_Handle, GL_TEXTURE_MIN_FILTER, ToGLMinFilter(desc.MinFilter, desc.Mipmap, hasMips));
		glSamplerParameteri(m_Handle, GL_TEXTURE_MAG_FILTER, desc.MagFilter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR);
		glSamplerParameteri(m_Handle, GL_TEXTURE_WRAP_S, ToGLWrap(desc.WrapU));
		glSamplerParameteri(m_Handle, GL_TEXTURE_WRAP_T, ToGLWrap(desc.WrapV));
		glSamplerParameteri(m_Handle, GL_TEXTURE_WRAP_R, ToGLWrap(desc.WrapW));
		glSamplerParameterf(m_Handle, GL_TEXTURE_MIN_LOD, desc.MinLod);
		glSamplerParameterf(m_Handle, GL_TEXTURE_MAX_LOD, desc.MaxLod);

		// Anisotropic filtering is core in 4.6; the vendored GLAD is generated
		// for core only, so there is no EXT fallback to fall back to.
		if (desc.MaxAnisotropy > 1.0f && GLAD_GL_VERSION_4_6)
			glSamplerParameterf(m_Handle, GL_TEXTURE_MAX_ANISOTROPY, desc.MaxAnisotropy);

		// Comparison sampling: the GLSL side sees sampler2DShadow.
		if (desc.CompareEnable)
		{
			glSamplerParameteri(m_Handle, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(m_Handle, GL_TEXTURE_COMPARE_FUNC, ToGLCompare(desc.Compare));
		}

		float border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		switch (desc.Border)
		{
			case BorderColor::TransparentBlack: border[3] = 0.0f; break;
			case BorderColor::OpaqueBlack:      border[3] = 1.0f; break;
			case BorderColor::OpaqueWhite:      border[0] = border[1] = border[2] = border[3] = 1.0f; break;
		}
		glSamplerParameterfv(m_Handle, GL_TEXTURE_BORDER_COLOR, border);
	}

	OpenGLSamplerRHI::~OpenGLSamplerRHI()
	{
		glDeleteSamplers(1, &m_Handle);
	}

	// -------------------------------------------------------------------------
	// Texture
	// -------------------------------------------------------------------------
	OpenGLTextureRHI::OpenGLTextureRHI(OpenGLDevice&, const TextureDesc& desc)
		: RHITexture(desc)
	{
		if (m_Desc.MipLevels == 0)
			m_Desc.MipLevels = 1 + (uint32_t)std::floor(std::log2(std::max(m_Desc.Width, m_Desc.Height)));

		const GLenum target = ToGLTarget(m_Desc.Type);
		const GLFormat format = ToGLFormat(m_Desc.Format);

		glCreateTextures(target, 1, &m_Handle);

		const bool layered = m_Desc.Type == TextureType::Texture2DArray ||
							 m_Desc.Type == TextureType::TextureCubeArray;
		if (layered)
		{
			glTextureStorage3D(m_Handle, (GLsizei)m_Desc.MipLevels, format.Internal,
							   (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height, (GLsizei)m_Desc.Layers);
		}
		else
		{
			// Cube maps use Storage2D too: the 6 faces are implied by the target.
			glTextureStorage2D(m_Handle, (GLsizei)m_Desc.MipLevels, format.Internal,
							   (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height);
		}

		if (!m_Desc.DebugName.empty())
			glObjectLabel(GL_TEXTURE, m_Handle, -1, m_Desc.DebugName.c_str());
	}

	OpenGLTextureRHI::OpenGLTextureRHI(const TextureDesc& desc, uint32_t handle, bool owned)
		: RHITexture(desc), m_Handle(handle), m_Owned(owned)
	{
	}

	OpenGLTextureRHI::~OpenGLTextureRHI()
	{
		if (m_Owned && m_Handle)
			glDeleteTextures(1, &m_Handle);
	}

	void OpenGLTextureRHI::Upload(const void* data, uint64_t size)
	{
		if (!data || size == 0)
			return;

		const GLFormat format = ToGLFormat(m_Desc.Format);
		glTextureSubImage2D(m_Handle, 0, 0, 0, (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height,
							format.Format, format.Type, data);

		if (m_Desc.MipLevels > 1)
			GenerateMips();
	}

	void OpenGLTextureRHI::UploadLayer(const void* data, uint64_t size, uint32_t layer)
	{
		if (!data || size == 0)
			return;

		const bool isCube = m_Desc.Type == TextureType::TextureCube ||
							m_Desc.Type == TextureType::TextureCubeArray;
		const uint32_t layers = isCube ? std::max(6u, m_Desc.Layers) : m_Desc.Layers;

		if (layer >= layers)
		{
			RV_CORE_WARN("Texture '{0}' has {1} layers; asked to upload layer {2}",
						 m_Desc.DebugName, layers, layer);
			return;
		}

		const GLFormat format = ToGLFormat(m_Desc.Format);

		if (m_Desc.Type == TextureType::Texture2D)
		{
			glTextureSubImage2D(m_Handle, 0, 0, 0, (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height,
								format.Format, format.Type, data);
			return;
		}

		// Cube faces go through the 3D entry point with the face as the Z
		// offset -- the per-face GL_TEXTURE_CUBE_MAP_POSITIVE_X targets are the
		// old bound-texture API and have no DSA equivalent.
		glTextureSubImage3D(m_Handle, 0, 0, 0, (GLint)layer,
							(GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height, 1,
							format.Format, format.Type, data);
	}

	void OpenGLTextureRHI::GenerateMips()
	{
		if (m_Desc.MipLevels > 1)
			glGenerateTextureMipmap(m_Handle);
	}

	// -------------------------------------------------------------------------
	// Shader
	// -------------------------------------------------------------------------
	OpenGLShaderRHI::OpenGLShaderRHI(OpenGLDevice&, const CompiledShader& compiled)
		: RHIShader(compiled.Name, compiled.Reflection)
	{
		// One assignment, shared with the resource sets, so the GLSL and the
		// binding calls cannot disagree about where a resource lives.
		m_Bindings = ShaderCompiler::BuildFlatBindingMap(compiled.Reflection);

		m_Program = glCreateProgram();
		std::vector<uint32_t> stages;

		for (const auto& stage : compiled.Stages)
		{
			auto source = ShaderCompiler::CrossCompileToGLSL(stage, m_Bindings, 450);
			if (!source)
			{
				RV_CORE_ERROR("Cross-compilation failed for {0}:{1}", compiled.Name, ShaderStageName(stage.Stage));
				continue;
			}

			GLenum glStage = GL_VERTEX_SHADER;
			switch (stage.Stage)
			{
				case ShaderStage::Vertex:   glStage = GL_VERTEX_SHADER; break;
				case ShaderStage::Fragment: glStage = GL_FRAGMENT_SHADER; break;
				case ShaderStage::Compute:  glStage = GL_COMPUTE_SHADER; break;
				default: break;
			}

			const uint32_t handle = glCreateShader(glStage);
			const char* text = source->c_str();
			glShaderSource(handle, 1, &text, nullptr);
			glCompileShader(handle);

			GLint compiledOk = GL_FALSE;
			glGetShaderiv(handle, GL_COMPILE_STATUS, &compiledOk);
			if (!compiledOk)
			{
				GLint length = 0;
				glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &length);
				std::string log((size_t)std::max(length, 1), '\0');
				glGetShaderInfoLog(handle, length, &length, log.data());
				RV_CORE_ERROR("GLSL compilation failed for {0}:{1}\n{2}\n--- generated source ---\n{3}",
							  compiled.Name, ShaderStageName(stage.Stage), log, *source);
				glDeleteShader(handle);
				continue;
			}

			glAttachShader(m_Program, handle);
			stages.push_back(handle);
		}

		glLinkProgram(m_Program);

		GLint linked = GL_FALSE;
		glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			GLint length = 0;
			glGetProgramiv(m_Program, GL_INFO_LOG_LENGTH, &length);
			std::string log((size_t)std::max(length, 1), '\0');
			glGetProgramInfoLog(m_Program, length, &length, log.data());
			RV_CORE_ERROR("GLSL link failed for {0}\n{1}", compiled.Name, log);
		}

		for (uint32_t stage : stages)
		{
			glDetachShader(m_Program, stage);
			glDeleteShader(stage);
		}

		if (!compiled.Name.empty())
			glObjectLabel(GL_PROGRAM, m_Program, -1, compiled.Name.c_str());

		if (m_Bindings.PushConstantSize > 0)
		{
			// Sized from reflection and rewritten per draw, so it is dynamic
			// storage rather than immutable.
			glCreateBuffers(1, &m_PushConstantBuffer);
			glNamedBufferStorage(m_PushConstantBuffer, (GLsizeiptr)m_Bindings.PushConstantSize,
								 nullptr, GL_DYNAMIC_STORAGE_BIT);
			glObjectLabel(GL_BUFFER, m_PushConstantBuffer, -1, (compiled.Name + ".pushconstants").c_str());
		}
	}

	OpenGLShaderRHI::~OpenGLShaderRHI()
	{
		if (m_PushConstantBuffer)
			glDeleteBuffers(1, &m_PushConstantBuffer);
		glDeleteProgram(m_Program);
	}

	// -------------------------------------------------------------------------
	// Pipeline
	// -------------------------------------------------------------------------
	OpenGLPipelineRHI::OpenGLPipelineRHI(OpenGLDevice&, const GraphicsPipelineDesc& desc)
		: RHIPipeline(desc)
	{
		m_GLTopology = ToGLTopology(m_Desc.Topology);
		m_ResolvedLayout = m_Desc.VertexInput.Attributes.empty()
						 ? m_Desc.Shader->GetReflection().VertexInput
						 : m_Desc.VertexInput;
		BuildVertexArray();
	}

	OpenGLPipelineRHI::~OpenGLPipelineRHI()
	{
		if (m_VertexArray)
			glDeleteVertexArrays(1, &m_VertexArray);
	}

	uint32_t OpenGLPipelineRHI::GetProgram() const
	{
		return std::static_pointer_cast<OpenGLShaderRHI>(m_Desc.Shader)->GetProgram();
	}

	const FlatBindingMap& OpenGLPipelineRHI::GetBindings() const
	{
		return std::static_pointer_cast<OpenGLShaderRHI>(m_Desc.Shader)->GetBindings();
	}

	uint32_t OpenGLPipelineRHI::GetPushConstantBuffer() const
	{
		return std::static_pointer_cast<OpenGLShaderRHI>(m_Desc.Shader)->GetPushConstantBuffer();
	}

	void OpenGLPipelineRHI::BuildVertexArray()
	{
		// The VAO stores only the vertex *format*; buffers are attached at bind
		// time with glVertexArrayVertexBuffer. This is the separate-format model
		// GL 4.5 added, and it maps cleanly onto the RHI's split between a
		// pipeline's vertex layout and the buffer bound to it.
		glCreateVertexArrays(1, &m_VertexArray);

		for (const auto& attribute : m_ResolvedLayout.Attributes)
		{
			glEnableVertexArrayAttrib(m_VertexArray, attribute.Location);

			const GLenum type = AttributeType(attribute.Format);
			const GLint components = (GLint)ComponentCount(attribute.Format);

			if (IsIntegerFormat(attribute.Format))
			{
				glVertexArrayAttribIFormat(m_VertexArray, attribute.Location, components, type, attribute.Offset);
			}
			else
			{
				glVertexArrayAttribFormat(m_VertexArray, attribute.Location, components, type,
										  GL_FALSE, attribute.Offset);
			}

			glVertexArrayAttribBinding(m_VertexArray, attribute.Location, attribute.Binding);
		}

		for (const auto& binding : m_ResolvedLayout.Bindings)
		{
			glVertexArrayBindingDivisor(m_VertexArray, binding.Binding, binding.PerInstance ? 1 : 0);
		}

		if (!m_Desc.Name.empty())
			glObjectLabel(GL_VERTEX_ARRAY, m_VertexArray, -1, m_Desc.Name.c_str());
	}

	void OpenGLPipelineRHI::Bind()
	{
		glUseProgram(GetProgram());
		glBindVertexArray(m_VertexArray);

		const auto& raster = m_Desc.Rasterizer;
		if (raster.Cull == CullMode::None)
		{
			glDisable(GL_CULL_FACE);
		}
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(raster.Cull == CullMode::Front ? GL_FRONT : GL_BACK);
		}
		glFrontFace(raster.Front == FrontFace::Clockwise ? GL_CW : GL_CCW);
		glPolygonMode(GL_FRONT_AND_BACK, raster.Polygon == PolygonMode::Line ? GL_LINE
									   : raster.Polygon == PolygonMode::Point ? GL_POINT : GL_FILL);

		if (raster.DepthBiasEnable)
		{
			glEnable(GL_POLYGON_OFFSET_FILL);
			// GL takes (slope factor, constant units) -- the reverse order of
			// Vulkan's struct fields.
			glPolygonOffset(raster.DepthBiasSlope, raster.DepthBiasConstant);
		}
		else
		{
			glDisable(GL_POLYGON_OFFSET_FILL);
		}

		const auto& depth = m_Desc.DepthStencil;
		if (depth.DepthTestEnable)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
		glDepthMask(depth.DepthWriteEnable ? GL_TRUE : GL_FALSE);
		glDepthFunc(ToGLCompare(depth.DepthCompare));

		switch (m_Desc.Blend)
		{
			case BlendPreset::Opaque:
				glDisable(GL_BLEND);
				break;
			case BlendPreset::AlphaBlend:
				glEnable(GL_BLEND);
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glBlendEquation(GL_FUNC_ADD);
				break;
			case BlendPreset::Additive:
				glEnable(GL_BLEND);
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
				glBlendEquation(GL_FUNC_ADD);
				break;
			case BlendPreset::PremultipliedAlpha:
				glEnable(GL_BLEND);
				glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glBlendEquation(GL_FUNC_ADD);
				break;
		}
	}

	// -------------------------------------------------------------------------
	// Resource set
	// -------------------------------------------------------------------------
	OpenGLResourceSetRHI::OpenGLResourceSetRHI(OpenGLDevice&, const Ref<OpenGLPipelineRHI>& pipeline, uint32_t set)
		: RHIResourceSet(set), m_Pipeline(pipeline)
	{
	}

	void OpenGLResourceSetRHI::SetUniformBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
												uint64_t offset, uint64_t range)
	{
		const uint32_t point = m_Pipeline->GetBindings().LookupUniformBuffer(m_Set, binding);
		if (point == UINT32_MAX)
		{
			RV_CORE_WARN("No uniform buffer binding for set {0} binding {1}", m_Set, binding);
			return;
		}

		auto glBuffer = std::static_pointer_cast<OpenGLBufferRHI>(buffer);

		BufferBinding entry;
		entry.Point = point;
		entry.Buffer = glBuffer->GetHandle();
		entry.Offset = offset;
		entry.Range = range == 0 ? glBuffer->GetSize() : range;

		for (auto& existing : m_Buffers)
		{
			if (existing.Point == entry.Point)
			{
				existing = entry;
				return;
			}
		}
		m_Buffers.push_back(entry);
	}

	void OpenGLResourceSetRHI::SetTexture(uint32_t binding, const Ref<RHITexture>& texture,
										  const Ref<RHISampler>& sampler, uint32_t arrayIndex)
	{
		const uint32_t base = m_Pipeline->GetBindings().LookupTexture(m_Set, binding);
		if (base == UINT32_MAX)
		{
			RV_CORE_WARN("No texture binding for set {0} binding {1}", m_Set, binding);
			return;
		}

		TextureBinding entry;
		entry.Unit = base + arrayIndex;
		entry.Texture = std::static_pointer_cast<OpenGLTextureRHI>(texture)->GetHandle();
		entry.Sampler = sampler ? std::static_pointer_cast<OpenGLSamplerRHI>(sampler)->GetHandle() : 0;

		for (auto& existing : m_Textures)
		{
			if (existing.Unit == entry.Unit)
			{
				existing = entry;
				return;
			}
		}
		m_Textures.push_back(entry);
	}

	void OpenGLResourceSetRHI::Commit()
	{
		// Nothing to do: GL has no descriptor sets to write. Bindings are
		// replayed by Apply() when the set is bound.
	}

	void OpenGLResourceSetRHI::Apply()
	{
		for (const auto& buffer : m_Buffers)
		{
			glBindBufferRange(GL_UNIFORM_BUFFER, buffer.Point, buffer.Buffer,
							  (GLintptr)buffer.Offset, (GLsizeiptr)buffer.Range);
		}

		for (const auto& texture : m_Textures)
		{
			glBindTextureUnit(texture.Unit, texture.Texture);
			glBindSampler(texture.Unit, texture.Sampler);
		}
	}

	// -------------------------------------------------------------------------
	// Render target
	// -------------------------------------------------------------------------
	OpenGLRenderTargetRHI::OpenGLRenderTargetRHI(OpenGLDevice& device, const RenderTargetDesc& desc)
		: RHIRenderTarget(desc), m_Device(device)
	{
		Build();
	}

	OpenGLRenderTargetRHI::~OpenGLRenderTargetRHI()
	{
		Destroy();
	}

	void OpenGLRenderTargetRHI::Destroy()
	{
		if (m_Framebuffer)
		{
			glDeleteFramebuffers(1, &m_Framebuffer);
			m_Framebuffer = 0;
		}
		m_Color.clear();
		m_Depth.reset();
	}

	void OpenGLRenderTargetRHI::Build()
	{
		glCreateFramebuffers(1, &m_Framebuffer);

		std::vector<GLenum> drawBuffers;
		for (size_t i = 0; i < m_Desc.ColorAttachments.size(); i++)
		{
			TextureDesc textureDesc;
			textureDesc.Width = m_Desc.Width;
			textureDesc.Height = m_Desc.Height;
			textureDesc.Format = m_Desc.ColorAttachments[i].Format;
			textureDesc.Layers = m_Desc.Layers;
			textureDesc.Type = m_Desc.Layers > 1 ? TextureType::Texture2DArray : TextureType::Texture2D;
			textureDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
			textureDesc.DebugName = m_Desc.DebugName + ".color" + std::to_string(i);

			auto texture = std::make_shared<OpenGLTextureRHI>(m_Device, textureDesc);
			glNamedFramebufferTexture(m_Framebuffer, GL_COLOR_ATTACHMENT0 + (GLenum)i, texture->GetHandle(), 0);
			drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + (GLenum)i);
			m_Color.push_back(std::move(texture));
		}

		if (drawBuffers.empty())
		{
			// A depth-only target (a shadow map) must say so explicitly or the
			// framebuffer is incomplete.
			glNamedFramebufferDrawBuffer(m_Framebuffer, GL_NONE);
			glNamedFramebufferReadBuffer(m_Framebuffer, GL_NONE);
		}
		else
		{
			glNamedFramebufferDrawBuffers(m_Framebuffer, (GLsizei)drawBuffers.size(), drawBuffers.data());
		}

		if (m_Desc.HasDepth)
		{
			TextureDesc depthDesc;
			depthDesc.Width = m_Desc.Width;
			depthDesc.Height = m_Desc.Height;
			depthDesc.Format = m_Desc.DepthAttachment.Format == Format::Undefined
							 ? Format::D24_UNORM_S8_UINT
							 : m_Desc.DepthAttachment.Format;
			depthDesc.Layers = m_Desc.Layers;
			depthDesc.Type = m_Desc.Layers > 1 ? TextureType::Texture2DArray : TextureType::Texture2D;
			depthDesc.Usage = TextureUsage::DepthAttachment;
			if (m_Desc.DepthSampled)
				depthDesc.Usage = depthDesc.Usage | TextureUsage::Sampled;
			depthDesc.DebugName = m_Desc.DebugName + ".depth";

			m_Depth = std::make_shared<OpenGLTextureRHI>(m_Device, depthDesc);

			const GLenum attachment = IsStencilFormat(depthDesc.Format) ? GL_DEPTH_STENCIL_ATTACHMENT
																		: GL_DEPTH_ATTACHMENT;
			glNamedFramebufferTexture(m_Framebuffer, attachment, m_Depth->GetHandle(), 0);
		}

		const GLenum status = glCheckNamedFramebufferStatus(m_Framebuffer, GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			RV_CORE_ERROR("Framebuffer '{0}' incomplete: 0x{1:x}", m_Desc.DebugName, (uint32_t)status);
	}

	void OpenGLRenderTargetRHI::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;
		if (width == m_Desc.Width && height == m_Desc.Height)
			return;

		m_Desc.Width = width;
		m_Desc.Height = height;
		Destroy();
		Build();
	}

	Ref<RHITexture> OpenGLRenderTargetRHI::GetColorTexture(uint32_t index) const
	{
		if (index >= m_Color.size())
			return nullptr;
		return m_Color[index];
	}

	// -------------------------------------------------------------------------
	// Command list
	// -------------------------------------------------------------------------
	OpenGLCommandListRHI::OpenGLCommandListRHI(OpenGLDevice& device)
		: m_Device(device)
	{
	}

	void OpenGLCommandListRHI::BeginRenderPass(const RenderPassBeginInfo& info)
	{
		uint32_t width = 0;
		uint32_t height = 0;

		if (info.Target)
		{
			auto* target = static_cast<OpenGLRenderTargetRHI*>(info.Target);
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFramebuffer());
			width = target->GetWidth();
			height = target->GetHeight();
		}
		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			width = m_Device.GetSwapchainWidth();
			height = m_Device.GetSwapchainHeight();
		}

		SetViewport(Viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f });
		SetScissor(Rect2D{ 0, 0, width, height });

		GLbitfield mask = 0;
		if (info.ClearColor)
		{
			glClearColor(info.Clear.Color[0], info.Clear.Color[1], info.Clear.Color[2], info.Clear.Color[3]);
			mask |= GL_COLOR_BUFFER_BIT;
		}
		if (info.ClearDepth && info.UseDepth)
		{
			glClearDepth(info.Clear.Depth);
			// glClear respects the depth mask, so a pipeline left with depth
			// writes disabled would silently skip the clear.
			glDepthMask(GL_TRUE);
			mask |= GL_DEPTH_BUFFER_BIT;
		}
		if (mask)
			glClear(mask);
	}

	void OpenGLCommandListRHI::EndRenderPass()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLCommandListRHI::SetViewport(const Viewport& viewport)
	{
		// The RHI uses Vulkan's negative-height convention to express a flip;
		// GL is already bottom-left, so normalise back to a positive rect.
		const float y = viewport.Height < 0.0f ? viewport.Y + viewport.Height : viewport.Y;
		glViewport((GLint)viewport.X, (GLint)y,
				   (GLsizei)viewport.Width, (GLsizei)std::abs(viewport.Height));
		glDepthRange(viewport.MinDepth, viewport.MaxDepth);
	}

	void OpenGLCommandListRHI::SetScissor(const Rect2D& scissor)
	{
		glScissor(scissor.X, scissor.Y, (GLsizei)scissor.Width, (GLsizei)scissor.Height);
	}

	void OpenGLCommandListRHI::BindPipeline(const Ref<RHIPipeline>& pipeline)
	{
		auto glPipeline = std::static_pointer_cast<OpenGLPipelineRHI>(pipeline);
		glPipeline->Bind();
		m_BoundPipeline = glPipeline.get();
	}

	void OpenGLCommandListRHI::BindResourceSet(uint32_t, const Ref<RHIResourceSet>& resources)
	{
		std::static_pointer_cast<OpenGLResourceSetRHI>(resources)->Apply();
	}

	void OpenGLCommandListRHI::BindVertexBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer, uint64_t offset)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "BindVertexBuffer requires a bound pipeline");

		auto glBuffer = std::static_pointer_cast<OpenGLBufferRHI>(buffer);

		// Stride comes from the pipeline's vertex layout, mirroring Vulkan
		// where it is baked into the pipeline rather than supplied per bind.
		uint32_t stride = 0;
		for (const auto& layoutBinding : m_BoundPipeline->GetDesc().VertexInput.Bindings)
			if (layoutBinding.Binding == binding)
				stride = layoutBinding.Stride;

		if (stride == 0)
		{
			for (const auto& layoutBinding : m_BoundPipeline->GetDesc().Shader->GetReflection().VertexInput.Bindings)
				if (layoutBinding.Binding == binding)
					stride = layoutBinding.Stride;
		}

		glVertexArrayVertexBuffer(m_BoundPipeline->GetVertexArray(), binding, glBuffer->GetHandle(),
								  (GLintptr)offset, (GLsizei)stride);
	}

	void OpenGLCommandListRHI::BindIndexBuffer(const Ref<RHIBuffer>& buffer, IndexType type, uint64_t offset)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "BindIndexBuffer requires a bound pipeline");

		auto glBuffer = std::static_pointer_cast<OpenGLBufferRHI>(buffer);
		glVertexArrayElementBuffer(m_BoundPipeline->GetVertexArray(), glBuffer->GetHandle());

		m_IndexType = type == IndexType::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
		m_IndexOffset = offset;
	}

	void OpenGLCommandListRHI::PushConstants(ShaderStage, uint32_t offset, uint32_t size, const void* data)
	{
		RV_CORE_ASSERT(m_BoundPipeline, "PushConstants requires a bound pipeline");

		const uint32_t buffer = m_BoundPipeline->GetPushConstantBuffer();
		if (!buffer)
		{
			RV_CORE_WARN("Pipeline '{0}' declares no push constants", m_BoundPipeline->GetDesc().Name);
			return;
		}

		// GL has no push constants, so this is a small uniform buffer rewritten
		// per draw. The driver renames the storage behind the scenes when a
		// previous draw still references it, so it is correct -- but it is a
		// buffer update per draw, not the free path Vulkan gives.
		glNamedBufferSubData(buffer, (GLintptr)offset, (GLsizeiptr)size, data);
		glBindBufferBase(GL_UNIFORM_BUFFER, m_BoundPipeline->GetBindings().PushConstantBinding, buffer);
	}

	void OpenGLCommandListRHI::Draw(uint32_t vertexCount, uint32_t instanceCount,
									uint32_t firstVertex, uint32_t firstInstance)
	{
		glDrawArraysInstancedBaseInstance(m_BoundPipeline->GetTopology(), (GLint)firstVertex,
										  (GLsizei)vertexCount, (GLsizei)instanceCount, firstInstance);
	}

	void OpenGLCommandListRHI::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
										   uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
	{
		const size_t indexSize = m_IndexType == GL_UNSIGNED_SHORT ? 2 : 4;
		const void* offset = (const void*)(uintptr_t)(m_IndexOffset + (uint64_t)firstIndex * indexSize);

		glDrawElementsInstancedBaseVertexBaseInstance(
			m_BoundPipeline->GetTopology(), (GLsizei)indexCount, m_IndexType, offset,
			(GLsizei)instanceCount, vertexOffset, firstInstance);
	}

	void OpenGLCommandListRHI::PushDebugGroup(const char* name)
	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
	}

	void OpenGLCommandListRHI::PopDebugGroup()
	{
		glPopDebugGroup();
	}

	// -------------------------------------------------------------------------
	// Device
	// -------------------------------------------------------------------------
	OpenGLDevice::OpenGLDevice(const DeviceDesc& desc)
		: m_Window(desc.Window), m_Width(desc.Width), m_Height(desc.Height), m_VSync(desc.VSync)
	{
		glfwMakeContextCurrent(m_Window);

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			RV_CORE_ERROR("Failed to load OpenGL function pointers");
			throw std::runtime_error("gladLoadGLLoader failed");
		}

		glfwSwapInterval(m_VSync ? 1 : 0);

		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;

		// Scissor is always on so the RHI's SetScissor is meaningful; it is set
		// to the full target at the start of every render pass.
		glEnable(GL_SCISSOR_TEST);

		// Vulkan filters across cube face edges and has no way not to. GL does
		// not unless asked, and the difference shows as a visible cross of
		// seams over a blurred environment map -- so ask once, here, rather
		// than letting the two backends disagree about what a cube map is.
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

		QueryCaps();

		m_CommandList = std::make_unique<OpenGLCommandListRHI>(*this);

		RV_CORE_INFO("OpenGL device ready: {0} ({1})", m_Caps.DeviceName, m_Caps.APIName);
	}

	OpenGLDevice::~OpenGLDevice() = default;

	void OpenGLDevice::QueryCaps()
	{
		m_Caps.DeviceName = (const char*)glGetString(GL_RENDERER);
		m_Caps.APIName = std::string("OpenGL ") + (const char*)glGetString(GL_VERSION);
		m_Caps.DriverInfo = (const char*)glGetString(GL_VENDOR);

		GLint value = 0;
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
		m_Caps.MaxTextureSlots = (uint32_t)value;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
		m_Caps.MaxTextureSize = (uint32_t)value;
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
		m_Caps.UniformBufferAlignment = (uint32_t)value;

		m_Caps.SupportsAnisotropy = GLAD_GL_VERSION_4_6 != 0;
		if (m_Caps.SupportsAnisotropy)
		{
			GLfloat maxAnisotropy = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);
			m_Caps.MaxAnisotropy = maxAnisotropy;
		}

		m_Caps.SupportsDynamicRendering = false;
		m_Caps.SupportsTimestampQueries = true;
		m_Caps.MaxPushConstantSize = 0;
	}

	RHICommandList* OpenGLDevice::BeginFrame()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (width == 0 || height == 0)
			return nullptr;   // minimised

		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;
		return m_CommandList.get();
	}

	void OpenGLDevice::EndFrame()
	{
		// Before the swap, not after: once the buffers are swapped the back
		// buffer's contents are undefined, so reading it then is reading
		// whatever the driver left there.
		if (m_Capture)
		{
			const uint32_t width = m_Width;
			const uint32_t height = m_Height;

			std::vector<uint8_t> pixels((size_t)width * height * 4);

			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadBuffer(GL_BACK);
			glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA,
						 GL_UNSIGNED_BYTE, pixels.data());

			// OpenGL hands back rows bottom-up; the contract is top row first,
			// so that a caller does not have to know which backend produced it.
			std::vector<uint8_t> flipped((size_t)width * height * 4);
			const size_t stride = (size_t)width * 4;
			for (uint32_t y = 0; y < height; y++)
			{
				memcpy(flipped.data() + (size_t)y * stride,
					   pixels.data() + (size_t)(height - 1 - y) * stride, stride);
			}

			// Moved out before invoking: the callback may request another
			// capture, and one that armed itself again here would never stop.
			CaptureCallback callback;
			callback.swap(m_Capture);
			callback(flipped.data(), width, height);
		}

		glfwSwapBuffers(m_Window);
	}

	void OpenGLDevice::RequestCapture(CaptureCallback callback)
	{
		m_Capture = std::move(callback);
	}

	void OpenGLDevice::WaitIdle()
	{
		glFinish();
	}

	void OpenGLDevice::OnResize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

	void OpenGLDevice::SetVSync(bool enabled)
	{
		m_VSync = enabled;
		glfwSwapInterval(enabled ? 1 : 0);
	}

	Ref<RHIBuffer> OpenGLDevice::CreateBuffer(const BufferDesc& desc)
	{
		return std::make_shared<OpenGLBufferRHI>(*this, desc);
	}

	Ref<RHITexture> OpenGLDevice::CreateTexture(const TextureDesc& desc)
	{
		return std::make_shared<OpenGLTextureRHI>(*this, desc);
	}

	Ref<RHISampler> OpenGLDevice::CreateSampler(const SamplerDesc& desc)
	{
		return std::make_shared<OpenGLSamplerRHI>(desc);
	}

	Ref<RHIShader> OpenGLDevice::CreateShader(const CompiledShader& compiled)
	{
		return std::make_shared<OpenGLShaderRHI>(*this, compiled);
	}

	Ref<RHIPipeline> OpenGLDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
	{
		return std::make_shared<OpenGLPipelineRHI>(*this, desc);
	}

	Ref<RHIRenderTarget> OpenGLDevice::CreateRenderTarget(const RenderTargetDesc& desc)
	{
		return std::make_shared<OpenGLRenderTargetRHI>(*this, desc);
	}

	Ref<RHIResourceSet> OpenGLDevice::CreateResourceSet(const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		return std::make_shared<OpenGLResourceSetRHI>(
			*this, std::static_pointer_cast<OpenGLPipelineRHI>(pipeline), set);
	}
}
