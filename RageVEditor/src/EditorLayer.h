#pragma once
#include <RageV.h>
#include "RageV/Managed/ScriptBuild.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "UI/SceneHierarchyPanel.h"
#include "UI/ContentBrowserPanel.h"
#include "UI/EditorTheme.h"
#include "RageV/Scene/SceneCommands.h"
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/EditorIcons.h"
#include "RageV/UI/Interaction.h"
// ImGuizmo.h does not include imgui.h itself and relies on it being included
// first.
#include "imgui.h"
#include "ImGuizmo.h"

// An internal ImGui type, forward-declared: only the .cpp -- which includes
// imgui_internal.h -- ever looks inside it.
struct ImGuiDockNode;

class EditorLayer : public RageV::Layer
{
public:
	EditorLayer();
	// Cancels and joins a build still running; a worker thread outliving the
	// layer would write into freed members.
	~EditorLayer() override;

	void OnAttach() override;
	// Opening the project's scene, on the boot worker. No device calls.
	void OnLoad(RageV::Boot::Progress& progress) override;
	// Its assets, back on the main thread where the device lives, a slice at
	// a time so the bar keeps moving through the uploads.
	bool OnLoadStep(RageV::Boot::Progress& progress) override;
	void OnLoaded() override;
	void OnUpdate(RageV::Timestep ts) override;
	void OnFixedUpdate(RageV::Timestep dt) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;
	bool OnKeyPressed(RageV::KeyPressedEvent& e);

	void NewScene();
	void OpenScene();
	void OpenSceneFile(const std::filesystem::path& filepath);
	void SaveScene();
	void NewProject();
	// The part of NewProject after its dialog: `picked` names the .rvproject,
	// and a folder of that name is created beside it.
	void CreateProjectAt(const std::filesystem::path& picked);
	void OpenProject();
	void SetStartSceneToCurrent();

	// Into the project's own bin/. BuildGameAs asks for somewhere else.
	void BuildGame();
	void BuildGameAs();
	void BuildInto(const std::filesystem::path& output);

	// The scene a freshly created project opens on. Deliberately not empty --
	// see the definition.
	void PopulateStarterScene();

	// Compiles the project's scripts -- the C++ game module when the project
	// has one, then C# -- on a worker thread, so the editor stays usable while
	// a module compiles. The panel is a live console during the build, with a
	// cancel that terminates the compiler's whole process tree, and becomes
	// the parsed diagnostics when it finishes.
	void BuildScripts();
	// Called each frame from OnUpdate; joins the worker and publishes its
	// results the frame the build finishes.
	void FinishBuild();
	void DrawScriptBuildPanel();
	void DrawBuildResult(const RageV::Managed::BuildResult& result);

	RageV::Managed::BuildResult m_ScriptBuild;
	bool m_ScriptBuildRan = false;
	bool m_ShowScriptBuild = false;

	// The C++ module's build, kept apart from the C# one because they fail
	// independently and the panel says which half is broken.
	RageV::Managed::BuildResult m_ModuleBuild;
	bool m_ModuleBuildRan = false;

	// The build worker. The worker writes its results and then sets
	// m_BuildDone; the main thread reads them only after seeing it, so the
	// atomics are the whole synchronisation story. The live log is the one
	// thing touched from both sides at once, and it has the mutex.
	std::thread m_BuildThread;
	std::atomic<bool> m_BuildCancel{ false };
	std::atomic<bool> m_BuildDone{ false };
	bool m_BuildInFlight = false;   // the main thread's view
	RageV::Managed::BuildResult m_WorkerModule;
	RageV::Managed::BuildResult m_WorkerScripts;
	bool m_WorkerRanModule = false;
	bool m_WorkerRanScripts = false;
	std::mutex m_BuildLogMutex;
	std::string m_BuildLiveLog;

	// A C# assembly built mid-play, waiting for Stop: the collectible-context
	// swap must not happen under live instances.
	void LoadScriptAssembly(const std::filesystem::path& assembly);
	std::filesystem::path m_PendingAssemblyLoad;

	// Set when a build interrupted Play (or Play was pressed mid-build):
	// FinishBuild restarts the scene if everything that ran succeeded.
	bool m_ResumePlayAfterBuild = false;


	void ImportModel();
	void OnScenePlay();
	void OnSceneStop();
	void OnScenePause(bool paused);
	void SaveSelectionAsPrefab();
	void OnAssetActivated(RageV::AssetHandle handle, RageV::AssetType type);

private:
	// --- menu bar and panels ------------------------------------------------
	void DrawMenuBar();
	void DrawToolbar();
	void DrawStatisticsPanel();
	void DrawRenderSettingsPanel();
	void DrawViewportPanel();
	void DrawGameViewportPanel();
	// Proportional dock layout, built on first run and rebuilt by
	// Window > Reset Layout.
	void BuildDefaultLayout(unsigned int dockspaceID);
	static bool LayoutVersionMatches();
	static void WriteLayoutVersion();

