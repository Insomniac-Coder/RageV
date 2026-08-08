#pragma once
#include "RageV/Renderer/Mesh.h"
#include "glm/glm.hpp"

namespace RageV
{
	// Six planes pulled out of a view-projection, and a test against a box.
	//
	// Built from the matrix rather than from a camera, because the frustums
	// that matter here are not all cameras: a shadow cascade has an orthographic
	// one, a probe face has a 90-degree perspective one, and a spot light has
	// its own cone. Each pass culls against the frustum it is actually drawing
	// with, which is the part that is easy to get wrong -- culling every pass
	// against the camera's frustum removes exactly the casters whose shadows
	// are visible while they are not.
	class Frustum
	{
	public:
		Frustum() = default;
		explicit Frustum(const glm::mat4& viewProjection) { Build(viewProjection); }

		// Gribb-Hartmann: the planes fall out of sums and differences of the
		// matrix rows, whatever projection produced it. Normalised, so the
		// distance a test computes is a real distance.
		void Build(const glm::mat4& viewProjection);

		// A box in world space. Conservative: a box that straddles a plane
		// counts as inside, because the cost of drawing something invisible is
		// a draw and the cost of skipping something visible is a hole.
		bool Intersects(const glm::vec3& centre, const glm::vec3& extents) const;

		// The mesh's own bounds put through a transform. The transformed box is
		// the axis-aligned box *around* the rotated one, so it grows under
		// rotation -- correct, and the reason a rotating object is culled
		// slightly late rather than slightly early.
		static void TransformBounds(const AABB& bounds, const glm::mat4& transform,
									glm::vec3& centre, glm::vec3& extents);

		const glm::vec4& Plane(int index) const { return m_Planes[index]; }

	private:
		// Left, right, bottom, top, near, far. Each is a normal in xyz and a
		// distance in w, pointing inwards.
		glm::vec4 m_Planes[6]{};
	};
}
