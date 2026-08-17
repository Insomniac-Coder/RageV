#include "TerrainBrushTool.h"

#include "RageV/Asset/AssetManager.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/ScenePicking.h"

namespace RageV::Tools
{
	namespace
	{
		constexpr int kRingSegments = 48;
		// A hair above the surface, so the ring is not z-fought by the ground
		// it sits on.
		constexpr float kRingLift = 0.05f;
	}

	bool TerrainBrushTool::Aim(const std::shared_ptr<Scene>& scene, Entity selected, const Ray& worldRay)
	{
		m_HasHit = false;

		if (!scene || !selected || !selected.HasComponent<TerrainComponent>())
			return false;

		auto& component = selected.GetComponent<TerrainComponent>();
		const RHI::Ref<Terrain>& terrain = Terrain::Resolve(component);
		if (!terrain)
			return false;

		m_Target = selected;
		m_Terrain = terrain;
		m_Asset = component.Terrain;
		m_World = scene->GetWorldTransform(selected);
		m_Inverse = Math::Inverse(m_World);
		m_Size = terrain->GetDimensions().Size;
		m_Height = terrain->GetDimensions().Height;

		// The ray into terrain space: a rotated or scaled terrain sculpts under
		// the cursor like an unrotated one. The local direction is
		// renormalised, so Raycast's distance is along it and the hit point
		// is origin plus that distance along it, both in local units.
		const Vec3 localOrigin = Vec3(m_Inverse * Vec4(worldRay.Origin, 1.0f));
		const Vec3 localDirection = Math::Normalize(Mat3(m_Inverse) * worldRay.Direction);
		float t = 0.0f;
		if (!terrain->Raycast(localOrigin, localDirection, t))
			return false;

		m_HasHit = true;
		m_HitLocal = localOrigin + localDirection * t;
		return true;
	}

	void TerrainBrushTool::Update(const std::shared_ptr<Scene>& scene, Entity selected,
								  const ViewportInput& input, CommandStack& commands)
	{
		// The selection moved off the terrain mid-stroke: the stroke ends as
		// it stands and is pushed, so nothing sculpted is lost to undo.
		if (m_Stroking && (!selected || selected != m_Target))
			End(scene, commands);

		// The target is the selection, whether or not the cursor is over it:
		// a stroke may run off the terrain's edge and back, and WantsMouse has
		// to answer for the viewport as a whole while the tool is on.
		if (!m_Stroking)
		{
			m_Target = Entity{};
			m_Terrain = nullptr;
			if (selected && selected.HasComponent<TerrainComponent>())
			{
				const RHI::Ref<Terrain>& terrain = Terrain::Resolve(selected.GetComponent<TerrainComponent>());
				if (terrain)
				{
					m_Target = selected;
					m_Terrain = terrain;
				}
			}
		}

		// Alt is the camera's: Alt+left orbits, and the brush must neither
		// aim nor stroke under it. A stroke already running continues to its
		// release, because letting go of Alt mid-drag would otherwise start a
		// second one.
		if (input.Hovered && m_Target && (!input.Alt || m_Stroking))
		{
			m_ScriptedRing = false;
			Aim(scene, m_Target, input.WorldRay);
		}
		else if (!m_ScriptedRing)
			m_HasHit = false;

		if (!Enabled || !m_Target)
		{
			if (m_Stroking)
				End(scene, commands);
			return;
		}

		Brush.Invert = input.Shift;

		if (m_Stroking)
		{
			if (!input.LeftDown)
				End(scene, commands);
			else if (m_HasHit)
				Step(input.Dt);
			return;
		}

		if (input.LeftPressed && input.Hovered && m_HasHit && !input.Alt)
		{
			Begin(scene);
			// A click without a drag is one step: the brush touches the ground
			// where it was pressed.
			if (m_Stroking)
				Step(Math::Max(input.Dt, TerrainBrush::kStepSeconds));
		}
	}

	void TerrainBrushTool::Begin(const std::shared_ptr<Scene>& scene)
	{
		(void)scene;
		TerrainData* data = Assets::Manager::EditTerrain(m_Asset);
		if (!data || !m_Terrain)
			return;

		m_Recorder.Begin(*data, Brush.EditsHeights());

		// Flatten aims at the height under the cursor when the button went
		// down, in the data's own [0, 1].
		const float cell = m_Size / (float)data->QuadCount();
		m_FlattenTarget = data->Sample((m_HitLocal.x + 0.5f * m_Size) / cell,
									   (m_HitLocal.z + 0.5f * m_Size) / cell);
		m_Stroking = true;
	}

