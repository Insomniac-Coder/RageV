// The standalone runtime: the engine without the editor.
//
// Everything a shipped game runs through. If a project cannot be opened or has
// no start scene, this says so and exits rather than presenting an empty
// window, because an empty window is indistinguishable from a broken game.

#include "RageV.h"
#include "RageV/Core/Entrypoint.h"
#include "RageV/Project/Project.h"
#include "RuntimeLayer.h"

namespace
{
	// The project is opened here rather than left to Application, because the
	// window is created in Application's constructor and its title is a base
	// initialiser -- evaluated before that constructor's body has run.
	// Application opens one only if none is already active, so this is not a
	// second load.
	std::string ResolveTitle()
	{
		if (!RageV::Project::GetActive())
			RageV::Project::OpenConfigured();

		return RageV::Project::GetActive() ? RageV::Project::Config().Name
										   : std::string("RageV");
	}
}

class RageVRuntime : public RageV::Application
{
public:
	RageVRuntime()
		: Application(ResolveTitle())
	{
		auto* layer = new RuntimeLayer();
		PushLayer(layer);

		// PushLayer runs OnAttach, so by here the layer knows whether it found
		// anything to run.
		if (!layer->IsReady())
			Close();
	}
};

RageV::Application* RageV::CreateApplication()
{
	return new RageVRuntime();
}
