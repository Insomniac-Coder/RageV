#pragma once
#include "glm/glm.hpp"
#include "Camera.h"
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

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		static void BeginScene(const Camera& camera, const glm::mat4& cameraTransform);
		static void EndScene();

		// --- primitives -------------------------------------------------------
		// All world space. `transform` carries position and rotation; scale is
		// taken from the shape's own arguments rather than the matrix, because
		// a collider's size is its own property and does not follow the
		// entity's scale twice.
		static void DrawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

		// Twelve edges.
		static void DrawBox(const glm::mat4& transform, const glm::vec3& halfExtents,
							const glm::vec4& color);

		// Three great circles, one per axis. Not a wireframe sphere: a lat/long
		// mesh costs an order of magnitude more lines and reads as a solid
		// blob, which is the opposite of what a debug overlay is for.
		static void DrawSphere(const glm::mat4& transform, float radius, const glm::vec4& color);

		// A cylinder between two hemispheres. `halfHeight` is the cylindrical
		// section only, matching ColliderComponent and Jolt.
		static void DrawCapsule(const glm::mat4& transform, float radius, float halfHeight,
								const glm::vec4& color);

		static uint32_t GetLineCount();

	private:
		static void EnsurePipeline();
		static void Flush();
	};
}
