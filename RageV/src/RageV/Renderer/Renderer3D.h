#pragma once
#include "glm/glm.hpp"
#include "Cameranew.h"
#include "Light.h"
#include "Mesh.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Lit mesh rendering. Unlike Renderer2D, geometry is not merged: each mesh
	// keeps its own buffers and per-object data goes through push constants, so
	// a draw costs a push-constant write rather than a descriptor update.
	class Renderer3D
	{
	public:
		using LightData = std::vector<std::tuple<glm::vec3, glm::vec3, Light::LightType>>;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);
		static void SetWireframe(bool enabled);

		static void BeginScene(const Cameranew& camera, const glm::mat4& cameraTransform,
							   const LightData& lightData = {});
		static void EndScene();

		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color);

		static unsigned int GetDrawCallCount();
		static unsigned int GetTriangleCount();

	private:
		static void EnsurePipeline();
	};
}
