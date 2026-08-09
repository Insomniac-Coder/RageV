#pragma once
#include "glm/glm.hpp"
#include "Camera.h"
#include "Light.h"
#include "Environment.h"
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

		// False when a shader did not compile. Meshes draw nothing in that
		// state, which is easier to diagnose from here than from the picture.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);
		static void SetWireframe(bool enabled);

		// Resets the per-frame scene-slot pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// `environmentMap` is what surfaces reflect -- the scene's environment
		// map, a probe's capture, or the sky's gradient baked into a small
		// cube. Null means nothing is reflected, not that the term is
		// undefined: the binding is filled with a neutral cube and the
		// intensity set to zero.
		static void BeginScene(const Camera& camera, const glm::mat4& cameraTransform,
							   const LightList& lights = {},
							   const SceneEnvironment& environment = {},
							   const RHI::Ref<RHI::RHITexture>& environmentMap = nullptr,
							   const RHI::Ref<RHI::RHITexture>& irradianceMap = nullptr);
		static void EndScene();

		// Depth only, from a light. Wraps the same mesh binding the lit path
		// uses, because a shadow pass draws the same geometry and differs only
		// in what it writes.
		//
		// The light's view-projection is folded into each draw's model matrix,
		// so a caster costs one push constant and nothing else -- no descriptor
		// set is bound in this pass at all.
		static void BeginShadow(const glm::mat4& viewProjection);
		static void DrawMeshShadow(const RHI::Ref<Mesh>& mesh, const glm::mat4& transform);
		static void EndShadow();

		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const glm::mat4& transform,
							 const RHI::Ref<Material>& material);

		// Shared by every mesh that has no material of its own.
		static RHI::Ref<Material> GetDefaultMaterial();

		// How many meshes were skipped by frustum culling this frame, across
		// every pass. Reported so the saving is visible rather than assumed.
		static unsigned int GetCulledCount();
		static void CountCulled();

		// The most lights any one cluster ended up holding, and the number that
		// actually decides a fragment's cost. If it equals the scene's light
		// count, clustering did nothing and is pure overhead.
		static unsigned int GetMaxCellLoad();
		static unsigned int GetLightCount();

		static unsigned int GetDrawCallCount();
		static unsigned int GetTriangleCount();

	private:
		static void EnsurePipeline();
	};
}
