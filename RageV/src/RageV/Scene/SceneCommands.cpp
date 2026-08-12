#include <rvpch.h>
#include "SceneCommands.h"
#include "SceneSerializer.h"
#include <algorithm>

namespace RageV
{
	namespace
	{
		// Resolves a component by name on an entity, or null. Commands hold
		// names rather than pointers because anything can happen between a
		// command being recorded and being undone.
		void* ResolveComponent(Scene& scene, UUID id, const std::string& name,
							   const ComponentDesc** outDesc = nullptr)
		{
			const ComponentDesc* desc = ComponentRegistry::Find(name);
			if (!desc)
				return nullptr;

			Entity entity = scene.GetEntityByUUID(id);
			if (!entity)
				return nullptr;

			if (outDesc)
				*outDesc = desc;
			return desc->TryGet(entity);
		}

		int IndexInParent(Scene& scene, Entity entity)
		{
			Entity parent = scene.GetParent(entity);
			if (!parent)
				return -1;

			const auto& siblings = scene.GetChildren(parent);
			const auto it = std::find(siblings.begin(), siblings.end(), entity.GetUUID());
			return it == siblings.end() ? -1 : (int)std::distance(siblings.begin(), it);
		}

		// Restores a child's position among its siblings. Without this, undoing
		// a delete sends the entity to the bottom of the hierarchy panel, which
		// reads as the undo having not quite worked.
		void MoveToIndex(Scene& scene, Entity entity, int index)
		{
			Entity parent = scene.GetParent(entity);
			if (!parent || index < 0)
				return;

			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			const UUID id = entity.GetUUID();

			siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
			siblings.insert(siblings.begin() + std::min((size_t)index, siblings.size()), id);
		}
	}

	// -------------------------------------------------------------------------
	// Stack
	// -------------------------------------------------------------------------
	void CommandStack::Push(std::unique_ptr<EditorCommand> command)
	{
		command->Execute();
		PushApplied(std::move(command));
	}

	void CommandStack::PushApplied(std::unique_ptr<EditorCommand> command)
	{
		// A new edit invalidates the redo branch -- there is no tree here, and
		// keeping one would mean deciding what "redo" means after a divergence.
		m_Redo.clear();
		m_Undo.push_back(std::move(command));

		if (m_Undo.size() > kLimit)
			m_Undo.erase(m_Undo.begin());
	}

	void CommandStack::Undo()
	{
		if (m_Undo.empty())
			return;

		std::unique_ptr<EditorCommand> command = std::move(m_Undo.back());
		m_Undo.pop_back();
		command->Undo();
		m_Redo.push_back(std::move(command));
	}

	void CommandStack::Redo()
	{
		if (m_Redo.empty())
			return;

		std::unique_ptr<EditorCommand> command = std::move(m_Redo.back());
		m_Redo.pop_back();
		command->Execute();
		m_Undo.push_back(std::move(command));
	}

	void CommandStack::Clear()
	{
		m_Undo.clear();
		m_Redo.clear();
	}

	// -------------------------------------------------------------------------
	// Field values
	// -------------------------------------------------------------------------
	FieldValue ReadFieldValue(const FieldDesc& field, void* component)
	{
		void* value = field.Access(component);

		switch (field.Type)
		{
			case FieldType::Bool:   return *(bool*)value;
			case FieldType::Int:    return *(int*)value;
			case FieldType::Enum:   return *(int*)value;
			case FieldType::Float:  return *(float*)value;
			case FieldType::Vec2:   return *(Vec2*)value;
			case FieldType::Vec3:   return *(Vec3*)value;
			case FieldType::Vec4:   return *(Vec4*)value;
			case FieldType::String: return *(std::string*)value;
			case FieldType::Asset:  return (uint64_t)*(AssetHandle*)value;
			// Through uint64_t rather than as an EntityRef of its own: the
			// variant already carries that alternative for assets, and a second
			// 64-bit alternative buys nothing an undo step can tell apart --
			// the FieldDesc beside it says which kind it came from.
			case FieldType::Entity: return (uint64_t)*(EntityRef*)value;
		}

		return 0;
	}

