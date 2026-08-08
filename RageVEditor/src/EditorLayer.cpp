#include "EditorLayer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "UI/EditorTheme.h"
#include "RageV/Utils/PlatformUtils.h"
#include "ImGuizmo.h"
#include "glm/gtc/type_ptr.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"
#include "RageV/Renderer/Chunk.h"

using namespace RageV;

namespace
{
	// Menu entries whose feature does not exist yet are shown disabled with an
	// explanation rather than omitted or, worse, wired to nothing. A menu that
	// lies about what the engine can do is harder to use than one that admits
	// a gap.
	void PendingMenuItem(const char* label, const char* reason)
	{
		ImGui::BeginDisabled();
		ImGui::MenuItem(label);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", reason);
	}

	void HelpMarker(const char* text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// A label/value row used throughout the panels.
	void StatRow(const char* label, const std::string& value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled("%s", label);
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(value.c_str());
	}
}

EditorLayer::EditorLayer()
	: Layer("EditorLayer"), m_CameraController(1270.f / 720.f, true)
{
}

void EditorLayer::OnAttach()
{
	EditorTheme::Apply();

	auto& device = Renderer::GetDevice();

	RHI::RenderTargetDesc targetDesc;
	targetDesc.Width = 1280;
	targetDesc.Height = 720;
	targetDesc.ColorAttachments = { { RHI::Format::R8G8B8A8_UNORM } };
	targetDesc.HasDepth = true;
	targetDesc.DepthAttachment.Format = RHI::Format::D32_SFLOAT;
	targetDesc.DebugName = "SceneViewport";
	m_SceneTarget = device.CreateRenderTarget(targetDesc);

	// Pipelines bake their attachment formats, so the renderer has to be told
	// what it is drawing into before the first frame.
	Renderer2D::SetTargetFormats(targetDesc.ColorAttachments[0].Format,
								 targetDesc.DepthAttachment.Format);

	NewScene();
}

// -----------------------------------------------------------------------------
// Entity creation
// -----------------------------------------------------------------------------
Entity EditorLayer::CreateEmpty(const std::string& name)
{
	Entity entity = m_Scene->CreateEntity(name);
	m_SceneHierarchyPanel.SetSelectedEntity(entity);
	return entity;
}

Entity EditorLayer::CreateQuad()
{
	Entity entity = CreateEmpty("Quad");
	entity.AddComponent<ColorComponent>(glm::vec4(0.8f, 0.8f, 0.82f, 1.0f));
	return entity;
}

Entity EditorLayer::CreateLight(Light::LightType type)
{
	const char* name = type == Light::LightType::Directional ? "Directional Light"
					 : type == Light::LightType::Point       ? "Point Light"
															 : "Spot Light";

	Entity entity = CreateEmpty(name);
	auto& light = entity.AddComponent<LightComponent>();
	light.Light.SetLightType(type);

	// Off the origin and angled down, so a new light does something visible
	// rather than sitting inside whatever it is meant to illuminate.
	auto& transform = entity.GetComponent<TransformComponent>();
	transform.Position = { 2.0f, 3.0f, 2.0f };
	if (type == Light::LightType::Directional)
		transform.Rotation = glm::radians(glm::vec3(-45.0f, -30.0f, 0.0f));

	return entity;
}

Entity EditorLayer::CreateCamera()
{
	Entity entity = CreateEmpty("Camera");
	auto& camera = entity.AddComponent<CameraComponent>();
	camera.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
	return entity;
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------
void EditorLayer::OnUpdate(Timestep ts)
{
	// Rolling average; the instantaneous value is too noisy to read.
	m_FrameTimeAccum += ts.GetMilliSeconds();
	m_FrameTimeSamples++;
	if (m_FrameTimeSamples >= 10)
	{
		m_FrameTimeMs = m_FrameTimeAccum / (float)m_FrameTimeSamples;
		m_FrameTimeAccum = 0.0f;
		m_FrameTimeSamples = 0;

		m_FrameHistory[m_FrameHistoryIndex] = m_FrameTimeMs;
		m_FrameHistoryIndex = (m_FrameHistoryIndex + 1) % IM_ARRAYSIZE(m_FrameHistory);
	}

	if (m_IsViewportFocused)
		m_CameraController.OnUpdate(ts);

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd)
		return;

	RHI::RenderPassBeginInfo pass;
	pass.Target = m_SceneTarget.get();
	pass.Clear.Color[0] = m_ClearColor.r;
	pass.Clear.Color[1] = m_ClearColor.g;
	pass.Clear.Color[2] = m_ClearColor.b;
	pass.Clear.Color[3] = 1.0f;

	cmd->PushDebugGroup("Scene");
	cmd->BeginRenderPass(pass);

	m_Scene->OnUpdate(ts);

	cmd->EndRenderPass();
	cmd->PopDebugGroup();
}

void EditorLayer::OnImGuiRender()
{
	// --- dockspace host -----------------------------------------------------
	static bool open = true;
	ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
								 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
								 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
								 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##EditorDockSpace", &open, hostFlags);
	ImGui::PopStyleVar(3);

	ImGuiStyle& style = ImGui::GetStyle();
	const float minWindowSize = style.WindowMinSize.x;
	style.WindowMinSize.x = 280.0f;
	ImGui::DockSpace(ImGui::GetID("EditorDockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	style.WindowMinSize.x = minWindowSize;

	DrawMenuBar();
	DrawToolbar();

	ImGui::End();

	// --- panels -------------------------------------------------------------
	m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchy, &m_ShowProperties);

	if (m_ShowStatistics)      DrawStatisticsPanel();
	if (m_ShowRenderSettings)  DrawRenderSettingsPanel();
	if (m_ShowViewport)        DrawViewportPanel();
	if (m_ShowDemoWindow)      ImGui::ShowDemoWindow(&m_ShowDemoWindow);

	DrawAboutPopup();
}

void EditorLayer::DrawMenuBar()
{
	if (!ImGui::BeginMenuBar())
		return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))   NewScene();
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) OpenScene();
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+S")) SaveScene();
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", "Alt+F4")) Application::Get().Close();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		PendingMenuItem("Undo", "No undo stack yet. Scene edits are applied directly to the registry.");
		PendingMenuItem("Redo", "No undo stack yet.");
		ImGui::Separator();
		Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		ImGui::BeginDisabled(!selected);
		if (ImGui::MenuItem("Delete Selected", "Del"))
		{
			m_Scene->DeleteEntity(selected);
			m_SceneHierarchyPanel.SetSelectedEntity({});
		}
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Entity"))
	{
		if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) CreateEmpty("Entity");

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::MenuItem("Quad")) CreateQuad();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional Light")) CreateLight(Light::LightType::Directional);
			if (ImGui::MenuItem("Point Light"))       CreateLight(Light::LightType::Point);
			if (ImGui::MenuItem("Spot Light"))        CreateLight(Light::LightType::Spot);
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Camera")) CreateCamera();

		ImGui::Separator();
		if (ImGui::MenuItem("Generate Terrain Chunk")) Generate();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowHierarchy);
		ImGui::MenuItem("Properties",      nullptr, &m_ShowProperties);
		ImGui::MenuItem("Viewport",        nullptr, &m_ShowViewport);
		ImGui::MenuItem("Statistics",      nullptr, &m_ShowStatistics);
		ImGui::MenuItem("Render Settings", nullptr, &m_ShowRenderSettings);
		ImGui::Separator();
		ImGui::MenuItem("ImGui Demo",      nullptr, &m_ShowDemoWindow);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("About RageV")) m_ShowAbout = true;
		ImGui::EndMenu();
	}

	// Right-aligned backend readout: which API this process is running is the
	// single most useful thing to see at a glance while both are supported.
	if (Renderer::HasDevice())
	{
		const auto& caps = Renderer::GetDevice().GetCaps();
		const char* backend = Renderer::GetDevice().GetBackend() == RHI::Backend::Vulkan ? "Vulkan" : "OpenGL";
		const float width = ImGui::CalcTextSize(backend).x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width);
		ImGui::TextColored(EditorTheme::Color::Accent, "%s", backend);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\n%s\n\nRestart with --rhi=vulkan|opengl to switch.",
							  caps.DeviceName.c_str(), caps.APIName.c_str());
	}

	ImGui::EndMenuBar();
}

