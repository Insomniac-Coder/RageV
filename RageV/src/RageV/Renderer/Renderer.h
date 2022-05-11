#pragma once
#include "RenderCommand.h"

namespace RageV
{
	class Renderer
	{
	public:
		inline static RenderAPI::API GetAPI() { return RenderAPI::GetAPI(); }
		static void BeginScene();
		static void EndScene();
		static void Submit(const std::shared_ptr<VertexArray>& vertexArray);
	};
}