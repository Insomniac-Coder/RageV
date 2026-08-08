#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Asset/AssetManager.h"
#include <glm/gtx/matrix_decompose.hpp>

namespace RageV
{
	namespace
	{
		const std::vector<UUID> s_NoChildren;
	}

	Scene::Scene()
	{
		m_Registry.on_destroy<NativeScriptComponent>().connect<&Scene::OnNativeScriptDestroyed>(this);
		m_Registry.on_destroy<IDComponent>().connect<&Scene::OnIDDestroyed>(this);
	}

	Scene::~Scene()
	{
		// Explicit, in the destructor body, rather than left to the registry's
		// own destructor: this way the on_destroy handlers run while every
		// member is still alive. Without it script instances leaked -- nothing
		// in the engine ever called the destroy hooks at all.
		m_Registry.clear();
	}

	void Scene::OnNativeScriptDestroyed(entt::registry& registry, entt::entity handle)
	{
		auto& component = registry.get<NativeScriptComponent>(handle);
		if (!component.Instance)
			return;

		component.Instance->OnDestroy();
		if (component.DestroyScript)
			component.DestroyScript(&component);
	}

	void Scene::OnIDDestroyed(entt::registry& registry, entt::entity handle)
	{
		m_EntityMap.erase(registry.get<IDComponent>(handle).ID);
	}

	// -------------------------------------------------------------------------
	// Entities
	// -------------------------------------------------------------------------
	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID id, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(id);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		TagComponent& tag = entity.AddComponent<TagComponent>();

