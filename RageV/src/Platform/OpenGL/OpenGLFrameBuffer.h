#pragma once
#include "RageV/Renderer/Framebuffer.h"

namespace RageV
{

	class OpenGLFrameBuffer : public FrameBuffer
	{
	public:
		OpenGLFrameBuffer(const FrameBufferData& fbdata);
		virtual ~OpenGLFrameBuffer();
		virtual void Bind() override;
		virtual void UnBind() override;
		virtual void RenderBuffer() override;
		virtual void Resize(unsigned int width, unsigned int height) override;
		virtual unsigned int GetColorAttachment() override { return m_ColorAttachmentID; }
		virtual const FrameBufferData& GetFrameBufferData() const override { return m_FrameBufferData; }
		virtual FrameBufferData& GetFrameBufferData() override { return m_FrameBufferData; }
	private:
		FrameBufferData m_FrameBufferData;
		unsigned int m_ID = 0;
		unsigned int m_ColorAttachmentID = 0, m_DepthAttachmentID = 0;
	};

}