	void WriteFieldValue(const FieldDesc& field, void* component, const FieldValue& source)
	{
		void* value = field.Access(component);

		switch (field.Type)
		{
			case FieldType::Bool:   *(bool*)value = std::get<bool>(source); break;
			case FieldType::Int:
			case FieldType::Enum:   *(int*)value = std::get<int>(source); break;
			case FieldType::Float:  *(float*)value = std::get<float>(source); break;
			case FieldType::Vec2:   *(Vec2*)value = std::get<Vec2>(source); break;
			case FieldType::Vec3:   *(Vec3*)value = std::get<Vec3>(source); break;
			case FieldType::Vec4:   *(Vec4*)value = std::get<Vec4>(source); break;
			case FieldType::String: *(std::string*)value = std::get<std::string>(source); break;
			case FieldType::Asset:  *(AssetHandle*)value = AssetHandle(std::get<uint64_t>(source)); break;
			case FieldType::Entity: *(EntityRef*)value = EntityRef(UUID(std::get<uint64_t>(source))); break;
		}
	}

	// -------------------------------------------------------------------------
	// Field edit
	// -------------------------------------------------------------------------
	FieldEditCommand::FieldEditCommand(const std::shared_ptr<Scene>& scene, UUID entity,
									   std::string component, std::string field,
									   FieldValue before, FieldValue after)
		: m_Scene(scene), m_Entity(entity), m_Component(std::move(component)),
		  m_Field(std::move(field)), m_Before(std::move(before)), m_After(std::move(after))
	{
	}

	void FieldEditCommand::Apply(const FieldValue& value)
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		const ComponentDesc* desc = nullptr;
		void* component = ResolveComponent(*scene, m_Entity, m_Component, &desc);
		if (!component || !desc)
			return;

