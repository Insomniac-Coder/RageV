#include "EditorLayer.h"
#include "EditorLayer.h"
#include "imgui.h"
#include "RageV/Utils/PlatformUtils.h"
#include "ImGuizmo.h"
#include "glm/gtc/type_ptr.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"

EditorLayer::EditorLayer() : Layer("Renderer2D"), m_CameraController(1270.f / 720.f, true), m_Color(1.0f, 0.0f, 0.0f) {

}

void EditorLayer::OnAttach()
{
	m_Texture = RageV::Texture2D::Create("assets/textures/square.png");

	RageV::FrameBufferData fbdata;
	fbdata.Width = 1280;
	fbdata.Height = 720;

	m_FrameBuffer = RageV::FrameBuffer::Create(fbdata);
	m_Scene = std::make_shared<RageV::Scene>();
	//auto entity = m_Scene->CreateEntity("Test Square");
	//entity.AddComponent<RageV::ColorComponent>(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	//m_Entity = entity;
	//
	//auto camera = m_Scene->CreateEntity("Scene camera");
	//camera.AddComponent<RageV::CameraComponent>();
	//
	//class CameraController : public RageV::ScriptableEntity {
	//public:
	//	void OnCreate()
	//	{
	//		std::cout << "Hello!" << std::endl;
	//	}
	//	void OnUpdate(RageV::Timestep ts)
	//	{
	//		float speed = 5.0f;
	//		auto& position = GetComponent<RageV::TransformComponent>().Position;
	//		if (RageV::Input::IsKeyPressed(RV_KEY_A))
	//			position.x += speed * ts;
	//		if (RageV::Input::IsKeyPressed(RV_KEY_D))
	//			position.x -= speed * ts;
	//		if (RageV::Input::IsKeyPressed(RV_KEY_W))
	//			position.y -= speed * ts;
	//		if (RageV::Input::IsKeyPressed(RV_KEY_S))
	//			position.y += speed * ts;
	//	}
	//};
	//
	//camera.AddComponent<RageV::NativeScriptComponent>().Bind<CameraController>();

	//RageV::SceneSerializer serializer(m_Scene);
	//
	////serializer.Serialize("assets/scenes/test.rage");
	//serializer.Deserialize("assets/scenes/test.rage");

	m_SceneHierarchyPanel.SetSceneRef(m_Scene);
	auto& camera = m_Scene->CreateEntity("Scene Camera");
	camera.AddComponent<RageV::CameraComponent>();
	auto& camComponent = camera.GetComponent<RageV::CameraComponent>();
	camComponent.Camera.SetProjectionType(RageV::SceneCamera::ProjectionType::Perspective);
}

void EditorLayer::OnUpdate(RageV::Timestep ts)
{
	PROFILER("On Update");
	//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
	if (m_IsViewportFocused)
		m_CameraController.OnUpdate(ts);

	m_FrameBuffer->Bind();

	RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	RageV::RenderCommand::Clear();
	m_Scene->OnUpdate(ts);
		
	m_FrameBuffer->UnBind();
}

