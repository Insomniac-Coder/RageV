#pragma once

// The hand that holds the terrain brush (ENGINE-NOTES 7ar).
//
// The brush itself -- what a step does to the samples -- is RageV::TerrainBrush
// in the engine, pure and headless. This is everything around it that only an
// editor has: which terrain the cursor is over and where, a stroke from press
// to release recorded as one undoable command, the rebuild and the ring, and
// the scripted stroke `--brush` uses so a check can hold the mouse.
//
// Edit mode only. A terrain is an asset; the height-field body is built from
// the data at the next Play, so a sculpt made here is what the ball rolls on.

#include "RageV.h"
#include "RageV/Asset/TerrainBrush.h"
#include "RageV/Renderer/Terrain.h"
#include "RageV/Scene/SceneCommands.h"
#include "RageV/Scene/ScenePicking.h"

#include <memory>

namespace RageV::Tools
{
	class TerrainBrushTool
	{
	public:
		// The settings the inspector edits.
		TerrainBrush Brush;
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

		// The ring at the cursor's point on the ground, and its hard core.
		// Inside a DebugRenderer scene the caller has begun.
		void DrawOverlay() const;

		// One whole stroke without a mouse: `seconds` of sixtieth-of-a-second
		// steps at terrain-local (localX, localZ) on `entity`'s terrain, through
		// the same begin, step and end a drag goes through -- so a check can
		// exercise the path from kernel to command to pixels. Nothing if the
		// entity has no terrain.
		bool ScriptStroke(const std::shared_ptr<Scene>& scene, Entity entity,
						  float localX, float localZ, float seconds, CommandStack& commands);

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
		float m_FlattenTarget = 0.0f;
		TerrainStrokeRecorder m_Recorder;
	};
}
