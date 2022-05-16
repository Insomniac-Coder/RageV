#pragma once
#include "RageV/Renderer/Texture.h"
#include "glad/glad.h"

namespace RageV
{

	class OpenGLTexture2D : public Texture2D
	{

	public:
		OpenGLTexture2D(const std::string& path);
		OpenGLTexture2D(const unsigned int& width, const unsigned int& height);
		virtual ~OpenGLTexture2D();

		virtual void SetData(void* data, const unsigned int& size) override;
		virtual void Bind(unsigned int slot = 0) const override;
		virtual const unsigned int& GetWidth() const override { return m_Width; }
		virtual const unsigned int& GetHeight() const override { return m_Height; }

	private:
		GLenum m_DataFormat, m_InternalFormat;
		unsigned int m_Width, m_Height;
		unsigned int m_ID;
		std::string m_Path;
	};

}