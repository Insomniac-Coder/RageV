#pragma once
#include "RageV/Renderer/Buffer.h"

namespace RageV
{

	class OpenGLVertexBuffer : public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(float* vertices, unsigned int size);

		virtual ~OpenGLVertexBuffer();
		virtual void Bind() const override;
		virtual void UnBind() const override;

	private:
		unsigned int m_ID;
	};

	class OpenGLIndexBuffer : public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(unsigned int* indices, unsigned int count);

		virtual ~OpenGLIndexBuffer();
		virtual void Bind() const override;
		virtual void UnBind() const override;
		inline virtual unsigned int GetCount() const { return m_Count; }

	private:
		unsigned int m_ID;
		unsigned int m_Count;
	};

}