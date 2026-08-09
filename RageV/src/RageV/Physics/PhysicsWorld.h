#pragma once
#include "PhysicsTypes.h"
#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"
#include <memory>
#include <vector>

namespace RageV
{
	class Scene;
	class Entity;

	struct RayHit
	{
		bool Hit = false;
		UUID Entity = UUID::Invalid();
		Vec3 Position{ 0.0f };
		Vec3 Normal{ 0.0f };
		float Distance = 0.0f;

		explicit operator bool() const { return Hit; }
	};

	enum class ContactPhase : uint8_t
	{
		// The two bodies were not touching last step and are now.
		Enter,
		// Still touching. One per step per pair.
		Stay,
		// Were touching and are not any more.
		Exit,
	};

	// One thing that happened between two bodies, in engine terms.
	//
	// Produced by the simulation and consumed by the scene, which is what keeps
	// the physics layer from having to know that scripts exist.
	struct ContactEvent
	{
		ContactPhase Phase = ContactPhase::Enter;

		UUID A = UUID::Invalid();
		UUID B = UUID::Invalid();

		// Either collider is a sensor, which routes this to OnTrigger* rather
		// than OnCollision*.
		bool Trigger = false;

		// World space. Both are zero on Exit: the contact is gone by then, and
		// inventing a position for it would be worse than reporting none.
		Vec3 Point{ 0.0f };
		// Points from A towards B. Delivery flips it per recipient so a script
		// always receives a normal pointing back at itself.
		Vec3 Normal{ 0.0f };

		// Closing speed along the normal at the moment of contact, in units per
		// second, never negative. What the volume of an impact sound wants.
		//
		// Not an impulse: contacts are reported before the solver runs, so the
		// real impulse is not known yet. Jolt can estimate one, at the cost of
		// re-running part of the collision response per contact.
		float ImpactSpeed = 0.0f;
	};

	// The simulation, for one scene, for the duration of one play session.
	//
	// Created when Play is pressed and destroyed on Stop, because the scene is
	// restored from a snapshot at that point and every body in here refers to
	// entities that are about to be replaced.
	//
	// Jolt's own types are kept out of this header. They pull in a large amount
	// of machinery and carry compile definitions that must match exactly
	// between the library and everything that includes it -- confining that to
	// one translation unit is the cheapest way to never debug an ABI mismatch.
	class PhysicsWorld
	{
	public:
		PhysicsWorld();
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Creates a body for every entity in the scene that has a rigid body,
		// in one batch.
		//
		// Batching is not an optimisation here. Adding bodies one at a time
		// leaves the broad-phase tree degenerate, and a degenerate tree misses
		// collisions -- not just slowly, wrongly. Loading a scene adds every
		// body at once, so this is the first thing that would have gone wrong.
		void Build(Scene& scene);

		void Step(float deltaTime, int collisionSteps = 1);

		// Copies simulated positions back onto the entities' transforms,
		// blended between the last two steps. Rendering the raw state means
		// anything fast visibly stutters, because the display refreshes
		// between two discrete simulation positions rather than on them.
		void SyncTransforms(Scene& scene, float interpolationAlpha = 1.0f);

		// Bodies for entities created after Build -- something a script
		// spawned. Cheap for a handful; a large batch should go through Build.
		void AddBody(Scene& scene, Entity entity);
		void RemoveBody(UUID entity);
		bool HasBody(UUID entity) const;

		// False for a body that has settled and been put to sleep, and for a
		// static one, which is never active.
		//
		// Worth exposing rather than keeping internal: sleep is the single
		// least visible thing the simulation does and the cause of the
		// subtlest behaviour in it -- contacts are withdrawn when a body falls
		// asleep. Being able to see which bodies are asleep turns that from a
		// thing you have to know into a thing you can look at.
		bool IsBodyAwake(UUID entity) const;

		// --- contacts --------------------------------------------------------
		// Moves everything that happened during the last Step into `out`,
		// leaving the queue empty.
		//
		// Moved rather than returned by reference on purpose. Delivering an
		// event runs script code, and a script may destroy an entity -- which
		// removes a body, which queues more events. Handing out a reference to
		// the live queue would mean appending to a container being iterated.
		void TakeContactEvents(std::vector<ContactEvent>& out);

		// Pairs currently touching. For tests and a future debug overlay.
		size_t GetContactPairCount() const;

		// --- queries ---------------------------------------------------------
		// Nearest hit along the ray. Direction need not be normalised; the ray
		// extends to its length.
		RayHit CastRay(const Vec3& origin, const Vec3& direction) const;

		// --- runtime control -------------------------------------------------
		void SetLinearVelocity(UUID entity, const Vec3& velocity);
		Vec3 GetLinearVelocity(UUID entity) const;
		void AddForce(UUID entity, const Vec3& force);
		void AddImpulse(UUID entity, const Vec3& impulse);
		void SetPosition(UUID entity, const Vec3& position);

		void SetGravity(const Vec3& gravity);
		Vec3 GetGravity() const;

		size_t GetBodyCount() const;

	private:
		// Jolt's headers stay in the .cpp.
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