void EditorLayer::OnImGuiRender()
{
	static bool p_open = true;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
	
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", &p_open, window_flags);
	ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);
	
	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowMinSize.x = 500.0f;
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Menu"))
		{
			if (ImGui::MenuItem("New", "Ctrl+N"))
			{
				NewScene();
			}
			if (ImGui::MenuItem("Open...", "Ctrl+O"))
			{
				OpenScene();
			}
			if (ImGui::MenuItem("Save As...", "Ctrl+S"))
			{
				SaveScene();
			}
			if (ImGui::MenuItem("Exit"))
				RageV::Application::Get().Close();
			ImGui::EndMenu();
		}
	
		ImGui::EndMenuBar();
	}
	
	ImGui::End();
	
	ImGui::Begin("Info Box");
	//ImGui::ColorEdit3("Color,", &m_Color[0]);
	ImGui::Text("DrawCalls: %d", RageV::Renderer2D::GetDrawCallCount());
	ImGui::Text("Quad Count: %d", RageV::Renderer2D::GetQuadCount());
	ImGui::Text("Vertices Count: %d", RageV::Renderer2D::GetVerticesCount());
	ImGui::Text("Indices Count: %d", RageV::Renderer2D::GetIndiciesCount());
	ImGui::Text("Graphics Card: %s", RageV::GraphicsInformation::GetGraphicsInfo().GPUName.c_str());
	ImGui::Text("API Name: %s", RageV::GraphicsInformation::GetGraphicsInfo().APIName.c_str());
	ImGui::End();
	
	m_SceneHierarchyPanel.OnImGuiRender();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene Viewport");
	m_IsViewportFocused = ImGui::IsWindowFocused();
	m_IsViewportHovered = ImGui::IsWindowHovered();
	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	RageV::Application::Get().GetImGuiLayer()->SetEventBlocker(!m_IsViewportFocused && !m_IsViewportHovered);
	if (m_ViewportSize != *((glm::vec2*)&viewportSize))
	{
		m_ViewportSize = { viewportSize.x, viewportSize.y };
		m_FrameBuffer->Resize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	};
	unsigned int id = m_FrameBuffer->GetColorAttachment();
	ImGui::Image((void*)id, ImVec2{ viewportSize.x, viewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
	m_Scene->OnViewportResize(viewportSize.x, viewportSize.y);
	//m_CameraController.OnResize(viewportSize.x, viewportSize.y);

	//ImGuizmo stuff
	RageV::Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
	if (selected)
	{
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);

		auto cameraEntity = m_Scene->GetPrimaryCameraEntity();
		const auto& camera = cameraEntity.GetComponent<RageV::CameraComponent>();

		glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<RageV::TransformComponent>().GetTransform());
		const glm::mat4 camProjection = camera.Camera.GetProjection();

		auto& tc = selected.GetComponent<RageV::TransformComponent>();
		glm::mat4 transform = tc.GetTransform();

		ImGuizmo::OPERATION type = ImGuizmo::OPERATION::TRANSLATE;

		if (RageV::Input::IsKeyPressed(RV_KEY_LEFT_CONTROL) || RageV::Input::IsKeyPressed(RV_KEY_RIGHT_CONTROL))
		{
			if (RageV::Input::IsKeyPressed(RV_KEY_Q))
			{
				type = ImGuizmo::OPERATION::ROTATE;
			}
			if (RageV::Input::IsKeyPressed(RV_KEY_E))
			{
				type = ImGuizmo::OPERATION::SCALE;
			}
		}

		ImGuizmo::Manipulate(glm::value_ptr(cameraView),glm::value_ptr(camProjection), type, ImGuizmo::LOCAL, glm::value_ptr(transform));

		if (ImGuizmo::IsUsing())
		{
			glm::vec3 position, scale, skew;
			glm::quat rotation;
			glm::vec4 t;
			glm::decompose(transform, scale, rotation, position, skew, t);
			glm::vec3 euler = glm::eulerAngles(rotation);
			glm::vec3 diff = euler - tc.Rotation;
			tc.Position = position;
			tc.Rotation += diff;
			tc.Scale = scale;
		}
	}

	ImGui::End();
	ImGui::PopStyleVar();
	
	m_ProfileDataList.clear();
}

void EditorLayer::OnEvent(RageV::Event& e)
{
	m_CameraController.OnEvent(e);

	RageV::EventDispatcher dispatcher(e);

	dispatcher.Dispatch<RageV::KeyPressedEvent>(RV_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
}

bool EditorLayer::OnKeyPressed(RageV::KeyPressedEvent& e)
{
	if (e.GetRepeatCount() > 0)
		return false;

	bool control = RageV::Input::IsKeyPressed(RV_KEY_LEFT_CONTROL) || RageV::Input::IsKeyPressed(RV_KEY_RIGHT_CONTROL);
	//bool shift = RageV::Input::IsKeyPressed(RV_KEY_LEFT_SHIFT) || RageV::Input::IsKeyPressed(RV_KEY_RIGHT_SHIFT);

	switch (e.GetKeyCode())
	{
		case RV_KEY_N:
		{
			if (control)
				NewScene();
			break;
		}
		case RV_KEY_O:
		{
			if (control)
				OpenScene();
			break;
		}
		case RV_KEY_S:
		{
			if (control)
				SaveScene();
			break;
		}
	}
}

void EditorLayer::NewScene()
{
	m_Scene = std::make_shared<RageV::Scene>();
	m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	auto& camera = m_Scene->CreateEntity("Scene Camera");
	camera.AddComponent<RageV::CameraComponent>();
	auto& camComponent = camera.GetComponent<RageV::CameraComponent>();
	camComponent.Camera.SetProjectionType(RageV::SceneCamera::ProjectionType::Perspective);
	m_SceneHierarchyPanel.SetSceneRef(m_Scene);
}

void EditorLayer::OpenScene()
{
	std::string filepath = RageV::FileDialogs::OpenFile("RageV Scene (*.rage)\0*.rage\0");
	if (!filepath.empty())
	{
		m_Scene = std::make_shared<RageV::Scene>();
		m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
		m_SceneHierarchyPanel.SetSceneRef(m_Scene);

		RageV::SceneSerializer serializer(m_Scene);
		serializer.Deserialize(filepath);
	}
}

void EditorLayer::SaveScene()
{
	std::string filepath = RageV::FileDialogs::SaveFile("RageV Scene (*.rage)\0*.rage\0");
	if (!filepath.empty())
	{
		RageV::SceneSerializer serializer(m_Scene);
		serializer.Serialize(filepath);
	}
}
