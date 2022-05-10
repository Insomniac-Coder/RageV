#pragma once

namespace RageV
{

	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		static VertexBuffer* Create(float* vertices, unsigned int size);
	};

	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		static IndexBuffer* Create(unsigned int* indices, unsigned int count);
	};

}