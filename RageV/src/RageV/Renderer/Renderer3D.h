#pragma once
#include "glm/glm.hpp"
#include "Camera.h"
#include "Light.h"
#include "Mesh.h"
#include "Material.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Lit mesh rendering. Unlike Renderer2D, geometry is not merged: each mesh
	// keeps its own buffers and per-object data goes through push constants, so
	// a draw costs a push-constant write rather than a descriptor update.
	class Renderer3D
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);
		static void SetWireframe(bool enabled);

		static void BeginScene(const Camera& camera, const glm::mat4& cameraTransform,
							   const LightList& lights = {});
		static void EndScene();

		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const glm::mat4& transform,
							 const RHI::Ref<Material>& material);

		// Shared by every mesh that has no material of its own.
		static RHI::Ref<Material> GetDefaultMaterial();

		static unsigned int GetDrawCallCount();
		static unsigned int GetTriangleCount();

	private:
		static void EnsurePipeline();
	};
}
