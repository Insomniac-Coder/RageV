#pragma once
#include <RageV.h>
#include <chrono>
#include "UI/SceneHierarchyPanel.h"

class EditorLayer : public RageV::Layer
{
public:
	EditorLayer();

	void OnAttach() override;
	void  OnUpdate(RageV::Timestep ts) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;
	bool OnKeyPressed(RageV::KeyPressedEvent& e);
	void NewScene();
	void OpenScene();
	void SaveScene();
	void Generate();

private:
	glm::vec3 m_Color;
	RageV::OrthographicCameraController m_CameraController;
	// The scene renders here; the viewport panel samples it. Replaces the
	// GL-only FrameBuffer so the same code works on either backend.
	RageV::RHI::Ref<RageV::RHI::RHIRenderTarget> m_SceneTarget;
	std::shared_ptr<RageV::Scene> m_Scene;
	RageV::Entity m_Entity;
	std::vector<ProfileData> m_ProfileDataList;
	glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
	bool m_IsViewportFocused = false, m_IsViewportHovered = false;
	float m_Rotation = 0.0f;
	RageV::SceneHierarchyPanel m_SceneHierarchyPanel;
};