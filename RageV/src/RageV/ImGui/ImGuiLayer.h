#pragma once
#include "RageV/Core/Layer.h"
#include "RageV/Renderer/RHI/RHITypes.h"
#include "RageV/Events/KeyEvent.h"
#include "RageV/Events/MouseEvent.h"
#include "RageV/Events/ApplicationEvent.h"

namespace RageV
{

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
	private:
		bool m_BlockEvents = false;
		bool m_ClearsBackbuffer = true;
		// Captured at OnAttach; the platform backend cannot change afterwards.
		RHI::Backend m_Backend = RHI::Backend::OpenGL;
	};

}