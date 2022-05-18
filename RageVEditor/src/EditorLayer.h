#pragma once
#include <RageV.h>
#include <chrono>

class EditorLayer : public RageV::Layer
{
public:
	EditorLayer();

	void OnAttach() override;
	void  OnUpdate(RageV::Timestep ts) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;

private:
	glm::vec3 m_Color;
	RageV::OrthographicCameraController m_CameraController;
	std::shared_ptr<RageV::Texture2D> m_Texture;
	std::shared_ptr<RageV::FrameBuffer> m_FrameBuffer;
	std::vector<ProfileData> m_ProfileDataList;
	glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
	float m_Rotation = 0.0f;
};