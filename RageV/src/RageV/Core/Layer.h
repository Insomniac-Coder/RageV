#pragma once

#include "Core.h"
#include "RageV/Events/Event.h"
#include "Timestep.h"

namespace RageV
{
	class RV_API Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		// Once per rendered frame, with the real elapsed time. Anything
		// presentational belongs here: camera navigation, UI state, animation
		// that nothing else depends on.
		virtual void OnUpdate(Timestep ts) {}

		// Zero or more times per frame, always with the same dt. Simulation
		// belongs here -- integrator behaviour depends on dt, so a variable one
		// means a scene that settles at 300 fps explodes at 40.
		virtual void OnFixedUpdate(Timestep dt) {}
		virtual void OnEvent(Event& e) {}
		virtual void OnImGuiRender() {}

		inline const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};
}