	// Bump when the default arrangement changes, so an existing imgui.ini does
	// not pin people to the old one.
	// 2: the Build Log gained a dock slot. Without a bump the saved layout
	//    from version 1 is reused and it keeps opening as a floating window
	//    over the hierarchy.
	static constexpr int kLayoutVersion = 2;
	void DrawAboutPopup();
	void DrawBackendRestartPopup();
	void DrawGizmo();

	// --- Entity creation ----------------------------------------------------
	RageV::Entity CreateEmpty(const std::string& name);
	RageV::Entity CreateMesh(RageV::PrimitiveType primitive);
	RageV::Entity CreateLight(RageV::Light::LightType type);
	RageV::Entity CreateCamera();
	void LoadDemoScene();

	// Frames the selection, or the origin when nothing is selected.
	void FocusSelection();

private:
	// The viewport's own camera. The scene's primary CameraComponent is still
	// used, but only when the viewport is explicitly switched to it -- a scene
	// no longer has to contain a camera to be visible.
	// Editing, running, or running-but-frozen. Play snapshots the scene and
	// Stop restores it, so anything done while running is discarded -- which
	// is what makes pressing Play feel free.
	enum class SceneState { Edit, Play, Paused };
	SceneState m_SceneState = SceneState::Edit;
	std::string m_SceneSnapshot;

	// Where the current scene came from. Needed because "set this as the start
	// scene" has to write a path, and a scene that has never been saved has
	// none to write.
	std::filesystem::path m_ScenePath;

	RageV::EditorCamera m_EditorCamera;
	bool m_UseEditorCamera = true;

	// Every scene edit routes through here. Cleared on new/open, since the
	// recorded commands refer to entities that no longer exist.
	RageV::CommandStack m_Commands;

	// A gizmo drag is one undo step, not one per frame. The transform is
	// captured on the first frame of the drag and recorded on release.
	bool m_GizmoDragging = false;
	RageV::TransformComponent m_GizmoBefore;
	RageV::SceneEnvironment m_AmbientBefore;
	// The scene renders here; the viewport panel samples it. Replaces the
	// GL-only FrameBuffer so the same code works on either backend.
	RageV::RHI::Ref<RageV::RHI::RHIRenderTarget> m_SceneTarget;
	// The game view draws the same scene through its camera, so it needs its
	// own target: the two panels are different sizes and therefore different
	// aspect ratios.
	RageV::RHI::Ref<RageV::RHI::RHIRenderTarget> m_GameTarget;
	std::shared_ptr<RageV::Scene> m_Scene;

	// One graph per viewport rather than one shared. Each keeps its own pool of
	// intermediates, and a shared pool would hand the game view whatever the
	// scene view had just finished with -- at the scene view's resolution.
	std::unique_ptr<RageV::RenderGraph> m_Graph;
	std::unique_ptr<RageV::RenderGraph> m_GameGraph;

	// What both viewport textures are, and what the post chain's last pass
	// writes. ImGui samples them, so they are LDR.
	static constexpr RageV::RHI::Format kViewportFormat = RageV::RHI::Format::R8G8B8A8_UNORM;
	RageV::SceneHierarchyPanel m_SceneHierarchyPanel;
	RageV::ContentBrowserPanel m_ContentBrowser;

	RageV::Vec2 m_ViewportSize = { 0.0f, 0.0f };
	RageV::Vec2 m_GameViewportSize = { 0.0f, 0.0f };

	// Panels run after OnUpdate has already recorded a render pass into these
	// targets, and resizing one destroys the images the command buffer holds.
	// So a panel records the size it wants and OnUpdate applies it before
	// recording anything.
	RageV::Vec2 m_RequestedViewportSize = { 0.0f, 0.0f };
	RageV::Vec2 m_RequestedGameSize = { 0.0f, 0.0f };
	void ApplyPendingResizes();
	void DrawColliderOverlay();
	void HandleViewportPicking(const ImVec2& imageOrigin, const ImVec2& imageSize);
	bool m_IsViewportFocused = false, m_IsViewportHovered = false;

	// --- the game UI's pointer ----------------------------------------------
	//
	// A shipped game's UI layer is the window, so the runtime hands the cursor
	// straight through. Here the same layer is an image inside a docked panel
	// that can be moved, resized and scaled, and there are two panels that show
	// it -- so the position has to be mapped, and one of them has to win.
	//
	// Captured while a panel is drawn, which is where its geometry is known,
	// and consumed at the top of the next OnUpdate -- the same place in the
	// frame the runtime consumes its own, so a button behaves identically in
	// both rather than nearly identically.
	void CapturePointer(const ImVec2& imageOrigin, const ImVec2& imageSize,
						const RageV::Vec2& layerSize, bool hovered);

	RageV::UI::PointerInput m_Pointer;
	RageV::Vec2 m_PointerLayer = { 0.0f, 0.0f };

