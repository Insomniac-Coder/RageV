#include <rvpch.h>
#include "PhysicsWorld.h"
#include "RageV/Core/Log.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/component_wise.hpp>
#include <cstdarg>
#include <thread>
#include <unordered_map>

namespace RageV
{
	namespace
	{
		// --- conversions -----------------------------------------------------
		JPH::Vec3 ToJolt(const glm::vec3& v) { return { v.x, v.y, v.z }; }

		// One overload, not two: with DOUBLE_PRECISION off -- which is how this
		// build is configured -- JPH::RVec3 is an alias for JPH::Vec3, so a
		// second overload is a redefinition rather than a convenience.
		glm::vec3 ToGlm(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }

		JPH::Quat ToJolt(const glm::quat& q) { return { q.x, q.y, q.z, q.w }; }
		glm::quat ToGlm(const JPH::Quat& q)  { return { q.GetW(), q.GetX(), q.GetY(), q.GetZ() }; }

		// --- layers ----------------------------------------------------------
		namespace BroadPhase
		{
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr uint32_t Count = 2;
		}

		// Static geometry lives in its own tree that is never rebuilt, which is
		// the entire reason for splitting them.
		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			BroadPhaseLayers()
			{
				m_Layers[Layers::NonMoving] = BroadPhase::NonMoving;
				m_Layers[Layers::Moving] = BroadPhase::Moving;
			}

			uint32_t GetNumBroadPhaseLayers() const override { return BroadPhase::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				JPH_ASSERT(layer < Layers::Count);
				return m_Layers[layer];
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return (JPH::BroadPhaseLayer::Type)layer == 0 ? "NonMoving" : "Moving";
			}
#endif

		private:
			JPH::BroadPhaseLayer m_Layers[Layers::Count];
		};

