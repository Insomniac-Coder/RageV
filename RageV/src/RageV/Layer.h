#pragma once

#include "Core.h"
#include "Events/Event.h"
#include "Core/Timestep.h"

namespace RageV
{
	class RV_API Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnEvent(Event& e) {}
		virtual void OnImGuiRender() {}

		inline const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};
}