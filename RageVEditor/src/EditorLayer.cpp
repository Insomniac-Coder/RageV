#include "EditorLayer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "UI/EditorTheme.h"
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Physics/PhysicsDebugDraw.h"
#include "RageV/Scene/ScenePicking.h"
#include "RageV/Project/Project.h"
#include "RageV/Project/ProjectPackager.h"
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Core/EngineConfig.h"
#include "ImGuizmo.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtx/component_wise.hpp"
#include <fstream>

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

	// The scene draws into the graph's linear HDR target, not into these --
	// these are what the finished, tone-mapped image lands in for ImGui to
	// sample. Pipelines bake their attachment formats, so the renderer is told
	// what the *scene* pass writes.
	Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT, RHI::Format::D32_SFLOAT);

	auto& graphDevice = Renderer::GetDevice();
	m_Graph = std::make_unique<RenderGraph>(graphDevice);
	m_GameGraph = std::make_unique<RenderGraph>(graphDevice);

	m_ContentBrowser.SetActivateCallback(
		[this](AssetHandle handle, AssetType type) { OnAssetActivated(handle, type); });

	// Open on the project's start scene -- the one the runtime will run.
	//
	// The editor used to build its own demo in code every time, which meant the
	// two disagreed the moment anyone edited the scene on disk, and disagreed
	// permanently about anything the file carries and a code-built scene does
	// not: most visibly the sky, since a scene constructed in memory has the
	// default environment rather than the project's.
	//
	// The built-in demo stays as the fallback, for a project with no start
	// scene and for no project at all. File > New Scene gives a blank one.
	const std::filesystem::path start =
		Project::GetActive() && !Project::Config().StartScene.empty()
			? Project::AssetPath(Project::Config().StartScene)
			: std::filesystem::path();

	if (!start.empty() && std::filesystem::exists(start))
		OpenSceneFile(start);
	else
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

	// Before anything is recorded: resizing a target destroys its images, and
	// doing that mid-frame invalidates the command buffer that is using them.
	ApplyPendingResizes();

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd)
		return;

	// Presentational per-frame work. Simulation happens on the fixed step in
	// OnFixedUpdate, and only while playing.
	if (m_SceneState == SceneState::Edit)
		m_Scene->OnUpdateEditor(ts);
	else
		m_Scene->OnUpdateRuntime(ts);

	// Before either frame graph, and once for both: a probe capture opens
	// render passes of its own, nothing may do that inside another one, and the
	// two viewports share the scene and so share its probes.
	{
		RV_PROFILE_PHASE(FramePhase::EnvironmentPrefilter);
		m_Scene->PrepareEnvironment();
	}

	{
		RV_PROFILE_PHASE(FramePhase::Probes);
		m_Scene->CaptureReflectionProbes();
	}

	// Cascades are fitted to a frustum, so they belong to whichever camera is
	// about to be drawn. The game view below re-renders them for its own.
	{
		RV_PROFILE_PHASE(FramePhase::Shadows);

		if (m_UseEditorCamera)
			m_Scene->RenderShadows(m_EditorCamera, m_EditorCamera.GetTransform());
		else if (Entity camera = m_Scene->GetPrimaryCameraEntity())
			m_Scene->RenderShadows(camera.GetComponent<CameraComponent>().Camera,
								   camera.GetComponent<TransformComponent>().World);
	}

	// The scene view and the game view are the same frame described twice,
	// differing only in the camera and where the result lands. Both go through
	// BuildFrame, so bloom and tone mapping cannot end up applied to one and
	// not the other -- which is exactly the drift that put two transfer
	// functions in one image before this.
	{
	RV_PROFILE_PHASE(FramePhase::Graph);

	m_Graph->Begin((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

	FrameDesc scene;
	scene.Output = m_Graph->Import(m_SceneTarget, "SceneView");
	scene.Width = (uint32_t)m_ViewportSize.x;
	scene.Height = (uint32_t)m_ViewportSize.y;
	scene.Environment = m_Scene->GetEnvironment();
	scene.ClearColor = glm::vec4(m_ClearColor, 1.0f);
	scene.OutputFormat = kViewportFormat;
	scene.DrawScene = [this](RGPassContext&)
	{
		if (m_UseEditorCamera)
			m_Scene->OnRenderEditor(m_EditorCamera);
		else if (m_ViewportSize.y > 0.0f)
			m_Scene->OnRenderRuntime(m_ViewportSize.x / m_ViewportSize.y);
	};

	if (m_ShowColliders)
		scene.DrawOverlay = [this](RGPassContext&) { DrawColliderOverlay(); };

	BuildFrame(*m_Graph, scene);

	if (m_Graph->Compile())
		m_Graph->Execute(*cmd);
	else
	{
		for (const std::string& error : m_Graph->Errors())
			RV_ERROR("Render graph (scene view): {0}", error);
	}
	}

	// The same scene again, through whichever camera holds the lowest ViewRank.
	// Drawing a scene twice in one frame is what the renderers' per-batch
	// storage exists for -- with one buffer per frame, this pass would
	// overwrite the data the pass above is about to read.
	if (m_ShowGameViewport && m_GameViewportVisible && m_GameViewportSize.y > 0.0f)
	{
		// The second fit, and the reason the editor's shadow cost is double the
		// runtime's while this panel is open. Counted under the same phase, so
		// the report shows the whole of it.
		if (Entity camera = m_Scene->GetPrimaryCameraEntity())
		{
			RV_PROFILE_PHASE(FramePhase::Shadows);
			m_Scene->RenderShadows(camera.GetComponent<CameraComponent>().Camera,
								   camera.GetComponent<TransformComponent>().World);
		}

		RV_PROFILE_PHASE(FramePhase::Graph);

		m_GameGraph->Begin((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);

		FrameDesc game;
		game.Output = m_GameGraph->Import(m_GameTarget, "GameView");
		game.Width = (uint32_t)m_GameViewportSize.x;
		game.Height = (uint32_t)m_GameViewportSize.y;
		game.Environment = m_Scene->GetEnvironment();
		game.ClearColor = glm::vec4(m_ClearColor, 1.0f);
		game.OutputFormat = kViewportFormat;
		game.DrawScene = [this](RGPassContext&)
		{
			m_Scene->OnRenderRuntime(m_GameViewportSize.x / m_GameViewportSize.y);
		};

		BuildFrame(*m_GameGraph, game);

		if (m_GameGraph->Compile())
			m_GameGraph->Execute(*cmd);
		else
		{
			for (const std::string& error : m_GameGraph->Errors())
				RV_ERROR("Render graph (game view): {0}", error);
		}
	}
}

// Click to select, the way every editor works.
//
// A ray through the cursor rather than an id buffer. The id-buffer version --
// render entity ids to a second attachment and read the pixel back -- is
// pixel-exact and costs an extra output in every shader plus a readback path
// in both backends. A ray tested against the triangles it hits is accurate
// enough that the difference is not visible in use, and it also answers
// questions an id buffer cannot: where on the surface the click landed, and
// what is behind the thing in front.
void EditorLayer::HandleViewportPicking(const ImVec2& imageOrigin, const ImVec2& imageSize)
{
	if (imageSize.x <= 0.0f || imageSize.y <= 0.0f)
		return;

	// On release, not press, and only if the mouse has not moved far since:
	// dragging inside the viewport is how the camera is flown, and every one
	// of those drags ends with a release that must not change the selection.
	if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		return;

	if (!m_IsViewportHovered || ImGuizmo::IsOver() || ImGuizmo::IsUsing())
		return;

	// The threshold is in screen pixels and deliberately small. Large enough to
	// forgive the hand moving on the way up, small enough that a deliberate
	// drag is never mistaken for a click.
	constexpr float kClickSlop = 4.0f;
	if (ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x != 0.0f ||
		ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y != 0.0f)
	{
		const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
		if (std::sqrt(drag.x * drag.x + drag.y * drag.y) > kClickSlop)
			return;
	}

	const ImVec2 mouse = ImGui::GetMousePos();
	const glm::vec2 local{ mouse.x - imageOrigin.x, mouse.y - imageOrigin.y };

	if (local.x < 0.0f || local.y < 0.0f || local.x > imageSize.x || local.y > imageSize.y)
		return;

	// Into normalised device coordinates. The y flip is because a window's
	// origin is top-left and clip space's is bottom-left; forgetting it gives
	// picking that works perfectly along the horizontal centre line and is
	// mirrored everywhere else.
	const glm::vec2 ndc{
		(local.x / imageSize.x) * 2.0f - 1.0f,
		1.0f - (local.y / imageSize.y) * 2.0f,
	};

	Ray ray;
	if (m_UseEditorCamera)
	{
		ray = ScreenPointToRay(m_EditorCamera, m_EditorCamera.GetTransform(), ndc);
	}
	else
	{
		Entity camera = m_Scene->GetPrimaryCameraEntity();
		if (!camera)
			return;

		auto& component = camera.GetComponent<CameraComponent>();
		if (!component.fixedAspectRatio)
			component.Camera.SetAspectRatio(imageSize.x / imageSize.y);

		ray = ScreenPointToRay(component.Camera,
							   camera.GetComponent<TransformComponent>().World, ndc);
	}

	// Clicking nothing clears the selection, which is how a click becomes a way
	// to deselect rather than a thing that can only ever select.
	const PickResult hit = PickEntity(*m_Scene, ray);
	m_SceneHierarchyPanel.SetSelectedEntity(hit ? hit.Entity : Entity{});
}

// The collider overlay, drawn through whichever camera the scene view is using.
//
// It has to agree with that camera rather than always using the editor one:
// the scene view can be switched to the game camera, and an overlay drawn from
// a different viewpoint than the image under it is worse than none.
void EditorLayer::DrawColliderOverlay()
{
	const UUID selected = m_SceneHierarchyPanel.GetSelectedEntity()
						? m_SceneHierarchyPanel.GetSelectedEntity().GetUUID()
						: UUID::Invalid();

	if (m_UseEditorCamera)
	{
		DebugRenderer::BeginScene(m_EditorCamera, m_EditorCamera.GetTransform());
	}
	else
	{
		Entity camera = m_Scene->GetPrimaryCameraEntity();
		if (!camera || m_ViewportSize.y <= 0.0f)
			return;

		auto& component = camera.GetComponent<CameraComponent>();
		if (!component.fixedAspectRatio)
			component.Camera.SetAspectRatio(m_ViewportSize.x / m_ViewportSize.y);

		DebugRenderer::BeginScene(component.Camera,
								  camera.GetComponent<TransformComponent>().World);
	}

	DrawPhysicsColliders(*m_Scene, selected);
	DebugRenderer::EndScene();
}

// Applied at the top of a frame rather than where the size is discovered.
//
// The panels are drawn in OnImGuiRender, which runs after OnUpdate has already
// begun a render pass on these targets. Calling Resize there destroys and
// recreates images the command buffer has bound, which the validation layer
// reports as a command buffer "in an invalid state" -- 140 of them on every
// startup, and more on every window resize.
void EditorLayer::ApplyPendingResizes()
{
	if (m_RequestedViewportSize.x > 0.0f &&
		(m_ViewportSize.x != m_RequestedViewportSize.x || m_ViewportSize.y != m_RequestedViewportSize.y))
	{
		m_ViewportSize = m_RequestedViewportSize;
		m_SceneTarget->Resize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
		m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
	}

	if (m_RequestedGameSize.x > 0.0f &&
		(m_GameViewportSize.x != m_RequestedGameSize.x || m_GameViewportSize.y != m_RequestedGameSize.y))
	{
		m_GameViewportSize = m_RequestedGameSize;
		m_GameTarget->Resize((unsigned int)m_GameViewportSize.x, (unsigned int)m_GameViewportSize.y);
	}
}

// Zero or more times a frame, always with the same dt. Paused stops stepping
// without leaving play mode, so the scene stays as it was mid-run.
void EditorLayer::OnFixedUpdate(Timestep dt)
{
	if (m_SceneState == SceneState::Play && m_Scene)
		m_Scene->OnFixedUpdateRuntime(dt);
}

// Play snapshots the scene; Stop restores it. Everything done while running --
// by a script, by physics, or by hand in the inspector -- is discarded, which
// is what makes pressing Play cost nothing.
//
// This is only as safe as serialization is lossless, which is why the
// round-trip test came first.
void EditorLayer::OnScenePlay()
{
	if (!m_Scene || m_SceneState != SceneState::Edit)
		return;

	SceneSerializer serializer(m_Scene);
	m_SceneSnapshot = serializer.SerializeToString();

	m_SceneState = SceneState::Play;
	// After the snapshot: the bodies are built from the scene as it is being
	// left, and torn down before it is restored.
	m_Scene->OnRuntimeStart();
	// Undo across a mode change would apply an edit to entities the restore is
	// about to replace.
	m_Commands.Clear();

	// The scene view deliberately stays on the editor camera. Switching it to
	// the game camera on Play means losing the ability to look around while
	// something is running, which is most of why you would run it in an editor
	// at all -- the Game panel is already showing the game's view.
	RV_INFO("Play");
}

void EditorLayer::OnSceneStop()
{
	if (!m_Scene || m_SceneState == SceneState::Edit)
		return;

	m_SceneState = SceneState::Edit;
	m_Scene->OnRuntimeStop();
	m_SceneHierarchyPanel.SetSelectedEntity({});

	SceneSerializer serializer(m_Scene);
	if (!serializer.DeserializeFromString(m_SceneSnapshot))
		RV_ERROR("Could not restore the scene; what is on screen is the state Play left behind");

	m_SceneSnapshot.clear();
	m_Commands.Clear();
	RV_INFO("Stop");
}

void EditorLayer::OnScenePause(bool paused)
{
	if (m_SceneState == SceneState::Edit)
		return;

	m_SceneState = paused ? SceneState::Paused : SceneState::Play;
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

	// Proportional, not a fixed 280. A hard minimum is fine on a wide monitor
	// and ruinous in a small window: every panel refuses to shrink, the space
	// has to come from somewhere, and one panel absorbs the entire shortfall.
	// That is how the content browser ended up 59 pixels tall.
	style.WindowMinSize.x = ImClamp(viewport->WorkSize.x * 0.08f, 90.0f, 280.0f);

	const ImGuiID dockspaceID = ImGui::GetID("EditorDockSpace");

	// Rebuild when the layout has never been built, when the user asked, or
	// when the default has changed since the saved one was written. Without
	// that last case an existing imgui.ini pins everyone to the old
	// arrangement, and the fix only reaches people who find the menu item.
	if (m_ResetLayoutRequested || ImGui::DockBuilderGetNode(dockspaceID) == nullptr ||
		!LayoutVersionMatches())
	{
		m_ResetLayoutRequested = false;
		BuildDefaultLayout(dockspaceID);
	}

	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
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
	if (m_ShowContentBrowser)  m_ContentBrowser.OnImGuiRender(&m_ShowContentBrowser);
	if (m_ShowDemoWindow)      ImGui::ShowDemoWindow(&m_ShowDemoWindow);

	DrawAboutPopup();
	DrawBackendRestartPopup();
}

// Splits are declared as fractions of what is left, so the arrangement is
// proportional by construction and survives a resize. The previous layout had
// never been designed at all -- it was whatever ImGui's automatic docking
// produced on first run, saved to imgui.ini with absolute pixel sizes, and
// those do not re-derive sensibly at another window size.
// The saved arrangement lives in imgui.ini, which has no room for a version.
// A file beside it is the least surprising place to keep one.
bool EditorLayer::LayoutVersionMatches()
{
	std::ifstream file("layout.version");
	int saved = 0;
	if (file >> saved)
		return saved == kLayoutVersion;
	return false;
}

void EditorLayer::WriteLayoutVersion()
{
	std::ofstream file("layout.version");
	if (file)
		file << kLayoutVersion;
}

void EditorLayer::BuildDefaultLayout(unsigned int dockspaceID)
{
	ImGui::DockBuilderRemoveNode(dockspaceID);
	ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetMainViewport()->WorkSize);

	ImGuiID centre = dockspaceID;
	ImGuiID left   = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left,  0.17f, nullptr, &centre);
	ImGuiID right  = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, nullptr, &centre);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down,  0.26f, nullptr, &centre);
	ImGuiID rightLower = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);

	ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
	ImGui::DockBuilderDockWindow("Properties", right);
	ImGui::DockBuilderDockWindow("Render Settings", rightLower);
	ImGui::DockBuilderDockWindow("Statistics", rightLower);
	ImGui::DockBuilderDockWindow("Content", bottom);

	// Tabbed rather than side by side. Two 3D views sharing the centre column
	// leaves both too small to work in at anything below a large monitor, and
	// the tab that is not on top costs nothing to keep.
	ImGui::DockBuilderDockWindow("Viewport", centre);
	ImGui::DockBuilderDockWindow("Game", centre);

	ImGui::DockBuilderFinish(dockspaceID);
	WriteLayoutVersion();
}

