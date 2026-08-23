#include <rvpch.h>
#include "InputMap.h"
#include "FrameClock.h"
#include "Input.h"
#include "KeyCodes.h"
#include "MouseButtonCodes.h"
#include "Log.h"
#include "RageV/Math/Math.h"
#include <algorithm>

namespace RageV
{
	namespace
	{
		struct KeyBinding
		{
			std::string Context;
			int Code = 0;
			bool Mouse = false;
		};

		struct AxisBinding
		{
			std::string Context;
			float Scale = 1.0f;

			// Either a key pair or a mouse axis.
			bool UseMouse = false;
			MouseAxis Mouse = MouseAxis::X;
			int PositiveKey = 0;
			int NegativeKey = 0;
		};

		struct ActionState
		{
			std::vector<KeyBinding> Bindings;
			bool Down = false;

			// Stamped rather than flagged. A frame may run zero fixed steps --
			// normal above 60 Hz -- so an edge cannot simply describe "this
			// frame"; it names the step that should see it as well, and each
			// reader asks about its own clock. Nobody clears these. See
			// Core/FrameClock.h.
			InputEdge Pressed;
			InputEdge Released;
		};

		struct AxisState
		{
			std::vector<AxisBinding> Bindings;
			float Value = 0.0f;
		};

		std::unordered_map<std::string, ActionState> s_Actions;
		std::unordered_map<std::string, AxisState> s_Axes;
		std::unordered_map<std::string, bool> s_Contexts;

		Vec2 s_LastMouse{ 0.0f };
		Vec2 s_MouseDelta{ 0.0f };
		float s_WheelDelta = 0.0f;
		bool s_HaveMouse = false;

		bool ContextEnabled(const std::string& context)
		{
			const auto it = s_Contexts.find(context);
			// Unknown contexts default to enabled: a binding added without
			// declaring its context first should work, not silently do nothing.
			return it == s_Contexts.end() || it->second;
		}

		bool BindingDown(const KeyBinding& binding)
		{
			if (!ContextEnabled(binding.Context))
				return false;

			return binding.Mouse ? Input::IsMouseButtonPressed(binding.Code)
								 : Input::IsKeyPressed(binding.Code);
		}
	}

	void InputMap::Init()
	{
		ClearBindings();
		LoadDefaults();
	}

	void InputMap::Shutdown()
	{
		ClearBindings();
	}

	void InputMap::ClearBindings()
	{
		s_Actions.clear();
		s_Axes.clear();
		s_Contexts.clear();
		s_HaveMouse = false;
	}

	void InputMap::SetContextEnabled(const std::string& context, bool enabled)
	{
		s_Contexts[context] = enabled;
	}

	bool InputMap::IsContextEnabled(const std::string& context)
	{
		return ContextEnabled(context);
	}

	void InputMap::BindKey(const std::string& context, const std::string& action, int keycode)
	{
		s_Contexts.try_emplace(context, true);
		s_Actions[action].Bindings.push_back({ context, keycode, false });
	}

	void InputMap::BindMouseButton(const std::string& context, const std::string& action, int button)
	{
		s_Contexts.try_emplace(context, true);
		s_Actions[action].Bindings.push_back({ context, button, true });
	}

	void InputMap::BindKeyAxis(const std::string& context, const std::string& axis,
							   int positiveKey, int negativeKey, float scale)
	{
		s_Contexts.try_emplace(context, true);

		AxisBinding binding;
		binding.Context = context;
		binding.Scale = scale;
		binding.PositiveKey = positiveKey;
		binding.NegativeKey = negativeKey;
		s_Axes[axis].Bindings.push_back(binding);
	}

	void InputMap::BindMouseAxis(const std::string& context, const std::string& axis,
								 MouseAxis which, float scale)
	{
		s_Contexts.try_emplace(context, true);

		AxisBinding binding;
		binding.Context = context;
		binding.Scale = scale;
		binding.UseMouse = true;
		binding.Mouse = which;
		s_Axes[axis].Bindings.push_back(binding);
	}