	// Panel visibility, driven by the Window menu.
	bool m_ShowHierarchy = true;
	bool m_ShowProperties = true;
	bool m_ShowStatistics = true;
	bool m_ShowRenderSettings = true;
	bool m_ShowViewport = true;
	bool m_ShowGameViewport = true;
	bool m_ShowContentBrowser = true;

	// Which panels are open, across restarts. ImGui's own imgui.ini remembers
	// where a window was docked but not whether the editor submits it -- that
	// is these bools, which would otherwise reset to their defaults on every
	// launch. Closing Build Log by restarting was the report that exposed it.
	void LoadPanelState();
	void SavePanelState();
	void SetTheme(RageV::EditorTheme::Theme theme);

	// Dock sizes are stored in pixels, so a resize would hand the whole
	// difference to the central viewport. Scaling the tree keeps proportions;
	// the reference size persists so it also works across runs.
	void RescaleDockTree(ImGuiDockNode* node, const ImVec2& scale);
	ImVec2 m_LastDockSize{ 0.0f, 0.0f };
	bool m_ResetLayoutRequested = false;

	// Collider wireframes in the scene view. Off by default: it is a diagnostic
	// overlay, and a scene that always draws one is a scene nobody looks at
	// properly. Scene view only -- the game view is meant to be what a player
	// would see.
	bool m_ShowColliders = false;

	// The ground grid. On by default, and the opposite call from the colliders
	// above: this is not a diagnostic, it is the floor. Without one a scene with
	// nothing in it is an empty gradient with no sense of scale, no horizon and
	// no way to tell which way the camera is pointing. Scene view only, for the
	// same reason the colliders are.
	bool m_ShowGrid = true;

	// The grid, in the current theme's colours. Built per frame rather than
	// stored, so switching theme moves the axes with everything else and there
	// is no second copy of them to forget to update.
	RageV::ViewportGridSettings GridSettings() const;

	// The viewport marks on entities with no geometry -- lights, cameras,
	// probes, audio sources. Scene view only, like the grid and the colliders,
	// and on by default: without them those entities cannot be clicked at all,
	// so this is closer to the floor than to a diagnostic.
	bool m_ShowIcons = true;

	// Built per frame like the grid's, and for the same reason -- the selected
	// entity and the theme both move underneath it.
	RageV::EditorIconSettings IconSettings() const;


	// Whether the game panel was actually visible last frame. A panel that is
	// collapsed or behind another tab still has m_ShowGameViewport set, and
	// rendering the scene a second time for something nobody can see is a whole
	// extra pass wasted.
	bool m_GameViewportVisible = false;
	bool m_ShowDemoWindow = false;
	bool m_ShowAbout = false;

	// Which theme is on. Remembered in panels.ini and overridable with
	// --theme, so a screenshot of either can be taken without clicking.
	// Dark is the default because that is what the engine is designed around
	// and what an editor is usually run in; light exists because a dark UI in
	// a bright room is its own kind of unreadable.
	RageV::EditorTheme::Theme m_Theme = RageV::EditorTheme::Theme::Dark;

	// The content browser's folder, read from panels.ini before the asset
	// registry exists. It cannot be handed to the panel until the registry is
	// up -- the panel resolves it against the asset root -- so it waits here
	// for the first frame rather than being applied where it is read.
	std::string m_PendingContentFolder;

	// The backend picker. The switch cannot happen in place -- the window is
	// created differently per backend -- so a change records a preference and
	// asks whether to restart now.
	bool m_ShowBackendRestart = false;
	RageV::RHI::Backend m_PendingBackend = RageV::RHI::Backend::Vulkan;
	// The preference is written once per popup, not once per frame it is drawn.
	bool m_BackendSaveAttempted = false;
	bool m_BackendSaved = false;

	// Gizmo mode is state now rather than being recomputed from the keyboard
	// every frame, so the toolbar and the shortcuts drive the same value.
	ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
	bool m_GizmoLocal = true;
	bool m_SnapEnabled = false;
	float m_SnapTranslate = 0.5f;
	float m_SnapRotate = 15.0f;
	float m_SnapScale = 0.25f;

	// Render settings.
	bool m_VSync = true;
	bool m_Wireframe = false;
	RageV::Vec3 m_ClearColor = { 0.1f, 0.1f, 0.1f };

	// Rolling frame-time average for the statistics panel.
	float m_FrameTimeMs = 0.0f;
	float m_FrameTimeAccum = 0.0f;
	int   m_FrameTimeSamples = 0;
	// Seconds since the readout and the graph were last updated. Time rather
	// than frames, so the rate they change at does not depend on the rate the
	// engine runs at.
	float m_ReadoutElapsed = 0.0f;
	float m_HistoryElapsed = 0.0f;
	float m_FrameHistory[120] = {};
	int   m_FrameHistoryIndex = 0;
};
