#pragma once
#include "Scene.h"
#include "Entity.h"
#include "Components.h"
#include "ComponentRegistry.h"
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace RageV
{
	// Undo/redo.
	//
	// Lives in the engine rather than the editor because none of it touches
	// ImGui -- it operates on a Scene, which also makes it testable without a
	// window.
	//
	// A discipline rather than a feature: every editor mutation routes through
	// here, so anything added later complies for free. Retrofitting it means
	// finding every write site that already exists.
	//
	// Commands address entities by UUID, never by handle or pointer. Undoing a
	// delete recreates the entity with its original id, and EnTT recycles
	// handles -- so a stored handle can come back pointing at something else
	// entirely.
	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		// Applied when pushed, and again on redo. Must be idempotent in the
		// sense that Execute -> Undo -> Execute lands on the same state.
		virtual void Execute() = 0;
		virtual void Undo() = 0;

		// Shown next to the Edit menu entries.
		virtual std::string Name() const = 0;
	};

	class CommandStack
	{
	public:
		// Executes and records. Anything already applied in place -- an ImGui
		// widget that wrote straight to the field -- should use PushApplied.
		void Push(std::unique_ptr<EditorCommand> command);
		void PushApplied(std::unique_ptr<EditorCommand> command);

		void Undo();
		void Redo();

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }
		std::string UndoName() const { return m_Undo.empty() ? std::string() : m_Undo.back()->Name(); }
		std::string RedoName() const { return m_Redo.empty() ? std::string() : m_Redo.back()->Name(); }

		// On scene load or new scene: the recorded commands refer to entities
		// that no longer exist.
		void Clear();

		size_t Depth() const { return m_Undo.size(); }

	private:
		std::vector<std::unique_ptr<EditorCommand>> m_Undo;
		std::vector<std::unique_ptr<EditorCommand>> m_Redo;

		// Bounded, because every DeleteEntity command holds a serialized copy
		// of its subtree and an unbounded stack would grow without limit.
		static constexpr size_t kLimit = 128;
	};

	// --- field edits ---------------------------------------------------------
	// One value of any type the component registry can describe.
	using FieldValue = std::variant<bool, int, float, glm::vec3, glm::vec4, std::string>;

	FieldValue ReadFieldValue(const FieldDesc& field, void* component);
	void WriteFieldValue(const FieldDesc& field, void* component, const FieldValue& value);

	// An inspector edit. Resolved by name on apply, so it survives the
	// component being removed and re-added between undo steps.
	class FieldEditCommand : public EditorCommand
	{
	public:
		FieldEditCommand(const std::shared_ptr<Scene>& scene, UUID entity,
						 std::string component, std::string field,
						 FieldValue before, FieldValue after);

		void Execute() override;
		void Undo() override;
		std::string Name() const override;

	private:
		void Apply(const FieldValue& value);

		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		std::string m_Component;
		std::string m_Field;
		FieldValue m_Before;
		FieldValue m_After;
	};

	// A gizmo drag or a transform typed into the inspector. Stored whole rather
	// than as three field edits so one drag is one undo step.
	class TransformEditCommand : public EditorCommand
	{
	public:
		TransformEditCommand(const std::shared_ptr<Scene>& scene, UUID entity,
							 const TransformComponent& before, const TransformComponent& after);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Transform"; }

	private:
		void Apply(const TransformComponent& value);

		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		glm::vec3 m_BeforePosition, m_BeforeRotation, m_BeforeScale;
		glm::vec3 m_AfterPosition, m_AfterRotation, m_AfterScale;
	};

	// --- structural ----------------------------------------------------------
	// Creating and deleting both go through a serialized snapshot of the
	// entity and its descendants, which is only possible because entities have
	// stable ids and serialization is lossless. Undoing a delete restores the
	// whole subtree, at its original sibling position, with its original ids --
	// so anything referring to it still resolves.
	class CreateEntityCommand : public EditorCommand
	{
	public:
		CreateEntityCommand(const std::shared_ptr<Scene>& scene, UUID entity, std::string name);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Create " + m_Name; }

		UUID GetEntity() const { return m_Entity; }

	private:
		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		std::string m_Name;
		std::string m_Snapshot;   // filled in on the first undo
	};

	class DeleteEntityCommand : public EditorCommand
	{
	public:
		DeleteEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Delete " + m_Name; }

	private:
		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		UUID m_Parent;
		int m_SiblingIndex = -1;
		std::string m_Name;
		std::string m_Snapshot;
	};

	class ReparentCommand : public EditorCommand
	{
	public:
		ReparentCommand(const std::shared_ptr<Scene>& scene, Entity child, Entity parent);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Reparent " + m_Name; }

		bool IsValid() const { return m_Valid; }

	private:
		std::weak_ptr<Scene> m_Scene;
		UUID m_Child;
		UUID m_NewParent;
		UUID m_OldParent;
		int m_OldSiblingIndex = -1;
		std::string m_Name;
		bool m_Valid = true;

		// The local transform on each side of the move, restored verbatim.
		//
		// Scene::SetParent preserves the world transform by decomposing it into
		// the new parent's space, and that round trip is not bit-exact: a
		// quaternion converted back to Euler angles need not reproduce the
		// angles it came from, even when the rotation is identical. Undo has to
		// put back the numbers that were there, not numbers that mean the same.
		glm::vec3 m_BeforePosition{ 0.0f }, m_BeforeRotation{ 0.0f }, m_BeforeScale{ 1.0f };
		glm::vec3 m_AfterPosition{ 0.0f }, m_AfterRotation{ 0.0f }, m_AfterScale{ 1.0f };
		bool m_HasAfter = false;
	};

	class AddComponentCommand : public EditorCommand
	{
	public:
		AddComponentCommand(const std::shared_ptr<Scene>& scene, UUID entity, std::string component);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Add " + m_Component; }

	private:
		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		std::string m_Component;
	};

	class RemoveComponentCommand : public EditorCommand
	{
	public:
		RemoveComponentCommand(const std::shared_ptr<Scene>& scene, UUID entity, std::string component);

		void Execute() override;
		void Undo() override;
		std::string Name() const override { return "Remove " + m_Component; }

	private:
		std::weak_ptr<Scene> m_Scene;
		UUID m_Entity;
		std::string m_Component;
		// Captured on Execute so undo puts the values back, not just the
		// component. Anything the field list cannot describe -- a mesh's
		// material -- goes through the same serialize hook the scene file uses.
		std::vector<std::pair<std::string, FieldValue>> m_Values;
		std::string m_Extra;
	};

	// For scene-wide settings that are not components -- the ambient term, for
	// one. A getter/setter pair rather than a new class per setting.
	class ValueEditCommand : public EditorCommand
	{
	public:
		ValueEditCommand(std::string name, std::function<void()> apply, std::function<void()> revert)
			: m_Name(std::move(name)), m_Apply(std::move(apply)), m_Revert(std::move(revert)) {}

		void Execute() override { m_Apply(); }
		void Undo() override { m_Revert(); }
		std::string Name() const override { return m_Name; }

	private:
		std::string m_Name;
		std::function<void()> m_Apply;
		std::function<void()> m_Revert;
	};
}
