#include "EditorLayer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "UI/EditorTheme.h"
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "ImGuizmo.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtx/component_wise.hpp"

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
	: Layer("EditorLayer"), m_EditorCamera(45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f)
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

	targetDesc.DebugName = "GameViewport";
	m_GameTarget = device.CreateRenderTarget(targetDesc);

	// Pipelines bake their attachment formats, so the renderer has to be told
	// what it is drawing into before the first frame.
	Renderer::SetTargetFormats(targetDesc.ColorAttachments[0].Format,
							   targetDesc.DepthAttachment.Format);

	// Open on something rather than an empty void. File > New Scene gives a
	// blank scene with just a camera.
	LoadDemoScene();
}

// -----------------------------------------------------------------------------
// Entity creation
// -----------------------------------------------------------------------------
Entity EditorLayer::CreateEmpty(const std::string& name)
{
	const UUID id;
	Entity entity = m_Scene->CreateEntityWithUUID(id, name);
	m_SceneHierarchyPanel.SetSelectedEntity(entity);

	// Recorded rather than executed: the entity exists already, and callers add
	// components to it after this returns. CreateEntityCommand snapshots the
	// subtree at undo time, so redo restores all of that, not a bare entity.
	m_Commands.PushApplied(std::make_unique<CreateEntityCommand>(m_Scene, id, name));
	return entity;
}

Entity EditorLayer::CreateQuad()
{
	Entity entity = CreateEmpty("Quad");
	entity.AddComponent<ColorComponent>(glm::vec4(0.8f, 0.8f, 0.82f, 1.0f));
	return entity;
}

Entity EditorLayer::CreateMesh(PrimitiveType primitive)
{
	Entity entity = CreateEmpty(PrimitiveTypeName(primitive));
	auto& mesh = entity.AddComponent<MeshComponent>(primitive);
	// Its own material rather than the shared default, so editing one object's
	// surface does not change every other object using the default.
	mesh.Material = std::make_shared<Material>(Renderer::GetDevice(), PrimitiveTypeName(primitive));
	return entity;
}

