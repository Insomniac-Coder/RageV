#include <rvpch.h>
#include "PhysicsWorld.h"
#include "ColliderShapes.h"
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
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include "RageV/Math/Math.h"
#include <algorithm>
#include <cstdarg>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace RageV::Physics
{
	namespace
	{
		// --- conversions -----------------------------------------------------
		JPH::Vec3 ToJolt(const Vec3& v) { return { v.x, v.y, v.z }; }

		// One overload, not two: with DOUBLE_PRECISION off -- which is how this
		// build is configured -- JPH::RVec3 is an alias for JPH::Vec3, so a
		// second overload is a redefinition rather than a convenience.
		Vec3 ToGlm(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }

		JPH::Quat ToJolt(const Quat& q) { return { q.x, q.y, q.z, q.w }; }
		Quat ToGlm(const JPH::Quat& q)  { return { q.GetW(), q.GetX(), q.GetY(), q.GetZ() }; }

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

		// Jolt's globals are process-wide and have to exist before *anything*
		// Jolt allocates.
		//
		// This cannot live in World::Impl's constructor body, which is
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

		JPH::ShapeRefC MakeShape(const ColliderComponent& collider, const Vec3& scale)
		{
			// Scale is baked in rather than wrapped in a ScaledShape: it never
			// changes for a body during a run, and a baked shape is one fewer
			// indirection per query.
			//
			// The dimensions come from ScaleCollider rather than being worked
			// out here, so the debug overlay draws exactly what is simulated.
			// They were two copies of this arithmetic until the overlay existed
			// to disagree with it.
			const ScaledCollider sized = ScaleCollider(collider, scale);
			JPH::ShapeSettings::ShapeResult result;

			switch (collider.Shape)
			{
				case ColliderShape::Sphere:
					result = JPH::SphereShapeSettings(sized.Radius).Create();
					break;

				case ColliderShape::Capsule:
					result = JPH::CapsuleShapeSettings(sized.HalfHeight, sized.Radius).Create();
					break;

				case ColliderShape::Box:
				default:
					result = JPH::BoxShapeSettings(ToJolt(sized.HalfExtents)).Create();
					break;
			}

			if (result.HasError())
			{
				RV_CORE_ERROR("Collider shape could not be created: {0}", result.GetError().c_str());
				return {};
			}

			JPH::ShapeRefC shape = result.Get();

			if (Math::Dot(sized.Offset, sized.Offset) > 0.0f)
			{
				auto offset = JPH::RotatedTranslatedShapeSettings(
					ToJolt(sized.Offset), JPH::Quat::sIdentity(), shape).Create();

				if (!offset.HasError())
					shape = offset.Get();
			}

			return shape;
		}

		// --- contacts --------------------------------------------------------
		// What the listener records, before anything is interpreted.
		//
		// Jolt calls contact callbacks from several job threads at once, with
		// every body locked, so the listener may not touch the scene, the body
		// interface, or any engine state. It records the raw fact and returns;
		// all of the interpretation happens on the main thread once Update has
		// returned.
		enum class RawKind : uint8_t { Added, Persisted, Removed };

		struct RawContact
		{
			RawKind Kind = RawKind::Added;
			JPH::BodyID Id1, Id2;

			// Read from the bodies' user data at callback time. Removed cannot
			// carry them -- the bodies may already be destroyed by then -- so
			// they are recovered from the tracked pair instead.
			UUID A = UUID::Invalid();
			UUID B = UUID::Invalid();

			bool Trigger = false;
			Vec3 Point{ 0.0f };
			Vec3 Normal{ 0.0f };
			float ImpactSpeed = 0.0f;
		};

		struct ContactQueue
		{
			std::mutex Mutex;
			std::vector<RawContact> Raw;
		};

		// Jolt sorts the two bodies by id before every contact callback, so the
		// pair (a, b) always arrives in the same order and one key identifies it.
		uint64_t PairKey(const JPH::BodyID& a, const JPH::BodyID& b)
		{
			return ((uint64_t)a.GetIndexAndSequenceNumber() << 32) |
					(uint64_t)b.GetIndexAndSequenceNumber();
		}

		class ContactListenerImpl final : public JPH::ContactListener
		{
		public:
			explicit ContactListenerImpl(ContactQueue& queue) : m_Queue(queue) {}

			void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
								const JPH::ContactManifold& manifold,
								JPH::ContactSettings&) override
			{
				Push(RawKind::Added, body1, body2, manifold);
			}

			void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2,
									const JPH::ContactManifold& manifold,
									JPH::ContactSettings&) override
			{
				Push(RawKind::Persisted, body1, body2, manifold);
			}

			void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
			{
				// Nothing may be read from the bodies here -- one of them may
				// already have been destroyed, and the rest are being written by
				// other threads. Only the ids are safe.
				RawContact contact;
				contact.Kind = RawKind::Removed;
				contact.Id1 = pair.GetBody1ID();
				contact.Id2 = pair.GetBody2ID();

				std::lock_guard<std::mutex> lock(m_Queue.Mutex);
				m_Queue.Raw.push_back(contact);
			}

		private:
			void Push(RawKind kind, const JPH::Body& body1, const JPH::Body& body2,
					  const JPH::ContactManifold& manifold)
			{
				RawContact contact;
				contact.Kind = kind;
				contact.Id1 = body1.GetID();
				contact.Id2 = body2.GetID();
				contact.A = UUID(body1.GetUserData());
				contact.B = UUID(body2.GetUserData());
				contact.Trigger = body1.IsSensor() || body2.IsSensor();
				contact.Normal = ToGlm(manifold.mWorldSpaceNormal);

				// A manifold is a surface, not a point. The average of its
				// points is where the contact reads as being; picking the first
				// would make a box landing flat report one arbitrary corner.
				const uint32_t count = manifold.mRelativeContactPointsOn1.size();
				if (count > 0)
				{
					JPH::Vec3 sum = JPH::Vec3::sZero();
					for (uint32_t i = 0; i < count; i++)
						sum += manifold.mRelativeContactPointsOn1[i];

					contact.Point = ToGlm(manifold.mBaseOffset + sum / (float)count);
				}
				else
				{
					contact.Point = ToGlm(manifold.mBaseOffset);
				}

				// Safe on a static body: Jolt returns zero rather than reading
				// motion properties it does not have.
				const Vec3 relative = ToGlm(body2.GetLinearVelocity()) -
										   ToGlm(body1.GetLinearVelocity());

				// The normal runs from body 1 into body 2, so approaching means
				// body 2 moves against it. Negative is separating, which a
				// speculative contact can be, and is reported as no impact.
				contact.ImpactSpeed = Math::Max(-Math::Dot(relative, contact.Normal), 0.0f);

				std::lock_guard<std::mutex> lock(m_Queue.Mutex);
				m_Queue.Raw.push_back(contact);
			}

			ContactQueue& m_Queue;
		};
	}

	// -------------------------------------------------------------------------
	struct World::Impl
	{
		// Per body, so rendering can interpolate. Without the previous state,
		// anything moving fast stutters: the display refreshes between two
		// discrete simulation positions, not on them.
		struct Body
		{
			JPH::BodyID Id;
			UUID Entity = UUID::Invalid();
			bool Simulated = false;   // false for static: nothing to interpolate

			Vec3 PreviousPosition{ 0.0f };
			Vec3 CurrentPosition{ 0.0f };
			Quat PreviousRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
			Quat CurrentRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		};

		// Two bodies that are touching, and how the engine last reported them.
		struct Pair
		{
			JPH::BodyID Id1, Id2;
			UUID A = UUID::Invalid();
			UUID B = UUID::Invalid();
			bool Trigger = false;

			// Jolt reports contacts per sub-shape pair, and a compound shape
			// can produce several for one pair of bodies. Enter fires when this
			// rises off zero and Exit when it returns to it, so a script sees
			// one touch rather than one per feature of the geometry.
			int SubShapes = 0;

			// Still touching, but Jolt has withdrawn the contact because both
			// bodies went to sleep. See ProcessContacts.
			bool Dormant = false;
		};

		Impl()
			: TempAllocator(16 * 1024 * 1024),
			  // One less than the hardware supports: the main thread is doing
			  // the rest of the frame and taking every core starves it.
			  JobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
						(int)Math::Max(1u, std::thread::hardware_concurrency() - 1u)),
			  Listener(Queue)
		{
			System.Init(kMaxBodies, kBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
						BroadPhaseLayerInterface, ObjectVsBroadPhase, ObjectLayerPair);
			System.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
			System.SetContactListener(&Listener);
		}

		~Impl()
		{
			// The listener is a member and is about to be destroyed, so the
			// system must stop pointing at it first. Nothing steps between here
			// and destruction today, but a dangling listener is not the kind of
			// thing to leave depending on that.
			System.SetContactListener(nullptr);
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

		// Queue first: the listener binds a reference to it, and members are
		// constructed in declaration order.
		ContactQueue Queue;
		ContactListenerImpl Listener;

		std::unordered_map<UUID, Body> Bodies;
		std::unordered_map<uint64_t, Pair> Pairs;
		std::vector<ContactEvent> Events;

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
			const Mat4 world = scene.GetWorldTransform(entity);

			Vec3 position, scale;
			Quat rotation;
			if (!Math::Decompose(world, position, rotation, scale))
				return false;

			JPH::ShapeRefC shape = MakeShape(collider, scale);
			if (!shape)
				return false;

			out = JPH::BodyCreationSettings(shape, JPH::RVec3(position.x, position.y, position.z),
											ToJolt(Math::Normalize(rotation)),
											ToMotionType(body.Type), ToObjectLayer(body.Type));

			out.mFriction = Math::Max(body.Friction, 0.0f);
			out.mRestitution = Math::Clamp(body.Restitution, 0.0f, 1.0f);
			out.mLinearDamping = Math::Max(body.LinearDamping, 0.0f);
			out.mAngularDamping = Math::Max(body.AngularDamping, 0.0f);
			out.mGravityFactor = body.GravityFactor;
			out.mIsSensor = collider.IsTrigger;

			if (body.Type == BodyType::Dynamic)
			{
				// Inertia is derived from the shape; only the total mass is
				// overridden, or a heavy object would also spin like a light one.
				out.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				out.mMassPropertiesOverride.mMass = Math::Max(body.Mass, 0.001f);
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

		// Turns what the listener recorded into engine events. Main thread,
		// after Update has returned, so bodies may be queried normally again.
		void ProcessContacts()
		{
			std::vector<RawContact> raw;
			{
				std::lock_guard<std::mutex> lock(Queue.Mutex);
				raw.swap(Queue.Raw);
			}

			if (raw.empty())
				return;

			const JPH::BodyInterface& bodies = Interface();

			for (const RawContact& contact : raw)
			{
				const uint64_t key = PairKey(contact.Id1, contact.Id2);

				if (contact.Kind == RawKind::Removed)
				{
					const auto it = Pairs.find(key);
					if (it == Pairs.end())
						continue;   // already retired, or never tracked

					Pair& pair = it->second;
					if (pair.SubShapes > 0)
						pair.SubShapes--;

					if (pair.SubShapes > 0)
						continue;   // other features of the same pair still touch

					// Jolt withdraws every contact of a body the moment it falls
					// asleep. A box that has settled on the floor is still on
					// the floor, so reporting that as separation would mean a
					// script's "am I standing on something" answer flips to no
					// about a second after it lands -- which is exactly when it
					// looks most stationary.
					//
					// Neither body being awake is what tells the two cases
					// apart: bodies that genuinely separate are moving, and a
					// body cannot fall asleep while it is.
					const bool bothPresent = Bodies.count(pair.A) != 0 && Bodies.count(pair.B) != 0;
					if (bothPresent && !bodies.IsActive(pair.Id1) && !bodies.IsActive(pair.Id2))
					{
						pair.Dormant = true;
						continue;
					}

					ContactEvent event;
					event.Phase = ContactPhase::Exit;
					event.A = pair.A;
					event.B = pair.B;
					event.Trigger = pair.Trigger;
					Events.push_back(event);

					Pairs.erase(it);
					continue;
				}

				const auto [it, inserted] = Pairs.try_emplace(key);
				Pair& pair = it->second;

				if (inserted)
				{
					pair.Id1 = contact.Id1;
					pair.Id2 = contact.Id2;
					pair.A = contact.A;
					pair.B = contact.B;
				}
				pair.Trigger = contact.Trigger;

				if (contact.Kind == RawKind::Added)
					pair.SubShapes++;
				else if (pair.SubShapes == 0)
					pair.SubShapes = 1;   // persisted across a sleep, never added

				ContactEvent event;
				event.Phase = ContactPhase::Stay;
				event.A = pair.A;
				event.B = pair.B;
				event.Trigger = pair.Trigger;
				event.Point = contact.Point;
				event.Normal = contact.Normal;
				event.ImpactSpeed = contact.ImpactSpeed;

				if (inserted)
				{
					event.Phase = ContactPhase::Enter;
				}
				else if (pair.Dormant)
				{
					// Waking up is not touching something new. Left as Stay, so
					// a pair that sleeps and wakes reports one Enter over its
					// whole life rather than one per nap.
					pair.Dormant = false;
				}

				Events.push_back(event);
			}

			// Contacts arrive from several job threads, so their order within a
			// step is whatever the scheduler produced. Sorting makes the events
			// a scene sees depend only on the scene.
			std::sort(Events.begin(), Events.end(), [](const ContactEvent& a, const ContactEvent& b)
			{
				if (a.A != b.A) return (uint64_t)a.A < (uint64_t)b.A;
				if (a.B != b.B) return (uint64_t)a.B < (uint64_t)b.B;
				return (uint8_t)a.Phase < (uint8_t)b.Phase;
			});
		}

		// Everything the entity was touching stops being touched, now rather
		// than on the next step.
		//
		// Jolt does report the contacts of a removed body as removed, but only
		// during the following Update -- and a pair left asleep is never
		// reported again at all, since its contact was withdrawn when it slept.
		void RetirePairs(UUID entity)
		{
			for (auto it = Pairs.begin(); it != Pairs.end(); )
			{
				if (it->second.A != entity && it->second.B != entity)
				{
					++it;
					continue;
				}

				ContactEvent event;
				event.Phase = ContactPhase::Exit;
				event.A = it->second.A;
				event.B = it->second.B;
				event.Trigger = it->second.Trigger;
				Events.push_back(event);

				it = Pairs.erase(it);
			}
		}

		void Record(UUID entity, JPH::BodyID id, BodyType type, const Vec3& position,
					const Quat& rotation)
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
	World::World()
	{
		// Before the members exist, not after: see EnsureJoltGlobals.
		EnsureJoltGlobals();
		m_Impl = std::make_unique<Impl>();
	}

	World::~World()
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

	void World::Build(Scene& scene)
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

	void World::AddBody(Scene& scene, Entity entity)
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

	void World::RemoveBody(UUID entity)
	{
		Impl::Body* record = m_Impl->Find(entity);
		if (!record)
			return;

		// Before the body goes, so anything touching it is told while there is
		// still something to name.
		m_Impl->RetirePairs(entity);

		JPH::BodyInterface& bodies = m_Impl->Interface();
		bodies.RemoveBody(record->Id);
		bodies.DestroyBody(record->Id);
		m_Impl->Bodies.erase(entity);
	}

	bool World::HasBody(UUID entity) const
	{
		return m_Impl->Find(entity) != nullptr;
	}

	bool World::IsBodyAwake(UUID entity) const
	{
		const Impl::Body* record = m_Impl->Find(entity);
		return record && m_Impl->Interface().IsActive(record->Id);
	}

	size_t World::GetBodyCount() const
	{
		return m_Impl->Bodies.size();
	}

	void World::Step(float deltaTime, int collisionSteps)
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

		// After Update, never inside a callback: the contact listener runs on
		// job threads with every body locked.
		m_Impl->ProcessContacts();

		for (auto& [entity, record] : m_Impl->Bodies)
		{
			if (!record.Simulated)
				continue;

			record.CurrentPosition = ToGlm(bodies.GetPosition(record.Id));
			record.CurrentRotation = ToGlm(bodies.GetRotation(record.Id));
		}
	}

	void World::TakeContactEvents(std::vector<ContactEvent>& out)
	{
		out.clear();
		out.swap(m_Impl->Events);
	}

	size_t World::GetContactPairCount() const
	{
		return m_Impl->Pairs.size();
	}

	void World::SyncTransforms(Scene& scene, float interpolationAlpha)
	{
		const float alpha = Math::Clamp(interpolationAlpha, 0.0f, 1.0f);

		for (auto& [id, record] : m_Impl->Bodies)
		{
			if (!record.Simulated)
				continue;

			Entity entity = scene.GetEntityByUUID(record.Entity);
			if (!entity || !entity.HasComponent<TransformComponent>())
				continue;

			const Vec3 position = Math::Mix(record.PreviousPosition, record.CurrentPosition, alpha);
			// slerp, not mix: interpolating quaternions linearly does not
			// travel at a constant angular rate, and the result is not unit
			// length.
			const Quat rotation = Math::Slerp(record.PreviousRotation, record.CurrentRotation, alpha);

			auto& transform = entity.GetComponent<TransformComponent>();

			// The simulation works in world space; the transform is local. A
			// body under a parent has to come back through it.
			const Mat4 world = Math::Translate(Mat4(1.0f), position) *
									Math::ToMat4(rotation) *
									Math::Scale(Mat4(1.0f), transform.Scale);

			const Mat4 local = Math::Inverse(scene.GetParentWorldTransform(entity)) * world;

			Vec3 outPosition, outScale;
			Quat outRotation;
			if (Math::Decompose(local, outPosition, outRotation, outScale))
			{
				transform.Position = outPosition;
				transform.Rotation = Math::ToEuler(outRotation);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Queries and control
	// -------------------------------------------------------------------------
	RayHit World::CastRay(const Vec3& origin, const Vec3& direction) const
	{
		RayHit hit;

		const JPH::RRayCast ray{ JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(direction) };
		JPH::RayCastResult result;

		if (!m_Impl->System.GetNarrowPhaseQuery().CastRay(ray, result))
			return hit;

		hit.Hit = true;
		hit.Distance = result.mFraction * Math::Length(direction);
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

	void World::SetLinearVelocity(UUID entity, const Vec3& velocity)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().SetLinearVelocity(record->Id, ToJolt(velocity));
	}

	Vec3 World::GetLinearVelocity(UUID entity) const
	{
		if (const Impl::Body* record = m_Impl->Find(entity))
			return ToGlm(m_Impl->Interface().GetLinearVelocity(record->Id));
		return Vec3(0.0f);
	}

	void World::AddForce(UUID entity, const Vec3& force)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().AddForce(record->Id, ToJolt(force), JPH::EActivation::Activate);
	}

	void World::AddImpulse(UUID entity, const Vec3& impulse)
	{
		if (Impl::Body* record = m_Impl->Find(entity))
			m_Impl->Interface().AddImpulse(record->Id, ToJolt(impulse));
	}

	void World::SetPosition(UUID entity, const Vec3& position)
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

	void World::SetGravity(const Vec3& gravity)
	{
		m_Impl->System.SetGravity(ToJolt(gravity));
	}

	Vec3 World::GetGravity() const
	{
		return ToGlm(m_Impl->System.GetGravity());
	}
}
