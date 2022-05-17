#pragma once

namespace RageV
{
	enum class ShaderDataType {
		Float,
		Float2,
		Float3,
		Float4,
		Mat3,
		Mat4,
		Int,
		Int2,
		Int3,
		Int4,
		Bool
	};

	struct BufferElement
	{
	public:
		BufferElement() {};
		BufferElement(const std::string& name, ShaderDataType sType, bool norm = false);
		const ShaderDataType& GetType() const { return sDataType; }
		const unsigned int& GetSize() const { return elementSize; }
		const unsigned int& GetCount() const { return elementCount; }
		void SetOffset(unsigned int& offset) { elementOffset = offset; }
		const unsigned int& GetOffset() const { return elementOffset; }
		const bool IsNormalised() const { return normalised; }
	private:
		std::string elementName;
		ShaderDataType sDataType;
		unsigned int elementSize;
		unsigned int elementCount;
		bool normalised;
		unsigned int elementOffset;
	};

	struct BufferLayout
	{
		BufferLayout() {};
		BufferLayout(const std::initializer_list<BufferElement>& bufferelements);
		std::vector<BufferElement>::iterator begin() { return m_BufferElements.begin(); }
		std::vector<BufferElement>::iterator end() { return m_BufferElements.end(); }
		std::vector<BufferElement>::const_iterator begin() const { return m_BufferElements.begin(); }
		std::vector<BufferElement>::const_iterator end() const { return m_BufferElements.end(); }
		const unsigned int GetStride() const { return m_Stride; }
	private:
		void CalculateStrideAndOffset();
		unsigned int m_Stride;
		std::vector<BufferElement> m_BufferElements;
	};

	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		virtual const BufferLayout& GetBufferLayout() const = 0;
		virtual void SetBufferLayout(const BufferLayout& bufferLayout) = 0;
		static std::shared_ptr<VertexBuffer> Create(float* vertices, unsigned int size);
		static std::shared_ptr<VertexBuffer> Create(unsigned int size);
		virtual void SetData(const void* data, unsigned int size) = 0;
	};

	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		static std::shared_ptr<IndexBuffer> Create(unsigned int* indices, unsigned int count);
		virtual unsigned int GetCount() const = 0;
	};

	class VertexArray
	{
	public:
		virtual ~VertexArray() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		static std::shared_ptr<VertexArray> Create();
		virtual const std::vector <std::shared_ptr<VertexBuffer>>& GetVertexBuffers() const = 0;
		virtual const std::shared_ptr<IndexBuffer> GetIndexBuffer() const = 0;
		virtual const void AddVertexBuffer(std::shared_ptr<VertexBuffer> vertexBuffer) = 0;
		virtual const void SetIndexBuffer(std::shared_ptr<IndexBuffer> indexBuffer) = 0;
	};

}