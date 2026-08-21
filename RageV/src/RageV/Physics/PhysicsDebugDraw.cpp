#include <rvpch.h>
#include "PhysicsDebugDraw.h"
#include "ColliderShapes.h"
#include "PhysicsWorld.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Math/Math.h"

namespace RageV::Physics
{
	namespace
	{
		Vec4 ColorFor(const RigidBodyComponent* body, bool trigger,
						   const DebugStyle& style)
		{
			// A trigger's colour wins over its body type: what matters about it
			// is that it does not stop anything, and that is true whether it is
			// static or kinematic.
			if (trigger)
				return style.Trigger;

			if (!body)
				return style.Static;   // a collider with no body is static geometry

			switch (body->Type)
			{
				case BodyType::Kinematic: return style.Kinematic;
				case BodyType::Dynamic:   return style.Dynamic;
				case BodyType::Static:
				default:                  return style.Static;
			}
		}
	}

	void DrawColliders(Scene& scene, UUID selected, const DebugStyle& style)
	{
		// The overlay reads the same world matrices everything else does, so
		// what it draws is where the object is being drawn -- not where it was
		// at the last simulation step.
		scene.UpdateWorldTransforms();

		// Null outside play mode, which is fine: sleep only exists while
		// something is simulating.
		World* physics = scene.GetPhysics();

		auto view = scene.GetRegistry().GetView<ColliderComponent, TransformComponent>();
		for (auto handle : view)
		{
			Entity entity{ handle, &scene };
			auto [collider, transform] = view.Get<ColliderComponent, TransformComponent>(handle);

			Vec3 position, worldScale;
			Quat rotation;
			if (!Math::Decompose(transform.World, position, rotation, worldScale))
				continue;

			const ScaledCollider sized = ScaleCollider(collider, worldScale);

			const auto* body = scene.GetRegistry().TryGet<RigidBodyComponent>(handle);
			const bool isSelected = selected.IsValid() && entity.GetUUID() == selected;

			Vec4 color = isSelected ? style.Selected : ColorFor(body, collider.IsTrigger, style);

			// Dimmed rather than hidden, and only for bodies that could be
			// awake: a static body is never active, so dimming every one of
			// them would say "asleep" about the half of the scene where the
			// word does not apply.
			if (style.DimSleeping && physics && body && body->Type != BodyType::Static &&
				!physics->IsBodyAwake(entity.GetUUID()))
			{
				color = Vec4(Vec3(color) * style.SleepingDim, color.a);
			}

			// Rotation and the offset, but not scale: the size is already baked
			// into `sized`, and carrying it in the matrix as well would apply
			// it twice.
			const Mat4 shapeTransform =
				Math::Translate(Mat4(1.0f), position) *
				Math::ToMat4(rotation) *
				Math::Translate(Mat4(1.0f), sized.Offset);

			// Drawn a hair larger than the collider itself, so the line sits
			// just outside the surface it describes rather than half-buried in
			// the mesh that usually shares that surface exactly -- where depth
			// testing eats one side of it and the rest z-fights.
			constexpr float kSkin = 0.01f;

			switch (collider.Shape)
			{
				case ColliderShape::Sphere:
					DebugRenderer::DrawSphere(shapeTransform, sized.Radius + kSkin, color);
					break;

				case ColliderShape::Capsule:
					DebugRenderer::DrawCapsule(shapeTransform, sized.Radius + kSkin,
											   sized.HalfHeight + kSkin, color);
					break;

				case ColliderShape::Box:
				default:
					DebugRenderer::DrawBox(shapeTransform,
										   sized.HalfExtents + Vec3(kSkin), color);
					break;
			}
		}
	}
}
