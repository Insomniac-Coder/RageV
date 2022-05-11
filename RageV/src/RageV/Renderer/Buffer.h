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

	struct BufferElement
	{
	public:
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
		static VertexBuffer* Create(float* vertices, unsigned int size);
	};

	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() {}
		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		static IndexBuffer* Create(unsigned int* indices, unsigned int count);
		virtual unsigned int GetCount() const = 0;
	};

}