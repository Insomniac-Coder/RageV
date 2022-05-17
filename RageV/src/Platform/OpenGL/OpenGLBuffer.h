#pragma once
#include "RageV/Renderer/Buffer.h"

namespace RageV
{

	class OpenGLVertexBuffer : public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(unsigned int size);
		OpenGLVertexBuffer(float* vertices, unsigned int size);

		virtual ~OpenGLVertexBuffer();
		virtual void Bind() const override;
		virtual void UnBind() const override;
		virtual const BufferLayout& GetBufferLayout() const override { return m_Layout; };
		virtual void SetBufferLayout(const BufferLayout& bufferLayout) { m_Layout = bufferLayout; };
		virtual void SetData(const void* data, unsigned int size) override;

	private:
		unsigned int m_ID;
		BufferLayout m_Layout;
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

	class OpenGLVertexArray : public VertexArray
	{
	public:
		OpenGLVertexArray();
		~OpenGLVertexArray();
		virtual void Bind() const override;
		virtual void UnBind() const override;
		virtual const std::vector <std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
		virtual const std::shared_ptr<IndexBuffer> GetIndexBuffer() const  override { return m_IndexBuffer; }
		virtual const void AddVertexBuffer(std::shared_ptr<VertexBuffer> vertexBuffer) override;
		virtual const void SetIndexBuffer(std::shared_ptr<IndexBuffer> indexBuffer) override { m_IndexBuffer = indexBuffer; }
	private:
		std::vector<std::shared_ptr<VertexBuffer>> m_VertexBuffers;
		std::shared_ptr<IndexBuffer> m_IndexBuffer;
		unsigned int m_ID;
	};

}