Entity EditorLayer::CreateLight(Light::LightType type)
{
	const char* name = type == Light::LightType::Directional ? "Directional Light"
					 : type == Light::LightType::Point       ? "Point Light"
															 : "Spot Light";

	Entity entity = CreateEmpty(name);
	auto& light = entity.AddComponent<LightComponent>();
	light.Light.Type = type;

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
	camera.Camera.Projection = SceneCamera::ProjectionType::Perspective;
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

	// Navigation only responds over the viewport, so a drag inside a panel
	// does not also fly the camera.
	m_EditorCamera.SetActive(m_IsViewportHovered || m_IsViewportFocused);
	m_EditorCamera.OnUpdate(ts);

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd)
		return;

	RHI::RenderPassBeginInfo pass;
	pass.Target = m_SceneTarget.get();
	pass.Clear.Color[0] = m_ClearColor.r;
	pass.Clear.Color[1] = m_ClearColor.g;
	pass.Clear.Color[2] = m_ClearColor.b;
	pass.Clear.Color[3] = 1.0f;

	// Simulation once, rendering once per view. The play/edit split lands on
	// the first of these; the rest only choose a viewpoint.
	m_Scene->OnUpdate(ts);

	cmd->PushDebugGroup("Scene view");
	cmd->BeginRenderPass(pass);

	if (m_UseEditorCamera)
		m_Scene->OnRenderEditor(m_EditorCamera);
	else
		m_Scene->OnRenderRuntime();

	cmd->EndRenderPass();
	cmd->PopDebugGroup();

	// The same scene again, through whichever camera holds the lowest ViewRank.
	// Drawing a scene twice in one frame is what the renderers' per-batch
	// storage exists for -- with one buffer per frame, this pass would
	// overwrite the data the pass above is about to read.
	if (m_ShowGameViewport)
	{
		RHI::RenderPassBeginInfo gamePass;
		gamePass.Target = m_GameTarget.get();
		gamePass.Clear.Color[0] = m_ClearColor.r;
		gamePass.Clear.Color[1] = m_ClearColor.g;
		gamePass.Clear.Color[2] = m_ClearColor.b;
		gamePass.Clear.Color[3] = 1.0f;

		cmd->PushDebugGroup("Game view");
		cmd->BeginRenderPass(gamePass);
		m_Scene->OnRenderRuntime();
		cmd->EndRenderPass();
		cmd->PopDebugGroup();
	}
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
	if (m_ShowGameViewport)    DrawGameViewportPanel();
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
		ImGui::Separator();
		if (ImGui::MenuItem("Import Model...")) ImportModel();
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+S")) SaveScene();
		ImGui::Separator();
		if (ImGui::MenuItem("Load Demo Scene")) LoadDemoScene();
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", "Alt+F4")) Application::Get().Close();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		const std::string undoLabel = m_Commands.CanUndo() ? "Undo " + m_Commands.UndoName() : "Undo";
		const std::string redoLabel = m_Commands.CanRedo() ? "Redo " + m_Commands.RedoName() : "Redo";

		if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, m_Commands.CanUndo()))
			m_Commands.Undo();
		if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, m_Commands.CanRedo()))
			m_Commands.Redo();
		ImGui::Separator();
		Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		ImGui::BeginDisabled(!selected);
		if (ImGui::MenuItem("Delete Selected", "Del"))
		{
			m_Commands.Push(std::make_unique<DeleteEntityCommand>(m_Scene, selected));
			m_SceneHierarchyPanel.SetSelectedEntity({});
		}
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Entity"))
	{
		if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N")) CreateEmpty("Entity");

		if (ImGui::BeginMenu("3D Object"))
		{
			if (ImGui::MenuItem("Cube"))     CreateMesh(PrimitiveType::Cube);
			if (ImGui::MenuItem("Sphere"))   CreateMesh(PrimitiveType::Sphere);
			if (ImGui::MenuItem("Cylinder")) CreateMesh(PrimitiveType::Cylinder);
			if (ImGui::MenuItem("Plane"))    CreateMesh(PrimitiveType::Plane);
			if (ImGui::MenuItem("Quad"))     CreateMesh(PrimitiveType::Quad);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::MenuItem("Sprite Quad")) CreateQuad();
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
		PendingMenuItem("Generate Terrain Chunk",
						"Moved to experiments/terrain and no longer built.\n\n"
						"It generated one entity per cube face -- thousands per\n"
						"chunk. A real version needs a greedy mesher producing\n"
						"one mesh per chunk. Terrain is a non-goal for now.");
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowHierarchy);
		ImGui::MenuItem("Properties",      nullptr, &m_ShowProperties);
		ImGui::MenuItem("Viewport",        nullptr, &m_ShowViewport);
		ImGui::MenuItem("Game",            nullptr, &m_ShowGameViewport);
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

	// Which camera the viewport draws through. Right-aligned because it
	// describes the view rather than the edit operation.
	{
		const char* label = m_UseEditorCamera ? "Editor Cam" : "Game Cam";
		const float width = 92.0f;
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - width + ImGui::GetCursorPosX());

		if (!m_UseEditorCamera)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Color::Accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Color::AccentHover);
		}
		if (ImGui::Button(label, ImVec2(width, 0.0f)))
			m_UseEditorCamera = !m_UseEditorCamera;
		if (!m_UseEditorCamera)
			ImGui::PopStyleColor(2);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(m_UseEditorCamera
				? "Viewport draws through the editor camera.\n\n"
				  "Alt+LMB orbit, MMB pan, RMB fly (WASD, QE, Shift),\n"
				  "wheel zoom, F frames the selection.\n\n"
				  "Click to preview the scene's primary camera instead."
				: "Viewport draws through the scene's primary camera.\n\n"
				  "Nothing renders if the scene has no camera entity.\n"
				  "Click to return to the editor camera.");
	}

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
		StatRow("Mesh draws", std::to_string(Renderer3D::GetDrawCallCount()));
		StatRow("Triangles",  std::to_string(Renderer3D::GetTriangleCount()));
		StatRow("Quad batches", std::to_string(Renderer2D::GetDrawCallCount()));
		StatRow("Quads",      std::to_string(Renderer2D::GetQuadCount()));
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
		Renderer::SetWireframe(m_Wireframe);
	HelpMarker("Rebuilds the pipeline with a line polygon mode. Polygon mode is baked into "
			   "the pipeline on Vulkan, so this is not a free state toggle.");

	ImGui::ColorEdit3("Clear colour", glm::value_ptr(m_ClearColor));

	ImGui::SeparatorText("Environment");

	// The ambient term used to be two constants inside pbr.rvshader with no way
	// to reach them. It is still a flat approximation of IBL, but it is a value
	// now, and it is stored with the scene rather than with the editor.
	if (m_Scene)
	{
		SceneEnvironment& environment = m_Scene->GetEnvironment();

		// Captured when the widget takes focus and recorded when it lets go,
		// so a drag across the slider is one undo step.
		auto trackAmbient = [&](const char* label)
		{
			if (ImGui::IsItemActivated())
				m_AmbientBefore = environment;

			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const SceneEnvironment before = m_AmbientBefore;
				const SceneEnvironment after = environment;
				std::weak_ptr<Scene> scene = m_Scene;

				m_Commands.PushApplied(std::make_unique<ValueEditCommand>(
					label,
					[scene, after]  { if (auto s = scene.lock()) s->GetEnvironment() = after; },
					[scene, before] { if (auto s = scene.lock()) s->GetEnvironment() = before; }));
			}
		};

		ImGui::ColorEdit3("Ambient colour", glm::value_ptr(environment.AmbientColor));
		trackAmbient("Ambient colour");
		ImGui::DragFloat("Ambient intensity", &environment.AmbientIntensity, 0.005f, 0.0f, 4.0f);
		trackAmbient("Ambient intensity");
		HelpMarker("A single colour arriving from every direction. It cannot vary with view "
				   "angle or roughness the way a real environment does -- image-based lighting "
				   "replaces it, and falls back to it for scenes with no environment map.\n\n"
				   "Set the intensity to 0 for pure direct lighting.");
	}

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

	ImGui::TextDisabled("Shading: Cook-Torrance PBR");
	HelpMarker("Meshes use pbr.rvshader with the metallic-roughness parameterisation. "
			   "Image-based lighting is not in yet, so the ambient term is a flat "
			   "approximation, and tone mapping runs in the shader rather than as a "
			   "dedicated pass over an HDR target.");

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
		m_EditorCamera.SetViewportSize(viewportSize.x, viewportSize.y);
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

