#include <rvpch.h>
#include "Interaction.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Managed/Interop.h"

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
		auto buttons = scene.GetRegistry().GetView<UIButtonComponent>();
		for (auto handle : buttons)
		{
			UIButtonComponent& button = buttons.Get<UIButtonComponent>(handle);
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

		auto buttons = scene.GetRegistry().GetView<UIButtonComponent>();
		for (auto handle : buttons)
		{
			UIButtonComponent& button = buttons.Get<UIButtonComponent>(handle);
			button.Hovered = false;
			button.Pressed = false;
			button.Clicked = false;
		}
	}

	void EndFixedStep(Scene& scene)
	{
		auto buttons = scene.GetRegistry().GetView<UIButtonComponent>();
		for (auto handle : buttons)
			buttons.Get<UIButtonComponent>(handle).Clicked = false;
	}

	std::string BindingProblem::Describe() const
	{
		if (TargetMissing)
		{
			return "UI button '" + Button + "': its OnClick target entity " + Target +
				   " is not in the scene, so '" + Method + "' can never be called. "
				   "It was probably deleted -- drag a replacement in, or clear the slot.";
		}

		return "UI button '" + Button + "': nothing on '" + Target + "' answers to '" +
			   Method + "'. A C++ script must register the method with .Method<>(); a C# "
			   "one needs it public, with no arguments, returning void.";
	}

	std::vector<BindingProblem> ValidateBindings(Scene& scene)
	{
		std::vector<BindingProblem> problems;

		auto view = scene.GetRegistry().GetView<UIButtonComponent>();
		for (auto handle : view)
		{
			const UIButtonComponent& button = view.Get<UIButtonComponent>(handle);

			// No method named is not a broken binding -- it is a button read by
			// polling, and most of them are. Reporting these would put a line
			// in the log for every ordinary button in the project and teach
			// everybody to ignore the check.
			if (button.OnClickMethod.empty())
				continue;

			Entity self{ handle, &scene };

			// The same empty-means-this-entity rule the dispatch uses. If these
			// two ever disagree the check is worse than useless, because it
			// would pass a binding that fails and fail one that works.
			Entity target = button.OnClickTarget.IsValid()
				? scene.GetEntityByUUID(button.OnClickTarget.Value)
				: self;

			if (!target)
			{
				problems.push_back({ self.GetName(), button.OnClickMethod,
									 std::to_string((uint64_t)button.OnClickTarget), true });
				continue;
			}

			// **An answer we cannot look up is not a "no".**
			//
			// A C# handler is only visible while .NET is up. If it is not --
			// the host failed to start, the assembly was never built -- then
			// every managed binding is *unknown*, and reporting them as broken
			// would refuse to package a game whose buttons all work. A check
			// that cries wolf is a check somebody turns off.
			//
			// The C++ registry needs nothing to be running, so a native script
			// is always answerable.
			const bool managedUnknown =
				target.HasComponent<ManagedScriptComponent>() &&
				!target.GetComponent<ManagedScriptComponent>().ScriptName.empty() &&
				!Managed::Interop::IsReady();

			if (managedUnknown)
				continue;

			if (!scene.CanInvokeScriptMethod(target, button.OnClickMethod))
			{
				problems.push_back({ self.GetName(), button.OnClickMethod,
									 target.GetName(), false });
			}
		}

		return problems;
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
