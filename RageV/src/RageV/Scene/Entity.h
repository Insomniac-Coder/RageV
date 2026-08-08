#pragma once
#include "Scene.h"
#include "RageV/Core/Log.h"
#include "RageV/Core/UUID.h"

namespace RageV
{
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity entity, Scene* scene) : m_Entity(entity), m_Scene(scene) {}
		Entity(const Entity& entity) = default;

		// Every member is const, because an Entity is a handle rather than the
		// thing it refers to -- the same reasoning that makes `T* const` allow
		// writes through it. Without this, anything holding an Entity by const
		// reference could not use it at all, which is what a script receiving a
		// Collision does.
		//
		// Defined in Entity.cpp: Components.h includes this header (through
		// ScriptableEntity.h), so IDComponent cannot be visible here.
		UUID GetUUID() const;
		const std::string& GetName() const;
		Scene& GetScene() const { return *m_Scene; }

		template<typename T, typename... Args>
		T& AddComponent(Args&& ... args) const
		{
			if (HasComponent<T>())
				RV_CORE_WARN("Entity already has this component!");
			return m_Scene->m_Registry.emplace<T>(m_Entity, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent() const
		{
			if (!HasComponent<T>())
				RV_CORE_WARN("Entity does not have the requested component");
			return m_Scene->m_Registry.get<T>(m_Entity);
		}

		template<typename T>
		bool HasComponent() const
		{
			if (m_Scene->m_Registry.try_get<T>(m_Entity) != nullptr)
				return true;
			return false;
		}

		template<typename T>
		void RemoveComponent() const
		{
			if (!HasComponent<T>())
				RV_CORE_WARN("Entity does not have the component that you want to remove!");
			m_Scene->m_Registry.remove<T>(m_Entity);
		}


		operator unsigned int() const { return (unsigned int)m_Entity; }
		operator bool() const { return m_Entity != entt::null; }
		operator entt::entity() const { return m_Entity; }
		bool operator ==(const Entity& other) const { return m_Entity == other.m_Entity && m_Scene == other.m_Scene; }
		bool operator !=(const Entity& other) const { return !(*this == other); }
	private:
		entt::entity m_Entity{entt::null};
		Scene* m_Scene = nullptr;
	};
}