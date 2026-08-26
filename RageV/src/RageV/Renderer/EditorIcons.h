#pragma once
#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

#include <vector>

namespace RageV
{
	class Scene;

	// Which mark an entity gets. The order is the atlas order and the
	// priority order both: an entity carrying two of these -- a camera with
	// an audio listener on it, say -- gets one icon, the first that matches,
	// so two marks never stack on one point and picking is never ambiguous
	// about which entity a click meant.
	enum class EditorIconKind : uint32_t
	{
		Light = 0,
		Camera,
		Probe,
		Audio,
		// An irradiance volume: a box, drawn as one. It has no mesh and no
		// collider, so without a mark the only way to select one is the
		// hierarchy -- the same argument the other four are here for.
		Volume,
		Count
	};

	// One mark to draw, and the same one to pick.
	struct EditorIcon
	{
		UUID Entity;
		EditorIconKind Kind = EditorIconKind::Light;
		Vec3 Position{ 0.0f };
	};

	// How the viewport's gizmo icons look. Nothing here is serialized: like
	// the grid, an icon is a property of somebody's viewport rather than of
	// the world.
	struct EditorIconSettings
	{
		// White art plus a tint, so a theme change needs no new PNG.
		Vec4 Tint{ 0.90f, 0.91f, 0.95f, 0.95f };
		Vec4 SelectedTint{ 1.0f, 0.62f, 0.24f, 1.0f };

		// Filled in by the editor from the hierarchy panel, so the mark that
		// is highlighted is the entity the inspector is showing.
		UUID Selected = UUID::Invalid();

		// Multiplies the angular size below. A viewport that wants smaller
		// marks turns this down rather than editing a constant.
		float Scale = 1.0f;
	};

	// The editor's viewport gizmo icons: a mark for every entity that has no
	// geometry of its own.
	//
	// **The point is picking, not decoration.** A light, a camera, a probe
	// and an audio source have no mesh and no collider, so `PickEntity` could
	// not see them and the hierarchy panel was the only way to select one.
	// The mark is what makes the entity visible; the mark's radius is what
	// makes it clickable. Design: ENGINE-NOTES 7j.
	class EditorIcons
	{
	public:
		// The half-angle a mark subtends, in radians-ish -- radius per unit of
		// distance. Constant *angular* size rather than constant pixels: it
		// needs nothing but the camera's position, which is the whole reason
		// the picker can agree with the drawer without being handed a camera.
		// The cost is that it tracks zoom the way a real object does instead
		// of pinning itself to a pixel count, which is the honest trade.
		static constexpr float kAngularRadius = 0.022f;

		// The world radius of the mark at `position`, seen from
		// `cameraPosition`.
		//
		// **One formula, two callers.** The drawer sizes the quad with it and
		// the picker sizes its sphere with it, so what is clicked is exactly
		// what is drawn. Deriving that number twice is the shape of bug this
		// codebase keeps finding: two derivations agree until one of them
		// grows a condition the other does not.
		static float Radius(const Vec3& position, const Vec3& cameraPosition,
							float scale = 1.0f);

		// Every mark in the scene. Used by the drawer *and* the picker, for
		// the same reason Radius is: one walk, one answer, no drift.
		//
		// Reads TransformComponent::World, so the caller must have derived
		// the transforms -- both callers already do.
		static std::vector<EditorIcon> Collect(Scene& scene);

		// Draws them, billboarded, into the world layer. A no-op when the
		// scene has none, so a scene with no lights pays nothing.
		static void Draw(Scene& scene, const Mat4& viewProjection,
						 const Mat4& cameraTransform, const EditorIconSettings& settings);
	};
}
