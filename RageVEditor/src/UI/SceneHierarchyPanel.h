#pragma once
#include "RageV.h"
#include <vector>
#include <functional>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/SceneCommands.h"

namespace RageV::Tools { class TerrainBrushTool; }

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

		// The terrain brush (ENGINE-NOTES 7ar), whose controls the Terrain
		// component's block draws under its fields. Null draws none.
		void SetTerrainTool(Tools::TerrainBrushTool* tool) { m_TerrainTool = tool; }

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

		// Show an asset in Properties instead of the selected entity.
		//
		// Invalid clears it and hands the panel back to the entity. Set from
		// the content browser when a file is clicked, which is the gesture the
		// owner asked for: "if the post processing asset file is tapped in the
		// content browser the game engine should show properties of it in
		// inspector".
		void SetInspectedAsset(AssetHandle handle) { m_InspectedAsset = handle; }

		// Called when this panel wants an asset opened -- today only a graph it
		// has just created. Deliberately the *same* signature the content
		// browser's activate callback uses, and pointed at the same handler:
		// "open this asset" is one behaviour the editor owns, and a second
		// route to it would be a second place for it to drift.
		void SetActivateCallback(std::function<void(AssetHandle, AssetType)> callback)
		{
			m_OnActivate = std::move(callback);
		}
		AssetHandle GetInspectedAsset() const { return m_InspectedAsset; }

	private:
		void DrawProperties();
		void DrawAssetProperties();
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

		// "New Graph..." (10.11). Same shape as the popup above and the same
		// reason for existing: a template rather than a blank file, and the
		// name it returns is selected straight away so that making one and
		// then hunting for it in a dropdown is not a step.
		bool DrawNewGraphPopup(std::string& chosenName);
		bool DrawScriptPicker(const std::vector<std::string>& available,
							  std::string& scriptName, bool managed);
		void DrawScriptNameRow(std::string& label);
		bool m_OpenNewScript = false;
		bool m_OpenNewGraph = false;

		// Scripts that exist as a file but are not in this build yet, refreshed
		// each time the dropdown opens rather than each frame it is open.
		static std::vector<std::string> ScanUnbuiltScripts(
			const std::vector<std::string>& built, bool managed);
		static bool FileRegistersScript(const std::filesystem::path& file,
										const std::string& name);
		std::vector<std::string> m_UnbuiltScripts;
		static bool WriteNewScript(const std::filesystem::path& file,
								   const std::string& name);

		// Writes `ScriptGraph::Starter` to disk. The graph itself lives in the
		// engine, where a headless check can reach it.
		static bool WriteNewGraph(const std::filesystem::path& file,
								  const std::string& name);
		static bool WriteNewNativeScript(const std::filesystem::path& file,
										 const std::string& name);
		static bool IsIdentifier(const std::string& name);
		char m_NewScriptName[64] = {};
		char m_NewGraphName[64] = {};

		std::function<void(AssetHandle, AssetType)> m_OnActivate;

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

		// The asset Properties is showing, if any. Invalid means the panel
		// belongs to the selected entity, which is the ordinary case.
		AssetHandle m_InspectedAsset = AssetHandle::Invalid();
		// The selected entity's ancestors, deepest last. Rebuilt each time a
		// reveal is pending; a hierarchy is a handful of levels, so a vector
		// and a linear search beat a set that has to be allocated.
		std::vector<uint64_t> m_RevealPath;
		CommandStack* m_Commands = nullptr;
		Tools::TerrainBrushTool* m_TerrainTool = nullptr;

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