void EditorLayer::DrawToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
	ImGui::BeginChild("##Toolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 10.0f), ImGuiChildFlags_None,
					  ImGuiWindowFlags_NoScrollbar);

	// The active mode is the accent: it is the state currently in effect.
	auto ModeButton = [&](const char* label, ImGuizmo::OPERATION op, const char* tip)
	{
		const bool active = m_GizmoOperation == op;
		if (active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Color::Accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Color::AccentHover);
		}
		if (ImGui::Button(label, ImVec2(34.0f, 0.0f)))
			m_GizmoOperation = op;
		if (active)
			ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tip);
		ImGui::SameLine();
	};

	ModeButton("Mov", ImGuizmo::OPERATION::TRANSLATE, "Translate (W)");
	ModeButton("Rot", ImGuizmo::OPERATION::ROTATE,    "Rotate (E)");
	ModeButton("Scl", ImGuizmo::OPERATION::SCALE,     "Scale (R)");

	ImGui::Dummy(ImVec2(8.0f, 0.0f));
	ImGui::SameLine();

	if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52.0f, 0.0f)))
		m_GizmoLocal = !m_GizmoLocal;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Gizmo space. Scaling is always local.");
	ImGui::SameLine();

	ImGui::Dummy(ImVec2(8.0f, 0.0f));
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &m_SnapEnabled);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Hold Ctrl while dragging for the same effect.");

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void EditorLayer::DrawStatisticsPanel()
{
	if (!ImGui::Begin("Statistics", &m_ShowStatistics))
	{
		ImGui::End();
		return;
	}

	const float fps = m_FrameTimeMs > 0.0f ? 1000.0f / m_FrameTimeMs : 0.0f;
	ImGui::Text("%.2f ms", m_FrameTimeMs);
	ImGui::SameLine();
	ImGui::TextDisabled("(%.0f FPS)", fps);

	ImGui::PlotLines("##FrameTimes", m_FrameHistory, IM_ARRAYSIZE(m_FrameHistory),
					 m_FrameHistoryIndex, nullptr, 0.0f, FLT_MAX,
					 ImVec2(-1.0f, 48.0f));

	ImGui::SeparatorText("Renderer");
	if (ImGui::BeginTable("##RendererStats", 2, ImGuiTableFlags_SizingStretchProp))
	{
		StatRow("Draw calls", std::to_string(Renderer2D::GetDrawCallCount()));
		StatRow("Quads",      std::to_string(Renderer2D::GetQuadCount()));
		StatRow("Vertices",   std::to_string(Renderer2D::GetVerticesCount()));
		StatRow("Indices",    std::to_string(Renderer2D::GetIndiciesCount()));
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Device");
	if (Renderer::HasDevice())
	{
		const auto& caps = Renderer::GetDevice().GetCaps();
		if (ImGui::BeginTable("##DeviceStats", 2, ImGuiTableFlags_SizingStretchProp))
		{
			StatRow("GPU", caps.DeviceName);
			StatRow("API", caps.APIName);
			if (caps.VideoMemoryBytes > 0)
				StatRow("VRAM", std::to_string(caps.VideoMemoryBytes / (1024 * 1024)) + " MB");
			StatRow("Frames in flight", std::to_string(Renderer::GetDevice().GetFramesInFlight()));
			StatRow("Max texture size", std::to_string(caps.MaxTextureSize));
			StatRow("Texture slots", std::to_string(caps.MaxTextureSlots));
			ImGui::EndTable();
		}
	}

	ImGui::End();
}

void EditorLayer::DrawRenderSettingsPanel()
{
	if (!ImGui::Begin("Render Settings", &m_ShowRenderSettings))
	{
		ImGui::End();
		return;
	}

	ImGui::SeparatorText("Presentation");

	if (ImGui::Checkbox("VSync", &m_VSync) && Renderer::HasDevice())
		Renderer::GetDevice().SetVSync(m_VSync);
	HelpMarker("Vulkan swaps present mode between FIFO and mailbox; OpenGL sets the swap interval.");

	ImGui::SeparatorText("Debug");

	if (ImGui::Checkbox("Wireframe", &m_Wireframe))
		Renderer2D::SetWireframe(m_Wireframe);
	HelpMarker("Rebuilds the pipeline with a line polygon mode. Polygon mode is baked into "
			   "the pipeline on Vulkan, so this is not a free state toggle.");

	ImGui::ColorEdit3("Clear colour", glm::value_ptr(m_ClearColor));

	ImGui::SeparatorText("Lighting");

	// Shadows are not implemented. A toggle wired to nothing would be worse
	// than no toggle, so it is present, disabled, and says why.
	bool shadows = false;
	ImGui::BeginDisabled();
	ImGui::Checkbox("Shadows", &shadows);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Not implemented yet.\n\nThe RHI already carries what shadows need -- depth-only\n"
						  "render targets, comparison samplers, slope-scaled depth bias,\n"
						  "cubemap and array textures -- but no shadow pass exists.");

	bool pbr = false;
	ImGui::BeginDisabled();
	ImGui::Checkbox("PBR shading", &pbr);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Not implemented yet.\n\nquad.rvshader is still Blinn-Phong.");

	ImGui::End();
}

