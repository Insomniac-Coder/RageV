#pragma once
#include "RageV/Math/Math.h"
#include "Camera.h"
#include "Frustum.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Batched world-space lines, unlit.
	//
	// What every engine ends up with and every engine calls something
	// different: gizmos, debug draw, immediate mode. It exists because a great
	// deal of an engine's state is invisible by construction -- a collider has
	// no mesh, a trigger volume is a hole in the air, a raycast is a number --
	// and the alternative to drawing it is inferring it from behaviour.
	//
	// Immediate in the sense that shapes are submitted per frame and kept for
	// nothing; batched in the sense that they all land in one draw. Lines are
	// generated on the CPU because the shapes are small and the count is
	// counted in hundreds -- a curve tessellated on the GPU would need a
	// pipeline per shape to save work nobody can measure.
	class DebugRenderer
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when the shader did not compile.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined);

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// Also builds the frustum every shape below is tested against. Culling
		// lives here rather than in each caller because the callers are debug
		// code scattered across the engine, and a rule that has to be
		// remembered in twenty places is a rule that holds in nineteen.
		static void BeginScene(const Camera& camera, const Mat4& cameraTransform);
		static void EndScene();

		// Shapes dropped by the frustum since BeginScene. Exposed so the
		// culling can be asserted rather than assumed -- a shape that is not
		// drawn and a shape that is drawn off screen look identical.
		static uint32_t GetCulledCount();

		// --- primitives -------------------------------------------------------
		// All world space. `transform` carries position and rotation; scale is
		// taken from the shape's own arguments rather than the matrix, because
		// a collider's size is its own property and does not follow the
		// entity's scale twice.
		static void DrawLine(const Vec3& from, const Vec3& to, const Vec4& color);

		// Twelve edges.
		static void DrawBox(const Mat4& transform, const Vec3& halfExtents,
							const Vec4& color);

		// Three great circles, one per axis. Not a wireframe sphere: a lat/long
		// mesh costs an order of magnitude more lines and reads as a solid
		// blob, which is the opposite of what a debug overlay is for.
		static void DrawSphere(const Mat4& transform, float radius, const Vec4& color);

		// A cylinder between two hemispheres. `halfHeight` is the cylindrical
		// section only, matching ColliderComponent and Jolt.
		static void DrawCapsule(const Mat4& transform, float radius, float halfHeight,
								const Vec4& color);

		static uint32_t GetLineCount();

	private:
		static void EnsurePipeline();
		static void Flush();

		// A sphere around the shape, tested against the scene's frustum. True
		// when nothing has begun a scene, so debug draw never disappears
		// because of a missing frustum.
		static bool Visible(const Vec3& centre, float radius);
	};
}
