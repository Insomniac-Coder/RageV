#include <rvpch.h>
#include "Interaction.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"

namespace RageV::UI
{
	namespace
	{
		// The button a press started on, held until it is released.
		//
		// Capture is what makes a press cancellable. Without it, pressing on A,
		// sliding to B and releasing would click B -- which is not what any
		// desktop toolkit does, and not what the hand doing it meant.
		uint64_t s_Captured = 0;

		bool s_WasDown = false;

		// Answers to WantsPointer and HoveredEntity, from the last update.
		bool s_WantsPointer = false;
		uint64_t s_Hovered = 0;

		UIButtonComponent* LiveButton(Scene& scene, uint64_t id)
		{
			if (id == 0)
				return nullptr;

			Entity entity = scene.GetEntityByUUID(UUID(id));
			if (!entity || !entity.HasComponent<UIButtonComponent>())
				return nullptr;

			UIButtonComponent& button = entity.GetComponent<UIButtonComponent>();
			return button.Interactable ? &button : nullptr;
		}
	}

	int32_t HitTest(const std::vector<ResolvedElement>& elements, float pixelX, float pixelY)
	{
		// Backwards: the list is in draw order, so the topmost element is the
		// last one in it.
		for (size_t i = elements.size(); i-- > 0;)
		{
			const ResolvedElement& element = elements[i];
			if (!element.BlocksPointer)
				continue;

			// Into pixels by this element's own scale, matching what DrawScene
			// does with the same numbers. Hit-testing in canvas units instead
			// would be right for exactly one canvas and wrong for a second one
			// with a different scale mode.
			const float scale = element.Scale;
			const UIRect pixels{ element.Rect.X * scale, element.Rect.Y * scale,
								 element.Rect.Width * scale, element.Rect.Height * scale };

			if (pixels.Contains(pixelX, pixelY))
				return (int32_t)i;
		}

		return -1;
	}

	void UpdatePointer(Scene& scene, float screenWidth, float screenHeight,
					   const PointerInput& pointer)
	{
		// Cleared on every button, not only the one under the cursor: a button
		// the pointer has left has to stop being hovered, and nothing else in
		// the frame would tell it so.
		//
		// Clicked is deliberately not cleared here. It is an edge, and it is
		// consumed by EndFixedStep on the same terms as an action press --
		// clearing it per frame would drop the click whenever a frame ran no
		// simulation step.
		auto buttons = scene.GetRegistry().view<UIButtonComponent>();
		for (auto handle : buttons)
		{
			UIButtonComponent& button = buttons.get<UIButtonComponent>(handle);
			button.Hovered = false;
			button.Pressed = false;
		}

		const std::vector<ResolvedElement> elements =
			ResolveScene(scene, screenWidth, screenHeight);

		const int32_t hit = pointer.Inside
			? HitTest(elements, pointer.X, pointer.Y)
			: -1;

		const uint64_t hitEntity = hit >= 0 ? elements[hit].Entity : 0;
		UIButtonComponent* hovered = LiveButton(scene, hitEntity);

		// Not gated on Inside. A press held while the pointer leaves the layer
		// keeps its capture, so coming back and releasing on the same button
		// still counts -- and releasing elsewhere still cancels, because the
		// release is only a click when the pointer is over the captured button.
		const bool down = pointer.Down;

		if (down && !s_WasDown)
		{
			s_Captured = hovered ? hitEntity : 0;
		}
		else if (!down && s_WasDown)
		{
			if (s_Captured != 0 && hitEntity == s_Captured && hovered)
				hovered->Clicked = true;

			s_Captured = 0;
		}

		if (hovered)
		{
			hovered->Hovered = true;

			// Pressed means down *on this button*: held after a press that
			// started somewhere else does not light it up.
			if (down && s_Captured == hitEntity)
				hovered->Pressed = true;
		}

		s_WasDown = down;
		s_Hovered = hit >= 0 ? hitEntity : 0;

		// A capture keeps the pointer even when it has been dragged off the
		// button, which is the case that would otherwise deliver one gesture to
		// both the menu and the world.
		s_WantsPointer = hit >= 0 || s_Captured != 0;
	}

	bool WantsPointer()
	{
		return s_WantsPointer;
	}

	uint64_t HoveredEntity()
	{
		return s_Hovered;
	}

	void ResetPointer()
	{
		s_Captured = 0;
		s_WasDown = false;
		s_WantsPointer = false;
		s_Hovered = 0;
	}

	void ResetPointer(Scene& scene)
	{
		ResetPointer();

		auto buttons = scene.GetRegistry().view<UIButtonComponent>();
		for (auto handle : buttons)
		{
			UIButtonComponent& button = buttons.get<UIButtonComponent>(handle);
			button.Hovered = false;
			button.Pressed = false;
			button.Clicked = false;
		}
	}

	void EndFixedStep(Scene& scene)
	{
		auto buttons = scene.GetRegistry().view<UIButtonComponent>();
		for (auto handle : buttons)
			buttons.get<UIButtonComponent>(handle).Clicked = false;
	}

	Vec4 ButtonTint(const UIButtonComponent& button)
	{
		// A button that cannot be interacted with is drawn at rest whatever the
		// pointer is doing -- and it never becomes hovered in the first place,
		// because ResolveScene does not let it block the pointer.
		if (!button.Interactable)
			return button.NormalColor;

		if (button.Pressed)
			return button.PressedColor;
		if (button.Hovered)
			return button.HoverColor;

		return button.NormalColor;
	}
}
