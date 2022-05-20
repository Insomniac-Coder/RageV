#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
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

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<TransformComponent>();
		TagComponent tag = entity.AddComponent<TagComponent>();

		tag.Name = name.empty() ? "GameObject" : name;
		return entity;
	}

	void Scene::OnUpdate(Timestep ts)
	{
		auto view = m_Registry.view<CameraComponent, TransformComponent>();
		TransformComponent cameraTransform;
		Cameranew* mainCamera;

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

		Renderer2D::BeginScene(*mainCamera, cameraTransform);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Renderer2D::DrawQuad(transform, color.Color);
		}

		Renderer2D::EndScene();

	}

}