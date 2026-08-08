#include <rvpch.h>
#include "Frustum.h"

namespace RageV
{
	void Frustum::Build(const glm::mat4& m)
	{
		// glm is column major, so m[column][row]. Row i of the matrix is
		// (m[0][i], m[1][i], m[2][i], m[3][i]).
		auto row = [&m](int i)
		{
			return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
		};

		const glm::vec4 r0 = row(0);
		const glm::vec4 r1 = row(1);
		const glm::vec4 r2 = row(2);
		const glm::vec4 r3 = row(3);

		m_Planes[0] = r3 + r0;   // left
		m_Planes[1] = r3 - r0;   // right
		m_Planes[2] = r3 + r1;   // bottom
		m_Planes[3] = r3 - r1;   // top

		// Near is r2 alone, not r3 + r2: glm is built with
		// GLM_FORCE_DEPTH_ZERO_TO_ONE, so the near plane is z = 0 rather than
		// z = -w. Using the OpenGL form here culls everything in front of the
		// camera on half the frustum's depth.
		m_Planes[4] = r2;        // near
		m_Planes[5] = r3 - r2;   // far

		for (glm::vec4& plane : m_Planes)
		{
			const float length = glm::length(glm::vec3(plane));
			if (length > 1e-6f)
				plane /= length;
		}
	}

	bool Frustum::Intersects(const glm::vec3& centre, const glm::vec3& extents) const
	{
		for (const glm::vec4& plane : m_Planes)
		{
			const glm::vec3 normal(plane);

			// The box's extent along this plane's normal. Using absolute values
			// gives the half-width of its projection, whichever way the normal
			// points.
			const float reach = extents.x * std::fabs(normal.x) +
								extents.y * std::fabs(normal.y) +
								extents.z * std::fabs(normal.z);

			if (glm::dot(normal, centre) + plane.w + reach < 0.0f)
				return false;   // wholly behind this plane
		}

		return true;
	}

	void Frustum::TransformBounds(const AABB& bounds, const glm::mat4& transform,
								  glm::vec3& centre, glm::vec3& extents)
	{
		const glm::vec3 localCentre = bounds.Centre();
		const glm::vec3 localExtents = bounds.Extents();

		centre = glm::vec3(transform * glm::vec4(localCentre, 1.0f));

		// The axis-aligned box around the transformed one: each axis grows by
		// the absolute sum of that row's contributions. Cheaper than
		// transforming eight corners and identical in result.
		const glm::mat3 basis(transform);
		extents = glm::vec3(
			std::fabs(basis[0][0]) * localExtents.x + std::fabs(basis[1][0]) * localExtents.y +
				std::fabs(basis[2][0]) * localExtents.z,
			std::fabs(basis[0][1]) * localExtents.x + std::fabs(basis[1][1]) * localExtents.y +
				std::fabs(basis[2][1]) * localExtents.z,
			std::fabs(basis[0][2]) * localExtents.x + std::fabs(basis[1][2]) * localExtents.y +
				std::fabs(basis[2][2]) * localExtents.z);
	}
}
