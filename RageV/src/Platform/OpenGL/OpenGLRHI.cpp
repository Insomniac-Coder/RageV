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

// The S3TC enums are an extension, so the core-profile GLAD header does not
// carry them. The values are ABI constants -- every driver that reports
// GL_EXT_texture_compression_s3tc accepts exactly these.
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

		GLFormat ToGLFormat(Format format)
		{
			switch (format)
			{
				// Compressed: Format/Type stay zero -- uploads go through the
				// glCompressed* entry points, which take the internal format
				// and a byte count instead of a pixel transfer description.
				case Format::BC1_UNORM:  return { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0 };
				case Format::BC1_SRGB:   return { GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, 0, 0 };
				case Format::BC3_UNORM:  return { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0 };
				case Format::BC3_SRGB:   return { GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 0, 0 };
				case Format::BC4_UNORM:  return { GL_COMPRESSED_RED_RGTC1, 0, 0 };
				case Format::BC5_UNORM:  return { GL_COMPRESSED_RG_RGTC2, 0, 0 };
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
				case TextureType::Texture3D:        return GL_TEXTURE_3D;
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
				case Format::R32G32_UINT:
				case Format::R32G32_SINT:         return 2;
				case Format::R32G32B32_UINT:
				case Format::R32G32B32_SINT:      return 3;
				case Format::R32G32B32A32_UINT:
				case Format::R32G32B32A32_SINT:   return 4;
				case Format::R32G32_SFLOAT:       return 2;
				case Format::R32G32B32_SFLOAT:    return 3;
				case Format::R32G32B32A32_SFLOAT: return 4;
				default:                          return 4;
			}
		}

		bool IsIntegerFormat(Format format)
		{
			switch (format)
			{
				case Format::R32_UINT:          case Format::R32_SINT:
				case Format::R32G32_UINT:       case Format::R32G32_SINT:
				case Format::R32G32B32_UINT:    case Format::R32G32B32_SINT:
				case Format::R32G32B32A32_UINT: case Format::R32G32B32A32_SINT:
					return true;
				default:
					return false;
			}
		}

		GLenum AttributeType(Format format)
		{
			switch (format)
			{
				case Format::R32_UINT:
				case Format::R32G32_UINT:
				case Format::R32G32B32_UINT:
				case Format::R32G32B32A32_UINT: return GL_UNSIGNED_INT;
				case Format::R32_SINT:
				case Format::R32G32_SINT:
				case Format::R32G32B32_SINT:
				case Format::R32G32B32A32_SINT: return GL_INT;
				default:                        return GL_FLOAT;
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
			m_Desc.MipLevels = 1 + (uint32_t)Math::Floor(Math::Log2((float)Math::Max(m_Desc.Width, m_Desc.Height)));

		const GLFormat format = ToGLFormat(m_Desc.Format);

		// A multisampled texture is a different target with different storage
		// and no mip chain -- it is a set of coverage samples, not an image
		// anything can filter. Nothing samples one directly here: the render
		// target resolves it into an ordinary texture before anyone asks.
		if (m_Desc.Samples > 1)
		{
			m_Desc.MipLevels = 1;
			glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_Handle);
			glTextureStorage2DMultisample(m_Handle, (GLsizei)m_Desc.Samples, format.Internal,
										  (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height,
										  GL_TRUE);
			if (!m_Desc.DebugName.empty())
				glObjectLabel(GL_TEXTURE, m_Handle, -1, m_Desc.DebugName.c_str());
			return;
		}

		const GLenum target = ToGLTarget(m_Desc.Type);

		glCreateTextures(target, 1, &m_Handle);

		// A volume takes Storage3D as well, and takes its *Depth* rather than
		// its layer count -- which is why the two are separate fields.
		const bool volume = m_Desc.Type == TextureType::Texture3D;
		const bool layered = volume ||
							 m_Desc.Type == TextureType::Texture2DArray ||
							 m_Desc.Type == TextureType::TextureCubeArray;
		if (layered)
		{
			// EffectiveLayers, not Layers: a cube array's depth is its faces,
			// six per cube, and this used to pass the field straight through
			// while the Vulkan path applied the floor. The two agreed for
			// 2D arrays -- the only layered texture that existed -- and would
			// have disagreed for the first cube array either allocated.
			glTextureStorage3D(m_Handle, (GLsizei)m_Desc.MipLevels, format.Internal,
							   (GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height,
							   (GLsizei)(volume ? m_Desc.Depth : EffectiveLayers(m_Desc)));
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

		const uint32_t layers = EffectiveLayers(m_Desc);

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

		// A volume is uploaded whole, every slice in one call: its data is one
		// contiguous block, and `layer` is not a slice index -- it is the
		// argument the layered types use, and matching the Vulkan side means
		// ignoring it here rather than reinterpreting it.
		if (m_Desc.Type == TextureType::Texture3D)
		{
			glTextureSubImage3D(m_Handle, 0, 0, 0, 0,
								(GLsizei)m_Desc.Width, (GLsizei)m_Desc.Height,
								(GLsizei)m_Desc.Depth,
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

	void OpenGLTextureRHI::UploadMip(const void* data, uint64_t size, uint32_t mip,
									 uint32_t layer)
	{
		if (!data || size == 0)
			return;

		if (mip >= m_Desc.MipLevels || layer >= EffectiveLayers(m_Desc))
		{
			RV_CORE_WARN("Texture '{0}' has {1} mips and {2} layers; asked for "
						 "mip {3} of layer {4}", m_Desc.DebugName,
						 m_Desc.MipLevels, EffectiveLayers(m_Desc), mip, layer);
			return;
		}

		const uint32_t width = Math::Max(m_Desc.Width >> mip, 1u);
		const uint32_t height = Math::Max(m_Desc.Height >> mip, 1u);
		const uint64_t expected = TextureDataSize(m_Desc.Format, width, height);

		// Exact, matching the Vulkan path: a short buffer reads out of
		// bounds, a long one means the mip math disagrees.
		if (size != expected)
		{
			RV_CORE_WARN("Texture '{0}' mip {1} is {2}x{3} and takes {4} bytes; "
						 "given {5}", m_Desc.DebugName, mip, width, height,
						 expected, size);
			return;
		}

		const GLFormat format = ToGLFormat(m_Desc.Format);
		const bool layered = m_Desc.Type != TextureType::Texture2D;

		if (IsCompressedFormat(m_Desc.Format))
		{
			if (layered)
				glCompressedTextureSubImage3D(m_Handle, (GLint)mip, 0, 0, (GLint)layer,
											  (GLsizei)width, (GLsizei)height, 1,
											  format.Internal, (GLsizei)size, data);
			else
				glCompressedTextureSubImage2D(m_Handle, (GLint)mip, 0, 0,
											  (GLsizei)width, (GLsizei)height,
											  format.Internal, (GLsizei)size, data);
			return;
		}

		if (layered)
			glTextureSubImage3D(m_Handle, (GLint)mip, 0, 0, (GLint)layer,
								(GLsizei)width, (GLsizei)height, 1,
								format.Format, format.Type, data);
		else
			glTextureSubImage2D(m_Handle, (GLint)mip, 0, 0,
								(GLsizei)width, (GLsizei)height,
								format.Format, format.Type, data);
	}

	void OpenGLTextureRHI::GenerateMips()
	{
		// A compressed image cannot be filtered into existence; its chain
		// arrives via UploadMip or not at all. Same rule as Vulkan's.
		if (IsCompressedFormat(m_Desc.Format))
		{
			RV_CORE_WARN("Texture '{0}' is block-compressed; its mips must be "
						 "uploaded, not generated", m_Desc.DebugName);
			return;
		}

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
				std::string log((size_t)Math::Max(length, 1), '\0');
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
			std::string log((size_t)Math::Max(length, 1), '\0');
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

	OpenGLComputePipelineRHI::OpenGLComputePipelineRHI(OpenGLDevice&, const ComputePipelineDesc& desc)
		: RHIComputePipeline(desc)
	{
	}

	uint32_t OpenGLComputePipelineRHI::GetProgram() const
	{
		return std::static_pointer_cast<OpenGLShaderRHI>(m_Desc.Shader)->GetProgram();
	}

	const FlatBindingMap& OpenGLComputePipelineRHI::GetBindings() const
	{
		return std::static_pointer_cast<OpenGLShaderRHI>(m_Desc.Shader)->GetBindings();
	}

	uint32_t OpenGLComputePipelineRHI::GetPushConstantBuffer() const
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

		// The core profile only guarantees width 1.0, but drivers in practice
		// honour more, and a request beyond the supported range is clamped by
		// the driver rather than being an error -- so this is safe to ask for
		// unconditionally, unlike Vulkan's wideLines feature.
		glLineWidth(raster.LineWidth);

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

		// One preset for every draw buffer, unless the pipeline named them
		// individually. The indexed entry points are GL 4.0; this context is
		// 4.6, and using them unconditionally would be correct but would also
		// rewrite state for attachments that do not exist -- so the single
		// case stays on the plain calls it always used.
		const size_t attachments = Math::Max<size_t>(1, m_Desc.ColorFormats.size());

		if (m_Desc.BlendPerAttachment.empty())
		{
			ApplyBlendState(m_Desc.Blend, -1);
			return;
		}

		for (size_t i = 0; i < attachments; i++)
		{
			const BlendPreset preset = i < m_Desc.BlendPerAttachment.size()
									 ? m_Desc.BlendPerAttachment[i]
									 : m_Desc.Blend;
			ApplyBlendState(preset, (int)i);
		}
	}

	// `drawBuffer` of -1 sets the state for all of them at once.
	void OpenGLPipelineRHI::ApplyBlendState(BlendPreset preset, int drawBuffer)
	{
		const bool indexed = drawBuffer >= 0;
		const GLuint index = indexed ? (GLuint)drawBuffer : 0u;

		const auto enable = [&]()
		{
			if (indexed) glEnablei(GL_BLEND, index);
			else         glEnable(GL_BLEND);
		};
		const auto funcs = [&](GLenum srcColor, GLenum dstColor, GLenum srcAlpha, GLenum dstAlpha)
		{
			if (indexed)
			{
				glBlendFuncSeparatei(index, srcColor, dstColor, srcAlpha, dstAlpha);
				glBlendEquationi(index, GL_FUNC_ADD);
			}
			else
			{
				glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
				glBlendEquation(GL_FUNC_ADD);
			}
		};

		switch (preset)
		{
			case BlendPreset::Opaque:
				if (indexed) glDisablei(GL_BLEND, index);
				else         glDisable(GL_BLEND);
				break;
			case BlendPreset::AlphaBlend:
				enable();
				funcs(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				break;
			case BlendPreset::Additive:
				enable();
				funcs(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
				break;
			case BlendPreset::PremultipliedAlpha:
				enable();
				funcs(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				break;
			case BlendPreset::WeightedAccumulate:
				enable();
				funcs(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
				break;
			case BlendPreset::WeightedRevealage:
				enable();
				funcs(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
				break;
		}
	}

	// -------------------------------------------------------------------------
	// Resource set
	// -------------------------------------------------------------------------
	OpenGLResourceSetRHI::OpenGLResourceSetRHI(OpenGLDevice&, OpenGLPipelineBindings* pipeline,
											   std::shared_ptr<void> owner, uint32_t set)
		: RHIResourceSet(set), m_Pipeline(pipeline), m_Owner(std::move(owner))
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
		entry.Target = GL_UNIFORM_BUFFER;

		for (auto& existing : m_Buffers)
		{
			if (existing.Point == entry.Point && existing.Target == entry.Target)
			{
				existing = entry;
				return;
			}
		}
		m_Buffers.push_back(entry);
	}

	void OpenGLResourceSetRHI::SetStorageBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
												uint64_t offset, uint64_t range)
	{
		// Its own point space, assigned by the same flat map that assigns the
		// uniform ones. Looking a storage buffer up in the uniform table would
		// silently return a point that belongs to something else.
		const uint32_t point = m_Pipeline->GetBindings().LookupStorageBuffer(m_Set, binding);
		if (point == UINT32_MAX)
		{
			RV_CORE_WARN("No storage buffer binding for set {0} binding {1}", m_Set, binding);
			return;
		}

		auto glBuffer = std::static_pointer_cast<OpenGLBufferRHI>(buffer);

		BufferBinding entry;
		entry.Point = point;
		entry.Buffer = glBuffer->GetHandle();
		entry.Offset = offset;
		entry.Range = range == 0 ? glBuffer->GetSize() : range;
		entry.Target = GL_SHADER_STORAGE_BUFFER;

		for (auto& existing : m_Buffers)
		{
			if (existing.Point == entry.Point && existing.Target == entry.Target)
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
			glBindBufferRange(buffer.Target, buffer.Point, buffer.Buffer,
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
		if (m_ResolveFramebuffer)
		{
			glDeleteFramebuffers(1, &m_ResolveFramebuffer);
			m_ResolveFramebuffer = 0;
		}
		m_Color.clear();
		m_Resolve.clear();
		m_Depth.reset();
		m_DepthResolve.reset();
	}

	void OpenGLRenderTargetRHI::Build()
	{
		glCreateFramebuffers(1, &m_Framebuffer);

		// A multisampled target carries a single-sampled twin per colour
		// attachment, in a framebuffer of its own, and blits into it when the
		// pass ends. GetColorTexture hands that twin out -- see the Vulkan
		// side and ENGINE-NOTES 7q; the mechanism differs, the contract does
		// not.
		const bool multisampled = m_Desc.Samples > 1;
		if (multisampled)
			glCreateFramebuffers(1, &m_ResolveFramebuffer);

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
			textureDesc.Samples = m_Desc.Samples;
			textureDesc.DebugName = m_Desc.DebugName + ".color" + std::to_string(i);

			auto texture = std::make_shared<OpenGLTextureRHI>(m_Device, textureDesc);
			glNamedFramebufferTexture(m_Framebuffer, GL_COLOR_ATTACHMENT0 + (GLenum)i, texture->GetHandle(), 0);
			drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + (GLenum)i);
			m_Color.push_back(std::move(texture));

			if (multisampled)
			{
				TextureDesc resolveDesc = textureDesc;
				resolveDesc.Samples = 1;
				resolveDesc.DebugName = m_Desc.DebugName + ".resolve" + std::to_string(i);

				auto resolve = std::make_shared<OpenGLTextureRHI>(m_Device, resolveDesc);
				glNamedFramebufferTexture(m_ResolveFramebuffer,
										  GL_COLOR_ATTACHMENT0 + (GLenum)i, resolve->GetHandle(), 0);
				m_Resolve.push_back(std::move(resolve));
			}
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
			if (multisampled)
				glNamedFramebufferDrawBuffers(m_ResolveFramebuffer,
											  (GLsizei)drawBuffers.size(), drawBuffers.data());
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
			// Depth has to match the colour's sample count or the framebuffer
			// is incomplete -- and a multisampled texture cannot be read by an
			// ordinary sampler2D, so where something samples the depth it gets
			// the same single-sampled twin the colours get. The flag goes on
			// the twin rather than here, which is what makes binding the
			// attachment itself fail loudly instead of quietly.
			depthDesc.Samples = m_Desc.Samples;
			if (m_Desc.DepthSampled && !multisampled)
				depthDesc.Usage = depthDesc.Usage | TextureUsage::Sampled;
			depthDesc.DebugName = m_Desc.DebugName + ".depth";

			m_Depth = std::make_shared<OpenGLTextureRHI>(m_Device, depthDesc);

			const GLenum attachment = IsStencilFormat(depthDesc.Format) ? GL_DEPTH_STENCIL_ATTACHMENT
																		: GL_DEPTH_ATTACHMENT;
			glNamedFramebufferTexture(m_Framebuffer, attachment, m_Depth->GetHandle(), 0);

			// Only when something samples it: an MSAA shadow map does not
			// exist, and a twin nobody reads is a full-size image per frame
			// chain. ENGINE-NOTES 7ai.
			if (multisampled && m_Desc.DepthSampled)
			{
				TextureDesc resolveDesc = depthDesc;
				resolveDesc.Samples = 1;
				resolveDesc.Usage = resolveDesc.Usage | TextureUsage::Sampled;
				resolveDesc.DebugName = m_Desc.DebugName + ".depth.resolve";

				m_DepthResolve = std::make_shared<OpenGLTextureRHI>(m_Device, resolveDesc);
				glNamedFramebufferTexture(m_ResolveFramebuffer, attachment,
										  m_DepthResolve->GetHandle(), 0);
			}
		}

		const GLenum status = glCheckNamedFramebufferStatus(m_Framebuffer, GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE)
			RV_CORE_ERROR("Framebuffer '{0}' incomplete: 0x{1:x}", m_Desc.DebugName, (uint32_t)status);

		// The resolve framebuffer is checked too. A blit into an incomplete
		// one is silently dropped, and the symptom is a depth buffer that
		// reads as whatever the allocation happened to contain.
		if (m_ResolveFramebuffer)
		{
			const GLenum resolveStatus = glCheckNamedFramebufferStatus(m_ResolveFramebuffer,
																	   GL_FRAMEBUFFER);
			if (resolveStatus != GL_FRAMEBUFFER_COMPLETE)
				RV_CORE_ERROR("Resolve framebuffer '{0}' incomplete: 0x{1:x}",
							  m_Desc.DebugName, (uint32_t)resolveStatus);
		}
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

	void OpenGLRenderTargetRHI::ResolveIfNeeded()
	{
		if (!m_ResolveFramebuffer)
			return;

		// One blit per attachment rather than one for all of them: the read
		// buffer selects which colour attachment is the source, and a blit
		// reads exactly one. glBlitNamedFramebuffer with different sample
		// counts *is* the resolve -- there is no shader and no second pass.
		for (uint32_t i = 0; i < (uint32_t)m_Resolve.size(); i++)
		{
			glNamedFramebufferReadBuffer(m_Framebuffer, GL_COLOR_ATTACHMENT0 + i);
			glNamedFramebufferDrawBuffer(m_ResolveFramebuffer, GL_COLOR_ATTACHMENT0 + i);
			glBlitNamedFramebuffer(m_Framebuffer, m_ResolveFramebuffer,
								   0, 0, (GLint)m_Desc.Width, (GLint)m_Desc.Height,
								   0, 0, (GLint)m_Desc.Width, (GLint)m_Desc.Height,
								   GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}

		// And the depth, in its own blit: the bit has to be separate because a
		// combined COLOR|DEPTH blit would carry whichever colour attachment the
		// read buffer happens to name, and the loop above names each in turn.
		// GL_NEAREST is required for depth, and a multisample source resolves
		// by taking one sample rather than averaging -- which is what is
		// wanted, and what the Vulkan side asks for by name. ENGINE-NOTES 7ai.
		if (m_DepthResolve)
		{
			glBlitNamedFramebuffer(m_Framebuffer, m_ResolveFramebuffer,
								   0, 0, (GLint)m_Desc.Width, (GLint)m_Desc.Height,
								   0, 0, (GLint)m_Desc.Width, (GLint)m_Desc.Height,
								   GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}

		// Put the draw buffers back: the next pass binds this framebuffer and
		// expects to write every attachment, and the loop above left it
		// naming one.
		std::vector<GLenum> drawBuffers;
		for (uint32_t i = 0; i < (uint32_t)m_Color.size(); i++)
			drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
		if (!drawBuffers.empty())
		{
			glNamedFramebufferDrawBuffers(m_Framebuffer,
										  (GLsizei)drawBuffers.size(), drawBuffers.data());
			glNamedFramebufferDrawBuffers(m_ResolveFramebuffer,
										  (GLsizei)drawBuffers.size(), drawBuffers.data());
		}
	}

	Ref<RHITexture> OpenGLRenderTargetRHI::GetColorTexture(uint32_t index) const
	{
		// The resolve, when there is one: a caller asking for "the colour" of
		// this target wants something it can sample, and a multisampled
		// texture is not that.
		if (index < m_Resolve.size())
			return m_Resolve[index];

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
		m_InRenderPass = true;

		uint32_t width = 0;
		uint32_t height = 0;

		if (info.Target)
		{
			auto* target = static_cast<OpenGLRenderTargetRHI*>(info.Target);
			glBindFramebuffer(GL_FRAMEBUFFER, target->GetFramebuffer());
			width = target->GetWidth();
			height = target->GetHeight();
			// Remembered so EndRenderPass can resolve it. The pass info is
			// gone by then, and a target that is never resolved reads back as
			// whatever the twin held last frame.
			m_BoundTarget = target;
		}
		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			width = m_Device.GetSwapchainWidth();
			height = m_Device.GetSwapchainHeight();
		}

		SetViewport(Viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f });
		SetScissor(Rect2D{ 0, 0, width, height });

		// glDrawBuffers decides which attachments the fragment shader's outputs
		// land in, and the clears below address them by index into *this* list
		// rather than into the framebuffer -- selecting attachments 1 and 2
		// means the shader's location 0 writes attachment 1.
		//
		// Set unconditionally, because it is framebuffer state that persists:
		// a pass that bound a subset would otherwise leave the next pass over
		// the same target writing into the subset it chose.
		if (info.Target)
		{
			auto* target = static_cast<OpenGLRenderTargetRHI*>(info.Target);

			std::vector<GLenum> buffers;
			if (info.ColorAttachments.empty())
			{
				for (uint32_t i = 0; i < target->GetColorCount(); i++)
					buffers.push_back(GL_COLOR_ATTACHMENT0 + i);
			}
			else
			{
				for (const ColorBinding& binding : info.ColorAttachments)
					buffers.push_back(GL_COLOR_ATTACHMENT0 + binding.Index);
			}

			if (!buffers.empty())
				glDrawBuffers((GLsizei)buffers.size(), buffers.data());
		}

		if (info.ClearColor)
		{
			if (info.Target && !info.ColorAttachments.empty())
			{
				// Per attachment, because the two halves of weighted blending
				// start from different values -- accumulation at zero and
				// revealage at one -- and one glClearColor cannot say both.
				//
				// The index here is into the *draw buffer list* just set, not
				// into the framebuffer's attachments, which is the one thing
				// about glClearBufferfv that is easy to get backwards.
				for (GLint i = 0; i < (GLint)info.ColorAttachments.size(); i++)
					glClearBufferfv(GL_COLOR, i, info.ColorAttachments[i].Clear);
			}
			else
			{
				glClearColor(info.Clear.Color[0], info.Clear.Color[1],
							 info.Clear.Color[2], info.Clear.Color[3]);
				glClear(GL_COLOR_BUFFER_BIT);
			}
		}

		if (info.ClearDepth && info.UseDepth)
		{
			glClearDepth(info.Clear.Depth);
			// glClear respects the depth mask, so a pipeline left with depth
			// writes disabled would silently skip the clear.
			glDepthMask(GL_TRUE);
			glClear(GL_DEPTH_BUFFER_BIT);
		}
	}

	void OpenGLCommandListRHI::EndRenderPass()
	{
		// Resolve before unbinding, and on *every* pass that wrote the target
		// rather than once at the end of the frame. Wasteful when a target is
		// written four times and read once -- and correct without the graph
		// having to know which of those writes was the last, which it does not
		// currently track. ENGINE-NOTES 7q.
		if (m_BoundTarget)
			m_BoundTarget->ResolveIfNeeded();
		m_BoundTarget = nullptr;

		m_InRenderPass = false;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLCommandListRHI::SetViewport(const Viewport& viewport)
	{
		// The RHI uses Vulkan's negative-height convention to express a flip;
		// GL is already bottom-left, so normalise back to a positive rect.
		const float y = viewport.Height < 0.0f ? viewport.Y + viewport.Height : viewport.Y;
		glViewport((GLint)viewport.X, (GLint)y,
				   (GLsizei)viewport.Width, (GLsizei)Math::Abs(viewport.Height));
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
		m_BoundBindings = glPipeline.get();
	}

	void OpenGLCommandListRHI::BindComputePipeline(const Ref<RHIComputePipeline>& pipeline)
	{
		auto glPipeline = std::static_pointer_cast<OpenGLComputePipelineRHI>(pipeline);

		// Just the program: there is no vertex array, no raster state and no
		// blend to apply, which is the whole of what OpenGLPipelineRHI::Bind
		// does beyond this line.
		glUseProgram(glPipeline->GetProgram());

		// The graphics pipeline stays unbound rather than stale: a dispatch
		// followed by a draw must rebind, and leaving the old pointer here
		// would let BindVertexBuffer read a vertex layout that is no longer
		// the bound program's.
		m_BoundPipeline = nullptr;
		m_BoundBindings = glPipeline.get();
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
		// Whichever kind was bound last: a dispatch pushes constants the same
		// way a draw does.
		RV_CORE_ASSERT(m_BoundBindings, "PushConstants requires a bound pipeline");

		const uint32_t buffer = m_BoundBindings->GetPushConstantBuffer();
		if (!buffer)
		{
			RV_CORE_WARN("The bound pipeline declares no push constants");
			return;
		}

		// GL has no push constants, so this is a small uniform buffer rewritten
		// per draw. The driver renames the storage behind the scenes when a
		// previous draw still references it, so it is correct -- but it is a
		// buffer update per draw, not the free path Vulkan gives.
		glNamedBufferSubData(buffer, (GLintptr)offset, (GLsizeiptr)size, data);
		glBindBufferBase(GL_UNIFORM_BUFFER, m_BoundBindings->GetBindings().PushConstantBinding, buffer);
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

	void OpenGLCommandListRHI::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
	{
		RV_CORE_ASSERT(m_BoundBindings, "Dispatch requires a bound compute pipeline");
		// GL would allow it. Vulkan would not, and a rule enforced on one
		// backend only is a Vulkan-only failure discovered late.
		RV_CORE_ASSERT(!m_InRenderPass, "Dispatch must be recorded outside a render pass");

		if (groupsX == 0 || groupsY == 0 || groupsZ == 0)
			return;

		glDispatchCompute(groupsX, groupsY, groupsZ);
	}

	void OpenGLCommandListRHI::BufferBarrier(const Ref<RHIBuffer>&, BufferSync, BufferSync)
	{
		// GL synchronises by what happens *after* the barrier rather than by a
		// pair of accesses on a named resource, so the buffer and the source
		// side have no expression here -- and the honest translation of the
		// RHI's pair is the union of what any of its `to` values can mean.
		//
		// Naming every relevant bit rather than the one this call implies:
		// glMemoryBarrier is a full pipeline flush of the named categories
		// whatever is passed, so a narrower mask would cost the same and
		// protect less.
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
					  | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
					  | GL_ELEMENT_ARRAY_BARRIER_BIT
					  | GL_UNIFORM_BARRIER_BIT
					  | GL_COMMAND_BARRIER_BIT
					  | GL_BUFFER_UPDATE_BARRIER_BIT);
	}

	void OpenGLCommandListRHI::WriteTimestamp(uint32_t slot)
	{
		m_Device.RecordTimestamp(slot);
	}

	void OpenGLCommandListRHI::GenerateMips(const Ref<RHITexture>& texture)
	{
		// GL has one queue and no recording, so issuing it here is already in
		// order with everything before it -- which is why this bug was only
		// ever visible on Vulkan.
		if (texture)
			texture->GenerateMips();
	}

	void OpenGLCommandListRHI::CopyToTextureLayer(const Ref<RHITexture>& source,
												  const Ref<RHITexture>& destination,
												  uint32_t layer, uint32_t mip)
	{
		if (!source || !destination)
			return;

		const uint32_t width = source->GetWidth();
		const uint32_t height = source->GetHeight();

		// The destination's own size at this mip. See the Vulkan path's note:
		// using the source's rectangle for both ends is right only while the
		// two agree, and a resampling copy -- a bigger probe filtered into a
		// smaller array slice -- would otherwise fill a corner and leave the
		// rest of the slice untouched.
		const uint32_t dstWidth  = Math::Max(destination->GetWidth() >> mip, 1u);
		const uint32_t dstHeight = Math::Max(destination->GetHeight() >> mip, 1u);

		if (!m_CopyRead) glCreateFramebuffers(1, &m_CopyRead);
		if (!m_CopyDraw) glCreateFramebuffers(1, &m_CopyDraw);

		const uint32_t src = std::static_pointer_cast<OpenGLTextureRHI>(source)->GetHandle();
		const uint32_t dst = std::static_pointer_cast<OpenGLTextureRHI>(destination)->GetHandle();

		// Depth as well as colour: a point light's shadow is a cube of it.
		const bool depth = IsDepthFormat(source->GetFormat());
		const GLenum attachment = depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
		const GLbitfield mask = depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;

		// Detach everything first.
		//
		// These two framebuffers are shared by every copy: a reflection probe's
		// colour faces and a point light's depth faces both come through here,
		// at different sizes. A stale attachment of the wrong size is not an
		// error in GL 4.5 -- the blittable region becomes the intersection --
		// so the copy silently moves a corner of the image and the rest of the
		// face keeps whatever it had. That is a whole feature quietly half
		// working, with nothing reported anywhere.
		glNamedFramebufferTexture(m_CopyRead, GL_COLOR_ATTACHMENT0, 0, 0);
		glNamedFramebufferTexture(m_CopyRead, GL_DEPTH_ATTACHMENT, 0, 0);
		glNamedFramebufferTexture(m_CopyDraw, GL_COLOR_ATTACHMENT0, 0, 0);
		glNamedFramebufferTexture(m_CopyDraw, GL_DEPTH_ATTACHMENT, 0, 0);

		glNamedFramebufferTexture(m_CopyRead, attachment, src, 0);
		glNamedFramebufferTextureLayer(m_CopyDraw, attachment, dst, (GLint)mip, (GLint)layer);

		// A framebuffer with no colour attachment has to say so explicitly, or
		// it is incomplete and the blit silently does nothing.
		glNamedFramebufferReadBuffer(m_CopyRead, depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
		glNamedFramebufferDrawBuffer(m_CopyDraw, depth ? GL_NONE : GL_COLOR_ATTACHMENT0);

		// Scissor is on for the life of the context so the RHI's SetScissor
		// means something, and a blit is scissored like anything else. Off for
		// the duration, or a face inherits whatever rectangle the last pass set.
		glDisable(GL_SCISSOR_TEST);

		// Straight through, no flip. A face is captured with the camera basis
		// every cube-map tutorial uses, which puts the face's first texel row
		// at the bottom of the rendered image -- and bottom-up is exactly how
		// this backend stores one. The Vulkan path is where the difference is
		// paid, because its viewport is flipped.
		// GL_LINEAR is illegal for a depth blit and pointless for a copy that
		// is not rescaling, which is every caller that predates cube arrays.
		const bool rescaling = dstWidth != width || dstHeight != height;
		const GLenum filter = (depth || !rescaling) ? GL_NEAREST : GL_LINEAR;

		glBlitNamedFramebuffer(m_CopyRead, m_CopyDraw,
							   0, 0, (GLint)width, (GLint)height,
							   0, 0, (GLint)dstWidth, (GLint)dstHeight,
							   mask, filter);

		glEnable(GL_SCISSOR_TEST);
	}

	void OpenGLCommandListRHI::CopyStripToTextureLayers(const Ref<RHITexture>& source,
														const Ref<RHITexture>& destination,
														uint32_t baseLayer, uint32_t layerCount,
														uint32_t mip)
	{
		if (!source || !destination || layerCount == 0)
			return;

		const uint32_t sliceWidth = source->GetWidth() / layerCount;
		const uint32_t height = source->GetHeight();
		const uint32_t dstWidth  = Math::Max(destination->GetWidth() >> mip, 1u);
		const uint32_t dstHeight = Math::Max(destination->GetHeight() >> mip, 1u);

		if (!m_CopyRead) glCreateFramebuffers(1, &m_CopyRead);
		if (!m_CopyDraw) glCreateFramebuffers(1, &m_CopyDraw);

		const uint32_t src = std::static_pointer_cast<OpenGLTextureRHI>(source)->GetHandle();
		const uint32_t dst = std::static_pointer_cast<OpenGLTextureRHI>(destination)->GetHandle();

		const bool depth = IsDepthFormat(source->GetFormat());
		const GLenum attachment = depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
		const GLbitfield mask = depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT;

		// Same detach-everything discipline as CopyToTextureLayer, for the same
		// reason: a stale attachment of another size makes the blit a corner.
		glNamedFramebufferTexture(m_CopyRead, GL_COLOR_ATTACHMENT0, 0, 0);
		glNamedFramebufferTexture(m_CopyRead, GL_DEPTH_ATTACHMENT, 0, 0);
		glNamedFramebufferTexture(m_CopyDraw, GL_COLOR_ATTACHMENT0, 0, 0);
		glNamedFramebufferTexture(m_CopyDraw, GL_DEPTH_ATTACHMENT, 0, 0);
		glNamedFramebufferTexture(m_CopyRead, attachment, src, 0);
		glNamedFramebufferReadBuffer(m_CopyRead, depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
		glNamedFramebufferDrawBuffer(m_CopyDraw, depth ? GL_NONE : GL_COLOR_ATTACHMENT0);

		glDisable(GL_SCISSOR_TEST);

		const bool rescaling = dstWidth != sliceWidth || dstHeight != height;
		const GLenum filter = (depth || !rescaling) ? GL_NEAREST : GL_LINEAR;

		// One blit per slice; the source stays attached and only the
		// destination layer moves. Straight through, no flip -- see
		// CopyToTextureLayer for why this backend needs none.
		for (uint32_t i = 0; i < layerCount; i++)
		{
			glNamedFramebufferTextureLayer(m_CopyDraw, attachment, dst, (GLint)mip,
										   (GLint)(baseLayer + i));
			glBlitNamedFramebuffer(m_CopyRead, m_CopyDraw,
								   (GLint)(i * sliceWidth), 0,
								   (GLint)((i + 1) * sliceWidth), (GLint)height,
								   0, 0, (GLint)dstWidth, (GLint)dstHeight,
								   mask, filter);
		}

		glEnable(GL_SCISSOR_TEST);
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
		// DeviceDesc carries the window as an opaque handle so the public
		// header names no GLFW type. This is the boundary where it becomes one
		// again, and the only place that assumes GLFW is the windowing library.
		: m_Window(static_cast<GLFWwindow*>(desc.Window)),
		  m_Width(desc.Width), m_Height(desc.Height), m_VSync(desc.VSync),
		  m_FramesInFlight(Math::Clamp(desc.FramesInFlight, 1u, 3u))
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

		// Depth in [0, 1], matching Vulkan and matching the projections RageV::Math
		// produces -- it is built with GLM_FORCE_DEPTH_ZERO_TO_ONE. Without
		// this, GL maps that same clip range onto [0.5, 1] of the depth buffer:
		// half the precision thrown away, and, worse, a stored depth that no
		// longer equals the value a shader computes from the same matrix.
		//
		// That second part is not theoretical. Every shadow comparison passed
		// on this backend and nothing was ever in shadow, because the reference
		// was a clip-space depth and the map held a window-space one. The
		// vendored CMake has claimed since the port that "the OpenGL backend
		// compensates once at the swapchain"; this is the line that finally
		// makes that true.
		//
		// LOWER_LEFT is kept. Flipping the origin here would invert every
		// render target's row order and move the problem rather than solve it.
		if (GLAD_GL_VERSION_4_5)
			glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
		else
			RV_CORE_ERROR("OpenGL 4.5 is required for glClipControl; depth will not match Vulkan");

		QueryCaps();
		CreateTimestampQueries();

		m_FrameFences.assign(m_FramesInFlight, nullptr);

		m_CommandList = std::make_unique<OpenGLCommandListRHI>(*this);

		RV_CORE_INFO("OpenGL device ready: {0} ({1})", m_Caps.DeviceName, m_Caps.APIName);
	}

	OpenGLDevice::~OpenGLDevice()
	{
		// Every outstanding fence, and the query objects. A sync object left
		// behind keeps a driver-side allocation alive for the life of the
		// context, which for a tool that opens and closes devices in a loop --
		// scenetest does -- accumulates.
		for (void* fence : m_FrameFences)
		{
			if (fence)
				glDeleteSync((GLsync)fence);
		}
		m_FrameFences.clear();

		for (int half = 0; half < 2; half++)
		{
			if (!m_TimestampQueries[half].empty())
			{
				glDeleteQueries((GLsizei)m_TimestampQueries[half].size(),
								m_TimestampQueries[half].data());
				m_TimestampQueries[half].clear();
			}
		}
	}

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

		// BC1/BC3 ride the S3TC extension (RGTC, for BC4/BC5, is core).
		// Every desktop driver has shipped it for two decades, but the
		// imageCubeArray lesson stands: state the requirement where the
		// device is examined, so the machine that lacks it says so at boot
		// instead of showing black textures with no explanation.
		{
			GLint extensionCount = 0;
			glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);

			bool s3tc = false;
			for (GLint i = 0; i < extensionCount && !s3tc; i++)
			{
				const char* name = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
				s3tc = name && std::strcmp(name, "GL_EXT_texture_compression_s3tc") == 0;
			}

			if (!s3tc)
				RV_CORE_ERROR("This driver lacks GL_EXT_texture_compression_s3tc; "
							  "cooked BC1/BC3 textures will not load");
		}

		m_Caps.SupportsDynamicRendering = false;
		m_Caps.SupportsTimestampQueries = true;
		m_Caps.MaxPushConstantSize = 0;

		// Compute is core in 4.3 and this context is 4.6, so this is a
		// statement rather than a question -- but it is asked, because the
		// alternative is a dispatch that silently does nothing.
		m_Caps.SupportsCompute = GLAD_GL_VERSION_4_3 != 0;
		if (m_Caps.SupportsCompute)
		{
			GLint workGroupSize = 0;
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSize);
			m_Caps.MaxComputeWorkGroupSize = (uint32_t)workGroupSize;

			GLint workGroupCount = 0;
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &workGroupCount);
			m_Caps.MaxComputeWorkGroupCount = (uint32_t)workGroupCount;
		}
	}

	void OpenGLDevice::CreateTimestampQueries()
	{
		m_TimestampsSupported = m_Caps.SupportsTimestampQueries;
		if (!m_TimestampsSupported)
		{
			RV_CORE_WARN("This context does not report timestamp queries; GPU timings "
						 "will read zero");
			return;
		}

		for (int half = 0; half < 2; half++)
		{
			m_TimestampQueries[half].assign(RHIDevice::kTimestampSlots, 0);
			m_TimestampWritten[half].assign(RHIDevice::kTimestampSlots, 0);
			glGenQueries((GLsizei)RHIDevice::kTimestampSlots, m_TimestampQueries[half].data());
		}

		m_ResolvedTicks.assign(RHIDevice::kTimestampSlots, 0);
		m_ResolvedWritten.assign(RHIDevice::kTimestampSlots, 0);
	}

	void OpenGLDevice::RecordTimestamp(uint32_t slot)
	{
		if (!m_TimestampsSupported || slot >= RHIDevice::kTimestampSlots)
			return;

		glQueryCounter(m_TimestampQueries[m_TimestampRing][slot], GL_TIMESTAMP);
		m_TimestampWritten[m_TimestampRing][slot] = 1;
	}

	void OpenGLDevice::RecycleTimestampQueries()
	{
		if (!m_TimestampsSupported)
			return;

		// The half that is *not* about to be written -- last frame's. Its
		// results are ready because a frame has been submitted and swapped
		// since, so nothing here blocks.
		const uint32_t previous = 1u - m_TimestampRing;

		for (uint32_t i = 0; i < RHIDevice::kTimestampSlots; i++)
		{
			m_ResolvedWritten[i] = 0;
			m_ResolvedTicks[i] = 0;

			if (!m_TimestampWritten[previous][i])
				continue;

			// Asked rather than assumed. A query that is not ready would block
			// on glGetQueryObjectui64v, and a profiler that stalls the thread
			// it is measuring reports its own cost.
			GLint ready = 0;
			glGetQueryObjectiv(m_TimestampQueries[previous][i], GL_QUERY_RESULT_AVAILABLE, &ready);
			if (!ready)
				continue;

			GLuint64 ticks = 0;
			glGetQueryObjectui64v(m_TimestampQueries[previous][i], GL_QUERY_RESULT, &ticks);
			m_ResolvedTicks[i] = (uint64_t)ticks;
			m_ResolvedWritten[i] = 1;
		}

		// This frame writes the half just read.
		m_TimestampRing = previous;
		std::fill(m_TimestampWritten[m_TimestampRing].begin(),
				  m_TimestampWritten[m_TimestampRing].end(), (uint8_t)0);
	}

	void OpenGLDevice::WaitForFrameSlot()
	{
		GLsync fence = (GLsync)m_FrameFences[m_FrameIndex];
		if (!fence)
			return;

		// GL_SYNC_FLUSH_COMMANDS_BIT, or a fence that has not been flushed to
		// the driver can never signal and this waits for the timeout every
		// frame. One second: long enough that no real frame reaches it, short
		// enough that a lost fence is a stutter rather than a hang.
		const GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
		if (result == GL_TIMEOUT_EXPIRED)
			RV_CORE_WARN("A frame's GPU work did not complete within a second");

		glDeleteSync(fence);
		m_FrameFences[m_FrameIndex] = nullptr;
	}

	RHICommandList* OpenGLDevice::BeginFrame()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (width == 0 || height == 0)
			return nullptr;   // minimised

		m_Width = (uint32_t)width;
		m_Height = (uint32_t)height;

		// Before anything writes into this slot's buffers.
		WaitForFrameSlot();

		RecycleTimestampQueries();

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

		// After every command this frame issued. The next pass through this
		// slot waits on it before touching the buffers those commands read.
		if (GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0))
			m_FrameFences[m_FrameIndex] = fence;

		m_FrameIndex = (m_FrameIndex + 1) % m_FramesInFlight;
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
		auto concrete = std::static_pointer_cast<OpenGLPipelineRHI>(pipeline);
		return std::make_shared<OpenGLResourceSetRHI>(*this, concrete.get(), concrete, set);
	}

	Ref<RHIResourceSet> OpenGLDevice::CreateResourceSet(const Ref<RHIComputePipeline>& pipeline,
													   uint32_t set)
	{
		auto concrete = std::static_pointer_cast<OpenGLComputePipelineRHI>(pipeline);
		return std::make_shared<OpenGLResourceSetRHI>(*this, concrete.get(), concrete, set);
	}

	Ref<RHIResourceSet> OpenGLDevice::CreateBindlessTextureSet(uint32_t)
	{
		// Stated once rather than silently, so a caller that forgot to ask the
		// caps first learns why it got nothing.
		RV_CORE_INFO("[OpenGL] bindless texture heap unavailable on this backend; "
					 "the bound path is used");
		return nullptr;
	}

	void OpenGLDevice::ExecuteImmediate(const std::function<void(RHICommandList&)>& record)
	{
		if (!record)
			return;

		// GL has one queue and no recording, so "record" is "issue" and the
		// only part with any content is the wait. glFinish rather than
		// glFlush: the contract is that the work has *finished* when this
		// returns, which is the whole reason a caller reaches for it.
		OpenGLCommandListRHI list(*this);
		record(list);
		glFinish();
	}

	Ref<RHIComputePipeline> OpenGLDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
	{
		if (!desc.Shader)
			return nullptr;

		// The program was linked when the shader was created; a shader with no
		// compute stage links a program that cannot be dispatched.
		if (!HasFlag(desc.Shader->GetReflection().Stages, ShaderStage::Compute))
		{
			RV_CORE_ERROR("'{0}' has no compute stage; no compute pipeline was created",
						  desc.Name);
			return nullptr;
		}

		return std::make_shared<OpenGLComputePipelineRHI>(*this, desc);
	}
}
