#include <rvpch.h>
#include "ColliderShapes.h"
#include "RageV/Scene/Components.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	ScaledCollider ScaleCollider(const ColliderComponent& collider, const Vec3& scale)
	{
		// Absolute, because a negative scale mirrors an object rather than
		// giving it a negative size, and a shape with a negative extent is not
		// a shape.
		const Vec3 absScale = Math::Abs(scale);

		ScaledCollider out;
		out.Offset = collider.Offset * scale;

		switch (collider.Shape)
		{
			case ColliderShape::Sphere:
				// A sphere has one radius, so a non-uniform scale has no honest
				// answer. The largest axis at least encloses the mesh, which is
				// the failure that produces too much collision rather than too
				// little.
				out.Radius = Math::Max(collider.Radius * Math::MaxComponent(absScale), kMinColliderExtent);
				break;

			case ColliderShape::Capsule:
				out.Radius = Math::Max(collider.Radius * Math::Max(absScale.x, absScale.z),
									  kMinColliderExtent);
				out.HalfHeight = Math::Max(collider.Height * 0.5f * absScale.y, kMinColliderExtent);
				break;

			case ColliderShape::Box:
			default:
				out.HalfExtents = Math::Max(collider.HalfExtents * absScale,
										   Vec3(kMinColliderExtent));
				break;
		}

		return out;
	}
}
