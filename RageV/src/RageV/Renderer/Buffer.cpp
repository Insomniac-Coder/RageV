#pragma once
#include  <rvpch.h>
#include "Buffer.h"
#include "Renderer.h"

#include "Platform/OpenGL/OpenGLBuffer.h"

namespace RageV
{

	VertexBuffer* VertexBuffer::Create(float* vertices, unsigned int size) {
		switch (Renderer::GetAPI())
		{
			case RendererAPI::None:
			{
				RV_ASSERT(false, "No API was chosen!");
				return nullptr;
			}
			case RendererAPI::OpenGL:
			{
				return new OpenGLVertexBuffer(vertices, size);
			}
		}

		return nullptr;
	}

	IndexBuffer* IndexBuffer::Create(unsigned int* indices, unsigned int count) {
		switch (Renderer::GetAPI())
		{
			case RendererAPI::None:
			{
				RV_ASSERT(false, "No API was chosen!");
				return nullptr;
			}
			case RendererAPI::OpenGL:
			{
				return new OpenGLIndexBuffer(indices, count);
			}
		}

		return nullptr;
	}

	BufferElement::BufferElement(const std::string& name, ShaderDataType sType, bool norm)
		: elementName(name), sDataType(sType), normalised(norm), elementSize(GetDataTypeSize(sType)), elementOffset(0), elementCount(GetDataTypeCount(sType))
	{}


	BufferLayout::BufferLayout(const std::initializer_list<BufferElement>& bufferElements)
		:m_BufferElements(bufferElements)
	{
		CalculateStrideAndOffset();
	}

	void BufferLayout::CalculateStrideAndOffset()
	{
		unsigned int offset = 0;
		for (auto& element : m_BufferElements)
		{
			element.SetOffset(offset);
			offset += element.GetSize();
		}
		m_Stride = offset;
	}


}