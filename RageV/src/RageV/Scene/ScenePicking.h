#pragma once
#include "Entity.h"
#include "RageV/Renderer/Camera.h"
#include <glm/glm.hpp>

namespace RageV
{
	class Scene;

	struct Ray
	{
		glm::vec3 Origin{ 0.0f };
		// Normalised, so a distance along it is a distance in world units.
		glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };

		glm::vec3 At(float distance) const { return Origin + Direction * distance; }
	};

	struct PickResult
	{
		Entity Entity;
		float Distance = 0.0f;
		glm::vec3 Point{ 0.0f };

		explicit operator bool() const { return (bool)Entity; }
	};

	// A ray through a point on the viewport.
	//
	// `ndc` is normalised device coordinates: -1 to 1 on both axes, y up. The
	// caller converts from pixels, because only it knows where the panel is and
	// how big it is -- and getting that conversion wrong is the usual reason
	// picking is off by a consistent offset.
	Ray ScreenPointToRay(const Camera& camera, const glm::mat4& cameraTransform,
						 const glm::vec2& ndc);

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
	PickResult PickEntity(Scene& scene, const Ray& ray);

	// Ray against an axis-aligned box in the box's own space. Exposed because
	// it is the useful half of picking for anything without geometry.
	// Returns false when the ray misses or the box is behind it.
	bool RayIntersectsBox(const Ray& ray, const glm::vec3& min, const glm::vec3& max,
						  float& distance);
}
