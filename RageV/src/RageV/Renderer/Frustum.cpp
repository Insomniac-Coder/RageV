#include <rvpch.h>
#include "Frustum.h"

namespace RageV
{
	void Frustum::Build(const Mat4& m)
	{
		// Mat4 is column major, so m[column][row]. Row i of the matrix is
		// (m[0][i], m[1][i], m[2][i], m[3][i]).
		auto row = [&m](int i)
		{
			return Vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
		};

		const Vec4 r0 = row(0);
		const Vec4 r1 = row(1);
		const Vec4 r2 = row(2);
		const Vec4 r3 = row(3);

		m_Planes[0] = r3 + r0;   // left
		m_Planes[1] = r3 - r0;   // right
		m_Planes[2] = r3 + r1;   // bottom
		m_Planes[3] = r3 - r1;   // top

		// The depth pair is r2 and r3 - r2, not r3 + r2 and r3 - r2: this
		// engine's projections put clip depth in [0, w] rather than [-w, w],
		// so one bound is z = 0. Using the OpenGL form here culls everything
		// in front of the camera on half the frustum's depth.
		//
		// **Reverse-Z swaps which is which and changes nothing else.** The
		// clip test is still 0 <= z <= w, so the same two half-spaces bound
		// the same volume -- but z = 0 is now the *far* plane and z = w the
		// near one. Every caller tests all six uniformly, so culling is
		// unaffected; the labels are corrected because a later reader
		// reaching for "the near plane" by index would otherwise get the far
		// one, and a frustum that culls correctly is the worst place to hide
		// a wrong name.
		m_Planes[4] = r2;        // far
		m_Planes[5] = r3 - r2;   // near

		for (Vec4& plane : m_Planes)
		{
			const float length = Math::Length(Vec3(plane));
			if (length > 1e-6f)
				plane /= length;
		}
	}

	bool Frustum::Intersects(const Vec3& centre, const Vec3& extents) const
	{
		for (const Vec4& plane : m_Planes)
		{
			const Vec3 normal(plane);

			// The box's extent along this plane's normal. Using absolute values
			// gives the half-width of its projection, whichever way the normal
			// points.
			const float reach = extents.x * Math::Abs(normal.x) +
								extents.y * Math::Abs(normal.y) +
								extents.z * Math::Abs(normal.z);

			if (Math::Dot(normal, centre) + plane.w + reach < 0.0f)
				return false;   // wholly behind this plane
		}

		return true;
	}

	void Frustum::TransformBounds(const AABB& bounds, const Mat4& transform,
								  Vec3& centre, Vec3& extents)
	{
		const Vec3 localCentre = bounds.Centre();
		const Vec3 localExtents = bounds.Extents();

		centre = Vec3(transform * Vec4(localCentre, 1.0f));

		// The axis-aligned box around the transformed one: each axis grows by
		// the absolute sum of that row's contributions. Cheaper than
		// transforming eight corners and identical in result.
		const Mat3 basis(transform);
		extents = Vec3(
			Math::Abs(basis[0][0]) * localExtents.x + Math::Abs(basis[1][0]) * localExtents.y +
				Math::Abs(basis[2][0]) * localExtents.z,
			Math::Abs(basis[0][1]) * localExtents.x + Math::Abs(basis[1][1]) * localExtents.y +
				Math::Abs(basis[2][1]) * localExtents.z,
			Math::Abs(basis[0][2]) * localExtents.x + Math::Abs(basis[1][2]) * localExtents.y +
				Math::Abs(basis[2][2]) * localExtents.z);
	}
}