	void InputMap::LoadDefaults()
	{
		// One context. Menus and vehicles get their own once something needs
		// them; the mechanism is here, the content is not invented up front.
		const char* game = "Gameplay";

		BindKeyAxis(game, "MoveForward", RV_KEY_W, RV_KEY_S);
		BindKeyAxis(game, "MoveRight",   RV_KEY_D, RV_KEY_A);
		BindKeyAxis(game, "MoveUp",      RV_KEY_E, RV_KEY_Q);

		BindMouseAxis(game, "LookX", MouseAxis::X);
		BindMouseAxis(game, "LookY", MouseAxis::Y);
		BindMouseAxis(game, "Zoom",  MouseAxis::Wheel);

		BindKey(game, "Jump", RV_KEY_SPACE);
		BindKey(game, "Sprint", RV_KEY_LEFT_SHIFT);
		BindKey(game, "Interact", RV_KEY_F);
		BindMouseButton(game, "Fire", RV_MOUSE_BUTTON_LEFT);
		BindMouseButton(game, "AltFire", RV_MOUSE_BUTTON_RIGHT);

		// The convention every engine and half the games since Minecraft share,
		// which is the whole argument for it: nobody has to be told what F3
		// does. A default binding rather than something the showroom invents,
		// because a diagnostic overlay is not a feature of one demo.
		BindKey(game, "ToggleStats", RV_KEY_F3);
	}

	void InputMap::Update()
	{
		const auto [mouseX, mouseY] = Input::GetMousePosition();
		const Vec2 mouse{ mouseX, mouseY };

		// The first sample has no previous position, so the delta would be the
		// whole screen and every look-axis would snap on frame one.
		s_MouseDelta = s_HaveMouse ? mouse - s_LastMouse : Vec2(0.0f);
		s_LastMouse = mouse;
		s_HaveMouse = true;

		for (auto& [name, action] : s_Actions)
		{
			bool down = false;
			for (const KeyBinding& binding : action.Bindings)
				down = down || BindingDown(binding);

			// Stamped with the frame this is, and with the step that will run
			// next -- which is the step that should see it, whether it runs in
			// a moment or three frames from now.
			if (down && !action.Down)
				action.Pressed.Raise();
			if (!down && action.Down)
				action.Released.Raise();

			action.Down = down;
		}

		for (auto& [name, axis] : s_Axes)
		{
			float value = 0.0f;

			for (const AxisBinding& binding : axis.Bindings)
			{
				if (!ContextEnabled(binding.Context))
					continue;

				if (binding.UseMouse)
				{
					switch (binding.Mouse)
					{
						case MouseAxis::X:     value += s_MouseDelta.x * binding.Scale; break;
						case MouseAxis::Y:     value += s_MouseDelta.y * binding.Scale; break;
						case MouseAxis::Wheel: value += s_WheelDelta * binding.Scale;   break;
					}
					continue;
				}

				// Both keys down cancels rather than favouring one, which is
				// what makes strafing feel right when a player rolls a finger
				// across two keys.
				float keyValue = 0.0f;
				if (Input::IsKeyPressed(binding.PositiveKey)) keyValue += 1.0f;
				if (Input::IsKeyPressed(binding.NegativeKey)) keyValue -= 1.0f;
				value += keyValue * binding.Scale;
			}

			axis.Value = value;
		}

		// The wheel is an event, not a state: it has to be consumed or it would
		// read as scrolling forever.
		s_WheelDelta = 0.0f;
	}

	bool InputMap::IsActionDown(const std::string& action)
	{
		const auto it = s_Actions.find(action);
		return it != s_Actions.end() && it->second.Down;
	}

	bool InputMap::WasActionPressed(const std::string& action)
	{
		const auto it = s_Actions.find(action);
		return it != s_Actions.end() && it->second.Pressed.IsNow();
	}

	bool InputMap::WasActionReleased(const std::string& action)
	{
		const auto it = s_Actions.find(action);
		return it != s_Actions.end() && it->second.Released.IsNow();
	}

	float InputMap::GetAxis(const std::string& axis)
	{
		const auto it = s_Axes.find(axis);
		return it == s_Axes.end() ? 0.0f : it->second.Value;
	}

	std::vector<std::string> InputMap::GetActionNames()
	{
		std::vector<std::string> names;
		names.reserve(s_Actions.size());
		for (const auto& [name, action] : s_Actions)
			names.push_back(name);

		// Sorted, because a hash map's order is not something to show a user.
		std::sort(names.begin(), names.end());
		return names;
	}

	std::vector<std::string> InputMap::GetAxisNames()
	{
		std::vector<std::string> names;
		names.reserve(s_Axes.size());
		for (const auto& [name, axis] : s_Axes)
			names.push_back(name);

		std::sort(names.begin(), names.end());
		return names;
	}

	void InputMap::OnScroll(float delta)
	{
		// Accumulated: several scroll events can arrive between two samples.
		s_WheelDelta += delta;
	}
}
