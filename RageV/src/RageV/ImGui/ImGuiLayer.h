#pragma once
#include "RageV/Core/Layer.h"
#include "RageV/Renderer/RHI/RHITypes.h"
#include "RageV/Events/KeyEvent.h"
#include "RageV/Events/MouseEvent.h"
#include "RageV/Events/ApplicationEvent.h"

namespace RageV
{
	// What ImGui says it wants from the input this frame. Named rather than
	// read straight off ImGuiIO so the rule below can be stated, and tested,
	// without an ImGui context or a window.
	struct UiCapture
	{
		// The pointer is over an ImGui window or dragging one of its widgets.
		bool WantsMouse = false;
		// ImGui would like key events. **This is true for keyboard *navigation*
		// as well as for typing**, so with ImGuiConfigFlags_NavEnableKeyboard on
		// it is true whenever any panel has focus -- which is most of the time
		// in a docked editor.
		bool WantsKeyboard = false;
		// A text field is being typed into. The narrow one.
		bool WantsTextInput = false;
	};

	// Whether the UI layer consumes an event before the application sees it.
	//
	// **The keyboard half asks about text input, not about capture.** It used
	// to ask `WantsKeyboard`, and that made every editor shortcut dead the
	// moment a panel had focus: change something in the Inspector, press
	// Ctrl+S, and the event was marked handled by the overlay above the editor
	// layer, so the editor never saw it and nothing was saved and nothing was
	// said. Navigation focus is not a claim on Ctrl+S; typing into a field is.
	//
	// ENGINE-NOTES 7ak.
	RV_API bool UiConsumesEvent(const UiCapture& capture, bool isMouse, bool isKeyboard);

	class RV_API ImGuiLayer : public Layer
	{
	private:
		float m_Time = 0.0f;


	public:
		ImGuiLayer();
		~ImGuiLayer();
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override;
		void SetEventBlocker(bool block) { m_BlockEvents = block; }

		// Whether the UI pass clears the swapchain before drawing.
		//
		// True suits the editor, where the UI is the only thing on the swapchain
		// and the scene lives in an offscreen target it samples. A game draws its
		// scene to the swapchain directly and the UI on top, so clearing here
		// would erase the frame it is meant to annotate.
		void SetClearsBackbuffer(bool clear) { m_ClearsBackbuffer = clear; }
		//virtual void OnImGuiRender() override;
		void Begin();
		void End();

		// What the UI was scaled by for this display. Panels that size things
		// in pixels need it; everything going through ImGui's style already
		// has it applied.
		float GetDpiScale() const { return m_DpiScale; }
	private:
		bool m_BlockEvents = false;
		bool m_ClearsBackbuffer = true;
		// Captured at OnAttach; the platform backend cannot change afterwards.
		RHI::Backend m_Backend = RHI::Backend::OpenGL;
		// The monitor's content scale at startup, clamped to 3x.
		float m_DpiScale = 1.0f;
	};

}