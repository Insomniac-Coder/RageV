#include <rvpch.h>
#include "PhysicsTypes.h"

namespace RageV::Physics
{
	const char* BodyTypeName(BodyType type)
	{
		switch (type)
		{
			case BodyType::Static:    return "Static";
			case BodyType::Kinematic: return "Kinematic";
			case BodyType::Dynamic:   return "Dynamic";
		}
		return "Static";
	}

	const char* ColliderShapeName(ColliderShape shape)
	{
		switch (shape)
		{
			case ColliderShape::Box:     return "Box";
			case ColliderShape::Sphere:  return "Sphere";
			case ColliderShape::Capsule: return "Capsule";
		}
		return "Box";
	}
}
