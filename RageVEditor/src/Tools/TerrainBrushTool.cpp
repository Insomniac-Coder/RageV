#include "TerrainBrushTool.h"

#include "RageV/Asset/AssetManager.h"
#include "RageV/IO/VFS.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/ScenePicking.h"

#include <algorithm>

namespace RageV::Tools
{
	namespace
	{
		constexpr int kRingSegments = 48;
		// A hair above the surface, so the ring is not z-fought by the ground
		// it sits on.
		constexpr float kRingLift = 0.05f;
		// Where the editor keeps its brush masks (7as).
		constexpr const char* kBrushFolder = "assets/brushes";
		// Metres the cursor must travel before the stroke's direction is
		// re-read: a jitter of a millimetre is not a change of heading, and a
		// mask that follows the stroke would spin under one.
		constexpr float kDirectionEpsilon = 0.05f;
	}

	// --- the mask library (7as) ------------------------------------------------

	void BrushLibrary::Scan()
	{
		m_Scanned = true;
		for (const std::string& entry : VFS::Enumerate(kBrushFolder))
		{
			const size_t dot = entry.find_last_of('.');
			if (dot == std::string::npos)
				continue;
			std::string extension = entry.substr(dot + 1);
			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
			if (extension != "png" && extension != "tga" && extension != "bmp" && extension != "jpg")
				continue;
			// A folder inside brushes/ is fine; the name keeps its path so two
			// masks of the same name in different folders stay apart.
			m_Names.push_back(entry.substr(0, dot));
		}
		std::sort(m_Names.begin(), m_Names.end());
		if (m_Names.empty())
			RV_WARN("No brush masks under {0}; the terrain brush has its disc and nothing else", kBrushFolder);
	}

	const std::vector<std::string>& BrushLibrary::Names()
	{
		if (!m_Scanned)
			Scan();
		return m_Names;
	}

	int BrushLibrary::IndexOf(const std::string& name)
	{
		const std::vector<std::string>& names = Names();
		for (size_t i = 0; i < names.size(); ++i)
			if (names[i] == name)
				return (int)i;
		return -1;
	}

	std::shared_ptr<const BrushMask> BrushLibrary::Get(const std::string& name)
	{
		if (name.empty())
			return nullptr;
		auto found = m_Masks.find(name);
		if (found != m_Masks.end())
			return found->second;

		std::shared_ptr<const BrushMask> mask;
		std::vector<uint8_t> bytes;
		// The extension is not in the name, so the same four the scan accepts
		// are tried in turn; a hit is cached, and so is a miss (as null), so a
		// missing mask is not re-read every frame the combo is open.
		for (const char* extension : { ".png", ".tga", ".bmp", ".jpg" })
		{
			if (!VFS::ReadBytes(std::string(kBrushFolder) + "/" + name + extension, bytes))
				continue;
			auto decoded = std::make_shared<BrushMask>();
			if (BrushMask::Decode(bytes, *decoded))
				mask = decoded;
			else
				RV_ERROR("Brush mask '{0}{1}' will not decode, or is not square", name, extension);
			break;
		}
		m_Masks[name] = mask;
		return mask;
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

	bool TerrainBrushTool::SetShapeMask(const std::string& name)
	{
		if (name.empty())
		{
			ShapeMaskName.clear();
			Brush.ShapeMask = nullptr;
			Brush.ShapeKind = TerrainBrush::Shape::Disc;
			return true;
		}
		std::shared_ptr<const BrushMask> mask = Library.Get(name);
		if (!mask)
			return false;
		ShapeMaskName = name;
		Brush.ShapeMask = mask;
		Brush.ShapeKind = TerrainBrush::Shape::Mask;
		return true;
	}

	bool TerrainBrushTool::SetPatternMask(const std::string& name)
	{
		if (name.empty())
		{
			PatternMaskName.clear();
			Brush.PatternMask = nullptr;
			if (Brush.PatternKind == TerrainBrush::Pattern::Tiled)
				Brush.PatternKind = TerrainBrush::Pattern::None;
			return true;
		}
		std::shared_ptr<const BrushMask> mask = Library.Get(name);
		if (!mask)
			return false;
		PatternMaskName = name;
		Brush.PatternMask = mask;
		Brush.PatternKind = TerrainBrush::Pattern::Tiled;
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

		// The stroke's context (7as): where it began and the height there --
		// Flatten's target and the ramp's near end, in the data's own [0, 1] --
		// a heading of nothing yet, and a seed that does not repeat between
		// strokes.
		const float cell = m_Size / (float)data->QuadCount();
		m_Stroke = TerrainBrush::Stroke{};
		m_Stroke.StartX = m_HitLocal.x;
		m_Stroke.StartZ = m_HitLocal.z;
		m_Stroke.StartHeight = data->Sample((m_HitLocal.x + 0.5f * m_Size) / cell,
											(m_HitLocal.z + 0.5f * m_Size) / cell);
		m_Stroke.Seed = m_NextSeed++;
		m_LastStep = m_HitLocal;
		m_HasLastStep = false;
		m_Stroking = true;
	}

	void TerrainBrushTool::Step(float dt)
	{
		TerrainData* data = Assets::Manager::EditTerrain(m_Asset);
		if (!data || !m_Terrain || dt <= 0.0f)
			return;

		// The heading, for a mask that follows the stroke: the direction of the
		// last movement worth calling one. A still cursor keeps the last.
		if (m_HasLastStep)
		{
			const float dx = m_HitLocal.x - m_LastStep.x;
			const float dz = m_HitLocal.z - m_LastStep.z;
			if (dx * dx + dz * dz > kDirectionEpsilon * kDirectionEpsilon)
				m_Stroke.Direction = Math::Atan2(dz, dx);
		}
		m_LastStep = m_HitLocal;
		m_HasLastStep = true;

		// What this step will write, recorded before it is written.
		const TerrainRect footprint = Brush.Footprint(*data, m_Size, m_HitLocal.x, m_HitLocal.z, m_Stroke);
		if (footprint.Empty())
			return;
		m_Recorder.Cover(*data, footprint);

		const TerrainRect touched = Brush.Apply(*data, m_Size, m_Height, m_HitLocal.x, m_HitLocal.z,
												m_Stroke, dt);
		// Every step draws again -- the noise pattern and the droplets are
		// functions of the seed, so a held brush that never advanced it would
		// rain on the same spots forever.
		m_Stroke.Seed = m_Stroke.Seed * 1664525u + 1013904223u;
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
										float localX, float localZ, float seconds, CommandStack& commands,
										const float* toX, const float* toZ)
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
		{
			// A press where it was told to, then -- when there is a "to" -- a
			// drag to there and a hold: what a ramp needs to be a line and not
			// a point, and what a following mask needs to have a heading.
			if (i == 1 && (toX || toZ))
			{
				const float x = toX ? *toX : localX;
				const float z = toZ ? *toZ : localZ;
				m_HitLocal = Vec3(x, terrain->HeightAt(x, z), z);
			}
			Step(TerrainBrush::kStepSeconds);
		}
		End(scene, commands);
		return true;
	}