void EditorLayer::DrawViewportPanel()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	if (!ImGui::Begin("Viewport", &m_ShowViewport))
	{
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	m_IsViewportFocused = ImGui::IsWindowFocused();
	m_IsViewportHovered = ImGui::IsWindowHovered();
	Application::Get().GetImGuiLayer()->SetEventBlocker(!m_IsViewportFocused && !m_IsViewportHovered);

	const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	if (m_ViewportSize.x != viewportSize.x || m_ViewportSize.y != viewportSize.y)
	{
		m_ViewportSize = { viewportSize.x, viewportSize.y };
		m_SceneTarget->Resize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
		// Only on an actual change: this recomputes every camera's projection
		// and used to run on every single frame.
		m_Scene->OnViewportResize(viewportSize.x, viewportSize.y);
	}

	// GL hands back a texture name, Vulkan a descriptor set the ImGui backend
	// owns; both are opaque to ImGui::Image.
	if (auto color = m_SceneTarget->GetColorTexture(0))
	{
		const ImTextureID handle = (ImTextureID)color->GetImGuiHandle();
		// Vulkan's framebuffer origin is top-left and OpenGL's is bottom-left,
		// so the sampled image needs flipping on GL only.
		const bool flip = Renderer::GetDevice().GetBackend() == RHI::Backend::OpenGL;
		ImGui::Image(handle, viewportSize,
					 ImVec2{ 0, flip ? 1.0f : 0.0f }, ImVec2{ 1, flip ? 0.0f : 1.0f });
	}

	DrawGizmo();

	ImGui::End();
	ImGui::PopStyleVar();
}

