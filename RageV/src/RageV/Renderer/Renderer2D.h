#pragma once
#include "glm/glm.hpp"
#include "OrthographicCamera.h"
#include "Texture.h"
#include "Cameranew.h"
#include "Light.h"

namespace RageV
{


	class Renderer2D {
	public:
		static void Init();
		static void Shutdown();
		static void BeginScene(const OrthographicCamera& camera);
		static void BeginScene(Cameranew& camera, glm::mat4& transform, std::vector<std::tuple<glm::vec3, glm::vec3, Light::LightType>> lightData = {});
		static void EndScene();
		static void DrawQuad(glm::mat4& transform, glm::vec4& color);
		static void DrawQuad(glm::mat4& transform, std::shared_ptr<Texture2D>& texture, float tilingfactor = 1.0f);
		static unsigned int GetDrawCallCount();
		static unsigned int GetVerticesCount();
		static unsigned int GetIndiciesCount();
		static unsigned int GetQuadCount();
	private:
		static void ResetScene();
	};

}