		class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
			{
				// Two static bodies can never collide with each other in a way
				// anyone cares about, so that whole tree is skipped.
				if (layer == Layers::NonMoving)
					return broadPhase == BroadPhase::Moving;
				return true;
			}
		};

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				if (a == Layers::NonMoving)
					return b == Layers::Moving;
				return true;
			}
		};

		// --- diagnostics -----------------------------------------------------
		void JoltTrace(const char* format, ...)
		{
			char buffer[1024];
			va_list args;
			va_start(args, format);
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);

			RV_CORE_TRACE("[Jolt] {0}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		// Routed into the log rather than left to break into the debugger, so a
		// misused body reports itself the way a validation error does.
		bool JoltAssertFailed(const char* expression, const char* message,
							  const char* file, uint32_t line)
		{
			RV_CORE_ERROR("[Jolt] {0}:{1}: ({2}) {3}", file, line, expression,
						  message ? message : "");
			// False: log and carry on rather than halting the editor.
			return false;
		}
#endif

		JPH::EMotionType ToMotionType(BodyType type)
		{
			switch (type)
			{
				case BodyType::Static:    return JPH::EMotionType::Static;
				case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
				case BodyType::Dynamic:   return JPH::EMotionType::Dynamic;
			}
			return JPH::EMotionType::Static;
		}

		JPH::ObjectLayer ToObjectLayer(BodyType type)
		{
			return type == BodyType::Static ? Layers::NonMoving : Layers::Moving;
		}

		// Jolt refuses shapes smaller than its convex radius, and a zero extent
		// is almost always an unset field rather than an intention.
		constexpr float kMinExtent = 0.01f;

		// Jolt's globals are process-wide and have to exist before *anything*
		// Jolt allocates.
		//
		// This cannot live in PhysicsWorld::Impl's constructor body, which is
		// where it was: TempAllocatorImpl allocates in its own constructor, and
		// member construction runs before the enclosing constructor's body. The
		// allocator function pointers were still null at that point and the
		// process died before reaching the first line of physics code.
		void EnsureJoltGlobals()
		{
			static bool done = false;
			if (done)
				return;
			done = true;

			JPH::RegisterDefaultAllocator();
			JPH::Trace = JoltTrace;
			JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)

			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}

		JPH::ShapeRefC MakeShape(const ColliderComponent& collider, const glm::vec3& scale)
		{
			// Scale is baked in rather than wrapped in a ScaledShape: it never
			// changes for a body during a run, and a baked shape is one fewer
			// indirection per query.
			const glm::vec3 absScale = glm::abs(scale);
			JPH::ShapeSettings::ShapeResult result;

			switch (collider.Shape)
			{
				case ColliderShape::Sphere:
				{
					// A sphere has one radius, so a non-uniform scale has no
					// honest answer; the largest axis at least encloses the mesh.
					const float radius = glm::max(collider.Radius * glm::compMax(absScale), kMinExtent);
					result = JPH::SphereShapeSettings(radius).Create();
					break;
				}
				case ColliderShape::Capsule:
				{
					const float radius = glm::max(collider.Radius * glm::max(absScale.x, absScale.z), kMinExtent);
					const float halfHeight = glm::max(collider.Height * 0.5f * absScale.y, kMinExtent);
					result = JPH::CapsuleShapeSettings(halfHeight, radius).Create();
					break;
				}
				case ColliderShape::Box:
				default:
				{
					const glm::vec3 extents = glm::max(collider.HalfExtents * absScale, glm::vec3(kMinExtent));
					result = JPH::BoxShapeSettings(ToJolt(extents)).Create();
					break;
				}
			}

			if (result.HasError())
			{
				RV_CORE_ERROR("Collider shape could not be created: {0}", result.GetError().c_str());
				return {};
			}

			JPH::ShapeRefC shape = result.Get();

			if (glm::dot(collider.Offset, collider.Offset) > 0.0f)
			{
				auto offset = JPH::RotatedTranslatedShapeSettings(
					ToJolt(collider.Offset * scale), JPH::Quat::sIdentity(), shape).Create();

				if (!offset.HasError())
					shape = offset.Get();
			}

			return shape;
		}
	}

	// -------------------------------------------------------------------------
	struct PhysicsWorld::Impl
	{
		// Per body, so rendering can interpolate. Without the previous state,
		// anything moving fast stutters: the display refreshes between two
		// discrete simulation positions, not on them.
		struct Body
		{
			JPH::BodyID Id;
			UUID Entity = UUID::Invalid();
			bool Simulated = false;   // false for static: nothing to interpolate

			glm::vec3 PreviousPosition{ 0.0f };
			glm::vec3 CurrentPosition{ 0.0f };
			glm::quat PreviousRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::quat CurrentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		Impl()
			: TempAllocator(16 * 1024 * 1024),
			  // One less than the hardware supports: the main thread is doing
			  // the rest of the frame and taking every core starves it.
			  JobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
						(int)glm::max(1u, std::thread::hardware_concurrency() - 1u))
		{
			System.Init(kMaxBodies, kBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
						BroadPhaseLayerInterface, ObjectVsBroadPhase, ObjectLayerPair);
			System.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
		}

		static constexpr uint32_t kMaxBodies = 8192;
		// Zero means Jolt picks a default. It only matters for multithreaded
		// access to bodies, which nothing here does yet.
		static constexpr uint32_t kBodyMutexes = 0;
		static constexpr uint32_t kMaxBodyPairs = 8192;
		static constexpr uint32_t kMaxContactConstraints = 4096;

		JPH::TempAllocatorImpl TempAllocator;
		JPH::JobSystemThreadPool JobSystem;

		BroadPhaseLayers BroadPhaseLayerInterface;
		ObjectVsBroadPhaseFilter ObjectVsBroadPhase;
		ObjectLayerPairFilter ObjectLayerPair;

		JPH::PhysicsSystem System;

		std::unordered_map<UUID, Body> Bodies;

		JPH::BodyInterface& Interface() { return System.GetBodyInterface(); }
		const JPH::BodyInterface& Interface() const { return System.GetBodyInterface(); }

		Body* Find(UUID entity)
		{
			const auto it = Bodies.find(entity);
			return it == Bodies.end() ? nullptr : &it->second;
		}

		const Body* Find(UUID entity) const
		{
			const auto it = Bodies.find(entity);
			return it == Bodies.end() ? nullptr : &it->second;
		}

		// Fills in settings from the components; returns false when the entity
		// has nothing to simulate.
		bool DescribeBody(Scene& scene, Entity entity, JPH::BodyCreationSettings& out)
		{
			if (!entity.HasComponent<RigidBodyComponent>() ||
				!entity.HasComponent<ColliderComponent>() ||
				!entity.HasComponent<TransformComponent>())
				return false;

			const auto& body = entity.GetComponent<RigidBodyComponent>();
			const auto& collider = entity.GetComponent<ColliderComponent>();

			// World, not local: a body parented under something else still
			// simulates in world space, and its parent's transform is part of
			// where it actually is.
			const glm::mat4 world = scene.GetWorldTransform(entity);

			glm::vec3 position, scale, skew;
			glm::quat rotation;
			glm::vec4 perspective;
			if (!glm::decompose(world, scale, rotation, position, skew, perspective))
				return false;

			JPH::ShapeRefC shape = MakeShape(collider, scale);
			if (!shape)
				return false;

			out = JPH::BodyCreationSettings(shape, JPH::RVec3(position.x, position.y, position.z),
											ToJolt(glm::normalize(rotation)),
											ToMotionType(body.Type), ToObjectLayer(body.Type));

			out.mFriction = glm::max(body.Friction, 0.0f);
			out.mRestitution = glm::clamp(body.Restitution, 0.0f, 1.0f);
			out.mLinearDamping = glm::max(body.LinearDamping, 0.0f);
			out.mAngularDamping = glm::max(body.AngularDamping, 0.0f);
			out.mGravityFactor = body.GravityFactor;
			out.mIsSensor = collider.IsTrigger;

			if (body.Type == BodyType::Dynamic)
			{
				// Inertia is derived from the shape; only the total mass is
				// overridden, or a heavy object would also spin like a light one.
				out.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				out.mMassPropertiesOverride.mMass = glm::max(body.Mass, 0.001f);
			}

			if (body.FreezeRotation)
				out.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
								   JPH::EAllowedDOFs::TranslationZ;

			// The link back from a collision or a ray hit to the entity that
			// owns the body. A UUID rather than a pointer, so it survives the
			// entity being recreated.
			out.mUserData = (uint64_t)entity.GetUUID();
			return true;
		}

		void Record(UUID entity, JPH::BodyID id, BodyType type, const glm::vec3& position,
					const glm::quat& rotation)
		{
			Body record;
			record.Id = id;
			record.Entity = entity;
			record.Simulated = type != BodyType::Static;
			record.PreviousPosition = record.CurrentPosition = position;
			record.PreviousRotation = record.CurrentRotation = rotation;

			Bodies[entity] = record;
		}
	};

	// -------------------------------------------------------------------------
	PhysicsWorld::PhysicsWorld()
	{
		// Before the members exist, not after: see EnsureJoltGlobals.
		EnsureJoltGlobals();
		m_Impl = std::make_unique<Impl>();
	}

	PhysicsWorld::~PhysicsWorld()
	{
		// Bodies have to leave the simulation before their memory does.
		JPH::BodyInterface& bodies = m_Impl->Interface();
		for (auto& [entity, record] : m_Impl->Bodies)
		{
			bodies.RemoveBody(record.Id);
			bodies.DestroyBody(record.Id);
		}
		m_Impl->Bodies.clear();
	}

	void PhysicsWorld::Build(Scene& scene)
	{
		scene.UpdateWorldTransforms();

		JPH::BodyInterface& bodies = m_Impl->Interface();

		// Created first, added second. Adding one at a time leaves the
		// broad-phase tree degenerate, and a degenerate tree misses collisions
		// -- this is not a speed optimisation.
		std::vector<JPH::BodyID> pending;
		pending.reserve(64);

		auto view = scene.GetRegistry().view<RigidBodyComponent, ColliderComponent, TransformComponent>();
		for (auto handle : view)
		{
			Entity entity{ handle, &scene };

			JPH::BodyCreationSettings settings;
			if (!m_Impl->DescribeBody(scene, entity, settings))
				continue;

			JPH::Body* body = bodies.CreateBody(settings);
			if (!body)
			{
				RV_CORE_ERROR("Physics: out of bodies at {0}", entity.GetName());
				break;
			}

			pending.push_back(body->GetID());
			m_Impl->Record(entity.GetUUID(), body->GetID(),
						   entity.GetComponent<RigidBodyComponent>().Type,
						   ToGlm(settings.mPosition), ToGlm(settings.mRotation));
		}

		if (!pending.empty())
		{
			// AddBodiesPrepare may reorder the array, which is why the mapping
			// from entity to id was recorded before this rather than after.
			const JPH::BodyInterface::AddState state =
				bodies.AddBodiesPrepare(pending.data(), (int)pending.size());
			bodies.AddBodiesFinalize(pending.data(), (int)pending.size(), state,
									 JPH::EActivation::Activate);
		}

		// Rebuilds the tree in one pass now that everything is in it.
		m_Impl->System.OptimizeBroadPhase();

		RV_CORE_INFO("Physics: {0} bodies", pending.size());
	}

	void PhysicsWorld::AddBody(Scene& scene, Entity entity)
	{
		if (!entity || HasBody(entity.GetUUID()))
			return;

		JPH::BodyCreationSettings settings;
		if (!m_Impl->DescribeBody(scene, entity, settings))
			return;

		JPH::BodyInterface& bodies = m_Impl->Interface();
		const JPH::BodyID id = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
		if (id.IsInvalid())
			return;

		m_Impl->Record(entity.GetUUID(), id, entity.GetComponent<RigidBodyComponent>().Type,
					   ToGlm(settings.mPosition), ToGlm(settings.mRotation));
	}

	void PhysicsWorld::RemoveBody(UUID entity)
	{
		Impl::Body* record = m_Impl->Find(entity);
		if (!record)
			return;

		JPH::BodyInterface& bodies = m_Impl->Interface();
		bodies.RemoveBody(record->Id);
		bodies.DestroyBody(record->Id);
		m_Impl->Bodies.erase(entity);
	}

	bool PhysicsWorld::HasBody(UUID entity) const
	{
		return m_Impl->Find(entity) != nullptr;
	}

	size_t PhysicsWorld::GetBodyCount() const
	{
		return m_Impl->Bodies.size();
	}

	void PhysicsWorld::Step(float deltaTime, int collisionSteps)
	{
		if (deltaTime <= 0.0f)
			return;

		// Captured before the step, so rendering can blend between where each
		// body was and where it now is.
		JPH::BodyInterface& bodies = m_Impl->Interface();
		for (auto& [entity, record] : m_Impl->Bodies)
		{
			if (!record.Simulated)
				continue;

			record.PreviousPosition = record.CurrentPosition;
			record.PreviousRotation = record.CurrentRotation;
		}

		m_Impl->System.Update(deltaTime, collisionSteps, &m_Impl->TempAllocator, &m_Impl->JobSystem);

		for (auto& [entity, record] : m_Impl->Bodies)
		{
			if (!record.Simulated)
				continue;

			record.CurrentPosition = ToGlm(bodies.GetPosition(record.Id));
			record.CurrentRotation = ToGlm(bodies.GetRotation(record.Id));
		}
	}

	void PhysicsWorld::SyncTransforms(Scene& scene, float interpolationAlpha)
	{
		const float alpha = glm::clamp(interpolationAlpha, 0.0f, 1.0f);

		for (auto& [id, record] : m_Impl->Bodies)
		{
			if (!record.Simulated)
				continue;

			Entity entity = scene.GetEntityByUUID(record.Entity);
			if (!entity || !entity.HasComponent<TransformComponent>())
				continue;

			const glm::vec3 position = glm::mix(record.PreviousPosition, record.CurrentPosition, alpha);
			// slerp, not mix: interpolating quaternions linearly does not
			// travel at a constant angular rate, and the result is not unit
			// length.
			const glm::quat rotation = glm::slerp(record.PreviousRotation, record.CurrentRotation, alpha);

			auto& transform = entity.GetComponent<TransformComponent>();

			// The simulation works in world space; the transform is local. A
			// body under a parent has to come back through it.
			const glm::mat4 world = glm::translate(glm::mat4(1.0f), position) *
									glm::toMat4(rotation) *
									glm::scale(glm::mat4(1.0f), transform.Scale);

			const glm::mat4 local = glm::inverse(scene.GetParentWorldTransform(entity)) * world;

			glm::vec3 outPosition, outScale, skew;
			glm::quat outRotation;
			glm::vec4 perspective;
			if (glm::decompose(local, outScale, outRotation, outPosition, skew, perspective))
			{
				transform.Position = outPosition;
				transform.Rotation = glm::eulerAngles(outRotation);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Queries and control
	// -------------------------------------------------------------------------
	RayHit PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction) const
	{
		RayHit hit;

		const JPH::RRayCast ray{ JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(direction) };
		JPH::RayCastResult result;

		if (!m_Impl->System.GetNarrowPhaseQuery().CastRay(ray, result))
			return hit;

		hit.Hit = true;
		hit.Distance = result.mFraction * glm::length(direction);
		hit.Position = origin + direction * result.mFraction;

		JPH::BodyLockRead lock(m_Impl->System.GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
		{
			const JPH::Body& body = lock.GetBody();
			hit.Entity = UUID(body.GetUserData());
			hit.Normal = ToGlm(body.GetWorldSpaceSurfaceNormal(
				result.mSubShapeID2, JPH::RVec3(hit.Position.x, hit.Position.y, hit.Position.z)));
		}

		return hit;
	}

	void PhysicsWorld::SetLinearVelocity(UUID entity, const glm::vec3& velocity)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().SetLinearVelocity(record->Id, ToJolt(velocity));
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(UUID entity) const
	{
		if (const Impl::Body* record = m_Impl->Find(entity))
			return ToGlm(m_Impl->Interface().GetLinearVelocity(record->Id));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::AddForce(UUID entity, const glm::vec3& force)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().AddForce(record->Id, ToJolt(force), JPH::EActivation::Activate);
	}

	void PhysicsWorld::AddImpulse(UUID entity, const glm::vec3& impulse)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().AddImpulse(record->Id, ToJolt(impulse));
	}

	void PhysicsWorld::SetPosition(UUID entity, const glm::vec3& position)
	{
		Impl::Body* record = m_Impl->Find(entity);
		if (!record)
			return;

		m_Impl->Interface().SetPosition(record->Id, JPH::RVec3(position.x, position.y, position.z),
										JPH::EActivation::Activate);

		// Teleporting must not leave the renderer interpolating from where the
		// body used to be, or the object visibly streaks across the gap.
		record->PreviousPosition = record->CurrentPosition = position;
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		m_Impl->System.SetGravity(ToJolt(gravity));
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return ToGlm(m_Impl->System.GetGravity());
	}
}