	void TerrainBrushTool::DrawOverlay() const
	{
		if (!Enabled || !m_HasHit || !m_Terrain)
			return;

		// A point on the ground at a terrain-local (x, z), lifted a hair so
		// the line is not z-fought by the surface it lies on.
		auto onGround = [&](float x, float z)
		{
			return Vec3(m_World * Vec4(x, m_Terrain->HeightAt(x, z) + kRingLift, z, 1.0f));
		};
		auto ring = [&](float radius, const Vec4& colour)
		{
			if (radius <= 0.0f)
				return;
			Vec3 previous;
			for (int i = 0; i <= kRingSegments; ++i)
			{
				const float angle = (float)i / (float)kRingSegments * 2.0f * Math::Pi;
				const Vec3 world = onGround(m_HitLocal.x + radius * Math::Cos(angle),
											m_HitLocal.z + radius * Math::Sin(angle));
				if (i > 0)
					DebugRenderer::DrawLine(previous, world, colour);
				previous = world;
			}
		};
		// A line on the ground, drawn in pieces so it follows the surface.
		auto groundLine = [&](float x0, float z0, float x1, float z1, const Vec4& colour)
		{
			constexpr int kPieces = 24;
			Vec3 previous;
			for (int i = 0; i <= kPieces; ++i)
			{
				const float t = (float)i / (float)kPieces;
				const Vec3 world = onGround(x0 + (x1 - x0) * t, z0 + (z1 - z0) * t);
				if (i > 0)
					DebugRenderer::DrawLine(previous, world, colour);
				previous = world;
			}
		};

		const Vec4 rim(1.0f, 1.0f, 1.0f, 0.95f);
		const Vec4 core(1.0f, 1.0f, 1.0f, 0.45f);

		if (Brush.ShapeKind == TerrainBrush::Shape::Mask && Brush.ShapeMask)
		{
			// The mask's square, turned as the kernel turns it -- so what is
			// drawn is where the brush will bite, angle and heading included.
			const float angle = Brush.Angle + (Brush.FollowStroke ? m_Stroke.Direction : 0.0f);
			const float c = Math::Cos(angle), s = Math::Sin(angle);
			const float r = Brush.Radius;
			const float cornersX[4] = { -r, r, r, -r };
			const float cornersZ[4] = { -r, -r, r, r };
			for (int i = 0; i < 4; ++i)
			{
				const int j = (i + 1) % 4;
				groundLine(m_HitLocal.x + cornersX[i] * c - cornersZ[i] * s,
						   m_HitLocal.z + cornersX[i] * s + cornersZ[i] * c,
						   m_HitLocal.x + cornersX[j] * c - cornersZ[j] * s,
						   m_HitLocal.z + cornersX[j] * s + cornersZ[j] * c, rim);
			}
			// Which way is forward, so a following mask is not a mystery.
			groundLine(m_HitLocal.x, m_HitLocal.z,
					   m_HitLocal.x + r * c, m_HitLocal.z + r * s, core);
		}
		else
		{
			// The rim, and the hard core inside it where the weight is one.
			ring(Brush.Radius, rim);
			ring(Brush.Radius * Math::Clamp(Brush.Hardness, 0.0f, 1.0f), core);
		}

		// A ramp is two points and the line between them: shown while it is
		// being drawn, because otherwise the far end is invisible.
		if (Brush.Mode == TerrainBrush::Op::Ramp && m_Stroking)
		{
			groundLine(m_Stroke.StartX, m_Stroke.StartZ, m_HitLocal.x, m_HitLocal.z,
					   Vec4(1.0f, 0.85f, 0.35f, 0.95f));
		}
	}
}
