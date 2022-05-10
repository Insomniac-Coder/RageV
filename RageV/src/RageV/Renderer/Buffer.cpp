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

}