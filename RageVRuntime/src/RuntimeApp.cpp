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
		PushLayer(new RuntimeLayer());

		// Whether there is anything to run is no longer knowable here: the
		// scene is opened on the boot worker, after this constructor returns.
		// RuntimeLayer::OnLoaded closes the application when it found nothing,
		// which is the first moment the answer exists.
	}

	// The game's name, not the window title's -- they are the same string
	// here, but the loading screen is asking what is loading rather than what
	// the title bar says, and a packaged game may want those to differ.
	std::string GetLoadingTitle() const override
	{
		return RageV::Project::GetActive() ? RageV::Project::Config().Name
										   : std::string("RageV");
	}
};

RageV::Application* RageV::CreateApplication()
{
	return new RageVRuntime();
}