// The scene as the player would see it. Same scene, different camera -- so a
// camera can be positioned in one panel while its result is watched in the
// other, which is the entire reason for having two.
void EditorLayer::DrawGameViewportPanel()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	if (!ImGui::Begin("Game", &m_ShowGameViewport))
	{
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	const ImVec2 size = ImGui::GetContentRegionAvail();
	if (size.x > 0.0f && size.y > 0.0f &&
		(m_GameViewportSize.x != size.x || m_GameViewportSize.y != size.y))
	{
		m_GameViewportSize = { size.x, size.y };
		m_GameTarget->Resize((unsigned int)size.x, (unsigned int)size.y);
		// The scene cameras' aspect follows *this* panel: it is what the game
		// actually renders into. Driving it from the editor viewport would give
		// the game view a stretched image whenever the two panels differed.
		m_Scene->OnViewportResize(size.x, size.y);
	}

	Entity camera = m_Scene->GetPrimaryCameraEntity();
	if (!camera)
	{
		ImGui::PopStyleVar();
		ImGui::TextDisabled("No camera in the scene.");
		ImGui::TextDisabled("Entity > Camera adds one.");
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	}
	else if (auto color = m_GameTarget->GetColorTexture(0))
	{
		const ImTextureID handle = (ImTextureID)color->GetImGuiHandle();
		const bool flip = Renderer::GetDevice().GetBackend() == RHI::Backend::OpenGL;
		ImGui::Image(handle, size,
					 ImVec2{ 0, flip ? 1.0f : 0.0f }, ImVec2{ 1, flip ? 0.0f : 1.0f });

		// Which camera won, and why -- otherwise a scene with several cameras
		// gives no clue about what is being looked through.
		ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
		ImGui::TextColored(EditorTheme::Color::Accent, "%s", camera.GetName().c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Rank %d. The lowest ViewRank in the scene wins;\nties break on entity id.",
							  camera.GetComponent<CameraComponent>().ViewRank);
		}
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

void EditorLayer::DrawGizmo()
{
	Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
	if (!selected || !selected.HasComponent<TransformComponent>())
		return;

	// The gizmo has to be projected with whatever the viewport is actually
	// showing, or the handles land somewhere other than the object.
	glm::mat4 cameraView;
	glm::mat4 cameraProjection;

	if (m_UseEditorCamera)
	{
		cameraView = m_EditorCamera.GetView();
		cameraProjection = m_EditorCamera.GetProjection();
	}
	else
	{
		Entity cameraEntity = m_Scene->GetPrimaryCameraEntity();
		if (!cameraEntity)
			return;

		cameraView = glm::inverse(m_Scene->GetWorldTransform(cameraEntity));
		cameraProjection = cameraEntity.GetComponent<CameraComponent>().Camera.GetProjection();
	}

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
					  (float)ImGui::GetWindowWidth(), (float)ImGui::GetWindowHeight());

	// The gizmo manipulates in world space; the component stores local. With a
	// hierarchy those differ, so the result is converted back through the
	// parent before being written.
	auto& tc = selected.GetComponent<TransformComponent>();
	glm::mat4 transform = m_Scene->GetWorldTransform(selected);

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
		// Captured before the first write of the drag, so undo returns to where
		// the object started rather than to one frame into the drag.
		if (!m_GizmoDragging)
		{
			m_GizmoDragging = true;
			m_GizmoBefore = tc;
		}

		// Back into the parent's space before decomposing, or dragging a child
		// would write its world transform into a local field and the object
		// would leap by the parent's transform.
		const glm::mat4 local = glm::inverse(m_Scene->GetParentWorldTransform(selected)) * transform;

		glm::vec3 position, scale, skew;
		glm::quat rotation;
		glm::vec4 perspective;
		glm::decompose(local, scale, rotation, position, skew, perspective);

		const glm::vec3 euler = glm::eulerAngles(rotation);
		// Accumulate the delta rather than assigning: decompose cannot
		// distinguish equivalent Euler representations, so assigning directly
		// makes the object snap when a rotation crosses a wrap boundary.
		tc.Rotation += euler - tc.Rotation;
		tc.Position = position;
		tc.Scale = scale;
	}
	else if (m_GizmoDragging)
	{
		// Released. The transform is already where the user left it, so the
		// command is recorded rather than executed.
		m_GizmoDragging = false;
		m_Commands.PushApplied(std::make_unique<TransformEditCommand>(
			m_Scene, selected.GetUUID(), m_GizmoBefore, tc));
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
	m_EditorCamera.OnEvent(e);

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

		// Ctrl+Shift+Z is the other common redo binding; both are accepted.
		case RV_KEY_Z:
		{
			if (control && shift) { m_Commands.Redo(); return true; }
			if (control)          { m_Commands.Undo(); return true; }
			break;
		}
		case RV_KEY_Y: if (control) { m_Commands.Redo(); return true; } break;
		case RV_KEY_S: if (control) { SaveScene(); return true; } break;

		// Gizmo modes, matching the convention most editors use. Ignored while
		// typing into a field.
		case RV_KEY_W: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::TRANSLATE; return true; } break;
		case RV_KEY_E: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::ROTATE;    return true; } break;
		case RV_KEY_R: if (!control && !ImGui::GetIO().WantTextInput) { m_GizmoOperation = ImGuizmo::OPERATION::SCALE;     return true; } break;

		// Frame the selection, the one navigation shortcut every editor shares.
		case RV_KEY_F: if (!control && !ImGui::GetIO().WantTextInput) { FocusSelection(); return true; } break;

		case RV_KEY_DELETE:
		{
			if (Entity selected = m_SceneHierarchyPanel.GetSelectedEntity())
			{
				m_Commands.Push(std::make_unique<DeleteEntityCommand>(m_Scene, selected));
				m_SceneHierarchyPanel.SetSelectedEntity({});
				return true;
			}
			break;
		}
	}

	return false;
}

