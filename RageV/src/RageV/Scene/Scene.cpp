#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/EditorCamera.h"

namespace RageV
{
	Scene::Scene()
	{
		m_Registry.on_destroy<NativeScriptComponent>().connect<&Scene::OnNativeScriptDestroyed>(this);
	}

	Scene::~Scene()
	{
		// Explicit, in the destructor body, rather than left to the registry's
		// own destructor: this way the on_destroy handler runs while every
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

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<TransformComponent>();
		TagComponent& tag = entity.AddComponent<TagComponent>();

		tag.Name = name.empty() ? "Entity" : name;
		return entity;
	}

	void Scene::DeleteEntity(Entity entity)
	{
		if (!entity)
			return;
		m_Registry.destroy(entity);
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
		auto view = m_Registry.view<CameraComponent, TransformComponent>();

		for (auto& item : view)
		{
			auto [camera, transform] = view.get<CameraComponent, TransformComponent>(item);
			if (!camera.isPrimary)
				continue;

			OnRender(camera.Camera, transform.GetTransform());
			return;
		}

		// No primary camera: there is nothing to render from. This used to
		// dereference an uninitialised pointer.
	}

	void Scene::OnRenderEditor(const EditorCamera& camera)
	{
		OnRender(camera, camera.GetTransform());
	}

	void Scene::OnRender(const Camera& camera, const glm::mat4& cameraTransform)
	{
		auto lightView = m_Registry.view<TransformComponent, LightComponent>();
		LightList lights;

		for (auto& item : lightView)
		{
			auto [transform, light] = lightView.get<TransformComponent, LightComponent>(item);
			const glm::mat4 worldTransform = transform.GetTransform();

			LightRenderData data;
			data.Position = glm::vec3(worldTransform[3]);
			// A light's forward axis is -Z, matching the camera convention.
			data.Direction = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
			data.Color = light.Light.GetLightColor();
			data.Intensity = light.Light.GetIntensity();
			data.Range = light.Light.GetRange();
			data.InnerCone = light.Light.GetInnerCone();
			data.OuterCone = light.Light.GetOuterCone();
			data.Type = light.Light.GetLightType();

			lights.push_back(data);
		}

		// Meshes first: they are opaque and depth-tested, so drawing them ahead
		// of the alpha-blended quads means the quads blend against a complete
		// depth buffer rather than over each other arbitrarily.
		auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
		if (meshView.begin() != meshView.end() && Renderer::HasDevice())
		{
			Renderer3D::BeginScene(camera, cameraTransform, lights);

			auto& device = Renderer::GetDevice();
			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.get<TransformComponent, MeshComponent>(item);
				Renderer3D::DrawMesh(Mesh::GetPrimitive(device, mesh.Primitive),
									 transform.GetTransform(), mesh.Material);
			}

			Renderer3D::EndScene();
		}

		Renderer2D::BeginScene(camera, cameraTransform, lights);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Renderer2D::DrawQuad(transform.GetTransform(), color.Color);
		}

		Renderer2D::EndScene();
	}
}
