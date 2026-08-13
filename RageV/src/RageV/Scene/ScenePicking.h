#pragma once
#include "Entity.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	class Scene;

	struct Ray
	{
		Vec3 Origin{ 0.0f };
		// Normalised, so a distance along it is a distance in world units.
		Vec3 Direction{ 0.0f, 0.0f, -1.0f };

		Vec3 At(float distance) const { return Origin + Direction * distance; }
	};

	struct PickResult
	{
		Entity Entity;
		float Distance = 0.0f;
		Vec3 Point{ 0.0f };

		explicit operator bool() const { return (bool)Entity; }
	};

	// A ray through a point on the viewport.
	//
	// `ndc` is normalised device coordinates: -1 to 1 on both axes, y up. The
	// caller converts from pixels, because only it knows where the panel is and
	// how big it is -- and getting that conversion wrong is the usual reason
	// picking is off by a consistent offset.
	Ray ScreenPointToRay(const Camera& camera, const Mat4& cameraTransform,
						 const Vec2& ndc);

	// What a pick is allowed to hit beyond geometry.
	struct PickOptions
	{
		// Let the viewport's gizmo icons be clicked (7.4). Off by default and
		// opt-in rather than always on, because an icon is a thing the editor
		// draws: a pick that hit one in a context where none is drawn would
		// select something the person clicking cannot see.
		bool IconHandles = false;

		// Must match the EditorIconSettings the same viewport draws with, or
		// the mark and its hit area are different sizes.
		float IconScale = 1.0f;
	};

	// The nearest entity the ray hits, or an invalid result.
	//
	// Meshes are tested against their triangles, not their bounding boxes. A
	// box is a good filter and a bad answer: a sphere's box is 90% empty at the
	// corners, and clicking that empty space to select it feels broken in a way
	// that is hard to put a finger on but obvious in use.
	//
	// Entities with a collider and no mesh -- a trigger volume, most usefully --
	// are picked by their collider instead, so a thing that is invisible is
	// still selectable.
	//
	// With `IconHandles`, entities with no geometry at all -- lights, cameras,
	// probes, audio sources -- are picked by the mark the viewport draws for
	// them. They compete with geometry by distance like everything else, so a
	// wall in front of a light wins and the icon behind it is not selectable,
	// which is what the depth-tested mark on screen already says.
	PickResult PickEntity(Scene& scene, const Ray& ray, const PickOptions& options = {});

	// Ray against an axis-aligned box in the box's own space. Exposed because
	// it is the useful half of picking for anything without geometry.
	// Returns false when the ray misses or the box is behind it.
	bool RayIntersectsBox(const Ray& ray, const Vec3& min, const Vec3& max,
						  float& distance);
}
