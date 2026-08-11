#pragma once
#include "RageV.h"
#include <vector>
#include "RageV/Scene/ScriptRegistry.h"
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
		// Selecting from anywhere -- a viewport click, a menu, loading a scene --
		// also *reveals* the entity in the tree: every ancestor is expanded and
		// the row is scrolled to. Without that, picking a nested object in the
		// viewport highlighted a row nobody could see, because its parent was
		// collapsed and the row was never drawn.
		void SetSelectedEntity(Entity entity);
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
		// The C# script component: type dropdown, then its reflected fields.
		// One script inspector for both languages. The language row converts the
		// component, which cannot happen while the component list is being
		// walked -- so it is queued and applied afterwards, the way removal is.
		void DrawScriptLanguageRow(bool managed);
		bool DrawNewScriptPopup(bool managed, std::string& chosenName);
		bool DrawScriptPicker(const std::vector<std::string>& available,
							  std::string& scriptName, bool managed);
		void DrawScriptNameRow(std::string& label);
		bool m_OpenNewScript = false;

		// Scripts that exist as a file but are not in this build yet, refreshed
		// each time the dropdown opens rather than each frame it is open.
		static std::vector<std::string> ScanUnbuiltScripts(
			const std::vector<std::string>& built, bool managed);
		static bool FileRegistersScript(const std::filesystem::path& file,
										const std::string& name);
		std::vector<std::string> m_UnbuiltScripts;
		static bool WriteNewScript(const std::filesystem::path& file,
								   const std::string& name);
		static bool WriteNewNativeScript(const std::filesystem::path& file,
										 const std::string& name);
		static bool IsIdentifier(const std::string& name);
		char m_NewScriptName[64] = {};

		enum class PendingScriptSwap { None, ToCpp, ToCSharp };
		PendingScriptSwap m_PendingScriptSwap = PendingScriptSwap::None;

		void DrawNativeScript(NativeScriptComponent& script);
		void DrawScriptField(const std::string& name, int kind,
							 const std::string& defaultValue,
							 ScriptFieldOverrides& overrides);
		static int ScriptKindToWidget(ScriptFieldKind kind);

		void DrawManagedScript(ManagedScriptComponent& script);
		static std::string FormatFloat(float value);

		void CommitPendingEdit();

	private:
		std::shared_ptr<Scene> m_SceneRef;
		Entity m_Selected;

		// Set when the selection changes, cleared by the draw that acts on it.
		// A flag rather than doing the work in the setter, because expanding a
		// tree node is something only ImGui can do and only while drawing.
		bool m_RevealSelection = false;
		// The selected entity's ancestors, deepest last. Rebuilt each time a
		// reveal is pending; a hierarchy is a handful of levels, so a vector
		// and a linear search beat a set that has to be allocated.
		std::vector<uint64_t> m_RevealPath;
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
