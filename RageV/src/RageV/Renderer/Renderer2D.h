#pragma once
#include "glm/glm.hpp"
#include "OrthographicCamera.h"
#include "Texture.h"

namespace RageV
{


	class Renderer2D {
	public:
		static void Init();
		static void Shutdown();
		static void BeginScene(const OrthographicCamera& camera);
		static void EndScene();
		static void DrawQuad(glm::vec2& position, glm::vec2& size, glm::vec4& color);
		static void DrawQuad(glm::vec3& position, glm::vec2& size, glm::vec4& color);
		static void DrawQuad(glm::vec2& position, glm::vec2& size, std::shared_ptr<Texture2D>& texture);
		static void DrawQuad(glm::vec3& position, glm::vec2& size, std::shared_ptr<Texture2D>& texture);
	};

}