void EditorLayer::DrawGizmo()
{
	Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
	if (!selected || !selected.HasComponent<TransformComponent>())
		return;

	Entity cameraEntity = m_Scene->GetPrimaryCameraEntity();
	if (!cameraEntity)
		return;

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
					  (float)ImGui::GetWindowWidth(), (float)ImGui::GetWindowHeight());

	const auto& camera = cameraEntity.GetComponent<CameraComponent>();
	glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());
	const glm::mat4 cameraProjection = camera.Camera.GetProjection();

	auto& tc = selected.GetComponent<TransformComponent>();
	glm::mat4 transform = tc.GetTransform();

	const bool snap = m_SnapEnabled || Input::IsKeyPressed(RV_KEY_LEFT_CONTROL);
	float snapValue = m_SnapTranslate;
	if (m_GizmoOperation == ImGuizmo::OPERATION::ROTATE) snapValue = m_SnapRotate;
	if (m_GizmoOperation == ImGuizmo::OPERATION::SCALE)  snapValue = m_SnapScale;
	const float snapValues[3] = { snapValue, snapValue, snapValue };

	// Scale is meaningless in world space, so force local for it.
	const ImGuizmo::MODE mode = (m_GizmoLocal || m_GizmoOperation == ImGuizmo::OPERATION::SCALE)
							  ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
						 m_GizmoOperation, mode, glm::value_ptr(transform),
						 nullptr, snap ? snapValues : nullptr);

	if (ImGuizmo::IsUsing())
	{
		glm::vec3 position, scale, skew;
		glm::quat rotation;
		glm::vec4 perspective;
		glm::decompose(transform, scale, rotation, position, skew, perspective);

		const glm::vec3 euler = glm::eulerAngles(rotation);
		// Accumulate the delta rather than assigning: decompose cannot
		// distinguish equivalent Euler representations, so assigning directly
		// makes the object snap when a rotation crosses a wrap boundary.
		tc.Rotation += euler - tc.Rotation;
		tc.Position = position;
		tc.Scale = scale;
	}
}

