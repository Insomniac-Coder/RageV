#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Renderer/Renderer2D.h"

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
		std::vector <std::tuple<glm::vec3, glm::vec3, Light::LightType>> lightData;
		
		for (auto& item : group2)
		{
			auto [transform, light] = group2.get<TransformComponent, LightComponent>(item);
		
			lightData.push_back(std::make_tuple(glm::vec3(transform.GetTransform()[3]), light.Light.GetLightColor(), light.Light.GetLightType()));
		}


		Renderer2D::BeginScene(*mainCamera, cameraTransform.GetTransform(), lightData);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Renderer2D::DrawQuad(transform.GetTransform(), color.Color);
		}

		Renderer2D::EndScene();

	}

}