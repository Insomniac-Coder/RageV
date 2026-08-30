#pragma once
#include "Core.h"
#include "Window.h"
#include "RageV/Events/ApplicationEvent.h"
#include "LayerStack.h"
#include "RageV/Core/Platform.h"
#include "RageV/Core/GraphicsInformation.h"
#include "RageV/ImGui/ImGuiLayer.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "FixedStep.h"

#include <filesystem>
#include <string>


namespace RageV {
	class RV_API Application
	{
	public:
		// [0, 1): how far the frame being rendered sits between the last
		// completed simulation step and the next. Anything simulated should
		// draw at lerp(previous, current, alpha) or it will stutter.
		static float GetInterpolationAlpha();
		static float GetFixedTimestep();
		// The rate actually in use: the project's, unless --fixed-hz overrode it.
		static uint32_t GetFixedHz();
		// Seconds since startup. Scripts want this without reaching for the
		// platform layer.
		static float GetElapsedTime();

		// `choosesProject` says this application can *ask* which project to
		// open when nothing named one -- which the editor can and a packaged
		// game cannot. Without it, an application with no project simply runs
		// without one, which is what every command-line tool wants.
		Application(const std::string& appname, bool choosesProject = false);
		virtual ~Application();
		void Run();
		void OnEvent(Event& e);
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
		void Close() { m_Running = false; }

		// Writes RGBA8 pixels, top row first, as a PNG. Public because the
		// screenshot flag is not the only reason to want one.
		static bool WriteScreenshot(const std::string& path, const uint8_t* rgba,
									uint32_t width, uint32_t height);
		void PushOverlay(Layer* layer);
		void PushLayer(Layer* layer);
		inline Window& GetWindow() { return *m_Window; }
		// Out of line: an inline accessor would read m_Instance directly, and a
		// static data member does not cross a DLL boundary the way a call does.
		// A game module asking for the application would otherwise link against
		// its own copy of the pointer, which is always null.
		static Application& Get();
		// Get() dereferences unconditionally, so anything that may run without
		// an application -- a headless tool, or code during construction --
		// has to ask first.
		static bool Exists();
		ImGuiLayer* GetImGuiLayer() const { return m_ImGuiLayer; }
		RHI::RHIDevice& GetDevice() { return *m_Device; }

		// What the loading screen puts above its bar. The runtime overrides
		// it with the game's name; the editor keeps the default.
		// **The project's name, because that is what is loading.** It read
		// "RageV Editor" for as long as the editor only ever opened one
		// project; now that it opens whichever you chose, the name is the one
		// thing on the screen that tells you the right one is coming up.
		// Falls back to the application's own name while there is no project
		// -- which is the startup screen and the moments either side of it.
		virtual std::string GetLoadingTitle() const;

	private:
		// Drives the layers' OnLoad on a worker while this thread pumps the
		// window and draws the loading screen, then their OnLoaded here.
		// Returns false when the window was closed during it, in which case
		// no frame should be rendered at all.
		bool RunBootPhase();

		// The startup screen, before any project exists: pump and draw until
		// the user opens one, creates one, or closes the window. Returns
		// whether there is a project to go on with.
		bool RunStartupPhase();

		// Project creation, on a worker, behind the same bar the loading
		// screen draws. Returns whether the project was created and opened.
		bool RunCreateProjectPhase(const std::filesystem::path& directory,
								   const std::string& name);

		// The half of startup that needs a project: rooting the asset registry
		// at it, and the graph generator that reads from it. Called from the
		// constructor when a project was configured, and from the startup
		// screen when one is chosen there.
		void AdoptProject();

		// One frame of loading screen: pump, render, present. Deliberately a
		// whole frame through the same path an ordinary one takes, rather
		// than a special case -- a bespoke present path during boot is a
		// second renderer to keep working on two backends.
		void DrawLoadingFrame(const Boot::Status& status);

		std::unique_ptr<Window> m_Window;
		// Declared after the window: the device is built from it and must be
		// destroyed before it.
		RHI::Scope<RHI::RHIDevice> m_Device;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minimised = false;
		// Set in the constructor when no project was configured and this
		// application is one that can ask for one.
		bool m_NeedsProject = false;
		// What the loading card says while there is no project to name itself
		// after -- which is exactly the moment one is being created.
		std::string m_LoadingTitle;
		LayerStack m_LayerStack;
		static Application* m_Instance;
		float m_LastTime = 0.0f;
		// The wall clock of the last completed frame, which is not the step
		// the simulation was advanced by whenever --frame-time is in force.
		float m_MeasuredFrameMs = 0.0f;
		std::string m_Name;

		// Frames run under --benchmark, warm-up included. Separate from the
		// screenshot counter, which only advances when a screenshot is armed.
		uint32_t m_BenchmarkFrame = 0;

		// Real time not yet spent in fixed steps, plus where the renderer sits
		// between the last completed step and the next one.
		FixedStep m_FixedStep;
		std::unique_ptr<Platform> m_Platform;
		std::function<double()> GetTime;

	};

	Application* CreateApplication();
}