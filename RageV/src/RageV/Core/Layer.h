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

		// Virtual, because LayerStack owns layers as Layer* and deletes them
		// through that pointer. Without it only ~Layer ran: every derived
		// member -- the editor's scene, its render targets, the runtime's
		// scene, every material in them -- was never destroyed, which is
		// undefined behaviour and leaked the whole scene on every shutdown.
		//
		// Invisible for the life of the project because nothing ever exited
		// cleanly enough to notice. The standalone runtime does, and VMA
		// asserts when allocations outlive its allocator.
		virtual ~Layer();

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