	void TerrainBrushTool::Step(float dt)
	{
		TerrainData* data = Assets::Manager::EditTerrain(m_Asset);
		if (!data || !m_Terrain || dt <= 0.0f)
			return;

		// What this step will write, recorded before it is written.
		const TerrainRect footprint = Brush.Footprint(*data, m_Size, m_HitLocal.x, m_HitLocal.z);
		if (footprint.Empty())
			return;
		m_Recorder.Cover(*data, footprint);

		const TerrainRect touched = Brush.Apply(*data, m_Size, m_Height, m_HitLocal.x, m_HitLocal.z,
												m_FlattenTarget, dt);
		if (!touched.Empty())
			m_Terrain->ApplyRegion(*data, touched);
	}

	void TerrainBrushTool::End(const std::shared_ptr<Scene>& scene, CommandStack& commands)
	{
		m_Stroking = false;
		if (!m_Recorder.Active())
			return;
		m_Recorder.End();

		const TerrainRect rect = m_Recorder.Rect();
		TerrainData* data = Assets::Manager::EditTerrain(m_Asset);
		if (rect.Empty() || !data)
			return;

		// The whole stroke as one command, already applied: the after is
		// what the grid holds now, the before is what the recorder kept.
		const std::string name = std::string("Terrain ") + TerrainBrush::ModeName(Brush.Mode);
		if (m_Recorder.RecordsHeights())
		{
			std::vector<uint16_t> after;
			CopyHeightsOut(*data, rect, after);
			commands.PushApplied(std::make_unique<TerrainStrokeCommand>(
				scene, m_Asset, rect, name, m_Recorder.BeforeHeights(), std::move(after)));
		}
		else
		{
			std::vector<uint8_t> after;
			CopyWeightsOut(*data, rect, after);
			commands.PushApplied(std::make_unique<TerrainStrokeCommand>(
				scene, m_Asset, rect, name, m_Recorder.BeforeWeights(), std::move(after)));
		}

		// Every level of every touched chunk, now that the hand is off.
		if (m_Terrain)
			m_Terrain->RebuildStale(true);
	}

	void TerrainBrushTool::Cancel()
	{
		Enabled = false;
		m_Stroking = false;
		m_Recorder.End();
		if (m_Terrain)
			m_Terrain->RebuildStale(true);
	}

	bool TerrainBrushTool::ScriptStroke(const std::shared_ptr<Scene>& scene, Entity entity,
										float localX, float localZ, float seconds, CommandStack& commands)
	{
		if (!scene || !entity || !entity.HasComponent<TerrainComponent>())
			return false;
		auto& component = entity.GetComponent<TerrainComponent>();
		const RHI::Ref<Terrain>& terrain = Terrain::Resolve(component);
		if (!terrain)
			return false;

		m_Target = entity;
		m_Terrain = terrain;
		m_Asset = component.Terrain;
		m_World = scene->GetWorldTransform(entity);
		m_Inverse = Math::Inverse(m_World);
		m_Size = terrain->GetDimensions().Size;
		m_Height = terrain->GetDimensions().Height;
		m_HitLocal = Vec3(localX, terrain->HeightAt(localX, localZ), localZ);
		m_HasHit = true;
		// The ring stays where the stroke was until a mouse enters the
		// viewport, so a frame taken after --brush shows where it went.
		m_ScriptedRing = true;
		Enabled = true;

		Begin(scene);
		if (!m_Stroking)
			return false;
		const int steps = Math::Max((int)Math::Round(seconds / TerrainBrush::kStepSeconds), 1);
		for (int i = 0; i < steps; ++i)
			Step(TerrainBrush::kStepSeconds);
		End(scene, commands);
		return true;
	}

	void TerrainBrushTool::DrawOverlay() const
	{
		if (!Enabled || !m_HasHit || !m_Terrain)
			return;

		auto ring = [&](float radius, const Vec4& colour)
		{
			if (radius <= 0.0f)
				return;
			Vec3 previous;
			for (int i = 0; i <= kRingSegments; ++i)
			{
				const float angle = (float)i / (float)kRingSegments * 2.0f * Math::Pi;
				const float x = m_HitLocal.x + radius * Math::Cos(angle);
				const float z = m_HitLocal.z + radius * Math::Sin(angle);
				const Vec3 local(x, m_Terrain->HeightAt(x, z) + kRingLift, z);
				const Vec3 world = Vec3(m_World * Vec4(local, 1.0f));
				if (i > 0)
					DebugRenderer::DrawLine(previous, world, colour);
				previous = world;
			}
		};

		// The rim, and the hard core inside it where the weight is one.
		ring(Brush.Radius, Vec4(1.0f, 1.0f, 1.0f, 0.95f));
		ring(Brush.Radius * Math::Clamp(Brush.Hardness, 0.0f, 1.0f), Vec4(1.0f, 1.0f, 1.0f, 0.45f));
	}
}
