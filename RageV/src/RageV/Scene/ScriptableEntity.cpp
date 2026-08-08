#include <rvpch.h>
#include "ScriptableEntity.h"
#include "Scene.h"
#include "Components.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Application.h"
#include "RageV/Asset/AssetManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

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
	glm::vec3& ScriptableEntity::GetPosition() { return GetComponent<TransformComponent>().Position; }
	glm::vec3& ScriptableEntity::GetRotation() { return GetComponent<TransformComponent>().Rotation; }
	glm::vec3& ScriptableEntity::GetScale()    { return GetComponent<TransformComponent>().Scale; }

	void ScriptableEntity::Translate(const glm::vec3& delta)
	{
		GetComponent<TransformComponent>().Position += delta;
	}

	void ScriptableEntity::Rotate(const glm::vec3& eulerDelta)
	{
		GetComponent<TransformComponent>().Rotation += eulerDelta;
	}

	void ScriptableEntity::LookAt(const glm::vec3& target, const glm::vec3& up)
	{
		const glm::vec3 from = GetWorldPosition();
		const glm::vec3 direction = target - from;

		// A zero-length direction has no rotation to describe, and normalising
		// it would produce NaNs that then spread through every transform below.
		if (glm::dot(direction, direction) < 1e-12f)
			return;

		// -Z is forward, matching the camera and light convention.
		const glm::mat4 view = glm::lookAt(from, target, up);
		GetComponent<TransformComponent>().Rotation = glm::eulerAngles(glm::quat_cast(glm::inverse(view)));
	}

	glm::mat4 ScriptableEntity::GetWorldTransform()
	{
		return GetScene().GetWorldTransform(m_Entity);
	}

	glm::vec3 ScriptableEntity::GetWorldPosition()
	{
		return glm::vec3(GetWorldTransform()[3]);
	}

	glm::vec3 ScriptableEntity::GetForward()
	{
		return glm::normalize(glm::vec3(GetWorldTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
	}

	glm::vec3 ScriptableEntity::GetRight()
	{
		return glm::normalize(glm::vec3(GetWorldTransform() * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
	}

	glm::vec3 ScriptableEntity::GetUp()
	{
		return glm::normalize(glm::vec3(GetWorldTransform() * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
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
		return AssetManager::InstantiatePrefab(GetScene(), prefab);
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
	// Input and time
	// -------------------------------------------------------------------------
	bool ScriptableEntity::IsActionDown(const std::string& action)      { return InputMap::IsActionDown(action); }
	bool ScriptableEntity::WasActionPressed(const std::string& action)  { return InputMap::WasActionPressed(action); }
	bool ScriptableEntity::WasActionReleased(const std::string& action) { return InputMap::WasActionReleased(action); }
	float ScriptableEntity::GetAxis(const std::string& axis)            { return InputMap::GetAxis(axis); }

	float ScriptableEntity::GetFixedDeltaTime() { return Application::GetFixedTimestep(); }
	float ScriptableEntity::GetTime()           { return Application::GetElapsedTime(); }
}