		for (const FieldDesc& field : desc->Fields)
		{
			if (m_Field != field.Name)
				continue;

			WriteFieldValue(field, component, value);
			if (desc->OnChanged)
				desc->OnChanged(component);
			return;
		}
	}

	void FieldEditCommand::Execute() { Apply(m_After); }
	void FieldEditCommand::Undo()    { Apply(m_Before); }

	std::string FieldEditCommand::Name() const
	{
		return m_Component + "." + m_Field;
	}

	// -------------------------------------------------------------------------
	// Transform edit
	// -------------------------------------------------------------------------
	TransformEditCommand::TransformEditCommand(const std::shared_ptr<Scene>& scene, UUID entity,
											   const TransformComponent& before,
											   const TransformComponent& after)
		: m_Scene(scene), m_Entity(entity),
		  m_BeforePosition(before.Position), m_BeforeRotation(before.Rotation), m_BeforeScale(before.Scale),
		  m_AfterPosition(after.Position), m_AfterRotation(after.Rotation), m_AfterScale(after.Scale)
	{
	}

	void TransformEditCommand::Apply(const TransformComponent& value)
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		Entity entity = scene->GetEntityByUUID(m_Entity);
		if (!entity || !entity.HasComponent<TransformComponent>())
			return;

		auto& transform = entity.GetComponent<TransformComponent>();
		transform.Position = value.Position;
		transform.Rotation = value.Rotation;
		transform.Scale = value.Scale;

		scene->UpdateWorldTransforms();
	}

	void TransformEditCommand::Execute()
	{
		TransformComponent value;
		value.Position = m_AfterPosition;
		value.Rotation = m_AfterRotation;
		value.Scale = m_AfterScale;
		Apply(value);
	}

	void TransformEditCommand::Undo()
	{
		TransformComponent value;
		value.Position = m_BeforePosition;
		value.Rotation = m_BeforeRotation;
		value.Scale = m_BeforeScale;
		Apply(value);
	}

	// -------------------------------------------------------------------------
	// Create / delete
	// -------------------------------------------------------------------------
	CreateEntityCommand::CreateEntityCommand(const std::shared_ptr<Scene>& scene, UUID entity,
											 std::string name)
		: m_Scene(scene), m_Entity(entity), m_Name(std::move(name))
	{
	}

	void CreateEntityCommand::Execute()
	{
		auto scene = m_Scene.lock();
		if (!scene || scene->HasEntity(m_Entity))
			return;

		if (m_Snapshot.empty())
		{
			scene->CreateEntityWithUUID(m_Entity, m_Name);
			return;
		}

		// Redo after an undo: whatever was configured on the entity between
		// creating it and undoing is restored too.
		SceneSerializer serializer(scene);
		serializer.DeserializeAdditive(m_Snapshot);
	}

	void CreateEntityCommand::Undo()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		Entity entity = scene->GetEntityByUUID(m_Entity);
		if (!entity)
			return;

		// Captured before the delete, so redo can bring back what was there
		// rather than a bare entity.
		SceneSerializer serializer(scene);
		m_Snapshot = serializer.SerializeSubtree(entity);

		scene->DeleteEntity(entity);
	}

	DeleteEntityCommand::DeleteEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity)
		: m_Scene(scene), m_Entity(entity.GetUUID())
	{
		m_Name = entity.GetName();

		Entity parent = scene->GetParent(entity);
		m_Parent = parent ? parent.GetUUID() : UUID::Invalid();
		m_SiblingIndex = IndexInParent(*scene, entity);

		SceneSerializer serializer(scene);
		m_Snapshot = serializer.SerializeSubtree(entity);
	}

	void DeleteEntityCommand::Execute()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		if (Entity entity = scene->GetEntityByUUID(m_Entity))
			scene->DeleteEntity(entity);
	}

	void DeleteEntityCommand::Undo()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		SceneSerializer serializer(scene);
		if (!serializer.DeserializeAdditive(m_Snapshot))
			return;

		// The parent link comes back with the snapshot, but the position among
		// its siblings does not -- it would land at the end.
		if (Entity entity = scene->GetEntityByUUID(m_Entity))
			MoveToIndex(*scene, entity, m_SiblingIndex);

		scene->UpdateWorldTransforms();
	}

	// -------------------------------------------------------------------------
	// Reparent
	// -------------------------------------------------------------------------
	ReparentCommand::ReparentCommand(const std::shared_ptr<Scene>& scene, Entity child, Entity parent)
		: m_Scene(scene), m_Child(child.GetUUID())
	{
		m_Name = child.GetName();
		m_NewParent = parent ? parent.GetUUID() : UUID::Invalid();

		Entity oldParent = scene->GetParent(child);
		m_OldParent = oldParent ? oldParent.GetUUID() : UUID::Invalid();
		m_OldSiblingIndex = IndexInParent(*scene, child);

		if (child.HasComponent<TransformComponent>())
		{
			const auto& transform = child.GetComponent<TransformComponent>();
			m_BeforePosition = transform.Position;
			m_BeforeRotation = transform.Rotation;
			m_BeforeScale = transform.Scale;
		}

		// Rejected up front rather than on Execute, so the caller can avoid
		// recording a command that does nothing.
		m_Valid = !(parent && (child == parent || scene->IsDescendantOf(parent, child)));
	}

	void ReparentCommand::Execute()
	{
		auto scene = m_Scene.lock();
		if (!scene || !m_Valid)
			return;

		Entity child = scene->GetEntityByUUID(m_Child);
		if (!child)
			return;

		scene->SetParent(child, scene->GetEntityByUUID(m_NewParent));

		auto& transform = child.GetComponent<TransformComponent>();
		if (m_HasAfter)
		{
			// Redo: put back exactly what the first move produced.
			transform.Position = m_AfterPosition;
			transform.Rotation = m_AfterRotation;
			transform.Scale = m_AfterScale;
			scene->UpdateWorldTransforms();
		}
		else
		{
			m_AfterPosition = transform.Position;
			m_AfterRotation = transform.Rotation;
			m_AfterScale = transform.Scale;
			m_HasAfter = true;
		}
	}

	void ReparentCommand::Undo()
	{
		auto scene = m_Scene.lock();
		if (!scene || !m_Valid)
			return;

		Entity child = scene->GetEntityByUUID(m_Child);
		if (!child)
			return;

		scene->SetParent(child, scene->GetEntityByUUID(m_OldParent));

		auto& transform = child.GetComponent<TransformComponent>();
		transform.Position = m_BeforePosition;
		transform.Rotation = m_BeforeRotation;
		transform.Scale = m_BeforeScale;

		MoveToIndex(*scene, child, m_OldSiblingIndex);
		scene->UpdateWorldTransforms();
	}

	// -------------------------------------------------------------------------
	// Add / remove component
	// -------------------------------------------------------------------------
	AddComponentCommand::AddComponentCommand(const std::shared_ptr<Scene>& scene, UUID entity,
											 std::string component)
		: m_Scene(scene), m_Entity(entity), m_Component(std::move(component))
	{
	}

	void AddComponentCommand::Execute()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		const ComponentDesc* desc = ComponentRegistry::Find(m_Component);
		Entity entity = scene->GetEntityByUUID(m_Entity);
		if (desc && entity)
			desc->Add(entity);
	}

	void AddComponentCommand::Undo()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		const ComponentDesc* desc = ComponentRegistry::Find(m_Component);
		Entity entity = scene->GetEntityByUUID(m_Entity);
		if (desc && entity)
			desc->Remove(entity);
	}

	RemoveComponentCommand::RemoveComponentCommand(const std::shared_ptr<Scene>& scene, UUID entity,
												   std::string component)
		: m_Scene(scene), m_Entity(entity), m_Component(std::move(component))
	{
	}

	void RemoveComponentCommand::Execute()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		const ComponentDesc* desc = nullptr;
		void* component = ResolveComponent(*scene, m_Entity, m_Component, &desc);
		if (!component || !desc)
			return;

		// Every field, captured before the component goes. Undo restores the
		// values, not just the component -- removing and undoing should be a
		// no-op, not a reset to defaults.
		m_Values.clear();
		for (const FieldDesc& field : desc->Fields)
			m_Values.emplace_back(field.Name, ReadFieldValue(field, component));

		// Fields alone are not the whole component: a MeshComponent's material
		// is a Ref the registry describes through a serialize hook, and without
		// this a removed mesh came back with its material silently dropped.
		m_Extra.clear();
		if (desc->SerializeExtra)
		{
			YAML::Emitter emitter;
			emitter << YAML::BeginMap;
			desc->SerializeExtra(emitter, component);
			emitter << YAML::EndMap;
			m_Extra = emitter.c_str();
		}

		desc->Remove(scene->GetEntityByUUID(m_Entity));
	}

	void RemoveComponentCommand::Undo()
	{
		auto scene = m_Scene.lock();
		if (!scene)
			return;

		const ComponentDesc* desc = ComponentRegistry::Find(m_Component);
		Entity entity = scene->GetEntityByUUID(m_Entity);
		if (!desc || !entity)
			return;

		void* component = desc->Add(entity);
		if (!component)
			return;

		for (const FieldDesc& field : desc->Fields)
		{
			for (const auto& [name, value] : m_Values)
			{
				if (name == field.Name)
				{
					WriteFieldValue(field, component, value);
					break;
				}
			}
		}

		if (desc->DeserializeExtra && !m_Extra.empty())
		{
			try
			{
				desc->DeserializeExtra(YAML::Load(m_Extra), component);
			}
			catch (const YAML::Exception& e)
			{
				RV_CORE_WARN("Could not restore {0}'s extra data on undo: {1}",
							 m_Component, e.what());
			}
		}

		if (desc->OnChanged)
			desc->OnChanged(component);
	}
}
