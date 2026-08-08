#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"

namespace RageV
{
	Scene::Scene()
	{
	}

	Scene::~Scene()
	{

	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto item : view)
		{
			auto& camera = view.get<CameraComponent>(item);

			return { item,  this };
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

	void Scene::DeleteEntity(Entity& entity)
	{
		m_Registry.destroy(entity);
	}

	void Scene::OnUpdate(Timestep ts)
	{
		m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nscript)
			{
				if (!nscript.m_ScriptableEntity)
				{
					nscript.OnInstantiateFunction();
					nscript.m_ScriptableEntity->m_Entity = Entity{ entity, this };
					nscript.OnCreateFunction(nscript.m_ScriptableEntity);
				}

				nscript.OnUpdateFunction(nscript.m_ScriptableEntity, ts);
			}
		);

		auto view = m_Registry.view<CameraComponent, TransformComponent>();
		TransformComponent cameraTransform;
		Cameranew* mainCamera = nullptr;

		for (auto& item : view)
		{
			auto [camera, transform] = view.get<CameraComponent, TransformComponent>(item);
			if (camera.isPrimary)
			{
				mainCamera = &camera.Camera;
				cameraTransform = transform;
				break;
			}
		}

		// A scene with no primary camera used to dereference an uninitialised
		// pointer below. There is nothing to render from, so stop here.
		if (!mainCamera)
			return;

		auto group2 = m_Registry.view<TransformComponent, LightComponent>();
		LightList lights;

		for (auto& item : group2)
		{
			auto [transform, light] = group2.get<TransformComponent, LightComponent>(item);
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
			Renderer3D::BeginScene(*mainCamera, cameraTransform.GetTransform(), lights);

			auto& device = Renderer::GetDevice();
			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.get<TransformComponent, MeshComponent>(item);
				Renderer3D::DrawMesh(Mesh::GetPrimitive(device, mesh.Primitive),
									 transform.GetTransform(), mesh.Material);
			}

			Renderer3D::EndScene();
		}

		Renderer2D::BeginScene(*mainCamera, cameraTransform.GetTransform(), lights);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Renderer2D::DrawQuad(transform.GetTransform(), color.Color);
		}

		Renderer2D::EndScene();

	}

}