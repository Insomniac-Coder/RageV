#pragma once
#include "RageV/Renderer/RenderAPI.h"

namespace RageV
{

	class OpenGLRenderAPI : public RenderAPI
	{
		virtual void SetClearColor(const glm::vec4& clearColor) override;
		virtual void Clear() override;
		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
	};

}