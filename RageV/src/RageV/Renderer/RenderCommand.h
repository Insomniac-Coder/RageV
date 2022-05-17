#pragma once
#include "RenderAPI.h"

namespace RageV
{

	class RenderCommand {
	public:
		inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray)
		{
			m_RenderAPI->DrawIndexed(vertexArray);
		}
		inline static void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, const unsigned int& count)
		{
			m_RenderAPI->DrawIndexed(vertexArray, count);
		}
		inline static void ResizeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
		{
			m_RenderAPI->ResizeViewport(x, y, width, height);
		}
		inline static void SetClearColor(const glm::vec4& clearColor)
		{
			m_RenderAPI->SetClearColor(clearColor);
		}
		inline static void Clear()
		{
			m_RenderAPI->Clear();
		}

		inline static void Init()
		{
			m_RenderAPI->Init();
		}

	private:
		static RenderAPI* m_RenderAPI;

	};

}