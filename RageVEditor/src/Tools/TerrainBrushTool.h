#pragma once

// The hand that holds the terrain brush (ENGINE-NOTES 7ar, 7as).
//
// The brush itself -- what a step does to the samples -- is RageV::TerrainBrush
// in the engine, pure and headless. This is everything around it that only an
// editor has: which terrain the cursor is over and where, a stroke from press
// to release recorded as one undoable command, the stroke's own context (where
// it began and how high, which way it is moving, its seed), the mask library,
// the rebuild and the overlay, and the scripted stroke `--brush` uses so a
// check can hold the mouse.
//
// Edit mode only. A terrain is an asset; the height-field body is built from
// the data at the next Play, so a sculpt made here is what the ball rolls on.

#include "RageV.h"
#include "RageV/Asset/TerrainBrush.h"
#include "RageV/Renderer/Terrain.h"
#include "RageV/Scene/SceneCommands.h"
#include "RageV/Scene/ScenePicking.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace RageV::Tools
{
	// The brush masks the editor ships (7as): every PNG in assets/brushes,
	// found through the VFS on first use and decoded on demand. Not assets --
	// a brush is a tool of the editor, not content of a project -- so a user
	// drops a PNG in the folder and has a brush, with no import step.
	class BrushLibrary
	{
	public:
		// The names, without folder or extension, sorted. Empty when the
		// folder is missing, which is survivable: the disc still works.
		const std::vector<std::string>& Names();
		// The mask by name, decoded once and kept; null when it is not there
		// or will not decode.
		std::shared_ptr<const BrushMask> Get(const std::string& name);
		// Where a name sits in Names(), or -1.
		int IndexOf(const std::string& name);

	private:
		void Scan();
		bool m_Scanned = false;
		std::vector<std::string> m_Names;
		std::unordered_map<std::string, std::shared_ptr<const BrushMask>> m_Masks;
	};

	class TerrainBrushTool
	{
	public:
		// The settings the inspector edits.
		TerrainBrush Brush;
		// The masks the shape and pattern combos offer.
		BrushLibrary Library;
		// Which library entry each of the two combos is on, by name; empty is
		// "none", and what the combo shows when the brush is on the disc or on
		// the noise. Kept beside the brush because the brush holds the decoded
		// mask, not its name.
		std::string ShapeMaskName;
		std::string PatternMaskName;
		// Whether a mode is chosen: while it is, a plain left drag on the
		// selected terrain sculpts and the click-to-select picker stands down.
		bool Enabled = false;
		// Set by the editor each frame: the scene is running, and the tool is
		// inert -- its block says so and Update is not called.
		bool Playing = false;

		// What the viewport knows this frame: the mouse ray in world space and
		// whether it is inside the image, the button and modifier state, and
		// the frame's dt.
		struct ViewportInput
		{
			Ray   WorldRay;
			bool  Hovered = false;
			bool  LeftPressed = false;
			bool  LeftDown = false;
			bool  Shift = false;
			bool  Alt = false;
			float Dt = 0.0f;
		};

		// Once per frame the viewport is drawn, with the selected entity (which
		// may not be a terrain: then nothing happens). Finds the cursor's point
		// on the terrain, starts, continues or ends a stroke, and pushes the
		// finished stroke on `commands`.
		void Update(const std::shared_ptr<Scene>& scene, Entity selected,
					const ViewportInput& input, CommandStack& commands);

		// True while a click in the viewport belongs to the brush and not to
		// picking: the tool is on and the selection is a terrain.
		bool WantsMouse() const { return Enabled && m_Target; }
		bool IsStroking() const { return m_Stroking; }
		bool HoversTerrain() const { return m_HasHit; }

		// What the brush covers, on the ground, inside a DebugRenderer scene
		// the caller has begun: the disc's rim and hard core as two rings, a
		// mask's square turned to its angle (and the stroke's direction when
		// it follows), and a ramp's centreline while one is being drawn.
		void DrawOverlay() const;

		// Sets the shape or the pattern from a library name; an empty name is
		// the disc, or no pattern. False when the name is not in the library.
		bool SetShapeMask(const std::string& name);
		bool SetPatternMask(const std::string& name);

		// One whole stroke without a mouse: `seconds` of sixtieth-of-a-second
		// steps at terrain-local (localX, localZ) on `entity`'s terrain, through
		// the same begin, step and end a drag goes through -- so a check can
		// exercise the path from kernel to command to pixels. Nothing if the
		// entity has no terrain. `toX`/`toZ`, when given, is where the stroke
		// holds after its first step -- a press at (localX, localZ), a drag to
		// there, and a hold, which is what a ramp needs to be more than a
		// point.
		bool ScriptStroke(const std::shared_ptr<Scene>& scene, Entity entity,
						  float localX, float localZ, float seconds, CommandStack& commands,
						  const float* toX = nullptr, const float* toZ = nullptr);

		// Turns the tool off and drops any stroke in progress without pushing
		// it. Escape, and the selection changing to something without a terrain.
		void Cancel();

	private:
		bool Aim(const std::shared_ptr<Scene>& scene, Entity selected, const Ray& worldRay);
		void Begin(const std::shared_ptr<Scene>& scene);
		void Step(float dt);
		void End(const std::shared_ptr<Scene>& scene, CommandStack& commands);

		// The terrain under the tool this frame, or nothing.
		Entity m_Target;
		RHI::Ref<Terrain> m_Terrain;
		AssetHandle m_Asset = AssetHandle::Invalid();
		Mat4 m_World{ 1.0f };
		Mat4 m_Inverse{ 1.0f };
		float m_Size = 0.0f;
		float m_Height = 0.0f;

		bool m_HasHit = false;
		Vec3 m_HitLocal{ 0.0f };
		// A scripted stroke leaves its ring where it went until the mouse
		// enters the viewport.
		bool m_ScriptedRing = false;

		bool m_Stroking = false;
		// The stroke's context (7as): where it began and how high, which way
		// it last moved, and the seed a step draws from. Advanced by Step, so
		// a scripted stroke is the same stroke twice.
		TerrainBrush::Stroke m_Stroke;
		// The last point a step was taken at, for the direction.
		Vec3 m_LastStep{ 0.0f };
		bool m_HasLastStep = false;
		// Where the next stroke's seed comes from: strokes differ, and a
		// scripted one is the same every run because this starts where it
		// starts.
		uint32_t m_NextSeed = 1u;
		TerrainStrokeRecorder m_Recorder;
	};
}