void EditorLayer::DrawMenuBar()
{
	if (!ImGui::BeginMenuBar())
		return;

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))   NewScene();
		if (ImGui::MenuItem("Open Project...")) OpenProject();
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("A project is a folder with a .rvproject in it.\n"
							  "Its assets folder is where handles are minted, which\n"
							  "is why they survive a rebuild.");
		}

		if (Project::GetActive())
		{
			if (ImGui::MenuItem("Build Game..."))
				BuildGame();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Package this project into a folder someone else\n"
								  "can run: the runtime, the assets, and a config\n"
								  "file, with no editor.");
			}

			if (ImGui::MenuItem("Set Start Scene"))
				SetStartSceneToCurrent();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The scene a standalone build opens on.\n"
								  "Saved into the .rvproject.");
			}
		}
		else
		{
			ImGui::MenuItem("Set Start Scene", nullptr, false, false);
		}

		ImGui::Separator();

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
		ImGui::Separator();
		if (ImGui::MenuItem("Save Selection as Prefab..."))
			SaveSelectionAsPrefab();
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Scene"))
	{
		const bool running = m_SceneState != SceneState::Edit;

		if (ImGui::MenuItem(running ? "Stop" : "Play", "Ctrl+P"))
		{
			if (running) OnSceneStop();
			else         OnScenePlay();
		}

		const bool paused = m_SceneState == SceneState::Paused;
		if (ImGui::MenuItem(paused ? "Resume" : "Pause", nullptr, false, running))
			OnScenePause(!paused);

		ImGui::Separator();
		ImGui::TextDisabled(running ? "Running -- changes are discarded on Stop"
								    : "Editing");
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
		ImGui::MenuItem("Content",         nullptr, &m_ShowContentBrowser);
		ImGui::MenuItem("Statistics",      nullptr, &m_ShowStatistics);
		ImGui::MenuItem("Render Settings", nullptr, &m_ShowRenderSettings);
		ImGui::Separator();

		ImGui::MenuItem("Show Colliders", "F3", &m_ShowColliders);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Wireframe every collider in the scene view.\n\n"
							  "Green is static, bright green dynamic, blue kinematic,\n"
							  "amber a trigger. While playing, a body the simulation\n"
							  "has put to sleep is drawn dimmed.");
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout"))
			m_ResetLayoutRequested = true;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Restore the default panel arrangement.");
		ImGui::MenuItem("ImGui Demo",      nullptr, &m_ShowDemoWindow);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("About RageV")) m_ShowAbout = true;
		ImGui::EndMenu();
	}

	// Right-aligned backend picker.
	//
	// A readout first and a control second: which API this process is running
	// is the most useful thing to see at a glance while both are supported, and
	// switching it is rare. Hence plain text that only colours on hover --
	// the theme rule is that red means "you can act on this", and a permanently
	// red label in the menu bar reads as a warning rather than as a button.
	//
	// The switch cannot take effect in place. The window itself is created
	// differently per backend, so this records a preference and restarts.
	if (Renderer::HasDevice())
	{
		const auto& caps = Renderer::GetDevice().GetCaps();
		const RHI::Backend current = Renderer::GetDevice().GetBackend();
		const char* names[] = { "Vulkan", "OpenGL" };
		const int currentIndex = current == RHI::Backend::Vulkan ? 0 : 1;

		const float arrow = ImGui::GetFrameHeight();
		const float width = ImGui::CalcTextSize("OpenGL").x + arrow +
							ImGui::GetStyle().ItemSpacing.x * 2.0f;

		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - width - ImGui::GetStyle().ItemSpacing.x);
		ImGui::SetNextItemWidth(width);

		// Transparent until hovered, so it sits in the menu bar as a label and
		// announces itself as a control only when reached for.
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, EditorTheme::Color::AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, EditorTheme::Color::Accent);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Color::Text);

		if (ImGui::BeginCombo("##Backend", names[currentIndex], ImGuiComboFlags_HeightSmall))
		{
			for (int i = 0; i < 2; i++)
			{
				const bool selected = i == currentIndex;
				if (ImGui::Selectable(names[i], selected) && !selected)
				{
					m_PendingBackend = i == 0 ? RHI::Backend::Vulkan : RHI::Backend::OpenGL;
					m_ShowBackendRestart = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::PopStyleColor(4);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\n%s\n\nSwitching restarts the editor.",
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

	// Every gap goes through here rather than through a hand-written
	// SameLine/Dummy/SameLine chain. One missing SameLine in that chain wraps
	// everything after it onto a second row -- inside a child exactly one row
	// tall, which clips it away entirely. That is how the Play button became
	// invisible the moment it was added after a checkbox.
	auto Gap = [](float width = 10.0f)
	{
		ImGui::SameLine(0.0f, width);
	};

	const bool running = m_SceneState != SceneState::Edit;

	// --- transform tools, left ----------------------------------------------
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
	};

	ModeButton("Mov", ImGuizmo::OPERATION::TRANSLATE, "Translate (W)");
	Gap(4.0f);
	ModeButton("Rot", ImGuizmo::OPERATION::ROTATE, "Rotate (E)");
	Gap(4.0f);
	ModeButton("Scl", ImGuizmo::OPERATION::SCALE, "Scale (R)");

	Gap();
	if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52.0f, 0.0f)))
		m_GizmoLocal = !m_GizmoLocal;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Gizmo space. Scaling is always local.");

	Gap();
	ImGui::Checkbox("Snap", &m_SnapEnabled);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Hold Ctrl while dragging for the same effect.");

	// --- transport, centred -------------------------------------------------
	// Centred because it is the control people reach for most, and because that
	// is where every other engine puts it.
	{
		constexpr float kPlayWidth = 52.0f;
		constexpr float kPauseWidth = 62.0f;
		const float transportWidth = kPlayWidth + kPauseWidth + 4.0f;

		const float centre = (ImGui::GetWindowWidth() - transportWidth) * 0.5f;
		// Never behind what is already drawn: at a narrow window the tools on
		// the left win and the transport simply sits after them.
		ImGui::SameLine(ImMax(centre, ImGui::GetCursorPosX() + 10.0f));

		if (running)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, EditorTheme::Color::Accent);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorTheme::Color::AccentHover);
		}
		if (ImGui::Button(running ? "Stop" : "Play", ImVec2(kPlayWidth, 0.0f)))
		{
			if (running) OnSceneStop();
			else         OnScenePlay();
		}
		if (running)
			ImGui::PopStyleColor(2);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(running
				? "Stop, and restore the scene to exactly what it was when Play was pressed. (Ctrl+P)"
				: "Run the scene. Everything that happens while running is discarded on Stop. (Ctrl+P)");

		Gap(4.0f);
		ImGui::BeginDisabled(!running);
		const bool paused = m_SceneState == SceneState::Paused;
		if (ImGui::Button(paused ? "Resume" : "Pause", ImVec2(kPauseWidth, 0.0f)))
			OnScenePause(!paused);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered() && running)
			ImGui::SetTooltip("Stop stepping the simulation without leaving play mode.");
	}

	// --- viewpoint, right ---------------------------------------------------
	{
		const char* label = m_UseEditorCamera ? "Editor Cam" : "Game Cam";
		constexpr float width = 96.0f;

		const float rightEdge = ImGui::GetWindowWidth() - width - 12.0f;
		ImGui::SameLine(ImMax(rightEdge, ImGui::GetCursorPosX() + 10.0f));

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
				: "Viewport draws through the scene's camera -- the lowest ViewRank wins.\n\n"
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

	// Where the frame goes, on both processors.
	//
	// CPU is wall time around each phase and GPU is a timestamp pair inside the
	// command buffer, so the two answer different questions: the first says
	// where the thread spent itself, the second where the work ran. A phase can
	// be large on one and small on the other, and which one it is decides what
	// there is to do about it.
	ImGui::SeparatorText("Frame");
	if (ImGui::BeginTable("##PhaseStats", 3, ImGuiTableFlags_SizingStretchProp |
											 ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Phase");
		ImGui::TableSetupColumn("CPU");
		ImGui::TableSetupColumn("GPU");
		ImGui::TableHeadersRow();

		float cpuTotal = 0.0f;
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			const auto phase = (FramePhase)i;
			const float cpu = FrameProfiler::LivePhaseMs(phase);
			const float gpu = FrameProfiler::LiveGpuPhaseMs(phase);
			cpuTotal += cpu;

			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(FramePhaseName(phase));
			ImGui::TableNextColumn(); ImGui::Text("%.3f", cpu);
			ImGui::TableNextColumn();
			if (gpu > 0.0f)
				ImGui::Text("%.3f", gpu);
			else
				ImGui::TextDisabled("--");
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled("total");
		ImGui::TableNextColumn(); ImGui::TextDisabled("%.3f", cpuTotal);
		ImGui::TableNextColumn();
		if (const float gpuFrame = FrameProfiler::LiveGpuFrameMs(); gpuFrame > 0.0f)
			ImGui::TextDisabled("%.3f", gpuFrame);
		else
			ImGui::TextDisabled("--");

		ImGui::EndTable();
	}

	if (!FrameProfiler::HasGpuTimings())
		ImGui::TextDisabled("No GPU timings: this device has no timestamp queries.");
	else if (EngineConfig::Get().VSync)
	{
		// Said here because it is the single most common way to misread this
		// panel, and it has already happened once in this project's history.
		ImGui::TextDisabled("Vsync is on: the frame time is the display's refresh, "
							"not the renderer's cost.");
	}

	ImGui::SeparatorText("Renderer");
	if (ImGui::BeginTable("##RendererStats", 2, ImGuiTableFlags_SizingStretchProp))
	{
		StatRow("Mesh draws", std::to_string(Renderer3D::GetDrawCallCount()));
		StatRow("Triangles",  std::to_string(Renderer3D::GetTriangleCount()));
		StatRow("Culled",     std::to_string(Renderer3D::GetCulledCount()));
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

		ImGui::SeparatorText("Sky");

		const char* skyModes[] = { "Colour", "Gradient", "Environment map" };
		int sky = (int)environment.Sky;
		if (ImGui::Combo("Background", &sky, skyModes, IM_ARRAYSIZE(skyModes)))
		{
			const SceneEnvironment before = environment;
			environment.Sky = (SkyType)sky;
			const SceneEnvironment after = environment;
			std::weak_ptr<Scene> scene = m_Scene;

			// Pushed here rather than through trackAmbient: a combo commits on
			// the click that closes it, so there is no activate/deactivate pair
			// for a drag to sit between.
			m_Commands.PushApplied(std::make_unique<ValueEditCommand>(
				"Background",
				[scene, after]  { if (auto s = scene.lock()) s->GetEnvironment() = after; },
				[scene, before] { if (auto s = scene.lock()) s->GetEnvironment() = before; }));
		}
		HelpMarker("Colour draws nothing and leaves the clear colour, which is what a 2D or "
				   "UI-only scene wants.\n\nGradient costs no asset.\n\nAn environment map is "
				   "a panorama -- .hdr for values brighter than white -- or one face of a "
				   "six-file set, in which case the other five come with it.");

		if (environment.Sky == SkyType::Gradient)
		{
			ImGui::ColorEdit3("Horizon", glm::value_ptr(environment.SkyHorizon));
			trackAmbient("Sky horizon");
			ImGui::ColorEdit3("Zenith", glm::value_ptr(environment.SkyZenith));
			trackAmbient("Sky zenith");
			ImGui::ColorEdit3("Ground", glm::value_ptr(environment.SkyGround));
			trackAmbient("Sky ground");
		}

		if (environment.Sky == SkyType::Cubemap)
		{
			const std::string name = AssetManager::GetDisplayName(environment.SkyTexture);
			ImGui::Button(name.c_str(), ImVec2(-1.0f, 0.0f));

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RAGEV_ASSET"))
				{
					const AssetHandle dropped = *(const AssetHandle*)payload->Data;

					// Refused rather than stored, for the same reason the
					// inspector's asset fields refuse: a handle of the wrong
					// type resolves to nothing, and the field would present as
					// simply not working.
					if (AssetRegistry::GetMetadata(dropped).Type == AssetType::Texture)
					{
						const SceneEnvironment before = environment;
						environment.SkyTexture = dropped;
						const SceneEnvironment after = environment;
						std::weak_ptr<Scene> scene = m_Scene;

						m_Commands.PushApplied(std::make_unique<ValueEditCommand>(
							"Environment map",
							[scene, after]  { if (auto s = scene.lock()) s->GetEnvironment() = after; },
							[scene, before] { if (auto s = scene.lock()) s->GetEnvironment() = before; }));
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Drop a texture from the Content browser.");

			float degrees = glm::degrees(environment.SkyRotation);
			if (ImGui::DragFloat("Sky rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg"))
				environment.SkyRotation = glm::radians(degrees);
			trackAmbient("Sky rotation");
			HelpMarker("A panorama points wherever it was shot, and the scene was not built "
					   "to match it.");
		}

		if (environment.Sky != SkyType::Color)
		{
			ImGui::DragFloat("Sky intensity", &environment.SkyIntensity, 0.01f, 0.0f, 16.0f);
			trackAmbient("Sky intensity");
		}

		ImGui::SeparatorText("Post processing");

		ImGui::DragFloat("Exposure", &environment.Exposure, 0.01f, 0.01f, 16.0f);
		trackAmbient("Exposure");
		HelpMarker("Applied before the tone curve, which is what makes this an exposure "
				   "control rather than a brightness one: it slides the scene along the "
				   "response curve instead of scaling the result of it.");

		ImGui::Checkbox("Bloom", &environment.BloomEnabled);
		trackAmbient("Bloom");

		if (environment.BloomEnabled)
		{
			ImGui::DragFloat("Threshold", &environment.BloomThreshold, 0.01f, 0.0f, 16.0f);
			trackAmbient("Bloom threshold");
			HelpMarker("Brightness at which a pixel starts to bleed. Above 1, only things "
					   "genuinely brighter than white glow.");

			ImGui::DragFloat("Knee", &environment.BloomKnee, 0.01f, 0.0f, 4.0f);
			trackAmbient("Bloom knee");
			HelpMarker("Width of the ramp around the threshold. Zero is a hard cut, which "
					   "pops as something crosses it and reads as flickering.");

			ImGui::DragFloat("Intensity", &environment.BloomIntensity, 0.002f, 0.0f, 2.0f);
			trackAmbient("Bloom intensity");
		}

		// Only the modes that exist. Offering SMAA and TAA here and doing
		// nothing would be worse than not offering them; the roadmap is where
		// "not yet" belongs.
		const char* aaModes[] = { "None", "FXAA" };
		int aa = (int)environment.AA;
		if (ImGui::Combo("Anti-aliasing", &aa, aaModes, IM_ARRAYSIZE(aaModes)))
			environment.AA = (AntiAliasing)aa;
		HelpMarker("FXAA is one pass over the tone-mapped image: cheap, no prerequisites, "
				   "and it softens the picture slightly.\n\n"
				   "SMAA is sharper for the same idea and needs two precomputed lookup "
				   "textures vendored in.\n\n"
				   "TAA is better than either and needs motion vectors -- every mesh "
				   "carrying its previous transform and the renderer writing a velocity "
				   "target. That is a renderer feature with its own prerequisites, not a "
				   "post pass.");
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
			   "Surfaces reflect the scene's environment, with roughness picking a mip "
			   "rather than a real prefiltered convolution; the diffuse ambient is still "
			   "the flat term above. The shader writes linear HDR and the tone curve is "
			   "its own pass.");

	ImGui::End();
}

namespace
{
	// A viewport is an image, not a document. Nothing in it scrolls, and the
	// wheel means zoom.
	constexpr ImGuiWindowFlags kViewportFlags =
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void EditorLayer::DrawViewportPanel()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	// The wheel belongs to the camera here, not to the window. Without this the
	// panel scrolls its own contents at the same time as the camera zooms,
	// which drags the image around under the cursor.
	if (!ImGui::Begin("Viewport", &m_ShowViewport, kViewportFlags))
	{
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	m_IsViewportFocused = ImGui::IsWindowFocused();
	m_IsViewportHovered = ImGui::IsWindowHovered();
	Application::Get().GetImGuiLayer()->SetEventBlocker(!m_IsViewportFocused && !m_IsViewportHovered);

	// Recorded, not applied: see ApplyPendingResizes.
	const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	m_RequestedViewportSize = { viewportSize.x, viewportSize.y };

	// Where the image starts on screen, captured before it is drawn: a click is
	// reported in screen coordinates and has to be turned into a position
	// within this image.
	const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();

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

	// Before the gizmo, so ImGuizmo has not yet claimed the mouse this frame,
	// and after the image, so the origin above is the image's.
	HandleViewportPicking(imageOrigin, viewportSize);

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
	if (!ImGui::Begin("Game", &m_ShowGameViewport, kViewportFlags))
	{
		// Collapsed, or behind another tab. Read next frame to skip the whole
		// second render pass rather than drawing for nobody.
		m_GameViewportVisible = false;
		ImGui::End();
		ImGui::PopStyleVar();
		return;
	}

	m_GameViewportVisible = true;

	// Recorded, not applied: see ApplyPendingResizes.
	const ImVec2 size = ImGui::GetContentRegionAvail();
	m_RequestedGameSize = { size.x, size.y };

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

void EditorLayer::DrawBackendRestartPopup()
{
	if (m_ShowBackendRestart)
	{
		ImGui::OpenPopup("Restart required");
		m_ShowBackendRestart = false;
	}

	ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("Restart required", nullptr, ImGuiWindowFlags_NoResize))
		return;

	const char* name = EngineConfig::BackendName(m_PendingBackend);

	ImGui::TextWrapped("The graphics backend is chosen when the window is created, "
					   "so switching to %s takes effect the next time the editor "
					   "starts.", name);
	ImGui::Spacing();
	ImGui::TextDisabled("Saved to ragev.ini, so a manual start uses it too.");
	ImGui::Spacing();
	// Said rather than worked around. The editor has no dirty tracking and
	// loses unsaved work when it closes for any other reason too; quietly
	// saving here would be this one button behaving unlike the rest.
	ImGui::TextDisabled("Restarting closes the editor. Save your scene first.");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Written before either button: whichever the user picks, the preference is
	// what they asked for. Restarting later must not mean forgetting.
	const bool saved = EngineConfig::SaveBackendPreference(m_PendingBackend);
	if (!saved)
	{
		ImGui::TextColored(EditorTheme::Color::Accent,
						   "Could not write ragev.ini; the choice will not survive.");
		ImGui::Spacing();
	}

	const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

	if (ImGui::Button("Restart Now", ImVec2(width, 0.0f)))
	{
		const std::string arguments =
			std::string("--rhi=") + (m_PendingBackend == RHI::Backend::Vulkan ? "vulkan" : "opengl");

		if (Process::RelaunchSelf(arguments))
		{
			ImGui::CloseCurrentPopup();
			Application::Get().Close();
		}
		else
		{
			// The new process did not start, so this one stays. Closing here
			// would leave the user with no editor at all.
			RV_ERROR("Could not relaunch; the preference is saved, so start the "
					 "editor again when convenient.");
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Restart Later", ImVec2(width, 0.0f)))
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

		// The conventional binding, and a second way to reach a control that
		// was previously only on the toolbar.
		case RV_KEY_P:
		{
			if (control)
			{
				if (m_SceneState == SceneState::Edit) OnScenePlay();
				else                                  OnSceneStop();
				return true;
			}
			break;
		}

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

		// A function key rather than a letter: the overlay is toggled while
		// looking at the scene, often with the other hand on the camera
		// controls, and every unmodified letter near WASD is already a gizmo.
		case RV_KEY_F3: m_ShowColliders = !m_ShowColliders; return true;

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

	// Something scripted, so pressing Play does something without having to
	// set it up first. Stop puts it back where it started, which is the whole
	// point of the snapshot.
	//
	// The cube rather than a sphere: a sphere turning about its own axis is
	// very nearly indistinguishable from a still one, since its silhouette and
	// its shading are both symmetric about that axis. A cube's corners make the
	// rotation unmistakable, which is what a demonstration has to be.
	if (Entity cube = m_Scene->FindEntityByName("Cube (dielectric)"))
		cube.AddComponent<NativeScriptComponent>("Spinner");

	// The ground becomes a static body, and a small stack is dropped onto it,
	// so Play shows the simulation rather than only the script. Stop puts every
	// one of them back where it started.
	if (Entity ground = m_Scene->FindEntityByName("Ground"))
	{
		ground.AddComponent<RigidBodyComponent>(BodyType::Static);
		auto& collider = ground.AddComponent<ColliderComponent>();
		// The plane primitive is a unit quad scaled to 20; the collider is a
		// thin slab under its surface rather than a plane, because an
		// infinitely thin box is something a fast body can pass through.
		collider.HalfExtents = { 10.0f, 0.25f, 10.0f };
		collider.Offset = { 0.0f, -0.25f, 0.0f };
	}

	for (int i = 0; i < 4; i++)
	{
		Entity box = m_Scene->CreateEntity("Falling Box " + std::to_string(i + 1));

		auto& mesh = box.AddComponent<MeshComponent>(PrimitiveType::Cube);
		mesh.Material = std::make_shared<Material>(Renderer::GetDevice(), "Falling Box");
		auto& params = mesh.Material->GetParams();
		params.BaseColor = { 0.30f, 0.55f, 0.85f, 1.0f };
		params.Roughness = 0.4f;

		auto& transform = box.GetComponent<TransformComponent>();
		// Offset slightly on each axis so they topple rather than landing in a
		// perfect column, which reads as nothing happening.
		transform.Position = { -0.4f + i * 0.22f, 4.0f + i * 1.6f, 0.35f - i * 0.18f };
		transform.Scale = glm::vec3(0.7f);

		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>().HalfExtents = glm::vec3(0.5f);

		// Landing shown as an event rather than only as a change of position.
		// Alternating, because an entity carries one script: half the boxes
		// flash on impact and half are heard, and between them the pair of
		// features is demonstrated on the same collisions.
		if (i % 2 == 0)
		{
			box.AddComponent<NativeScriptComponent>("ImpactFlash");
		}
		else
		{
			box.AddComponent<NativeScriptComponent>("ImpactSound");

			auto& sound = box.AddComponent<AudioSourceComponent>();
			sound.Clip = AssetRegistry::GetHandle("audio/impact.wav");
			// The script starts it. On awake it would fire as the scene loads,
			// before the box has hit anything.
			sound.PlayOnAwake = false;
			sound.MaxDistance = 40.0f;
		}
	}

	// A band of air the boxes fall through, tinting them on the way. Invisible
	// -- no mesh -- which is what a trigger volume normally is, and what makes
	// it worth having something in the demo scene that proves one is there.
	{
		Entity zone = m_Scene->CreateEntity("Trigger Zone");
		zone.GetComponent<TransformComponent>().Position = { 0.0f, 2.2f, 0.0f };

		// Static: a trigger is a place, not an object. It still detects the
		// moving bodies that pass through it.
		zone.AddComponent<RigidBodyComponent>(BodyType::Static);

		auto& collider = zone.AddComponent<ColliderComponent>();
		collider.HalfExtents = { 3.0f, 0.6f, 3.0f };
		collider.IsTrigger = true;

		zone.AddComponent<NativeScriptComponent>("TriggerZone");

		auto& chime = zone.AddComponent<AudioSourceComponent>();
		chime.Clip = AssetRegistry::GetHandle("audio/chime.wav");
		chime.PlayOnAwake = false;
		chime.Volume = 0.7f;
	}

	// A looping spatial drone on the pedestal, off to one side, so the demo
	// scene proves the parts of audio that a one-shot cannot: that a sound
	// loops without a seam, that it starts with the scene, and that it is
	// panned and attenuated by where it is relative to the camera.
	{
		auto& ambience = pedestal.AddComponent<AudioSourceComponent>();
		ambience.Clip = AssetRegistry::GetHandle("audio/hum.wav");
		ambience.Bus = AudioBus::Music;
		ambience.Loop = true;
		ambience.PlayOnAwake = true;
		ambience.Volume = 0.35f;
		ambience.MinDistance = 2.0f;
		ambience.MaxDistance = 25.0f;
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

// A prefab is a scene file holding one entity tree -- the same snapshot undo
// takes when it deletes something. Instantiating one remaps every id, so two
// copies do not share identities.
void EditorLayer::SaveSelectionAsPrefab()
{
	Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
	if (!selected)
		return;

	const std::string name = selected.GetName();
	const std::filesystem::path relative = std::filesystem::path("prefabs") / (name + ".rprefab");

	const AssetHandle handle = AssetManager::CreatePrefab(*m_Scene, selected, relative);
	if (!handle.IsValid())
	{
		RV_WARN("Could not create a prefab from '{0}'", name);
		return;
	}

	RV_INFO("Saved prefab '{0}'", relative.generic_string());
}

void EditorLayer::OnAssetActivated(AssetHandle handle, AssetType type)
{
	if (!m_Scene)
		return;

	Entity root;

	switch (type)
	{
		case AssetType::Mesh:   root = AssetManager::InstantiateModel(*m_Scene, handle); break;
		case AssetType::Prefab: root = AssetManager::InstantiatePrefab(*m_Scene, handle); break;
		default:
			RV_WARN("Nothing to do with a {0} asset yet", AssetTypeName(type));
			return;
	}

	if (!root)
		return;

	m_SceneHierarchyPanel.SetSelectedEntity(root);
	// One undo step for the whole thing: the command snapshots the subtree when
	// it is undone, so redo brings back the entire tree.
	m_Commands.PushApplied(std::make_unique<CreateEntityCommand>(m_Scene, root.GetUUID(),
																 root.GetName()));
	FocusSelection();
}

void EditorLayer::OpenScene()
{
	const std::string filepath = FileDialogs::OpenFile("RageV Scene (*.rage)\0*.rage\0");
	if (filepath.empty())
		return;

	OpenSceneFile(filepath);
}

// The one place a scene is loaded, so opening one from a dialog, from the
// content browser and from a project's start scene all end up in the same
// state -- including remembering the path, which "Set Start Scene" needs.
void EditorLayer::OpenSceneFile(const std::filesystem::path& filepath)
{
	m_Scene = std::make_shared<Scene>();
	if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		m_Scene->OnViewportResize((unsigned int)m_ViewportSize.x, (unsigned int)m_ViewportSize.y);
	m_SceneHierarchyPanel.SetSceneRef(m_Scene);
	m_SceneHierarchyPanel.SetCommandStack(&m_Commands);
	m_Commands.Clear();

	SceneSerializer serializer(m_Scene);
	if (serializer.Deserialize(filepath.string()))
		m_ScenePath = filepath;
	else
		m_ScenePath.clear();
}

void EditorLayer::SaveScene()
{
	const std::string filepath = FileDialogs::SaveFile("RageV Scene (*.rage)\0*.rage\0");
	if (filepath.empty())
		return;

	SceneSerializer serializer(m_Scene);
	serializer.Serialize(filepath);
	m_ScenePath = filepath;
}

// Switching projects re-roots the asset registry, which is the whole point of
// a project: handles are minted in and resolved against its folder.
void EditorLayer::OpenProject()
{
	const std::string filepath = FileDialogs::OpenFile("RageV Project (*.rvproject)\0*.rvproject\0");
	if (filepath.empty())
		return;

	if (!Project::Load(filepath))
		return;

	// The cache is keyed by handle, and handles from the old project mean
	// something else -- or nothing -- in the new one.
	AssetManager::ClearCache();
	AssetRegistry::Init(Project::AssetRoot());

	const std::string& start = Project::Config().StartScene;
	if (!start.empty() && std::filesystem::exists(Project::AssetPath(start)))
	{
		OpenSceneFile(Project::AssetPath(start));
		return;
	}

	// A project with no start scene opens empty rather than on whatever scene
	// the previous project happened to be showing.
	NewScene();
	m_ScenePath.clear();
}

// Package the project into a folder someone else can run.
//
// A folder dialog would be better than a file one, but the platform layer only
// has file dialogs; asking for a name inside the target folder and using its
// parent is the honest version of that until it grows one.
void EditorLayer::BuildGame()
{
	if (!Project::GetActive())
		return;

	const std::string chosen = FileDialogs::SaveFile("Build folder\0*.*\0");
	if (chosen.empty())
		return;

	PackageDesc desc;
	desc.OutputDirectory = std::filesystem::path(chosen).parent_path();
	// The dialog picked a location inside an existing folder, which will
	// usually already hold something. Refusing here would make the menu item
	// unusable; the CLI keeps the guard, where a scripted build could
	// otherwise flatten a directory nobody looked at.
	desc.Overwrite = true;

	const PackageResult result = PackageProject(desc);

	for (const std::string& warning : result.Warnings)
		RV_WARN("{0}", warning);

	if (!result.Success)
	{
		for (const std::string& error : result.Errors)
			RV_ERROR("{0}", error);
		return;
	}

	RV_INFO("Built '{0}': {1} files, {2} MB -> {3}", Project::Config().Name,
			result.FilesCopied, result.BytesCopied / (1024 * 1024),
			result.Executable.string());
}

void EditorLayer::SetStartSceneToCurrent()
{
	if (!Project::GetActive())
		return;

	if (m_ScenePath.empty())
	{
		RV_WARN("Save the scene before setting it as the start scene");
		return;
	}

	// Relative to the asset directory, and refused when it is not inside one:
	// an absolute path in a project file is a project that only opens on the
	// machine that wrote it, which is exactly what a packaged game cannot be.
	const std::string relative = Project::MakeRelative(m_ScenePath);
	if (relative.empty())
	{
		RV_WARN("'{0}' is outside the project's assets folder", m_ScenePath.string());
		return;
	}

	Project::Config().StartScene = relative;
	if (Project::Save())
		RV_INFO("Start scene is now {0}", relative);
}

