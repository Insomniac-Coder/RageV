#include <rvpch.h>
#include "OpenGLTexture2D.h"
#include "stb_image.h"

namespace RageV
{
	OpenGLTexture2D::OpenGLTexture2D(const std::string& path) : m_Path(path)
	{
		stbi_set_flip_vertically_on_load(1);
		int width, height, channels;

		unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		RV_CORE_ASSERT(data, "Failed to load image");
		m_Width = width;
		m_Height = height;

		if (channels == 4)
		{
			m_DataFormat = GL_RGBA8;
			m_InternalFormat = GL_RGBA;
		}
		else if (channels == 3)
		{
			m_DataFormat = GL_RGB8;
			m_InternalFormat = GL_RGB;
		}
		RV_CORE_ASSERT(dataFormat & internalFormat, "Image format not supported!");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_ID);
		glTextureStorage2D(m_ID, 1, m_DataFormat, m_Width, m_Height);
		
		glTextureParameteri(m_ID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_ID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(m_ID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_ID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureSubImage2D(m_ID, 0, 0, 0, m_Width, m_Height, m_InternalFormat, GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
	}

	OpenGLTexture2D::OpenGLTexture2D(const unsigned int& width, const unsigned int& height)
		: m_Width(width), m_Height(height)
	{
		m_DataFormat = GL_RGBA8;
		m_InternalFormat = GL_RGBA;
		glCreateTextures(GL_TEXTURE_2D, 1, &m_ID);
		glTextureStorage2D(m_ID, 1, m_DataFormat, m_Width, m_Height);

		glTextureParameteri(m_ID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_ID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(m_ID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_ID, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		glDeleteTextures(1, &m_ID);
	}

	void OpenGLTexture2D::SetData(void* data, const unsigned int& size)
	{
		glTextureSubImage2D(m_ID, 0, 0, 0, m_Width, m_Height, m_InternalFormat, GL_UNSIGNED_BYTE, data);
	}

	void OpenGLTexture2D::Bind(unsigned int slot) const
	{
		glBindTextureUnit(slot, m_ID);
	}

}