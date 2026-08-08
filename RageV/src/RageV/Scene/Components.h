#pragma once
#include <string>
#include <vector>
#include "RageV/Core/UUID.h"
#include "RageV/Physics/PhysicsTypes.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "RageV/Renderer/Camera.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
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