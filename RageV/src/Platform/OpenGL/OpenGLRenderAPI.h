#pragma once
#include "RageV/Renderer/RenderAPI.h"

namespace RageV
{

	class OpenGLRenderAPI : public RenderAPI
	{
		virtual void SetClearColor(const glm::vec4& clearColor) override;
		virtual void ResizeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height) override;
		virtual void Clear() override;
		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray) override;
		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, const unsigned int& count) override;
		virtual void Init() override;
	};

}