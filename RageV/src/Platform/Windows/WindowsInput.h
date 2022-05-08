#pragma once
#include "RageV/Input.h"

namespace RageV
{

	class WindowsInput : public Input {
		virtual bool IsKeyPressedImpl(int keycode) override;
		virtual bool IsMouseButtonPressedImpl(int button) override;
		virtual float GetMouseXImpl() override;
		virtual float GetMouseYImpl() override;
	};

}