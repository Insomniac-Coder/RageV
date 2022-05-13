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

		GLenum dataFormat = 0, internalFormat = 0;
		if (channels == 4)
		{
			dataFormat = GL_RGBA8;
			internalFormat = GL_RGBA;
		}
		else if (channels == 3)
		{
			dataFormat = GL_RGB8;
			internalFormat = GL_RGB;
		}
		RV_CORE_ASSERT(dataFormat & internalFormat, "Image format not supported!");

		glCreateTextures(GL_TEXTURE_2D, 1, &m_ID);
		glTextureStorage2D(m_ID, 1, dataFormat, m_Width, m_Height);
		
		glTextureParameteri(m_ID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_ID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureSubImage2D(m_ID, 0, 0, 0, m_Width, m_Height, internalFormat, GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		glDeleteTextures(1, &m_ID);
	}

	void OpenGLTexture2D::Bind(unsigned int slot) const
	{
		glBindTextureUnit(slot, m_ID);
	}

}