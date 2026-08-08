#pragma once
#include <RageV.h>
#include <chrono>
#include "UI/SceneHierarchyPanel.h"
#include "UI/ContentBrowserPanel.h"
#include "RageV/Scene/SceneCommands.h"
// ImGuizmo.h does not include imgui.h itself and relies on it being included
// first.
#include "imgui.h"
#include "ImGuizmo.h"

class EditorLayer : public RageV::Layer
{
public:
	EditorLayer();

	void OnAttach() override;
	void OnUpdate(RageV::Timestep ts) override;
	void OnFixedUpdate(RageV::Timestep dt) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;
	bool OnKeyPressed(RageV::KeyPressedEvent& e);

	void NewScene();
	void OpenScene();
	void OpenSceneFile(const std::filesystem::path& filepath);
	void SaveScene();
	void OpenProject();
	void SetStartSceneToCurrent();
	void BuildGame();
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
	static constexpr int kLayoutVersion = 1;
	void DrawAboutPopup();
	void DrawGizmo();

	// --- Entity creation ----------------------------------------------------
	RageV::Entity CreateEmpty(const std::string& name);
	RageV::Entity CreateQuad();
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
	RageV::SceneHierarchyPanel m_SceneHierarchyPanel;
	RageV::ContentBrowserPanel m_ContentBrowser;

	glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
	glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };

	// Panels run after OnUpdate has already recorded a render pass into these
	// targets, and resizing one destroys the images the command buffer holds.
	// So a panel records the size it wants and OnUpdate applies it before
	// recording anything.
	glm::vec2 m_RequestedViewportSize = { 0.0f, 0.0f };
	glm::vec2 m_RequestedGameSize = { 0.0f, 0.0f };
	void ApplyPendingResizes();
	void DrawColliderOverlay();
	void HandleViewportPicking(const ImVec2& imageOrigin, const ImVec2& imageSize);
	bool m_IsViewportFocused = false, m_IsViewportHovered = false;

	// Panel visibility, driven by the Window menu.
	bool m_ShowHierarchy = true;
	bool m_ShowProperties = true;
	bool m_ShowStatistics = true;
	bool m_ShowRenderSettings = true;
	bool m_ShowViewport = true;
	bool m_ShowGameViewport = true;
	bool m_ShowContentBrowser = true;
	bool m_ResetLayoutRequested = false;

	// Collider wireframes in the scene view. Off by default: it is a diagnostic
	// overlay, and a scene that always draws one is a scene nobody looks at
	// properly. Scene view only -- the game view is meant to be what a player
	// would see.
	bool m_ShowColliders = false;

	// Whether the game panel was actually visible last frame. A panel that is
	// collapsed or behind another tab still has m_ShowGameViewport set, and
	// rendering the scene a second time for something nobody can see is a whole
	// extra pass wasted.
	bool m_GameViewportVisible = false;
	bool m_ShowDemoWindow = false;
	bool m_ShowAbout = false;

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
	glm::vec3 m_ClearColor = { 0.1f, 0.1f, 0.1f };

	// Rolling frame-time average for the statistics panel.
	float m_FrameTimeMs = 0.0f;
	float m_FrameTimeAccum = 0.0f;
	int   m_FrameTimeSamples = 0;
	float m_FrameHistory[120] = {};
	int   m_FrameHistoryIndex = 0;
};
