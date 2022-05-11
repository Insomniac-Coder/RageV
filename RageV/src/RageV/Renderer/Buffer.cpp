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

	static unsigned int GetDataTypeSize(ShaderDataType& sDataType)
	{
		switch (sDataType)
		{
		case ShaderDataType::Float:		return 4;
		case ShaderDataType::Float2:	return 4 * 2;
		case ShaderDataType::Float3:	return 4 * 3;
		case ShaderDataType::Float4:	return 4 * 4;
		case ShaderDataType::Mat3:		return 4 * 3 * 3;
		case ShaderDataType::Mat4:		return 4 * 4 * 4;
		case ShaderDataType::Int:		return 4;
		case ShaderDataType::Int2:		return 4 * 2;
		case ShaderDataType::Int3:		return 4 * 3;
		case ShaderDataType::Int4:		return 4 * 4;
		case ShaderDataType::Bool:		return 1;
		}

		RV_ASSERT(false, "Unknown Shader data type provided!");
		return 0;
	}

	static unsigned int GetDataTypeCount(ShaderDataType& sDataType)
	{
		switch (sDataType)
		{
		case ShaderDataType::Float:		return 1;
		case ShaderDataType::Float2:	return 2;
		case ShaderDataType::Float3:	return 3;
		case ShaderDataType::Float4:	return 4;
		case ShaderDataType::Mat3:		return 3 * 3;
		case ShaderDataType::Mat4:		return 4 * 4;
		case ShaderDataType::Int:		return 1;
		case ShaderDataType::Int2:		return 2;
		case ShaderDataType::Int3:		return 3;
		case ShaderDataType::Int4:		return 4;
		case ShaderDataType::Bool:		return 1;
		}

		RV_ASSERT(false, "Unknown Shader data type provided!");
		return 0;
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


	VertexArray* VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::None:
			{
				RV_ASSERT(false, "No API was chosen!");
				return nullptr;
			}
			case RendererAPI::OpenGL:
			{
				return new OpenGLVertexArray();
			}
		}

		return nullptr;
	}


}