void EditorLayer::FocusSelection()
{
	Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
	if (!selected || !selected.HasComponent<TransformComponent>())
	{
		m_EditorCamera.Focus({ 0.0f, 0.0f, 0.0f }, 5.0f);
		return;
	}

	const auto& transform = selected.GetComponent<TransformComponent>();
	// Approximate the object's extent from its scale. Real bounds need mesh
	// AABBs, which arrive with the asset system.
	const float radius = glm::compMax(glm::abs(transform.Scale)) * 1.2f;
	m_EditorCamera.Focus(transform.Position, radius);
}

void EditorLayer::NewScene()
{
	m_Scene = std::make_shared<Scene>();
	if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	m_SceneHierarchyPanel.SetSceneRef(m_Scene);
	m_SceneHierarchyPanel.SetCommandStack(&m_Commands);
	// The recorded commands refer to entities that no longer exist.
	m_Commands.Clear();

	// A scene with no camera renders nothing, so seed one.
	Entity camera = m_Scene->CreateEntity("Scene Camera");
	auto& cameraComponent = camera.AddComponent<CameraComponent>();
	cameraComponent.Camera.Projection = SceneCamera::ProjectionType::Perspective;
	camera.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 6.0f };
}

// A scene worth opening the editor to: a ground plane, a row of primitives and
// two coloured lights from opposite sides so the shading reads as shading
// rather than flat colour.
void EditorLayer::LoadDemoScene()
{
	NewScene();

	Entity camera = m_Scene->GetPrimaryCameraEntity();
	if (camera)
	{
		auto& transform = camera.GetComponent<TransformComponent>();
		transform.Position = { 0.0f, 2.5f, 8.0f };
		transform.Rotation = glm::radians(glm::vec3(-12.0f, 0.0f, 0.0f));
	}

	auto place = [&](PrimitiveType primitive, const glm::vec3& position, const glm::vec3& scale,
					 const glm::vec4& color, float metallic, float roughness, const char* name,
					 Entity parent = {})
	{
		Entity entity = m_Scene->CreateEntity(name);
		auto& mesh = entity.AddComponent<MeshComponent>(primitive);

		mesh.Material = std::make_shared<Material>(Renderer::GetDevice(), name);
		auto& params = mesh.Material->GetParams();
		params.BaseColor = color;
		params.Metallic = metallic;
		params.Roughness = roughness;

		auto& transform = entity.GetComponent<TransformComponent>();
		transform.Position = position;
		transform.Scale = scale;

		// Parented after the transform is set, so SetParent converts the value
		// just written into the parent's space rather than the default.
		if (parent)
			m_Scene->SetParent(entity, parent);

		return entity;
	};

	// Spread across the metallic and roughness axes: a scene where everything
	// shares one surface tells you nothing about whether the BRDF is right.
	place(PrimitiveType::Plane, { 0.0f, -1.0f, 0.0f }, { 20.0f, 1.0f, 20.0f },
		  { 0.14f, 0.14f, 0.16f, 1.0f }, 0.0f, 0.85f, "Ground");

	place(PrimitiveType::Cube,     { -3.0f, 0.0f, 0.0f }, glm::vec3(1.5f),
		  { 0.85f, 0.17f, 0.19f, 1.0f }, 0.0f, 0.35f, "Cube (dielectric)");
	place(PrimitiveType::Sphere,   {  0.0f, 0.1f, 0.0f }, glm::vec3(1.8f),
		  { 0.94f, 0.78f, 0.38f, 1.0f }, 1.0f, 0.20f, "Sphere (gold)");
	place(PrimitiveType::Cylinder, {  3.0f, 0.0f, 0.0f }, glm::vec3(1.4f),
		  { 0.90f, 0.91f, 0.92f, 1.0f }, 1.0f, 0.45f, "Cylinder (brushed metal)");

	// A rough/smooth pair behind, to make the roughness axis visible directly.
	place(PrimitiveType::Sphere, { -1.6f, -0.5f, -3.0f }, glm::vec3(0.9f),
		  { 0.80f, 0.80f, 0.82f, 1.0f }, 0.0f, 0.08f, "Sphere (smooth)");
	place(PrimitiveType::Sphere, {  1.6f, -0.5f, -3.5f }, glm::vec3(0.9f),
		  { 0.80f, 0.80f, 0.82f, 1.0f }, 0.0f, 0.95f, "Sphere (rough)");

	// A parented arrangement, so the hierarchy is exercised by the scene the
	// editor opens on rather than only by the round-trip test. Dragging the
	// pedestal moves the whole stack; the children keep their local offsets.
	Entity pedestal = m_Scene->CreateEntity("Pedestal");
	pedestal.GetComponent<TransformComponent>().Position = { -6.5f, -1.0f, -1.0f };

	place(PrimitiveType::Cylinder, { -6.5f, -0.6f, -1.0f }, { 1.2f, 0.6f, 1.2f },
		  { 0.22f, 0.23f, 0.26f, 1.0f }, 0.0f, 0.7f, "Pedestal Base", pedestal);
	place(PrimitiveType::Cube, { -6.5f, 0.2f, -1.0f }, glm::vec3(0.55f),
		  { 0.85f, 0.17f, 0.19f, 1.0f }, 0.0f, 0.25f, "Pedestal Ornament", pedestal);

	// A warm key light and a cool fill from the opposite side: a single white
	// light flattens everything, which is what makes untextured primitives look
	// like a debug view rather than a scene.
	// Intensities are large because the falloff is inverse-square: a point
	// light of intensity 1 is essentially invisible a few units away.
	Entity key = m_Scene->CreateEntity("Key Light");
	{
		auto& light = key.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Point;
		light.Color = { 1.0f, 0.87f, 0.72f };
		light.Intensity = 120.0f;
		light.Range = 30.0f;
		key.GetComponent<TransformComponent>().Position = { 4.0f, 5.0f, 4.0f };
	}

	Entity fill = m_Scene->CreateEntity("Fill Light");
	{
		auto& light = fill.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Point;
		light.Color = { 0.38f, 0.50f, 0.85f };
		light.Intensity = 60.0f;
		light.Range = 30.0f;
		fill.GetComponent<TransformComponent>().Position = { -5.0f, 3.0f, -2.0f };
	}

	Entity sun = m_Scene->CreateEntity("Sun");
	{
		auto& light = sun.AddComponent<LightComponent>().Light;
		light.Type = Light::LightType::Directional;
		light.Color = { 0.55f, 0.58f, 0.65f };
		light.Intensity = 1.2f;
		sun.GetComponent<TransformComponent>().Rotation = glm::radians(glm::vec3(-55.0f, -30.0f, 0.0f));
	}

	// An imported glTF alongside the generated primitives, so the demo scene
	// exercises the asset path rather than only the built-in one. Absent
	// quietly if the file is not there -- a missing sample should not stop the
	// editor opening.
	if (const AssetHandle model = AssetRegistry::GetHandle("models/testcube.gltf"); model.IsValid())
	{
		if (Entity imported = AssetManager::InstantiateModel(*m_Scene, model))
		{
			auto& transform = imported.GetComponent<TransformComponent>();
			transform.Position = { 6.0f, 0.0f, -1.0f };
			transform.Scale = glm::vec3(1.6f);
		}
	}

	m_SceneHierarchyPanel.SetSelectedEntity({});
	// Loading is not an edit; without this the demo scene arrives with an undo
	// history that would delete parts of itself.
	m_Commands.Clear();
	RV_INFO("Demo scene loaded");
}

