#pragma once
#include <RageV.h>

// The game, with no editor around it.
//
// This target exists as much to prove something as to ship something: if
// anything load-bearing had leaked into the editor -- input bindings set up in
// a panel, a scene only the hierarchy knows how to start, a renderer that needs
// a viewport texture -- it would fail to link or fail to run here, loudly, in a
// file small enough to read in one sitting.
//
// It is deliberately thin. Everything it does is something a game needs and
// nothing it does is something only an editor would.
class RuntimeLayer : public RageV::Layer
{
public:
	RuntimeLayer();

	void OnAttach() override;
	void OnUpdate(RageV::Timestep ts) override;
	void OnFixedUpdate(RageV::Timestep dt) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;
	bool OnKeyPressed(RageV::KeyPressedEvent& e);

	// True when there is a scene to run. False means the runtime should say why
	// and exit rather than presenting an empty window that looks like a bug.
	bool IsReady() const { return m_Ready; }

private:
	void ResizeTarget(uint32_t width, uint32_t height);

	std::shared_ptr<RageV::Scene> m_Scene;
	RageV::RHI::Ref<RageV::RHI::RHIRenderTarget> m_Target;

	uint32_t m_Width = 0;
	uint32_t m_Height = 0;
	bool m_Ready = false;

	// A frame-time readout, toggled with F1. The one piece of developer
	// furniture that earns its place in a shipped build: "is it slow" is the
	// first question anyone asks about a game and the hardest to answer from
	// the outside.
	bool m_ShowStats = false;
	float m_FrameTimeMs = 0.0f;
	float m_FrameTimeAccum = 0.0f;
	int m_FrameTimeSamples = 0;
};
