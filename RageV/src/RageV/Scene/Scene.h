#pragma once
#include "EnTT/entt.hpp"
#include "RageV/Core/Timestep.h"

namespace RageV
{
	class Entity;
	class Scene
	{
		friend class Entity;
	public:
		Scene();
		~Scene();
		Entity CreateEntity(const std::string& name = std::string());
		void OnUpdate(Timestep ts);
	private:
		entt::registry m_Registry;
	};
}