// glTF only, and deliberately: it is the one interchange format whose material
// model already matches the renderer's, so an import needs no translation layer
// that can quietly get a channel wrong.
void EditorLayer::ImportModel()
{
	const std::string filepath = FileDialogs::OpenFile("glTF Model (*.gltf;*.glb)\0*.gltf;*.glb\0");
	if (filepath.empty())
		return;

	// Picked up so a file dropped into the folder since startup has a handle.
	AssetRegistry::Refresh();

	const std::filesystem::path absolute = std::filesystem::absolute(filepath);
	std::error_code error;
	const std::filesystem::path relative =
		std::filesystem::relative(absolute, AssetRegistry::Root(), error);

	if (error || relative.empty() || relative.native().rfind(L"..", 0) == 0)
	{
		RV_WARN("'{0}' is outside the assets folder. Copy it in first -- an asset "
				"the registry cannot see has no handle, so nothing referring to it "
				"would survive a reload.", filepath);
		return;
	}

	const AssetHandle handle = AssetRegistry::GetHandle(relative.generic_string());
	if (!handle.IsValid())
	{
		RV_WARN("No asset handle for '{0}'", relative.generic_string());
		return;
	}

	Entity root = AssetManager::InstantiateModel(*m_Scene, handle);
	if (!root)
		return;

	m_SceneHierarchyPanel.SetSelectedEntity(root);
	// One undo step for the whole import: the command snapshots the subtree at
	// undo time, so the entire imported tree comes back on redo.
	m_Commands.PushApplied(std::make_unique<CreateEntityCommand>(m_Scene, root.GetUUID(),
																 root.GetName()));
	FocusSelection();
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
	m_SceneHierarchyPanel.SetCommandStack(&m_Commands);
	m_Commands.Clear();

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

