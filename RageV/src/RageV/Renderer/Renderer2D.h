#pragma once
#include "glm/glm.hpp"
#include "OrthographicCamera.h"
#include "Texture.h"
#include "Transform.h"

namespace RageV
{


	class Renderer2D {
	public:
		static void Init();
		static void Shutdown();
		static void BeginScene(const OrthographicCamera& camera);
		static void EndScene();
		static void DrawQuad(Transform2D& transform, glm::vec4& color);
		static void DrawQuad(Transform2D& transform, std::shared_ptr<Texture2D>& texture, float tilingfactor = 1.0f);
		static unsigned int GetDrawCallCount();
		static unsigned int GetVerticesCount();
		static unsigned int GetIndiciesCount();
		static unsigned int GetQuadCount();
	private:
		static void ResetScene();
	};

}