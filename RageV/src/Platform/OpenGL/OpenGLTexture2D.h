#pragma once
#include "RageV/Renderer/Texture.h"
#include "glad/glad.h"

namespace RageV
{

	class OpenGLTexture2D : public Texture2D
	{

	public:
		OpenGLTexture2D(const std::string& path);
		virtual ~OpenGLTexture2D();

		virtual void Bind(unsigned int slot = 0) const override;
		virtual const unsigned int& GetWidth() const override { return m_Width; }
		virtual const unsigned int& GetHeight() const override { return m_Height; }

	private:
		unsigned int m_Width, m_Height;
		unsigned int m_ID;
		std::string m_Path;
	};

}