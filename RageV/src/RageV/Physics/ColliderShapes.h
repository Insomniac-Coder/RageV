#pragma once
#include "PhysicsTypes.h"
#include <glm/glm.hpp>

namespace RageV
{
	struct ColliderComponent;

	// A collider's dimensions after the entity's scale has been applied.
	//
	// Extracted so the simulation and the debug overlay cannot disagree. They
	// are the same numbers derived from the same inputs, and an overlay that
	// draws a box a different size from the one being simulated is worse than
	// no overlay -- it answers the question wrongly and looks authoritative
	// doing it.
	struct ScaledCollider
	{
		glm::vec3 HalfExtents{ 0.5f };   // box
		float Radius = 0.5f;             // sphere, capsule
		float HalfHeight = 0.5f;         // capsule, cylindrical section only
		glm::vec3 Offset{ 0.0f };        // in the entity's rotated local space
	};

	// Jolt refuses shapes below its convex radius, and a zero extent is almost
	// always an unset field rather than an intention.
	constexpr float kMinColliderExtent = 0.01f;

	ScaledCollider ScaleCollider(const ColliderComponent& collider, const glm::vec3& scale);
}
