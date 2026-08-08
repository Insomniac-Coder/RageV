#include <rvpch.h>
#include "ScenePicking.h"
#include "Scene.h"
#include "Components.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Physics/ColliderShapes.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace RageV
{
	namespace
	{
		// Möller-Trumbore. Returns the distance along the ray, or false.
		//
		// Double-sided on purpose: an editor has no business refusing to select
		// something because you are looking at the back of it, and imported
		// meshes routinely have inconsistent winding.
		bool RayIntersectsTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b,
								   const glm::vec3& c, float& distance)
		{
			constexpr float kEpsilon = 1e-7f;

			const glm::vec3 ab = b - a;
			const glm::vec3 ac = c - a;
			const glm::vec3 p = glm::cross(ray.Direction, ac);
			const float determinant = glm::dot(ab, p);

			// Near zero means the ray is parallel to the triangle's plane. Not
			// culled by sign, which is what would make this single-sided.
			if (std::fabs(determinant) < kEpsilon)
				return false;

			const float inverse = 1.0f / determinant;
			const glm::vec3 t = ray.Origin - a;

			const float u = glm::dot(t, p) * inverse;
			if (u < 0.0f || u > 1.0f)
				return false;

			const glm::vec3 q = glm::cross(t, ab);
			const float v = glm::dot(ray.Direction, q) * inverse;
			if (v < 0.0f || u + v > 1.0f)
				return false;

			distance = glm::dot(ac, q) * inverse;
			return distance > kEpsilon;   // ahead of the ray, not behind it
		}
	}

	bool RayIntersectsBox(const Ray& ray, const glm::vec3& min, const glm::vec3& max,
						  float& distance)
	{
		// The slab method. Division by a zero component gives an infinity,
		// which compares correctly here -- a ray parallel to a slab is either
		// inside it for all t or outside it for all t, and the infinities say
		// exactly that. Guarding against it with a branch per axis is the
		// version that gets the parallel case wrong.
		//
		// Named entry/exit rather than near/far: both of those are macros in
		// the Windows headers.
		float entry = -std::numeric_limits<float>::infinity();
		float exitAt = std::numeric_limits<float>::infinity();

		for (int axis = 0; axis < 3; axis++)
		{
			const float inverse = 1.0f / ray.Direction[axis];
			float t0 = (min[axis] - ray.Origin[axis]) * inverse;
			float t1 = (max[axis] - ray.Origin[axis]) * inverse;

			if (t0 > t1)
				std::swap(t0, t1);

			entry = glm::max(entry, t0);
			exitAt = glm::min(exitAt, t1);

			if (entry > exitAt)
				return false;
		}

		if (exitAt < 0.0f)
			return false;   // entirely behind the ray

		// Inside the box counts as a hit at zero, which is what makes clicking
		// something the camera is inside of select it rather than nothing.
		distance = entry < 0.0f ? 0.0f : entry;
		return true;
	}

	Ray ScreenPointToRay(const Camera& camera, const glm::mat4& cameraTransform,
						 const glm::vec2& ndc)
	{
		const glm::mat4 view = glm::inverse(cameraTransform);
		const glm::mat4 inverseViewProjection = glm::inverse(camera.GetProjection() * view);

		// Two points on the ray rather than an origin and a direction: the
		// origin of a perspective ray is the eye but the origin of an
		// orthographic one is on the near plane, and unprojecting both ends
		// gets that right without the camera having to say which it is.
		//
		// z = 0 is the near plane and z = 1 the far one: the project is built
		// with GLM_FORCE_DEPTH_ZERO_TO_ONE to match Vulkan's clip range.
		glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);

		if (std::fabs(nearPoint.w) < 1e-9f || std::fabs(farPoint.w) < 1e-9f)
			return {};

		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		Ray ray;
		ray.Origin = glm::vec3(nearPoint);

		const glm::vec3 along = glm::vec3(farPoint) - glm::vec3(nearPoint);
		if (glm::dot(along, along) < 1e-12f)
			return {};

		ray.Direction = glm::normalize(along);
		return ray;
	}

	PickResult PickEntity(Scene& scene, const Ray& ray)
	{
		// The same world matrices everything else uses, so what is picked is
		// what is drawn.
		scene.UpdateWorldTransforms();

		PickResult best;
		float bestDistance = std::numeric_limits<float>::max();

		auto consider = [&](Entity entity, float distance)
		{
			if (distance >= bestDistance)
				return;

			bestDistance = distance;
			best.Entity = entity;
			best.Distance = distance;
			best.Point = ray.At(distance);
		};

		// --- meshes, triangle-exact -------------------------------------------
		auto meshes = scene.GetRegistry().view<MeshComponent, TransformComponent>();
		for (auto handle : meshes)
		{
			auto [mesh, transform] = meshes.get<MeshComponent, TransformComponent>(handle);

			RHI::Ref<Mesh> resolved = AssetManager::GetMesh(mesh.Mesh);
			if (!resolved || resolved->GetPositions().empty())
				continue;

			// The ray is moved into the mesh's space rather than the mesh into
			// the world: one matrix inverse against thousands of vertex
			// transforms.
			const glm::mat4 inverseWorld = glm::inverse(transform.World);
			Ray local;
			local.Origin = glm::vec3(inverseWorld * glm::vec4(ray.Origin, 1.0f));
			local.Direction = glm::vec3(inverseWorld * glm::vec4(ray.Direction, 0.0f));

			// Not normalised: leaving the scale in means a distance in local
			// space is still a distance in world units, so results from
			// differently scaled objects remain comparable.
			const float lengthScale = glm::length(local.Direction);
			if (lengthScale < 1e-9f)
				continue;
			local.Direction /= lengthScale;

			const AABB& bounds = resolved->GetBounds();
			float boxDistance = 0.0f;
			if (!RayIntersectsBox(local, bounds.Min, bounds.Max, boxDistance))
				continue;

			// The box was only a filter; the answer comes from the triangles.
			const auto& positions = resolved->GetPositions();
			const auto& indices = resolved->GetIndices();

			float nearest = std::numeric_limits<float>::max();
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				float distance = 0.0f;
				if (RayIntersectsTriangle(local, positions[indices[i]], positions[indices[i + 1]],
										  positions[indices[i + 2]], distance))
				{
					nearest = glm::min(nearest, distance);
				}
			}

			if (nearest < std::numeric_limits<float>::max())
				consider(Entity{ handle, &scene }, nearest / lengthScale);
		}

		// --- colliders with no mesh -------------------------------------------
		// A trigger volume is invisible by construction, so without this the
		// only way to select one is to find it in the hierarchy.
		auto colliders = scene.GetRegistry().view<ColliderComponent, TransformComponent>();
		for (auto handle : colliders)
		{
			if (scene.GetRegistry().all_of<MeshComponent>(handle))
				continue;   // already tested, and its geometry is the better answer

			auto [collider, transform] = colliders.get<ColliderComponent, TransformComponent>(handle);

			glm::vec3 position, worldScale, skew;
			glm::quat rotation;
			glm::vec4 perspective;
			if (!glm::decompose(transform.World, worldScale, rotation, position, skew, perspective))
				continue;

			const ScaledCollider sized = ScaleCollider(collider, worldScale);

			// Every shape as its own bounding box. A sphere collider picked by
			// its box is imprecise, but a collider is a handle for selecting
			// rather than something being aimed at.
			glm::vec3 extents = sized.HalfExtents;
			if (collider.Shape == ColliderShape::Sphere)
				extents = glm::vec3(sized.Radius);
			else if (collider.Shape == ColliderShape::Capsule)
				extents = { sized.Radius, sized.HalfHeight + sized.Radius, sized.Radius };

			const glm::mat4 shapeTransform =
				glm::translate(glm::mat4(1.0f), position) *
				glm::toMat4(rotation) *
				glm::translate(glm::mat4(1.0f), sized.Offset);

			const glm::mat4 inverseShape = glm::inverse(shapeTransform);
			Ray local;
			local.Origin = glm::vec3(inverseShape * glm::vec4(ray.Origin, 1.0f));
			local.Direction = glm::vec3(inverseShape * glm::vec4(ray.Direction, 0.0f));

			const float lengthScale = glm::length(local.Direction);
			if (lengthScale < 1e-9f)
				continue;
			local.Direction /= lengthScale;

			float distance = 0.0f;
			if (RayIntersectsBox(local, -extents, extents, distance))
				consider(Entity{ handle, &scene }, distance / lengthScale);
		}

		return best;
	}
}