void EditorLayer::DrawAboutPopup()
{
	if (m_ShowAbout)
	{
		ImGui::OpenPopup("About RageV");
		m_ShowAbout = false;
	}

	ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("About RageV", nullptr, ImGuiWindowFlags_NoResize))
		return;

	ImGui::TextColored(EditorTheme::Color::Accent, "RageV Engine");
	ImGui::TextDisabled("A Vulkan/OpenGL renderer with an EnTT scene system.");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (Renderer::HasDevice())
	{
		const auto& caps = Renderer::GetDevice().GetCaps();
		ImGui::TextWrapped("Running on %s", caps.APIName.c_str());
		ImGui::TextWrapped("%s", caps.DeviceName.c_str());
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Switch backend with --rhi=vulkan|opengl,");
	ImGui::TextDisabled("or by editing ragev.ini next to the executable.");
	ImGui::Spacing();

	if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

// -----------------------------------------------------------------------------
// Scene operations
// -----------------------------------------------------------------------------
void EditorLayer::OnEvent(Event& e)
{
	m_CameraController.OnEvent(e);

	EventDispatcher dispatcher(e);
	dispatcher.Dispatch<KeyPressedEvent>(RV_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
}

// Returns true when the shortcut was consumed. Every path used to fall off the
// end of the function without returning.
bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
{
	if (e.GetRepeatCount() > 0)
		return false;

	const bool control = Input::IsKeyPressed(RV_KEY_LEFT_CONTROL) || Input::IsKeyPressed(RV_KEY_RIGHT_CONTROL);
	const bool shift = Input::IsKeyPressed(RV_KEY_LEFT_SHIFT) || Input::IsKeyPressed(RV_KEY_RIGHT_SHIFT);

	switch (e.GetKeyCode())
	{
		case RV_KEY_N:
		{
			if (control && shift) { CreateEmpty("Entity"); return true; }
			if (control)          { NewScene();            return true; }
			break;
		}
		case RV_KEY_O: if (control) { OpenScene(); return true; } break;
		case RV_KEY_S: if (control) { SaveScene(); return true; } break;

		// Gizmo modes, matching the convention most editors use. Ignored while
		// typing into a field.
		case RV_KEY_W: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::TRANSLATE; return true; } break;
		case RV_KEY_E: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::ROTATE;    return true; } break;
		case RV_KEY_R: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::SCALE;     return true; } break;

		case RV_KEY_DELETE:
		{
			if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity())
			{
				m_Scene->DeleteEntity(selected);
				m_SceneHierarchyPanel.SetSelectedEntity({});
				return true;
			}
			break;
		}
	}

	return false;
}

void EditorLayer::NewScene()
{
	m_Scene = std::make_shared<Scene>();
	if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	m_SceneHierarchyPanel.SetSceneRef(m_Scene);

	// A scene with no camera renders nothing, so seed one.
	Entity camera = m_Scene->CreateEntity("Scene Camera");
	auto& cameraComponent = camera.AddComponent<CameraComponent>();
	cameraComponent.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
	camera.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 6.0f };
}

void EditorLayer::OpenScene()
{
	const std::string filepath = FileDialogs::OpenFile("RageV Scene (*.rage)\0*.rage\0");
	if (filepath.empty())
		return;

	m_Scene = std::make_shared<Scene>();
	if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	m_SceneHierarchyPanel.SetSceneRef(m_Scene);

	SceneSerializer serializer(m_Scene);
	serializer.Deserialize(filepath);
}

void EditorLayer::SaveScene()
{
	const std::string filepath = FileDialogs::SaveFile("RageV Scene (*.rage)\0*.rage\0");
	if (filepath.empty())
		return;

	SceneSerializer serializer(m_Scene);
	serializer.Serialize(filepath);
}

void EditorLayer::Generate()
{
	Chunk chunk(0, 0, 1453422, 1349244, 4832123);
	unsigned int counter = 0;

	for (const auto& point : chunk.GetData())
	{
		Entity entity = m_Scene->CreateEntity("TerrainObject" + std::to_string(counter));
		entity.AddComponent<ColorComponent>();

		auto& transform = entity.GetComponent<TransformComponent>();
		transform.Position = { point.x, point.y, point.z };

		switch (point.faceType)
		{
			case FaceType::TOP:    transform.Rotation = glm::radians(glm::vec3(-90.f, 0.f, 0.f));  break;
			case FaceType::BOTTOM: transform.Rotation = glm::radians(glm::vec3(90.f, 0.f, 0.f));   break;
			case FaceType::LEFT:   transform.Rotation = glm::radians(glm::vec3(0.f, -90.f, 0.f));  break;
			case FaceType::RIGHT:  transform.Rotation = glm::radians(glm::vec3(0.f, 90.f, 0.f));   break;
			case FaceType::FRONT:  transform.Rotation = glm::radians(glm::vec3(0.f, 0.f, 0.f));    break;
			case FaceType::BACK:   transform.Rotation = glm::radians(glm::vec3(0.f, 180.f, 0.f));  break;
		}
		counter++;
	}
}
