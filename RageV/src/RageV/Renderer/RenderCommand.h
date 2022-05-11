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
		inline static void SetClearColor(const glm::vec4& clearColor)
		{
			m_RenderAPI->SetClearColor(clearColor);
		}
		inline static void Clear()
		{
			m_RenderAPI->Clear();
		}

	private:
		static RenderAPI* m_RenderAPI;

	};

}