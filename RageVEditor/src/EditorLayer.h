#pragma once
#include <RageV.h>
#include <chrono>
#include "UI/SceneHierarchyPanel.h"
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
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;
	bool OnKeyPressed(RageV::KeyPressedEvent& e);

	void NewScene();
	void OpenScene();
	void SaveScene();
	void Generate();

private:
	// --- menu bar and panels ------------------------------------------------
	void DrawMenuBar();
	void DrawToolbar();
	void DrawStatisticsPanel();
	void DrawRenderSettingsPanel();
	void DrawViewportPanel();
	void DrawAboutPopup();
	void DrawGizmo();

	// --- Entity creation ----------------------------------------------------
	RageV::Entity CreateEmpty(const std::string& name);
	RageV::Entity CreateQuad();
	RageV::Entity CreateLight(RageV::Light::LightType type);
	RageV::Entity CreateCamera();

private:
	RageV::OrthographicCameraController m_CameraController;
	// The scene renders here; the viewport panel samples it. Replaces the
	// GL-only FrameBuffer so the same code works on either backend.
	RageV::RHI::Ref<RageV::RHI::RHIRenderTarget> m_SceneTarget;
	std::shared_ptr<RageV::Scene> m_Scene;
	RageV::SceneHierarchyPanel m_SceneHierarchyPanel;

	glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
	bool m_IsViewportFocused = false, m_IsViewportHovered = false;

	// Panel visibility, driven by the Window menu.
	bool m_ShowHierarchy = true;
	bool m_ShowProperties = true;
	bool m_ShowStatistics = true;
	bool m_ShowRenderSettings = true;
	bool m_ShowViewport = true;
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
