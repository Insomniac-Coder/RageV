#pragma once
#include "glm/glm.hpp"
#include <memory>
#include "RageV/Renderer/Buffer.h"

namespace RageV {

	class RenderAPI
	{

	public:
		enum class API
		{
			None = 0,
			OpenGL = 1
		};

	public:
		virtual void SetClearColor(const glm::vec4& clearColor) = 0;
		virtual void Clear() = 0;
		virtual void ResizeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height) = 0;
		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) = 0;
		virtual void Init() = 0;
		static API GetAPI() { return m_API; }
	private:
		static API m_API;
	};

}