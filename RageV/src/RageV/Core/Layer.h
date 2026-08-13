#pragma once

#include "Core.h"
#include "Boot.h"
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

		// Cheap setup, on the main thread, before the window is shown.
		// Render targets, panel state, anything that needs the device.
		virtual void OnAttach() {}

		// The expensive half of starting up: opening the project, scanning
		// the registry, parsing a scene, cooking assets.
		//
		// **Runs on a worker thread**, while the main thread pumps the window
		// and draws the loading screen. That is the whole point -- this used
		// to happen inside OnAttach, with nothing pumping, which is why a
		// launch showed a white "Not Responding" rectangle for seconds.
		//
		// The rule that keeps it simple: **no device calls in here.** The RHI
		// is not thread-safe and a GL context belongs to one thread. Do the
		// files and the CPU work here; leave GPU resources to OnLoaded, which
		// runs on the main thread once this returns.
		//
		// Poll `progress.Cancelled()` between units of work and return early
		// when it is set, so closing the window during a long boot does not
		// mean waiting for the boot to finish.
		virtual void OnLoad(Boot::Progress& progress) {}

		// A slice of main-thread loading, called once per loading-screen frame
		// after OnLoad has finished. Return true while there is more to do.
		//
		// This is where GPU work belongs. Doing it in one lump instead would
		// leave the bar at 100%, remove the screen, and *then* freeze the
		// window for as long as the uploads take -- the original complaint,
		// relocated rather than fixed. A slice per frame keeps the pump
		// running and the bar moving through the whole of it.
		//
		// `progress` is the same one OnLoad wrote to, so a layer reports the
		// upload half on the same bar as the decode half.
		virtual bool OnLoadStep(Boot::Progress& progress) { return false; }

		// Back on the main thread, after OnLoadStep has finished and before
		// the first frame. For the cheap finishing touches -- selecting an
		// entity, placing a camera -- rather than for anything slow.
		virtual void OnLoaded() {}

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