#include <rvpch.h>
#include "OpenGLBuffer.h"
#include "OpenGLFrameBuffer.h"
#include "glad/glad.h"

namespace RageV
{
	static unsigned int s_MaxFramebuffersize = 8192;
	OpenGLFrameBuffer::OpenGLFrameBuffer(const FrameBufferData& fbdata) : m_FrameBufferData(fbdata)
	{
		RenderBuffer();
	}
	OpenGLFrameBuffer::~OpenGLFrameBuffer()
	{
		glDeleteFramebuffers(1, &m_ID);
		glDeleteTextures(1, &m_ColorAttachmentID);
		glDeleteTextures(1, &m_DepthAttachmentID);
	}

	void OpenGLFrameBuffer::Bind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_ID);
		glViewport(0, 0, m_FrameBufferData.Width, m_FrameBufferData.Height);
	}

	void OpenGLFrameBuffer::UnBind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFrameBuffer::RenderBuffer()
	{
		if (m_ID)
		{
			glDeleteFramebuffers(1, &m_ID);
			glDeleteTextures(1, &m_ColorAttachmentID);
			glDeleteTextures(1, &m_DepthAttachmentID);
		}

		glGenFramebuffers(1, &m_ID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_ID);

		glGenTextures(1, &m_ColorAttachmentID);
		glBindTexture(GL_TEXTURE_2D, m_ColorAttachmentID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_FrameBufferData.Width, m_FrameBufferData.Height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachmentID, 0);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachmentID);
		glBindTexture(GL_TEXTURE_2D, m_DepthAttachmentID);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, m_FrameBufferData.Width, m_FrameBufferData.Height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachmentID, 0);

		RV_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Frame buffer incomplete!");

		UnBind();
	}

	void OpenGLFrameBuffer::Resize(unsigned int width, unsigned int height)
	{
		std::cout << width << ", " << height << std::endl;
		if (width == 0 || height == 0 || width > s_MaxFramebuffersize || height > s_MaxFramebuffersize)
		{
			RV_WARN("Invalid framebuffer size {0} x {1}", width, height);
			return;
		}
		m_FrameBufferData.Width = width;
		m_FrameBufferData.Height = height;

		RenderBuffer();
	}


}