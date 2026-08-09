#pragma once
#include "EnTT/entt.hpp"
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Renderer/RHI/RHIResources.h"
#include "RageV/Math/Math.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace RageV
{
	class Entity;
	class Camera;
	class EditorCamera;

	class Scene
	{
	public:
		Scene();
		~Scene();

		void OnViewportResize(float width, float height);

		Entity CreateEntity(const std::string& name = std::string());
		// Preserves an existing identity. The deserializer needs this: creating
		// entities with fresh IDs would break every reference in the file.
		Entity CreateEntityWithUUID(UUID id, const std::string& name = std::string());

		// By value: an Entity is a handle pair, and taking it by reference meant
		// callers could not pass a temporary. Children are destroyed with it.
		void DeleteEntity(Entity entity);

		Entity GetEntityByUUID(UUID id);

		// First match, or an invalid entity. Names are not unique -- they are a
		// label, not an identity -- so FindEntitiesByName exists for the cases
		// where that matters.
		Entity FindEntityByName(const std::string& name);
		std::vector<Entity> FindEntitiesByName(const std::string& name);

		// Queued rather than immediate. A script destroying an entity while the
		// script pass is walking them would invalidate that iteration, and a
		// script destroying itself would delete the object it is executing in.
		void DestroyDeferred(Entity entity);
		void FlushDestroyQueue();
		bool HasEntity(UUID id) const { return m_EntityMap.find(id) != m_EntityMap.end(); }

		// --- hierarchy ------------------------------------------------------
		// Passing an invalid parent unparents. The child's world transform is
		// preserved across the move, which is what makes reparenting in the
		// hierarchy panel feel non-destructive.
		//
		// Returns false and changes nothing if the move would form a cycle.
		// This is the only place a cycle can be created, so rejecting it here
		// is what lets the transform pass recurse without a depth guard.
		bool SetParent(Entity child, Entity parent);
		bool IsDescendantOf(Entity entity, Entity possibleAncestor);

		Entity GetParent(Entity entity);
		const std::vector<UUID>& GetChildren(Entity entity);

		// Identity when the entity has no parent.
		Mat4 GetParentWorldTransform(Entity entity);
		Mat4 GetWorldTransform(Entity entity);

		// One top-down pass writing TransformComponent::World. Called by the
		// render entry points; call it directly after mutating transforms if a
		// world value is needed before the next frame.
		void UpdateWorldTransforms();

		// Advances every animator and rebuilds its pose.
		//
		// Public because both update paths call it and neither is the other's
		// business. On the frame rather than the fixed step: an animation is
		// presentation, like the audio positions and the transform blend.
		void UpdateAnimators(Timestep ts);

		// --- frame ----------------------------------------------------------
		// Creates the physics world and every body in it. Called on Play.
		void OnRuntimeStart();
		// Tears it down. Called on Stop, before the scene is restored -- every
		// body refers to entities that are about to be replaced.
		void OnRuntimeStop();

		// Null outside play mode.
		// The managed half of the script pass, and its teardown.
		void StepManagedScripts(Timestep dt);
		void ReleaseManagedScripts();

		Physics::World* GetPhysics() { return m_Physics.get(); }

		// Per rendered frame while playing. Presentational work only, plus
		// pulling simulated transforms across at the frame's blend factor.
		void OnUpdateRuntime(Timestep ts);

		// Per fixed simulation step while playing. Scripts and, later, physics.
		// Nothing here may depend on the frame rate.
		void OnFixedUpdateRuntime(Timestep dt);

		// Per frame while editing. Deliberately empty of simulation: scripts
		// used to run in the editor, which meant a script could modify a scene
		// nobody had pressed Play on -- and those edits were then saved.
		void OnUpdateEditor(Timestep ts);

		// Draws through the primary camera entity. Draws nothing without one.
		//
		// The aspect ratio is per pass, not per scene. A camera carries one
		// projection but may be drawn into two panels of different shapes -- the
		// game view and a preview in the scene view -- and a single stored
		// aspect can only be right for one of them. It is also why a camera's
		// aspect is not serialized: it belongs to the surface being drawn into,
		// not to the scene.
		void OnRenderRuntime(float aspectRatio = 0.0f);
		// Draws through the viewport's own camera, which needs no entity.
		void OnRenderEditor(const EditorCamera& camera);

		// Convolves the scene's environment map into roughness levels, once.
		//
		// Like the shadow and probe passes, it opens render passes of its own
		// and so belongs before the frame graph. Unlike them it does nothing
		// after the first frame with a given environment.
		void PrepareEnvironment();

		// Renders the shadow cascades for the first directional light that casts.
		//
		// Takes the camera it is about to be rendered with, because cascades are
		// fitted to a frustum: shadows for a camera other than the one looking
		// at them would be the wrong size in the wrong place. Like the probe
		// capture, it opens render passes and so must run before the graph.
		void RenderShadows(const Camera& camera, const Mat4& cameraTransform);

		// Re-renders whatever the scene's reflection probes can see.
		//
		// Called by the application between BeginFrame and the frame graph,
		// because a capture opens render passes of its own and nothing may do
		// that inside another one. It is not folded into OnRender for the same
		// reason: OnRender runs *inside* the scene pass.
		void CaptureReflectionProbes();

		Entity GetPrimaryCameraEntity();

		// Lowest ListenerRank wins. Falls back to the primary camera when the
		// scene has no AudioListenerComponent at all, which is the case almost
		// every time and is what makes audio work without being set up.
		Entity GetPrimaryListenerEntity();

		SceneEnvironment& GetEnvironment() { return m_Environment; }
		const SceneEnvironment& GetEnvironment() const { return m_Environment; }

		// For tools and tests that need to iterate arbitrary component sets.
		// The editor panels reach it through friendship instead; this exists so
		// that code outside the engine does not have to.
		entt::registry& GetRegistry() { return m_Registry; }

	private:
		void OnRender(const Camera& camera, const Mat4& cameraTransform);

		// The cube surfaces reflect: the nearest complete probe whose influence
		// reaches the viewer, and the sky otherwise.
		RHI::Ref<RHI::RHITexture> ResolveEnvironment(const Mat4& cameraTransform,
													 const RHI::Ref<RHI::RHITexture>& sky);
		void PropagateTransform(entt::entity handle, const Mat4& parentWorld);
		void UnlinkFromParent(Entity entity);

		// Wired to EnTT's on_destroy signals rather than called from
		// DeleteEntity. Destroying an entity, removing a component and clearing
		// the registry all have to run these; a signal cannot be forgotten at
		// one of those call sites.
		void OnNativeScriptDestroyed(entt::registry& registry, entt::entity handle);
		void OnAudioSourceDestroyed(entt::registry& registry, entt::entity handle);
		void OnIDDestroyed(entt::registry& registry, entt::entity handle);

		// Hands what the simulation reported to the scripts on both entities.
		void DispatchContactEvents();

		// Starts every PlayOnAwake source, stops everything the scene started,
		// and pushes the listener and each source's world position to the mixer.
		void StartAudioSources();
		void StopAudioSources();
		void UpdateAudio();
		// One side of one event. `flip` is true for the entity the stored
		// normal points away from.
		void DeliverContact(const ContactEvent& event, UUID to, UUID other, bool flip);

	private:
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		// True while probe faces are being rendered. A probe capture draws the
		// scene, and the scene reflects a probe -- without this the second
		// probe in a scene would capture the first one's reflection of it, and
		// a probe would capture itself.
		bool m_CapturingProbes = false;

		// Said once per scene, not once per frame. A light that asks to cast
		// and silently does not is the kind of thing someone spends an evening
		// on.
		bool m_ShadowBudgetWarned = false;

		SceneEnvironment m_Environment;
		std::vector<UUID> m_PendingDestroy;
		std::unique_ptr<Physics::World> m_Physics;

		// Kept between steps rather than allocated each one: TakeContactEvents
		// swaps, so the two buffers trade places and neither reallocates once
		// they have grown to the scene's busiest step.
		std::vector<ContactEvent> m_ContactEvents;

		friend class Entity;
		friend class SceneHierarchyPanel;
		friend class SceneSerializer;
	};
}
