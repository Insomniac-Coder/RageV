#include <rvpch.h>
#include "ScriptableEntity.h"
#include "Scene.h"
#include "Components.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Application.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/UI/Interaction.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	Scene& ScriptableEntity::GetScene() const
	{
		return m_Entity.GetScene();
	}

	UUID ScriptableEntity::GetUUID()                        { return m_Entity.GetUUID(); }
	const std::string& ScriptableEntity::GetName()          { return m_Entity.GetName(); }
	void ScriptableEntity::SetName(const std::string& name) { GetComponent<TagComponent>().Name = name; }

	// -------------------------------------------------------------------------
	// Transform
	// -------------------------------------------------------------------------
	Vec3& ScriptableEntity::GetPosition() { return GetComponent<TransformComponent>().Position; }
	Vec3& ScriptableEntity::GetRotation() { return GetComponent<TransformComponent>().Rotation; }
	Vec3& ScriptableEntity::GetScale()    { return GetComponent<TransformComponent>().Scale; }

	void ScriptableEntity::Translate(const Vec3& delta)
	{
		GetComponent<TransformComponent>().Position += delta;
	}

	void ScriptableEntity::Rotate(const Vec3& eulerDelta)
	{
		GetComponent<TransformComponent>().Rotation += eulerDelta;
	}

	void ScriptableEntity::LookAt(const Vec3& target, const Vec3& up)
	{
		const Vec3 from = GetWorldPosition();
		const Vec3 direction = target - from;

		// A zero-length direction has no rotation to describe, and normalising
		// it would produce NaNs that then spread through every transform below.
		if (Math::Dot(direction, direction) < 1e-12f)
			return;

		// -Z is forward, matching the camera and light convention.
		const Mat4 view = Math::LookAt(from, target, up);
		GetComponent<TransformComponent>().Rotation = Math::ToEuler(Math::ToQuat(Math::Inverse(view)));
	}

	Mat4 ScriptableEntity::GetWorldTransform()
	{
		return GetScene().GetWorldTransform(m_Entity);
	}

	Vec3 ScriptableEntity::GetWorldPosition()
	{
		return Vec3(GetWorldTransform()[3]);
	}

	Vec3 ScriptableEntity::GetForward()
	{
		return Math::Normalize(Vec3(GetWorldTransform() * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
	}

	Vec3 ScriptableEntity::GetRight()
	{
		return Math::Normalize(Vec3(GetWorldTransform() * Vec4(1.0f, 0.0f, 0.0f, 0.0f)));
	}

	Vec3 ScriptableEntity::GetUp()
	{
		return Math::Normalize(Vec3(GetWorldTransform() * Vec4(0.0f, 1.0f, 0.0f, 0.0f)));
	}

	// -------------------------------------------------------------------------
	// Other entities
	// -------------------------------------------------------------------------
	Entity ScriptableEntity::FindEntityByName(const std::string& name)
	{
		return GetScene().FindEntityByName(name);
	}

	Entity ScriptableEntity::FindEntityByUUID(UUID id)
	{
		return GetScene().GetEntityByUUID(id);
	}

	std::vector<Entity> ScriptableEntity::FindEntitiesByName(const std::string& name)
	{
		return GetScene().FindEntitiesByName(name);
	}

	Entity ScriptableEntity::Spawn(const std::string& name)
	{
		return GetScene().CreateEntity(name);
	}

	Entity ScriptableEntity::SpawnPrefab(AssetHandle prefab)
	{
		return Assets::Manager::InstantiatePrefab(GetScene(), prefab);
	}

	void ScriptableEntity::Destroy()          { GetScene().DestroyDeferred(m_Entity); }
	void ScriptableEntity::Destroy(Entity e)  { GetScene().DestroyDeferred(e); }

	// -------------------------------------------------------------------------
	// Hierarchy
	// -------------------------------------------------------------------------
	Entity ScriptableEntity::GetParent()
	{
		return GetScene().GetParent(m_Entity);
	}

	void ScriptableEntity::SetParent(Entity parent)
	{
		GetScene().SetParent(m_Entity, parent);
	}

	std::vector<Entity> ScriptableEntity::GetChildren()
	{
		Scene& scene = GetScene();

		// Resolved to entities rather than handed back as ids: a script should
		// not have to know that the hierarchy is stored by UUID.
		std::vector<Entity> children;
		for (UUID id : scene.GetChildren(m_Entity))
		{
			if (Entity child = scene.GetEntityByUUID(id))
				children.push_back(child);
		}
		return children;
	}

	// -------------------------------------------------------------------------
	// Physics
	// -------------------------------------------------------------------------
	// Every one of these tolerates there being no simulation. A script runs in
	// play mode, but it can be stepped by a tool or a test that never started
	// one, and asking a scene at rest to push something is a no-op rather than
	// a mistake.
	void ScriptableEntity::AddForce(const Vec3& force)
	{
		if (Physics::World* physics = GetScene().GetPhysics())
			physics->AddForce(m_Entity.GetUUID(), force);
	}

	void ScriptableEntity::AddImpulse(const Vec3& impulse)
	{
		if (Physics::World* physics = GetScene().GetPhysics())
			physics->AddImpulse(m_Entity.GetUUID(), impulse);
	}

	void ScriptableEntity::SetLinearVelocity(const Vec3& velocity)
	{
		if (Physics::World* physics = GetScene().GetPhysics())
			physics->SetLinearVelocity(m_Entity.GetUUID(), velocity);
	}

	Vec3 ScriptableEntity::GetLinearVelocity()
	{
		if (Physics::World* physics = GetScene().GetPhysics())
			return physics->GetLinearVelocity(m_Entity.GetUUID());
		return Vec3(0.0f);
	}

	RayHit ScriptableEntity::Raycast(const Vec3& origin, const Vec3& direction)
	{
		if (Physics::World* physics = GetScene().GetPhysics())
			return physics->CastRay(origin, direction);
		return {};
	}

	// -------------------------------------------------------------------------
	// Audio
	// -------------------------------------------------------------------------
	AudioVoice ScriptableEntity::PlaySource()
	{
		if (!HasComponent<AudioSourceComponent>())
			return 0;

		auto& source = GetComponent<AudioSourceComponent>();

		// Restart rather than overlap. Two copies of one source playing over
		// each other is a bug in every case where a component is involved --
		// overlapping repeats are what PlayOneShot is for.
		Audio::Engine::Stop(source.Voice);

		AudioPlayback playback;
		playback.Clip = source.Clip;
		playback.Bus = source.Bus;
		playback.Volume = source.Volume;
		playback.Pitch = source.Pitch;
		playback.Loop = source.Loop;
		playback.Stream = source.Stream;
		playback.Spatial = source.Spatial;
		playback.Position = GetWorldPosition();
		playback.MinDistance = source.MinDistance;
		playback.MaxDistance = source.MaxDistance;

		source.Voice = Audio::Engine::Play(playback);
		return source.Voice;
	}

	void ScriptableEntity::StopSource()
	{
		if (!HasComponent<AudioSourceComponent>())
			return;

		auto& source = GetComponent<AudioSourceComponent>();
		Audio::Engine::Stop(source.Voice);
		source.Voice = 0;
	}

	bool ScriptableEntity::IsSourcePlaying()
	{
		if (!HasComponent<AudioSourceComponent>())
			return false;

		return Audio::Engine::IsPlaying(GetComponent<AudioSourceComponent>().Voice);
	}

	AudioVoice ScriptableEntity::PlayOneShot(AssetHandle clip, float volume, float pitch)
	{
		return PlayOneShotAt(clip, GetWorldPosition(), volume, pitch);
	}

	AudioVoice ScriptableEntity::PlayOneShotAt(AssetHandle clip, const Vec3& position,
											   float volume, float pitch)
	{
		AudioPlayback playback;
		playback.Clip = clip;
		playback.Volume = volume;
		playback.Pitch = pitch;
		playback.Spatial = true;
		playback.Position = position;
		return Audio::Engine::Play(playback);
	}

	AudioVoice ScriptableEntity::PlayOneShot2D(AssetHandle clip, float volume, float pitch)
	{
		AudioPlayback playback;
		playback.Clip = clip;
		playback.Bus = AudioBus::UI;
		playback.Volume = volume;
		playback.Pitch = pitch;
		playback.Spatial = false;
		return Audio::Engine::Play(playback);
	}

	// -------------------------------------------------------------------------
	// Input and time
	// -------------------------------------------------------------------------
	bool ScriptableEntity::IsActionDown(const std::string& action)      { return InputMap::IsActionDown(action); }
	bool ScriptableEntity::WasActionPressed(const std::string& action)  { return InputMap::WasActionPressed(action); }
	bool ScriptableEntity::WasActionReleased(const std::string& action) { return InputMap::WasActionReleased(action); }
	float ScriptableEntity::GetAxis(const std::string& axis)            { return InputMap::GetAxis(axis); }

	float ScriptableEntity::GetFixedDeltaTime()      { return Application::GetFixedTimestep(); }
	float ScriptableEntity::GetTime()                { return Application::GetElapsedTime(); }
	float ScriptableEntity::GetInterpolationAlpha()  { return Application::GetInterpolationAlpha(); }

	bool ScriptableEntity::IsPointerOverUI()         { return UI::WantsPointer(); }

	// -------------------------------------------------------------------------
	// The game's UI
	// -------------------------------------------------------------------------
	void ScriptableEntity::SetText(const std::string& text)
	{
		SetText(m_Entity, text);
	}

	void ScriptableEntity::SetText(Entity entity, const std::string& text)
	{
		if (entity && entity.HasComponent<UITextComponent>())
			entity.GetComponent<UITextComponent>().Text = text;
	}

	std::string ScriptableEntity::GetText()
	{
		return GetText(m_Entity);
	}

	std::string ScriptableEntity::GetText(Entity entity)
	{
		if (entity && entity.HasComponent<UITextComponent>())
			return entity.GetComponent<UITextComponent>().Text;

		return {};
	}

	bool ScriptableEntity::WasButtonClicked()
	{
		return WasButtonClicked(m_Entity);
	}

	bool ScriptableEntity::WasButtonClicked(Entity entity)
	{
		return entity && entity.HasComponent<UIButtonComponent>()
			&& entity.GetComponent<UIButtonComponent>().Clicked;
	}
}
