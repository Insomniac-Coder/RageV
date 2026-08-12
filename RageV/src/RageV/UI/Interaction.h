#pragma once

#include "RageV/UI/Canvas.h"

#include <cstdint>
#include <vector>

namespace RageV
{
	class Scene;
	struct UIButtonComponent;
}

namespace RageV::UI
{
	// The pointer, and what it lands on.
	//
	// **The one rule that matters here is that the UI has to say when it took a
	// click.** A UI that silently swallows one is a game that fires a weapon
	// when somebody presses the pause button, and the symptom shows up in the
	// gameplay code rather than anywhere near the menu that caused it. So
	// hit-testing is not a private detail of the button: WantsPointer is part of
	// the surface, and game code is expected to ask.
	//
	// The action map stays exactly what it is. This sits in front of it.

	// Where the pointer is, in the **UI layer's pixel space** -- the same space
	// DrawScene was handed, origin top left, y down.
	//
	// It is the caller's job to get it into that space, because only the caller
	// knows what "the screen" is: the runtime's layer is the window, and the
	// editor's is an image inside a docked panel that has been scrolled,
	// resized and possibly scaled. Passing a window coordinate into the editor
	// would put every button a title bar's height away from where it is drawn.
	struct PointerInput
	{
		float X = 0.0f;
		float Y = 0.0f;

		// The primary button's state this frame, not an edge. The edges are
		// worked out here, because a press and a release have to be paired
		// against the same widget to be a click at all.
		bool Down = false;

		// Whether the pointer is over the layer. False parks it: nothing
		// hovers, nothing can be pressed, and a press held from inside is
		// cancelled rather than completing somewhere it cannot be seen.
		bool Inside = true;
	};

	// Index into `elements` of the topmost one covering the point, or -1.
	//
	// `elements` arrives in **draw order**, which is what ResolveScene returns,
	// so this walks it backwards: the last thing drawn is the first thing hit,
	// which is the only reading of "topmost" that agrees with what is on screen.
	//
	// Pixels, matching PointerInput. Each element converts by its own Scale,
	// so two canvases at different scales hit-test correctly in one pass.
	int32_t HitTest(const std::vector<ResolvedElement>& elements, float pixelX, float pixelY);

	// One frame of pointer input against a scene's canvases.
	//
	// Call it once a frame, before the scripts that will read the result. It
	// resolves the layout, hit-tests, and writes Hovered/Pressed/Clicked onto
	// every UIButtonComponent in the scene -- including clearing them on the
	// buttons the pointer left, which is why it walks all of them rather than
	// only the one under the cursor.
	void UpdatePointer(Scene& scene, float screenWidth, float screenHeight,
					   const PointerInput& pointer);

	// Did the UI take the pointer this frame?
	//
	// True while it is over anything that blocks -- a button, or a rect that
	// asked to -- and true throughout a press that started on a button, even if
	// the pointer has since been dragged off it. That second half is what stops
	// a click being delivered to *both* the menu and the world when the hand
	// moves between press and release.
	bool WantsPointer();

	// The entity under the pointer that blocked it, or 0. Mostly for tests and
	// for a debug overlay; game code wants WantsPointer.
	uint64_t HoveredEntity();

	// Forgets the pointer and clears every button in the scene. Play mode
	// stopping, a scene closing: state describing a press on an entity that no
	// longer exists is state that will be wrong the moment it is read.
	void ResetPointer(Scene& scene);
	void ResetPointer();

	// Consumes the click edges, exactly as InputMap::EndFixedStep consumes the
	// action edges and for the same reason: a press must be seen by one
	// simulation step, not by every step a slow frame happens to run. A frame
	// with no steps carries the click forward rather than losing it.
	void EndFixedStep(Scene& scene);

	// Which tint a button is currently drawn with. Pure, and separate from the
	// drawing so a test can assert the state machine without a GPU.
	Vec4 ButtonTint(const UIButtonComponent& button);
}
