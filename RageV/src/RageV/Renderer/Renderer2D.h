#pragma once
#include "RageV/Math/Math.h"
#include "Camera.h"
#include "Light.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Batched quad renderer on the RHI. Identical code drives OpenGL and
	// Vulkan; the backend is chosen once at startup by EngineConfig.
	class Renderer2D {
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when the shader did not compile.
		static bool IsReady();

		// Colour and depth formats of the target this renderer draws into.
		// Pipelines are built against them, so they must be known up front.
		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

		// Rebuilds the pipeline with PolygonMode::Line. Both backends support
		// it, so this is a genuine toggle rather than a GL-only debug aid.
		static void SetWireframe(bool enabled);
		static bool IsWireframe();

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		static void BeginScene(const Camera& camera, const Mat4& transform, const LightList& lights = {});
		static void EndScene();

		static void DrawQuad(const Mat4& transform, const Vec4& color);
		static void DrawQuad(const Mat4& transform, const RHI::Ref<RHI::RHITexture>& texture, float tilingfactor = 1.0f);

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
