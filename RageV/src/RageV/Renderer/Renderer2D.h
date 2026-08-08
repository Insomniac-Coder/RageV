#pragma once
#include "glm/glm.hpp"
#include "OrthographicCamera.h"
#include "Cameranew.h"
#include "Light.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Batched quad renderer on the RHI. Identical code drives OpenGL and
	// Vulkan; the backend is chosen once at startup by EngineConfig.
	class Renderer2D {
	public:
		using LightData = std::vector<std::tuple<glm::vec3, glm::vec3, Light::LightType>>;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Colour and depth formats of the target this renderer draws into.
		// Pipelines are built against them, so they must be known up front.
		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

		static void BeginScene(const OrthographicCamera& camera);
		static void BeginScene(const Cameranew& camera, const glm::mat4& transform, const LightData& lightData = {});
		static void EndScene();

		static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
		static void DrawQuad(const glm::mat4& transform, const RHI::Ref<RHI::RHITexture>& texture, float tilingfactor = 1.0f);

		static unsigned int GetDrawCallCount();
		static unsigned int GetVerticesCount();
		static unsigned int GetIndiciesCount();
		static unsigned int GetQuadCount();

	private:
		static void ResetScene();
		static void FlushAndReset();
		static void EnsurePipeline();
		static unsigned int ResolveTextureSlot(const RHI::Ref<RHI::RHITexture>& texture);
	};
}
