#pragma once
#include <memory>

namespace RageV
{

	struct FrameBufferData
	{
		unsigned int Width = 1280;
		unsigned int Height = 720;
		unsigned int Samples = 1;
	};

	class FrameBuffer
	{
	public:
		virtual ~FrameBuffer() {}
		virtual void Bind() = 0;
		virtual void UnBind() = 0;
		virtual void RenderBuffer() = 0;
		virtual void Resize(unsigned int width, unsigned int height) = 0;
		virtual unsigned int GetColorAttachment() = 0;
		virtual const FrameBufferData& GetFrameBufferData() const = 0;
		virtual FrameBufferData& GetFrameBufferData() = 0;

		static std::shared_ptr<FrameBuffer> Create(const FrameBufferData& framebufferdata);
	};

}