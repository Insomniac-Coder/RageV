#include <rvpch.h>
#include "OpenGLBuffer.h"

#include "glad/glad.h"

namespace RageV
{



	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, unsigned int size)
	{
		glCreateBuffers(1, &m_ID);
		Bind();
		glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	}

	OpenGLVertexBuffer::~OpenGLVertexBuffer()
	{
		glDeleteBuffers(1, &m_ID);
	}

	void OpenGLVertexBuffer::Bind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_ID);
	}

	void OpenGLVertexBuffer::UnBind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}


	OpenGLIndexBuffer::OpenGLIndexBuffer(unsigned int* indices, unsigned int count)
	{
		glCreateBuffers(1, &m_ID);
		Bind();
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(unsigned int), indices, GL_STATIC_DRAW);
	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer()
	{
		glDeleteBuffers(1, &m_ID);
	}

	void OpenGLIndexBuffer::Bind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ID);
	}

	void OpenGLIndexBuffer::UnBind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}


}