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
		using LightData = std::vector<std::tuple<glm::vec3, glm::vec3, Light::LightType>>;

		static void BeginScene(const OrthographicCamera& camera);
		static void BeginScene(const Cameranew& camera, const glm::mat4& transform, const LightData& lightData = {});
		static void EndScene();
		static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
		static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingfactor = 1.0f);
		static unsigned int GetDrawCallCount();
		static unsigned int GetVerticesCount();
		static unsigned int GetIndiciesCount();
		static unsigned int GetQuadCount();
	private:
		static void ResetScene();
	};

}