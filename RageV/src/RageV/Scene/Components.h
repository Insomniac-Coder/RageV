#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "RageV/Core/UUID.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsTypes.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Animation/Skeleton.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Renderer/ReflectionProbe.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// Stable identity. Added to every entity at creation, preserved across
	// save/load, and the only durable way to name an entity -- entt::entity
	// handles are recycled and mean nothing outside one registry instance.
	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
		IDComponent(UUID id) : ID(id) {}
	};

	// Parent/child links, by UUID rather than by handle so they survive
	// serialization.
	//
	// Children is derived state: only Parent is written to disk, and the child
	// lists are rebuilt on load. Storing both would let them disagree, and a
	// hierarchy that disagrees with itself is the kind of bug that only shows
	// up three features later.
	struct RelationshipComponent
	{
		UUID Parent = UUID::Invalid();
		std::vector<UUID> Children;

		RelationshipComponent() = default;
		RelationshipComponent(const RelationshipComponent&) = default;
	};

	struct TagComponent {
		std::string Name;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& name) { Name = name; }
	};

	struct TransformComponent
	{
		// Local, relative to the parent. World is derived from these.
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		Vec3 Scale{ 1.0f };

		// Recomputed unconditionally by Scene::UpdateWorldTransforms in one
		// top-down pass per frame.
		//
		// Deliberately not a dirty-flag cache. A flag has to be set at every
		// write site -- the inspector, the gizmo, scripts, the serializer, and
		// everything added later -- and a single missed one leaves an object
		// silently rendering in the wrong place. Recomputing is O(n) at scene
		// scale and cannot be got wrong.
		Mat4 World{ 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const Vec3& position) { Position = position; }

		Mat4 GetLocalTransform() const
		{
			Mat4 rotation = Math::ToMat4(Math::FromEuler(Rotation));
			return	Math::Translate(Mat4(1.0f), Position) *
					rotation *
					Math::Scale(Mat4(1.0f), Scale);
		}
	};

	struct ColorComponent
	{
		Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		ColorComponent() = default;
		ColorComponent(const ColorComponent&) = default;
		ColorComponent(const Vec4& color) { Color = color; }
	};

	struct CameraComponent
	{
		RageV::SceneCamera Camera;

		// Which camera the game view renders through: the lowest rank wins,
		// 0 highest priority through 99 lowest.
		//
		// A rank rather than the `isPrimary` flag this replaced. A boolean can
		// be true on two cameras at once, and then which one renders depends on
		// registry iteration order -- so adding a camera could silently change
		// the view. A rank always has a single winner, and ties break on entity
		// id so the answer is the same on every run.
		int ViewRank = 0;

		bool fixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
	};

	struct LightComponent
	{
		RageV::Light Light;

		LightComponent() = default;
		LightComponent(const LightComponent&) = default;
		LightComponent(const RageV::Light& light) : Light(light) {}
	};

	// How often a probe re-renders what it can see.
	enum class ProbeUpdate : uint32_t
	{
		// Captured once and kept. Costs nothing after the first frame, which is
		// what makes it the right answer for a room, a corridor, or anything
		// else whose surroundings do not move.
		Baked = 0,

		// Re-captured continuously, one face per frame by default. Costs one
		// extra scene render per frame and shows moving objects in reflections.
		Realtime = 1,
	};

	// Captures the scene into a cube map from one point, for surfaces near it
	// to reflect.
	//
	// The scene picks one probe per render: the nearest whose influence radius
	// contains the camera, and the sky otherwise. Per-object selection would
	// mean rebinding the scene descriptor set per draw, and blending between
	// probes needs both bound at once -- neither is worth it before the thing
	// works with one.
	struct ReflectionProbeComponent
	{
		ProbeUpdate Update = ProbeUpdate::Baked;

		// Per face. Reflections are seen through a rough surface or a curved
		// one, so this can be far smaller than it feels like it should be: 128
		// is six 128x128 renders and is hard to fault on anything but a mirror.
		//
		// Signed because the reflected inspector has no unsigned widget, and a
		// second numeric type for one field is worse than one clamp.
		int Resolution = 128;

		float NearClip = 0.05f;
		float FarClip = 100.0f;

		// How far from this probe its capture is still a reasonable answer.
		// Reflections come from a single point, so the further a surface is from
		// that point the more wrong the parallax, and past some distance the sky
		// is the better lie.
		float Influence = 20.0f;

		// Realtime only. Six renders in one frame is a visible hitch; one per
		// frame is a sixth of the cost and a sixth of a second of latency on a
		// reflection.
		int FacesPerFrame = 1;

		// Runtime state. Not serialized: a capture is derived from the scene,
		// like a shadow map, and writing one into a text file would be storing
		// a render in a scene description.
		std::shared_ptr<ReflectionProbe> Probe;
		uint32_t NextFace = 0;
		// Set when the probe has never been captured, or when something that
		// invalidates the capture changed. A baked probe watches this; a
		// realtime one ignores it and re-captures regardless.
		bool Dirty = true;

		ReflectionProbeComponent() = default;
		ReflectionProbeComponent(const ReflectionProbeComponent&) = default;
	};

	// 3D geometry, referenced by handle rather than embedded. A scene file
	// carries the handle; the vertex data stays in whatever the handle points
	// at, which is a built-in primitive or an imported model.
	struct MeshComponent
	{
		AssetHandle Mesh = PrimitiveHandle(PrimitiveType::Cube);
		// Null means the renderer's shared default material.
		RHI::Ref<Material> Material;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
		MeshComponent(PrimitiveType primitive) : Mesh(PrimitiveHandle(primitive)) {}
		MeshComponent(AssetHandle mesh) : Mesh(mesh) {}
	};

	// Plays a clip from the model a MeshComponent points at.
	//
	// Separate from MeshComponent so a character's body, head and clothing can
	// be three meshes driven by one animator, which is how every rig of any
	// size is built. The animator lives on the entity that owns the skeleton
	// and the meshes are its children.
	struct AnimatorComponent
	{
		// Which clip of the model's own list. -1 is the bind pose, which is a
		// legitimate state and not an error -- it is what a character that has
		// never been told to move should look like.
		int Clip = 0;
		bool Playing = true;
		bool Loop = true;
		float Speed = 1.0f;

		// Seconds into the clip. Not serialized: it means nothing outside the
		// run that produced it, for the same reason an audio voice is not.
		float Time = 0.0f;

		// The pose, rebuilt each frame. Kept here rather than in the renderer
		// so a script can read a bone's position -- a weapon in a hand needs
		// exactly this, and recomputing it would be a second answer to a
		// question already answered.
		std::vector<Mat4> Skinning;

		AnimatorComponent() = default;
		AnimatorComponent(const AnimatorComponent&) = default;
	};

	// Takes part in the physics simulation. Needs a ColliderComponent to have
	// any shape; without one the entity is simulated as a point and falls
	// through everything.
	struct RigidBodyComponent
	{
		BodyType Type = BodyType::Dynamic;

		float Mass = 1.0f;
		float Friction = 0.4f;
		// 0 is a dead drop, 1 bounces back to the height it fell from. Above
		// about 0.95 a stack gains energy and never settles.
		float Restitution = 0.1f;

		// Bleeds off velocity over time. A little of both is what stops light
		// objects drifting forever on a flat surface.
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;

		// Scales gravity for this body alone: 0 floats, negative rises.
		float GravityFactor = 1.0f;

		// Locks rotation. What a character controller wants, since a capsule
		// that tips over stops being a character.
		bool FreezeRotation = false;

		RigidBodyComponent() = default;
		RigidBodyComponent(const RigidBodyComponent&) = default;
		RigidBodyComponent(BodyType type) : Type(type) {}
	};

	struct ColliderComponent
	{
		ColliderShape Shape = ColliderShape::Box;

		// Box. Half-extents, so the default is a 1x1x1 cube -- matching the
		// cube primitive, so a cube with a collider lines up without tuning.
		Vec3 HalfExtents{ 0.5f };

		// Sphere and capsule.
		float Radius = 0.5f;
		// Capsule: the cylindrical section between the two caps, so the total
		// height is this plus two radii.
		float Height = 1.0f;

		// Moves the shape relative to the entity, for a collider that should
		// not sit on the origin -- feet at the bottom of a character, say.
		Vec3 Offset{ 0.0f };

		// Reports overlaps without resisting them.
		bool IsTrigger = false;

		ColliderComponent() = default;
		ColliderComponent(const ColliderComponent&) = default;
		ColliderComponent(ColliderShape shape) : Shape(shape) {}
	};

	// A sound this entity can play, positioned where the entity is.
	//
	// One clip per component rather than a list. A component holding several
	// would need a way to say which one to play, which is a script's job -- and
	// a script can play any clip through the one-shot helpers without a
	// component at all.
	struct AudioSourceComponent
	{
		AssetHandle Clip = AssetHandle::Invalid();
		AudioBus Bus = AudioBus::SFX;

		float Volume = 1.0f;
		// Also changes speed: this is a resampling ratio, not a shift. 2 is an
		// octave up and half as long.
		float Pitch = 1.0f;
		bool Loop = false;

		// Starts when the scene starts playing. Off for anything a script or a
		// collision is meant to trigger.
		bool PlayOnAwake = true;

		// Positioned in the world, so it is quieter further away and pans as
		// the listener turns. Off for music and UI.
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;

		// Decoded while playing rather than up front. For music; a long track
		// decoded into memory costs tens of megabytes to play once.
		bool Stream = false;

		// Runtime only, and deliberately not a registered field: a voice is
		// meaningless outside the run that created it, so it is neither
		// serialized nor shown.
		AudioVoice Voice = 0;

		AudioSourceComponent() = default;
		AudioSourceComponent(const AudioSourceComponent& other)
		{
			*this = other;
		}

		// Copying a component must not copy the voice, for the same reason a
		// script instance is not copied: two components would own one playing
		// sound and both would stop it.
		AudioSourceComponent& operator=(const AudioSourceComponent& other)
		{
			Clip = other.Clip;
			Bus = other.Bus;
			Volume = other.Volume;
			Pitch = other.Pitch;
			Loop = other.Loop;
			PlayOnAwake = other.PlayOnAwake;
			Spatial = other.Spatial;
			MinDistance = other.MinDistance;
			MaxDistance = other.MaxDistance;
			Stream = other.Stream;
			Voice = 0;
			return *this;
		}
	};

	// How a particle quad faces the world. Billboard turns to the camera --
	// smoke, fire, sparks in a 3D scene. Flat lies in the XY plane facing +Z,
	// which is what a 2D game's particles are.
	enum class ParticleFacing { Billboard, Flat };

	// Alpha reads as matter -- smoke, dust, debris -- and needs drawing back
	// to front. Additive reads as light -- fire, sparks, magic -- and sums the
	// same from any order, which also makes it the cheaper one to draw.
	//
	// WeightedBlended is alpha that does not need the sort: fragments
	// accumulate and a resolve pass works out the answer, so a thousand
	// overlapping particles land in any order and look the same. It is what
	// a GPU emitter wants, since sorting its pool would mean reading it back.
	// The cost is two extra full-resolution targets and a resolve, paid once
	// per frame no matter how many emitters use it -- and nothing at all when
	// none do.
	enum class ParticleBlend { Alpha, Additive, WeightedBlended };

	// World leaves a particle where it was born -- smoke keeps hanging where
	// the chimney was. Local carries it with the emitter -- a torch flame
	// moves with the torch.
	enum class ParticleSpace { World, Local };

	// One live particle. CPU-simulated; the GPU path keeps the same layout on
	// its own buffers and never reads it back.
	struct Particle
	{
		Vec3 Position{ 0.0f };   // emitter space per ParticleSpace
		Vec3 Velocity{ 0.0f };
		float Age = 0.0f;
		float Lifetime = 1.0f;
		float Rotation = 0.0f;   // radians, around the view axis
		float Spin = 0.0f;       // radians per second
	};

	struct ParticleEmitterComponent
	{
		// Continuous emission. Zero with a Burst is the explosion shape: one
		// bang when the scene starts or when a script asks, nothing after.
		bool  Emit = true;
		float Rate = 20.0f;              // particles per second

		// Consumed on the next simulation step, then zero. Authored non-zero
		// it fires once on the first step after Play -- which is what makes an
		// explosion prefab work with no script at all. Scripts write it too,
		// in either language.
		int   Burst = 0;

		float Lifetime = 1.5f;           // seconds each particle lives
		float LifetimeJitter = 0.25f;    // fraction of Lifetime, randomised away

		// The cone particles leave through, in the emitter's own frame.
		Vec3  Direction{ 0.0f, 1.0f, 0.0f };
		float Spread = 25.0f;            // half-angle, degrees
		float Speed = 3.0f;
		float SpeedJitter = 0.3f;        // fraction of Speed

		// The emitter's own gravity, not the physics world's: snow wants a
		// drift, sparks want a plunge, and neither is a rigid body.
		Vec3  Gravity{ 0.0f, -3.0f, 0.0f };
		float Drag = 0.0f;               // fraction of velocity lost per second

		float SizeStart = 0.25f;
		float SizeEnd = 0.05f;
		Vec4  ColorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec4  ColorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
		float Spin = 0.0f;               // max degrees per second, signed at random

		ParticleFacing Facing = ParticleFacing::Billboard;
		ParticleBlend  Blend = ParticleBlend::Alpha;
		ParticleSpace  Space = ParticleSpace::World;

		// Optional sprite; a plain white quad without one.
		AssetHandle Texture = AssetHandle::Invalid();

		int  MaxParticles = 1000;

		// Simulate on the GPU: the pool lives in a storage buffer, a compute
		// pass integrates it, and the CPU never touches a particle again.
		// The visual contract is the CPU path's; what changes is who pays.
		bool SimulateOnGpu = false;

		// --- runtime, neither serialized nor copied --------------------------
		std::vector<Particle> Pool;
		float EmitCarry = 0.0f;
		uint32_t Rng = 0;                // 0 means "seed me on first use"

		ParticleEmitterComponent() = default;
		ParticleEmitterComponent(const ParticleEmitterComponent& other) { *this = other; }

		// The pool is the run's, not the file's: play mode copies the scene,
		// and a copied emitter starts empty exactly as a loaded one does.
		ParticleEmitterComponent& operator=(const ParticleEmitterComponent& other)
		{
			Emit = other.Emit;
			Rate = other.Rate;
			Burst = other.Burst;
			Lifetime = other.Lifetime;
			LifetimeJitter = other.LifetimeJitter;
			Direction = other.Direction;
			Spread = other.Spread;
			Speed = other.Speed;
			SpeedJitter = other.SpeedJitter;
			Gravity = other.Gravity;
			Drag = other.Drag;
			SizeStart = other.SizeStart;
			SizeEnd = other.SizeEnd;
			ColorStart = other.ColorStart;
			ColorEnd = other.ColorEnd;
			Spin = other.Spin;
			Facing = other.Facing;
			Blend = other.Blend;
			Space = other.Space;
			Texture = other.Texture;
			MaxParticles = other.MaxParticles;
			SimulateOnGpu = other.SimulateOnGpu;
			Pool.clear();
			EmitCarry = 0.0f;
			Rng = 0;
			return *this;
		}
	};

	// Where the scene is heard from.
	//
	// Optional: with no listener in the scene the primary camera is used, which
	// is what is wanted almost every time and means audio works in a scene
	// nobody thought about it in. Add one to hear from somewhere other than the
	// camera -- a first-person game whose camera sits at the eyes but whose
	// listener belongs at the character.
	struct AudioListenerComponent
	{
		// Lowest wins, ties broken on entity id. The same rule as a camera's
		// ViewRank, and for the same reason: a boolean can be true twice.
		int ListenerRank = 0;

		AudioListenerComponent() = default;
		AudioListenerComponent(const AudioListenerComponent&) = default;
	};

	// Marks the root of an entity tree stamped out from a prefab asset.
	//
	// The instance is fully materialised into the scene -- every entity is a
	// real entity and anything about it can be edited. This records where it
	// came from, which is what "select all instances" and, later, propagating
	// an edit back to the source will hang off.
	//
	// NOT YET: editing a prefab does not update instances already placed. That
	// needs each instance entity to remember which prefab entity it came from
	// and a diff of what has been changed since -- and the hard part is not the
	// diff, it is deciding what an added or reordered child means.
	struct PrefabComponent
	{
		AssetHandle Source = AssetHandle::Invalid();

		PrefabComponent() = default;
		PrefabComponent(const PrefabComponent&) = default;
		PrefabComponent(AssetHandle source) : Source(source) {}
	};

	// A script by name, instantiated through ScriptRegistry.
	//
	// The name is what goes to disk. Holding a factory instead -- which is what
	// this did -- meant a script could only be attached from C++ with a
	// compile-time type, so it could be neither chosen in the inspector nor
	// saved: a scene with scripted behaviour lost all of it on save.
	//
	// The instance is deleted through ScriptableEntity's virtual destructor, so
	// the per-type destroy thunk this used to carry is gone with it.
	// Field values authored in the inspector, for either kind of script.
	//
	// Shared between the native and managed components because the rule is the
	// same in both: only fields somebody actually changed are stored, so editing
	// a default in code reaches every entity that never overrode it. Text,
	// because the scene file is text and because a tagged union would be two
	// languages' worth of upkeep for an operation that happens when a person
	// types rather than per step.
	struct ScriptFieldOverrides
	{
		struct Entry
		{
			std::string Name;
			std::string Value;
		};
		std::vector<Entry> Values;

		const std::string* Find(const std::string& name) const
		{
			for (const Entry& entry : Values)
			{
				if (entry.Name == name)
					return &entry.Value;
			}
			return nullptr;
		}

		void Set(const std::string& name, const std::string& value)
		{
			for (Entry& entry : Values)
			{
				if (entry.Name == name)
				{
					entry.Value = value;
					return;
				}
			}
			Values.push_back({ name, value });
		}

		void Clear(const std::string& name)
		{
			Values.erase(std::remove_if(Values.begin(), Values.end(),
										[&name](const Entry& entry) { return entry.Name == name; }),
						 Values.end());
		}

		bool Empty() const { return Values.empty(); }
	};

	// A C# script on an entity.
	//
	// Deliberately separate from NativeScriptComponent rather than one component
	// with a language flag. The two have different failure modes, different
	// lifetimes and different inspector needs -- a managed script can be
	// recompiled while the editor runs, a native one cannot -- and folding them
	// together would mean every use site asking which kind it was holding.
	//
	// An entity may carry both. Nothing in the design prevents it and the
	// simulation steps them in a defined order: native first, then managed.
	struct ManagedScriptComponent
	{
		// The C# type, as written in the assembly: "Example" or "Game.Enemy".
		std::string ScriptName;

		// What to call this component in the inspector. A tag, nothing more:
		// nothing looks it up and nothing validates it. Which script runs is
		// ScriptName's job, and that has to be a name the build actually has.
		std::string Label;

		// Values the inspector authored, applied to the instance after it is
		// created and before OnCreate runs.
		//
		// Stored as text keyed by field name, because the scene file is text and
		// because the alternative -- a variant that must be kept in step with
		// the managed field types in two languages -- is upkeep bought for
		// nothing on an operation that happens when somebody types.
		//
		// Only fields the user actually changed are stored. A field left alone
		// keeps whatever the script's own initialiser gave it, so editing a
		// default in code changes every entity that never overrode it.
		ScriptFieldOverrides Fields;

		// Forwarded so a caller says `script.Set("Speed", "2")` rather than
		// reaching through to the storage. Both script components expose the
		// same three, because from the inspector's point of view they are the
		// same component in two languages.
		const std::string* Find(const std::string& name) const { return Fields.Find(name); }
		void Set(const std::string& name, const std::string& value) { Fields.Set(name, value); }
		void Clear(const std::string& name) { Fields.Clear(name); }

		// Runtime only, not serialized. Zero means nothing is instantiated.
		int32_t Handle = 0;
		// Which name the live instance was built from, so choosing a different
		// script in the inspector mid-run actually takes effect.
		std::string ActiveScript;

		ManagedScriptComponent() = default;
		ManagedScriptComponent(const std::string& script) : ScriptName(script) {}

		// The handle is not copied. Two components owning one managed instance
		// would both destroy it, and EnTT copies components when a scene is
		// duplicated -- which is what play mode does on every press of Play.
		ManagedScriptComponent(const ManagedScriptComponent& other)
			: ScriptName(other.ScriptName), Fields(other.Fields) {}

		ManagedScriptComponent& operator=(const ManagedScriptComponent& other)
		{
			ScriptName = other.ScriptName;
			Fields = other.Fields;
			Handle = 0;
			ActiveScript.clear();
			return *this;
		}

	};

	struct NativeScriptComponent
	{
		std::string ScriptName;

		// A tag for the inspector, on the same terms as the managed component's.
		std::string Label;

		// Runtime only. Created on the first simulation step after Play and
		// destroyed with the component, so neither is serialized.
		ScriptableEntity* Instance = nullptr;

		// Which name the live instance was built from. Without it, changing the
		// choice in the inspector while the scene runs does nothing at all --
		// the step only ever created an instance when there was not one, so the
		// old script kept running under the new name.
		std::string ActiveScript;

		NativeScriptComponent() = default;
		NativeScriptComponent(const std::string& script) : ScriptName(script) {}

		// Copying a component must not copy the pointer, or two components
		// would own one instance and both would delete it. EnTT copies
		// components when a scene is duplicated.
		// Neither the instance nor what it was built from is copied: two
		// components owning one instance would both delete it.
		NativeScriptComponent(const NativeScriptComponent& other)
			: ScriptName(other.ScriptName), Label(other.Label), Fields(other.Fields) {}

		NativeScriptComponent& operator=(const NativeScriptComponent& other)
		{
			ScriptName = other.ScriptName;
			Label = other.Label;
			Fields = other.Fields;
			Instance = nullptr;
			ActiveScript.clear();
			return *this;
		}

		// Applied to the instance after it is constructed and before OnCreate,
		// so a script that reads its own configuration in OnCreate sees what the
		// inspector set rather than the constructor's default.
		ScriptFieldOverrides Fields;

		// Forwarded so a caller says `script.Set("Speed", "2")` rather than
		// reaching through to the storage. Both script components expose the
		// same three, because from the inspector's point of view they are the
		// same component in two languages.
		const std::string* Find(const std::string& name) const { return Fields.Find(name); }
		void Set(const std::string& name, const std::string& value) { Fields.Set(name, value); }
		void Clear(const std::string& name) { Fields.Clear(name); }
	};

}