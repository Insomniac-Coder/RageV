#include <rvpch.h>
#include "ScenePicking.h"
#include "Scene.h"
#include "Components.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Renderer/EditorIcons.h"
#include "RageV/Physics/ColliderShapes.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	namespace
	{
		// Möller-Trumbore. Returns the distance along the ray, or false.
		//
		// Double-sided on purpose: an editor has no business refusing to select
		// something because you are looking at the back of it, and imported
		// meshes routinely have inconsistent winding.
		bool RayIntersectsTriangle(const Ray& ray, const Vec3& a, const Vec3& b,
								   const Vec3& c, float& distance)
		{
			constexpr float kEpsilon = 1e-7f;

			const Vec3 ab = b - a;
			const Vec3 ac = c - a;
			const Vec3 p = Math::Cross(ray.Direction, ac);
			const float determinant = Math::Dot(ab, p);

			// Near zero means the ray is parallel to the triangle's plane. Not
			// culled by sign, which is what would make this single-sided.
			if (Math::Abs(determinant) < kEpsilon)
				return false;

			const float inverse = 1.0f / determinant;
			const Vec3 t = ray.Origin - a;

			const float u = Math::Dot(t, p) * inverse;
			if (u < 0.0f || u > 1.0f)
				return false;

			const Vec3 q = Math::Cross(t, ab);
			const float v = Math::Dot(ray.Direction, q) * inverse;
			if (v < 0.0f || u + v > 1.0f)
				return false;

			distance = Math::Dot(ac, q) * inverse;
			return distance > kEpsilon;   // ahead of the ray, not behind it
		}

		// A camera-facing disc of world radius `radius` at `center`.
		//
		// The test is the perpendicular distance from the centre to the ray,
		// which for a disc that always faces the viewer is not an
		// approximation of the quad on screen -- it *is* it. A ray-vs-quad
		// test would need the billboard's axes; this needs nothing but the
		// ray, which is what keeps the picker free of the camera's basis.
		//
		// The distance reported is the closest approach, so the mark sits at
		// the depth of the point it marks. Reporting the sphere's near face
		// instead would put it a radius closer and let an icon win against
		// geometry genuinely in front of it.
		bool RayIntersectsBillboard(const Ray& ray, const Vec3& center, float radius,
									float& distance)
		{
			const float length = Math::Length(ray.Direction);
			if (length < 1e-9f)
				return false;

			// Normalised here rather than assumed: every distance this
			// function reports is compared against the mesh path's world-unit
			// distances, and a ray whose direction is not unit length would
			// make those two scales silently different.
			const Vec3 direction = ray.Direction / length;
			const Vec3 toCenter = center - ray.Origin;

			const float along = Math::Dot(toCenter, direction);
			if (along <= 0.0f)
				return false;   // behind the camera

			const Vec3 closest = ray.Origin + direction * along;
			if (Math::Length(closest - center) > radius)
				return false;

			distance = along;
			return true;
		}
	}

	bool RayIntersectsBox(const Ray& ray, const Vec3& min, const Vec3& max,
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

			entry = Math::Max(entry, t0);
			exitAt = Math::Min(exitAt, t1);

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

	Ray ScreenPointToRay(const Camera& camera, const Mat4& cameraTransform,
						 const Vec2& ndc)
	{
		const Mat4 view = Math::Inverse(cameraTransform);
		const Mat4 inverseViewProjection = Math::Inverse(camera.GetProjection() * view);

		// Two points on the ray rather than an origin and a direction: the
		// origin of a perspective ray is the eye but the origin of an
		// orthographic one is on the near plane, and unprojecting both ends
		// gets that right without the camera having to say which it is.
		//
		// z = 0 is the near plane and z = 1 the far one: the project is built
		// with GLM_FORCE_DEPTH_ZERO_TO_ONE to match Vulkan's clip range.
		Vec4 nearPoint = inverseViewProjection * Vec4(ndc.x, ndc.y, 0.0f, 1.0f);
		Vec4 farPoint = inverseViewProjection * Vec4(ndc.x, ndc.y, 1.0f, 1.0f);

		if (Math::Abs(nearPoint.w) < 1e-9f || Math::Abs(farPoint.w) < 1e-9f)
			return {};

		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		Ray ray;
		ray.Origin = Vec3(nearPoint);

		const Vec3 along = Vec3(farPoint) - Vec3(nearPoint);
		if (Math::Dot(along, along) < 1e-12f)
			return {};

		ray.Direction = Math::Normalize(along);
		return ray;
	}

	PickResult PickEntity(Scene& scene, const Ray& ray, const PickOptions& options)
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
		auto meshes = scene.GetRegistry().GetView<MeshComponent, TransformComponent>();
		for (auto handle : meshes)
		{
			auto [mesh, transform] = meshes.Get<MeshComponent, TransformComponent>(handle);

			RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
			if (!resolved || resolved->GetPositions().empty())
				continue;

			// The ray is moved into the mesh's space rather than the mesh into
			// the world: one matrix inverse against thousands of vertex
			// transforms.
			const Mat4 inverseWorld = Math::Inverse(transform.World);
			Ray local;
			local.Origin = Vec3(inverseWorld * Vec4(ray.Origin, 1.0f));
			local.Direction = Vec3(inverseWorld * Vec4(ray.Direction, 0.0f));

			// Not normalised: leaving the scale in means a distance in local
			// space is still a distance in world units, so results from
			// differently scaled objects remain comparable.
			const float lengthScale = Math::Length(local.Direction);
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
					nearest = Math::Min(nearest, distance);
				}
			}

			if (nearest < std::numeric_limits<float>::max())
				consider(Entity{ handle, &scene }, nearest / lengthScale);
		}

		// --- terrain, triangle-exact at the finest level ----------------------
		// The same test the meshes get, over each chunk's finest mesh (7ap):
		// the box is the filter, the triangles are the answer, and a click on
		// a hill selects the terrain entity. Level 0 rather than the drawn
		// level, so what is picked is the surface and not its approximation.
		scene.ForEachTerrainChunk([&](Entity entity, TransformComponent& transform, TerrainComponent&,
									  Terrain&, Terrain::Chunk& chunk)
		{
			const RHI::Ref<Mesh>& resolved = chunk.Levels[0];
			if (!resolved || resolved->GetPositions().empty())
				return;

			const Mat4 inverseWorld = Math::Inverse(transform.World);
			Ray local;
			local.Origin = Vec3(inverseWorld * Vec4(ray.Origin, 1.0f));
			local.Direction = Vec3(inverseWorld * Vec4(ray.Direction, 0.0f));
			const float lengthScale = Math::Length(local.Direction);
			if (lengthScale < 1e-9f)
				return;
			local.Direction /= lengthScale;

			float boxDistance = 0.0f;
			if (!RayIntersectsBox(local, chunk.Bounds.Min, chunk.Bounds.Max, boxDistance))
				return;

			const auto& positions = resolved->GetPositions();
			const auto& indices = resolved->GetIndices();
			float nearest = std::numeric_limits<float>::max();
			for (size_t i = 0; i + 2 < indices.size(); i += 3)
			{
				float distance = 0.0f;
				if (RayIntersectsTriangle(local, positions[indices[i]], positions[indices[i + 1]],
										  positions[indices[i + 2]], distance))
					nearest = Math::Min(nearest, distance);
			}
			if (nearest < std::numeric_limits<float>::max())
				consider(entity, nearest / lengthScale);
		});

		// --- colliders with no mesh -------------------------------------------
		// A trigger volume is invisible by construction, so without this the
		// only way to select one is to find it in the hierarchy.
		auto colliders = scene.GetRegistry().GetView<ColliderComponent, TransformComponent>();
		for (auto handle : colliders)
		{
			if (scene.GetRegistry().AllOf<MeshComponent>(handle))
				continue;   // already tested, and its geometry is the better answer

			auto [collider, transform] = colliders.Get<ColliderComponent, TransformComponent>(handle);

			Vec3 position, worldScale;
			Quat rotation;
			if (!Math::Decompose(transform.World, position, rotation, worldScale))
				continue;

			const ScaledCollider sized = Physics::ScaleCollider(collider, worldScale);

			// Every shape as its own bounding box. A sphere collider picked by
			// its box is imprecise, but a collider is a handle for selecting
			// rather than something being aimed at.
			Vec3 extents = sized.HalfExtents;
			if (collider.Shape == ColliderShape::Sphere)
				extents = Vec3(sized.Radius);
			else if (collider.Shape == ColliderShape::Capsule)
				extents = { sized.Radius, sized.HalfHeight + sized.Radius, sized.Radius };

			const Mat4 shapeTransform =
				Math::Translate(Mat4(1.0f), position) *
				Math::ToMat4(rotation) *
				Math::Translate(Mat4(1.0f), sized.Offset);

			const Mat4 inverseShape = Math::Inverse(shapeTransform);
			Ray local;
			local.Origin = Vec3(inverseShape * Vec4(ray.Origin, 1.0f));
			local.Direction = Vec3(inverseShape * Vec4(ray.Direction, 0.0f));

			const float lengthScale = Math::Length(local.Direction);
			if (lengthScale < 1e-9f)
				continue;
			local.Direction /= lengthScale;

			float distance = 0.0f;
			if (RayIntersectsBox(local, -extents, extents, distance))
				consider(Entity{ handle, &scene }, distance / lengthScale);
		}

		// --- gizmo icons, for entities with no geometry at all ----------------
		// A light or a camera has neither mesh nor collider, so without this
		// the hierarchy panel is the only way to select one.
		if (options.IconHandles)
		{
			for (const EditorIcon& icon : EditorIcons::Collect(scene))
			{
				// The same radius the drawer uses, from the same function --
				// and it needs only the camera's position, which is where a
				// screen-point ray starts.
				const float radius =
					EditorIcons::Radius(icon.Position, ray.Origin, options.IconScale);

				float distance = 0.0f;
				if (RayIntersectsBillboard(ray, icon.Position, radius, distance))
					consider(scene.GetEntityByUUID(icon.Entity), distance);
			}
		}

		return best;
	}
}
