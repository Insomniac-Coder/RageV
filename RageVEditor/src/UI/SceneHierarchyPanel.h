#pragma once
#include "RageV.h"
#include "RageV/Scene/SceneCommands.h"

namespace RageV
{

	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const std::shared_ptr<Scene>& sceneref);
		void SetSceneRef(const std::shared_ptr<Scene>& sceneref);

		// Every mutation the panel makes goes through here. Null disables undo
		// rather than crashing, so a panel can be constructed before the stack.
		void SetCommandStack(CommandStack* stack) { m_Commands = stack; }

		Entity GetSelectedEntity() const { return m_Selected; }
		void SetSelectedEntity(Entity entity) { m_Selected = entity; }
		// Both panels are drawn here because they share the selection; the
		// Window menu owns their visibility.
		void OnImGuiRender(bool* showHierarchy, bool* showProperties);

		void ShowProperties(Entity entity);

	private:
		void DrawEntityNode(Entity entity);

		// Continuous edits -- a drag, a slider scrub, typing in a field --
		// become one undo step rather than one per frame. The pre-edit value is
		// captured the frame the value first changes, and the command is
		// recorded once no widget is active any more.
		void CommitPendingEdit();

	private:
		std::shared_ptr<Scene> m_SceneRef;
		Entity m_Selected;
		CommandStack* m_Commands = nullptr;

		// Structural changes are applied after the tree has been walked;
		// mutating the hierarchy mid-traversal invalidates the child lists the
		// recursion is iterating.
		Entity m_PendingDelete;
		Entity m_PendingReparentChild;
		Entity m_PendingReparentParent;
		bool   m_PendingReparent = false;
		Entity m_PendingCreateChildParent;
		bool   m_PendingCreateChild = false;

		struct PendingFieldEdit
		{
			bool Active = false;
			UUID Entity = UUID::Invalid();
			std::string Component;
			std::string Field;
			FieldValue Before;
		};
		PendingFieldEdit m_PendingEdit;
	};

}
