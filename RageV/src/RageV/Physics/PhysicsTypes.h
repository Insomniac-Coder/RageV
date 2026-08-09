#pragma once
#include <cstdint>

namespace RageV::Physics
{
	// How a body participates in the simulation.
	enum class BodyType : uint32_t
	{
		// Never moves. The cheapest kind, and what a level should be made of --
		// static bodies live in their own broad-phase tree that is not rebuilt.
		Static = 0,
		// Moved by code, never by forces, and pushes dynamic bodies out of the
		// way rather than being pushed. Platforms, doors, anything animated.
		Kinematic = 1,
		// Moved by the solver.
		Dynamic = 2,
	};

	enum class ColliderShape : uint32_t
	{
		Box = 0,
		Sphere = 1,
		Capsule = 2,
	};

	const char* BodyTypeName(BodyType type);
	const char* ColliderShapeName(ColliderShape shape);

	// Object layers.
	//
	// Deliberately two, mapping onto two broad-phase layers. Each broad-phase
	// layer is a separate tree with its own query and maintenance cost, so the
	// intended shape is many object layers onto few broad-phase layers -- and
	// starting with more than two is how that gets expensive before anything
	// needs it.
	namespace Layers
	{
		constexpr uint16_t NonMoving = 0;
		constexpr uint16_t Moving = 1;
		constexpr uint16_t Count = 2;
	}
}

// Re-exported into RageV: both are fields on components, and a component
// declaration reading `Physics::ColliderShape Shape` would be the only
// qualified type in a struct full of plain ones. See AudioEngine.h for the rule.
namespace RageV
{
	using Physics::BodyType;
	using Physics::ColliderShape;
}
