#pragma once
#include "PhysicsTypes.h"
#include "RageV/Math/Math.h"

// Declared in the enclosing namespace on purpose. Inside
// `namespace RageV::Physics` these would declare new types that nothing
// ever defines, and the error would surface far from here.
namespace RageV
{
	struct ColliderComponent;
}

namespace RageV::Physics
{

	// A collider's dimensions after the entity's scale has been applied.
	//
	// Extracted so the simulation and the debug overlay cannot disagree. They
	// are the same numbers derived from the same inputs, and an overlay that
	// draws a box a different size from the one being simulated is worse than
	// no overlay -- it answers the question wrongly and looks authoritative
	// doing it.
	struct ScaledCollider
	{
		Vec3 HalfExtents{ 0.5f };   // box
		float Radius = 0.5f;             // sphere, capsule
		float HalfHeight = 0.5f;         // capsule, cylindrical section only
		Vec3 Offset{ 0.0f };        // in the entity's rotated local space
	};

	// Jolt refuses shapes below its convex radius, and a zero extent is almost
	// always an unset field rather than an intention.
	constexpr float kMinColliderExtent = 0.01f;

	ScaledCollider ScaleCollider(const ColliderComponent& collider, const Vec3& scale);
}

namespace RageV
{
	using Physics::ScaledCollider;
}
