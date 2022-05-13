#pragma once
#include "RenderCommand.h"
#include "Camera.h"
#include "Shader.h"

namespace RageV
{
	class Renderer
	{
	public:
		static void Init();
		inline static RenderAPI::API GetAPI() { return RenderAPI::GetAPI(); }
		static void BeginScene(Camera& camera);
		static void EndScene();
		static void Submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));
	private:
		struct SceneData
		{
			glm::mat4 ViewProjection;
		};

		static SceneData* m_SceneData;
	};
}