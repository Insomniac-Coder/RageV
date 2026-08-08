#include <rvpch.h>
#include "ColliderShapes.h"
#include "RageV/Scene/Components.h"
#include <glm/gtx/component_wise.hpp>

namespace RageV
{
	ScaledCollider ScaleCollider(const ColliderComponent& collider, const glm::vec3& scale)
	{
		// Absolute, because a negative scale mirrors an object rather than
		// giving it a negative size, and a shape with a negative extent is not
		// a shape.
		const glm::vec3 absScale = glm::abs(scale);

		ScaledCollider out;
		out.Offset = collider.Offset * scale;

		switch (collider.Shape)
		{
			case ColliderShape::Sphere:
				// A sphere has one radius, so a non-uniform scale has no honest
				// answer. The largest axis at least encloses the mesh, which is
				// the failure that produces too much collision rather than too
				// little.
				out.Radius = glm::max(collider.Radius * glm::compMax(absScale), kMinColliderExtent);
				break;

			case ColliderShape::Capsule:
				out.Radius = glm::max(collider.Radius * glm::max(absScale.x, absScale.z),
									  kMinColliderExtent);
				out.HalfHeight = glm::max(collider.Height * 0.5f * absScale.y, kMinColliderExtent);
				break;

			case ColliderShape::Box:
			default:
				out.HalfExtents = glm::max(collider.HalfExtents * absScale,
										   glm::vec3(kMinColliderExtent));
				break;
		}

		return out;
	}
}
