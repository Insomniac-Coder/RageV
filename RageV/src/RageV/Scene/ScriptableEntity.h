#pragma once
#include "RageV/Core/Timestep.h"
#include "Entity.h"

namespace RageV
{
	class ScriptableEntity {
	public:
		virtual void OnCreate() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnDestroy() {}
		template <typename T>
		T& GetComponent()
		{
			return m_Entity.GetComponent<T>();
		}
	private:
		Entity m_Entity;
		friend class Scene;
	};
}