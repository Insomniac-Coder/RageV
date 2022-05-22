#pragma once
#include "Scene.h"
#include "RageV/Core/Log.h"

namespace RageV
{
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity entity, Scene* scene) : m_Entity(entity), m_Scene(scene) {}
		Entity(const Entity& entity) = default;

		template<typename T, typename... Args>
		T& AddComponent(Args&& ... args)
		{
			if (HasComponent<T>())
				RV_CORE_WARN("Entity already has this component!");
			return m_Scene->m_Registry.emplace<T>(m_Entity, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent()
		{
			if (!HasComponent<T>())
				RV_CORE_WARN("Entity does not have the requested component");
			return m_Scene->m_Registry.get<T>(m_Entity);
		}

		template<typename T>
		bool HasComponent()
		{
			if (m_Scene->m_Registry.try_get<T>(m_Entity) != nullptr)
				return true;
			return false;
		}

		template<typename T>
		void RemoveComponent()
		{
			if (!HasComponent<T>())
				RV_CORE_WARN("Entity does not have the component that you want to remove!");
			m_Scene->m_Registry.remove<T>(m_Entity);
		}


		operator unsigned int() { return (unsigned int)m_Entity; }
		operator bool() { return m_Entity != entt::null; }
		operator entt::entity() { return m_Entity; }
		bool operator ==(const Entity& other) { return m_Entity == other.m_Entity && m_Scene == other.m_Scene; }
		bool operator !=(const Entity& other) { return !(*this == other); }
	private:
		entt::entity m_Entity{entt::null};
		Scene* m_Scene = nullptr;
	};
}