#pragma once
#include <string>
#include <vector>
#include "RageV/Core/UUID.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsTypes.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "RageV/Renderer/Camera.h"
#include "RageV/Animation/Skeleton.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Renderer/ReflectionProbe.h"
#include <glm/gtx/quaternion.hpp>

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
		glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };

		// Recomputed unconditionally by Scene::UpdateWorldTransforms in one
		// top-down pass per frame.
		//
		// Deliberately not a dirty-flag cache. A flag has to be set at every
		// write site -- the inspector, the gizmo, scripts, the serializer, and
		// everything added later -- and a single missed one leaves an object
		// silently rendering in the wrong place. Recomputing is O(n) at scene
		// scale and cannot be got wrong.
		glm::mat4 World{ 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& position) { Position = position; }

		glm::mat4 GetLocalTransform() const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
			return	glm::translate(glm::mat4(1.0f), Position) *
					rotation *
					glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	struct ColorComponent
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		ColorComponent() = default;
		ColorComponent(const ColorComponent&) = default;
		ColorComponent(const glm::vec4& color) { Color = color; }
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
		std::vector<glm::mat4> Skinning;

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
		glm::vec3 HalfExtents{ 0.5f };

		// Sphere and capsule.
		float Radius = 0.5f;
		// Capsule: the cylindrical section between the two caps, so the total
		// height is this plus two radii.
		float Height = 1.0f;

		// Moves the shape relative to the entity, for a collider that should
		// not sit on the origin -- feet at the bottom of a character, say.
		glm::vec3 Offset{ 0.0f };

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
	struct NativeScriptComponent
	{
		std::string ScriptName;

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
			: ScriptName(other.ScriptName) {}

		NativeScriptComponent& operator=(const NativeScriptComponent& other)
		{
			ScriptName = other.ScriptName;
			return *this;
		}
	};

}