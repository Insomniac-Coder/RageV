#pragma once
#include "RageV/Core/Layer.h"
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
		//virtual void OnImGuiRender() override;
		void Begin();
		void End();
	};

}