		tag.Name = name.empty() ? "Entity" : name;
		m_EntityMap[id] = entity;
		return entity;
	}

	Entity Scene::GetEntityByUUID(UUID id)
	{
		auto it = m_EntityMap.find(id);
		if (it == m_EntityMap.end())
			return {};
		return { it->second, this };
	}

	void Scene::DeleteEntity(Entity entity)
	{
		if (!entity)
			return;

		// Copied, not referenced: destroying a child mutates the parent's list
		// through UnlinkFromParent, and iterating a container being modified
		// underneath is how this becomes an intermittent crash.
		if (entity.HasComponent<RelationshipComponent>())
		{
			const std::vector<UUID> children = entity.GetComponent<RelationshipComponent>().Children;
			for (UUID childID : children)
				DeleteEntity(GetEntityByUUID(childID));
		}

		UnlinkFromParent(entity);
		m_Registry.destroy(entity);
	}

	// -------------------------------------------------------------------------
	// Hierarchy
	// -------------------------------------------------------------------------
	void Scene::UnlinkFromParent(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return;

		auto& relationship = entity.GetComponent<RelationshipComponent>();
		if (!relationship.Parent.IsValid())
			return;

		if (Entity parent = GetEntityByUUID(relationship.Parent))
		{
			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), entity.GetUUID()), siblings.end());
		}

		relationship.Parent = UUID::Invalid();
	}

	bool Scene::IsDescendantOf(Entity entity, Entity possibleAncestor)
	{
		if (!entity || !possibleAncestor)
			return false;

		Entity current = GetParent(entity);
		while (current)
		{
			if (current == possibleAncestor)
				return true;
			current = GetParent(current);
		}
		return false;
	}

	bool Scene::SetParent(Entity child, Entity parent)
	{
		if (!child || !child.HasComponent<RelationshipComponent>())
			return false;

		// Parenting an entity to itself or to one of its own descendants would
		// detach the subtree from every root and make PropagateTransform
		// recurse forever. This is the only place a cycle can form.
		if (parent && (child == parent || IsDescendantOf(parent, child)))
			return false;

		// Captured before the move so the entity does not visibly jump when its
		// new parent has a different transform.
		const glm::mat4 world = GetWorldTransform(child);

		UnlinkFromParent(child);

		if (parent && parent.HasComponent<RelationshipComponent>())
		{
			child.GetComponent<RelationshipComponent>().Parent = parent.GetUUID();
			parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());
		}

		// Re-express the same world transform relative to the new parent.
		const glm::mat4 local = glm::inverse(GetParentWorldTransform(child)) * world;

		glm::vec3 position, scale, skew;
		glm::quat rotation;
		glm::vec4 perspective;
		if (glm::decompose(local, scale, rotation, position, skew, perspective))
		{
			auto& transform = child.GetComponent<TransformComponent>();
			transform.Position = position;
			transform.Rotation = glm::eulerAngles(rotation);
			transform.Scale = scale;
		}

		UpdateWorldTransforms();
		return true;
	}

	Entity Scene::GetParent(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return {};
		return GetEntityByUUID(entity.GetComponent<RelationshipComponent>().Parent);
	}

	const std::vector<UUID>& Scene::GetChildren(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return s_NoChildren;
		return entity.GetComponent<RelationshipComponent>().Children;
	}

	glm::mat4 Scene::GetParentWorldTransform(Entity entity)
	{
		Entity parent = GetParent(entity);
		if (!parent)
			return glm::mat4(1.0f);
		return GetWorldTransform(parent);
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		if (!entity || !entity.HasComponent<TransformComponent>())
			return glm::mat4(1.0f);

		// Walks the chain rather than reading the cached World, so callers that
		// have just mutated a local transform get the current answer without
		// having to remember to run the pass first.
		const auto& transform = entity.GetComponent<TransformComponent>();
		return GetParentWorldTransform(entity) * transform.GetLocalTransform();
	}

	void Scene::PropagateTransform(entt::entity handle, const glm::mat4& parentWorld)
	{
		auto& transform = m_Registry.get<TransformComponent>(handle);
		transform.World = parentWorld * transform.GetLocalTransform();

		// Copied out before recursing: the reference above stays valid only
		// while the registry is not restructured, and the copy costs one matrix.
		const glm::mat4 world = transform.World;

		for (UUID childID : m_Registry.get<RelationshipComponent>(handle).Children)
		{
			if (Entity child = GetEntityByUUID(childID))
				PropagateTransform(child, world);
		}
	}

	void Scene::UpdateWorldTransforms()
	{
		auto view = m_Registry.view<TransformComponent, RelationshipComponent>();
		for (auto handle : view)
		{
			// Roots only; children are reached by recursion, and starting from
			// every entity would compute deep nodes once per ancestor.
			if (!view.get<RelationshipComponent>(handle).Parent.IsValid())
				PropagateTransform(handle, glm::mat4(1.0f));
		}
	}

	// -------------------------------------------------------------------------
	// Frame
	// -------------------------------------------------------------------------
	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto item : view)
		{
			// Used to return the first camera whether or not it was primary,
			// which disagreed with what the renderer picked.
			if (view.get<CameraComponent>(item).isPrimary)
				return { item, this };
		}

		return {};
	}

	void Scene::OnViewportResize(float width, float height)
	{
		auto view = m_Registry.view<CameraComponent>();

		for (auto& item : view)
		{
			CameraComponent& cam = view.get<CameraComponent>(item);

			if (!cam.fixedAspectRatio)
			{
				cam.Camera.SetViewport(width, height);
			}
		}
	}

	void Scene::OnUpdate(Timestep ts)
	{
		m_Registry.view<NativeScriptComponent>().each([&](auto handle, NativeScriptComponent& script)
			{
				if (!script.Instance)
				{
					if (!script.InstantiateScript)
						return;

					script.Instance = script.InstantiateScript();
					script.Instance->m_Entity = Entity{ handle, this };
					script.Instance->OnCreate();
				}

				script.Instance->OnUpdate(ts);
			}
		);
	}

	void Scene::OnRenderRuntime()
	{
		UpdateWorldTransforms();

		auto view = m_Registry.view<CameraComponent, TransformComponent>();

		for (auto& item : view)
		{
			auto [camera, transform] = view.get<CameraComponent, TransformComponent>(item);
			if (!camera.isPrimary)
				continue;

			// World, so a camera parented to a rig follows it.
			OnRender(camera.Camera, transform.World);
			return;
		}

		// No primary camera: there is nothing to render from. This used to
		// dereference an uninitialised pointer.
	}

	void Scene::OnRenderEditor(const EditorCamera& camera)
	{
		UpdateWorldTransforms();
		OnRender(camera, camera.GetTransform());
	}

	void Scene::OnRender(const Camera& camera, const glm::mat4& cameraTransform)
	{
		auto lightView = m_Registry.view<TransformComponent, LightComponent>();
		LightList lights;

		for (auto& item : lightView)
		{
			auto [transform, light] = lightView.get<TransformComponent, LightComponent>(item);

			LightRenderData data;
			data.Position = glm::vec3(transform.World[3]);
			// A light's forward axis is -Z, matching the camera convention.
			data.Direction = glm::normalize(glm::vec3(transform.World * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
			data.Color = light.Light.Color;
			data.Intensity = light.Light.Intensity;
			data.Range = light.Light.Range;
			data.InnerCone = light.Light.InnerCone;
			data.OuterCone = light.Light.OuterCone;
			data.Type = light.Light.Type;

			lights.push_back(data);
		}

		// Meshes first: they are opaque and depth-tested, so drawing them ahead
		// of the alpha-blended quads means the quads blend against a complete
		// depth buffer rather than over each other arbitrarily.
		auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
		if (meshView.begin() != meshView.end() && Renderer::HasDevice())
		{
			Renderer3D::BeginScene(camera, cameraTransform, lights, m_Environment);

			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.get<TransformComponent, MeshComponent>(item);

				// Null when the handle points at nothing loadable -- a deleted
				// model, or a scene opened without its assets. Skipped rather
				// than substituted, so the gap is visible.
				if (RHI::Ref<Mesh> resolved = AssetManager::GetMesh(mesh.Mesh))
					Renderer3D::DrawMesh(resolved, transform.World, mesh.Material);
			}

			Renderer3D::EndScene();
		}

		Renderer2D::BeginScene(camera, cameraTransform, lights);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Renderer2D::DrawQuad(transform.World, color.Color);
		}

		Renderer2D::EndScene();
	}
}
