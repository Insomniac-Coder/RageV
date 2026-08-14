#include "EditorLayer.h"
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"
#include "UI/EditorTheme.h"
#include "UI/Widgets.h"
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Renderer/DebugRenderer.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Physics/PhysicsDebugDraw.h"
#include "RageV/Scene/ScenePicking.h"
#include "RageV/Project/Project.h"
#include "RageV/Managed/Interop.h"
#include "RageV/Project/ModuleBuild.h"
#include "RageV/Project/GameModule.h"
#include "RageV/Project/ProjectPackager.h"
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/Renderer/UIRenderer.h"
#include "RageV/Renderer/TextureLoader.h"
#include "RageV/UI/Canvas.h"
#include "RageV/UI/Interaction.h"
#include "ImGuizmo.h"
#include "RageV/ImGui/ImGuiBinding.h"
#include "RageV/Math/Math.h"
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

EditorLayer::~EditorLayer()
{
	SavePanelState();

	// A worker still building would write into freed members. Cancel takes
	// the compiler tree down, and the join waits for the worker to notice.
	if (m_BuildThread.joinable())
	{
		m_BuildCancel = true;
		m_BuildThread.join();
	}
}

void EditorLayer::OnAttach()
{
	// Before the first ImGui call in this module, theme included. The engine is
	// a DLL and ImGui's state hides behind a global, so this executable has its
	// own until it is handed the engine's. See ImGuiBinding.h.
	ImGuiBinding::Bind();

	// Before the first frame, so a panel closed last session never flashes
	// open for one frame this one.
	LoadPanelState();

	// The command line wins over the remembered choice, so a screenshot of
	// either theme is one flag rather than an edit to a settings file.
	if (!EngineConfig::Get().Theme.empty())
		m_Theme = EditorTheme::Parse(EngineConfig::Get().Theme.c_str());

	EditorTheme::Apply(m_Theme);

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

	// The scene itself is opened in OnLoad, on the boot worker. Everything
	// above needs the device and is cheap; parsing a scene and pulling in its
	// assets is the opposite of both, and doing it here is what used to leave
	// the window unpumped for seconds.
}

// On the boot worker. Files and CPU only -- see Layer::OnLoad.
void EditorLayer::OnLoad(Boot::Progress& progress)
{
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
	// --scene wins over the project's start scene, the same way it does for the
	// runtime, and for the same reason: opening a particular scene to look at
	// something should not mean editing the project's configuration first.
	const EngineConfig& config = EngineConfig::Get();
	const std::filesystem::path requested =
		!config.ScenePath.empty() && Project::GetActive()
			? Project::AssetPath(config.ScenePath)
			: std::filesystem::path();

	const std::filesystem::path start =
		Project::GetActive() && !Project::Config().StartScene.empty()
			? Project::AssetPath(Project::Config().StartScene)
			: std::filesystem::path();

	const std::filesystem::path scene =
		!requested.empty() && std::filesystem::exists(requested) ? requested
		: !start.empty() && std::filesystem::exists(start)       ? start
																 : std::filesystem::path();

	progress.BeginPhase("Opening scene", 0.0f, 0.15f);
	if (!scene.empty())
		progress.SetDetail(scene.filename().string());

	if (!scene.empty())
		OpenSceneFile(scene);
	else
		LoadDemoScene();

	if (progress.Cancelled())
		return;

	// Decode and cook everything the scene names, so that the first frame
	// after this has nothing left to fetch. The expensive half; it reports
	// per-asset, which is what the bar's detail line shows.
	progress.BeginPhase("Loading assets", 0.15f, 0.95f);
	if (m_Scene)
		Assets::Manager::PrepareScene(*m_Scene, progress);
}

// Main thread, one slice per loading-screen frame.
bool EditorLayer::OnLoadStep(Boot::Progress& progress)
{
	progress.BeginPhase("Uploading assets", 0.95f, 1.0f);
	return Assets::Manager::UploadPrepared(progress, 1.0f / 12.0f);
}

// Back on the main thread, after the uploads. Cheap finishing touches only.
void EditorLayer::OnLoaded()
{
	const EngineConfig& config = EngineConfig::Get();

	// --select names an entity to open with selected, so an inspector widget
	// can be checked without somebody clicking the hierarchy first.
	if (!config.SelectEntity.empty() && m_Scene)
	{
		for (auto handle : m_Scene->GetRegistry().view<TagComponent>())
		{
			Entity entity{ handle, m_Scene.get() };
			if (entity.GetName() == config.SelectEntity)
			{
				m_SceneHierarchyPanel.SetSelectedEntity(entity);
				break;
			}
		}
	}

	// --camera puts the viewport somewhere repeatable. After the scene loads,
	// because opening one is free to frame something.
	if (config.HasCameraPose)
	{
		m_EditorCamera.SetOrbit(config.CameraFocus, config.CameraDistance,
								config.CameraYaw, config.CameraPitch);
	}
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

Entity EditorLayer::CreateMesh(PrimitiveType primitive)
{
	Entity entity = CreateEmpty(PrimitiveTypeName(primitive));
	entity.AddComponent<MeshComponent>(primitive);
	// No material asset, and no private Material object either.
	//
	// This used to build one per primitive so that editing an object's surface
	// did not change every other object using the default. Overrides give that
	// for free: the scalars are per entity already, so a new cube starts on the
	// shared default and can be recoloured without a material of its own.
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
		transform.Rotation = Math::Radians(Vec3(-45.0f, -30.0f, 0.0f));

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
	// The frame a background build finishes, its results are published here --
	// on this thread, before anything else reads them.
	FinishBuild();

	// Averaged over a fixed slice of *time*, not a fixed number of frames.
	//
	// Ten frames is a quarter of a second at 40 FPS and a twenty-fourth of one
	// at 240, so the same code that reads steadily on a slow machine flickers
	// too fast to read on a quick one -- which is exactly what it did once the
	// editor started holding 240. A window in seconds updates at the same rate
	// whatever the frame rate.
	//
	// The graph is sampled more often than the number is redrawn: a plot needs
	// resolution and a readout needs to hold still long enough to be read.
	constexpr float kReadoutSeconds = 0.25f;
	constexpr float kHistorySeconds = 0.10f;

	const float elapsed = ts.GetSeconds();

	m_FrameTimeAccum += ts.GetMilliSeconds();
	m_FrameTimeSamples++;
	m_ReadoutElapsed += elapsed;
	m_HistoryElapsed += elapsed;

	if (m_FrameTimeSamples > 0 && m_HistoryElapsed >= kHistorySeconds)
	{
		const float average = m_FrameTimeAccum / (float)m_FrameTimeSamples;

		m_FrameHistory[m_FrameHistoryIndex] = average;
		m_FrameHistoryIndex = (m_FrameHistoryIndex + 1) % IM_ARRAYSIZE(m_FrameHistory);
		m_HistoryElapsed = 0.0f;

		if (m_ReadoutElapsed >= kReadoutSeconds)
		{
			m_FrameTimeMs = average;
			m_ReadoutElapsed = 0.0f;
		}

		m_FrameTimeAccum = 0.0f;
		m_FrameTimeSamples = 0;
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

	// The pointer a panel recorded last frame, spent before the scripts that
	// will read it -- the same order the runtime uses.
	//
	// Cleared afterwards rather than before, so a frame in which neither
	// viewport was hovered parks the pointer instead of leaving a button lit
	// under a cursor that has moved to the inspector.
	if (m_SceneState != SceneState::Edit)
		UI::UpdatePointer(*m_Scene, m_PointerLayer.x, m_PointerLayer.y, m_Pointer);

	m_Pointer.Inside = false;

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
	// Nothing to draw into until the dock layout has run, which is the frame
	// after this one. Skipped rather than described: BuildFrame adds no passes
	// for a zero-sized target, and compiling an empty graph reported "the graph
	// has no passes" on every startup -- an error for something that is simply
	// not ready yet.
	if (m_ViewportSize.x >= 1.0f && m_ViewportSize.y >= 1.0f)
	{
	RV_PROFILE_PHASE(FramePhase::Graph);

	m_Graph->Begin((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

	FrameDesc scene;
	scene.Output = m_Graph->Import(m_SceneTarget, "SceneView");
	scene.Width = (uint32_t)m_ViewportSize.x;
	scene.Height = (uint32_t)m_ViewportSize.y;
	scene.Environment = m_Scene->GetEnvironment();
	scene.ClearColor = Vec4(m_ClearColor, 1.0f);
	scene.OutputFormat = kViewportFormat;
	scene.DrawScene = [this](RGPassContext&)
	{
		if (m_UseEditorCamera)
		{
			// Locals, so they outlive the call rather than the expression.
			const ViewportGridSettings grid = GridSettings();
			const EditorIconSettings icons = IconSettings();
			m_Scene->OnRenderEditor(m_EditorCamera, m_ShowGrid ? &grid : nullptr,
									m_ShowIcons ? &icons : nullptr);
		}
		else if (m_ViewportSize.y > 0.0f)
		{
			m_Scene->OnRenderRuntime(m_ViewportSize.x / m_ViewportSize.y);
		}
	};

	if (m_ShowColliders)
		scene.DrawOverlay = [this](RGPassContext&) { DrawColliderOverlay(); };

	// The scene's own canvases. Drawn last of all, after tone mapping, so no
	// glyph is ever softened by anti-aliasing -- see FrameDesc::DrawUI.
	scene.DrawUI = [this](RGPassContext&)
	{
		UI::DrawScene(*m_Scene, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
	};

	// Only when something asked for it: the two extra attachments and the
	// resolve cost nothing at all in a frame with no weighted emitters, and
	// this is what keeps that true.
	if (Particles::System::HasWeightedEmitters(*m_Scene))
	{
		scene.DrawTransparent = [](RGPassContext&) { ParticleRenderer::FlushWeighted(); };
		scene.ResolveTransparent = [](RGPassContext&, const RHI::Ref<RHI::RHITexture>& accumulate,
									  const RHI::Ref<RHI::RHITexture>& revealage)
		{
			ParticleRenderer::ResolveWeighted(accumulate, revealage);
		};
	}

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
		game.ClearColor = Vec4(m_ClearColor, 1.0f);
		game.OutputFormat = kViewportFormat;
		game.DrawScene = [this](RGPassContext&)
		{
			m_Scene->OnRenderRuntime(m_GameViewportSize.x / m_GameViewportSize.y);
		};

		// The panel's whole claim is that it shows what the player sees, and a
		// player sees the HUD. Resolved against this panel's own size rather
		// than the scene view's, which is the point of a canvas that scales.
		game.DrawUI = [this](RGPassContext& context)
		{
			UI::DrawScene(*m_Scene, context.Width, context.Height);
		};

		if (Particles::System::HasWeightedEmitters(*m_Scene))
		{
			game.DrawTransparent = [](RGPassContext&) { ParticleRenderer::FlushWeighted(); };
			game.ResolveTransparent = [](RGPassContext&, const RHI::Ref<RHI::RHITexture>& accumulate,
										 const RHI::Ref<RHI::RHITexture>& revealage)
			{
				ParticleRenderer::ResolveWeighted(accumulate, revealage);
			};
		}

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

// One panel's claim on the pointer, recorded for the next OnUpdate.
//
// The mapping is two steps and both are needed: screen to panel-local, then
// panel pixels to *layer* pixels. Those last two agree except on the frame a
// panel is resized -- the render target follows a frame later, by design (see
// ApplyPendingResizes) -- and skipping the ratio would put every button a few
// pixels out for exactly as long as somebody is dragging a splitter, which is
// precisely when they are least likely to blame the splitter.
void EditorLayer::CapturePointer(const ImVec2& imageOrigin, const ImVec2& imageSize,
								 const Vec2& layerSize, bool hovered)
{
	if (!hovered || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
		return;

	const ImVec2 mouse = ImGui::GetMousePos();
	const Vec2 local{ mouse.x - imageOrigin.x, mouse.y - imageOrigin.y };

	m_PointerLayer = layerSize;
	m_Pointer.X = local.x * (layerSize.x / imageSize.x);
	m_Pointer.Y = local.y * (layerSize.y / imageSize.y);
	m_Pointer.Down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	m_Pointer.Inside = local.x >= 0.0f && local.y >= 0.0f
					&& local.x < imageSize.x && local.y < imageSize.y;
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
	const Vec2 local{ mouse.x - imageOrigin.x, mouse.y - imageOrigin.y };

	if (local.x < 0.0f || local.y < 0.0f || local.x > imageSize.x || local.y > imageSize.y)
		return;

	// Into normalised device coordinates. The y flip is because a window's
	// origin is top-left and clip space's is bottom-left; forgetting it gives
	// picking that works perfectly along the horizontal centre line and is
	// mirrored everywhere else.
	const Vec2 ndc{
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

	// The marks are clickable exactly when they are drawn -- otherwise a click
	// would select a light nobody can see.
	PickOptions options;
	options.IconHandles = m_ShowIcons && m_UseEditorCamera;
	options.IconScale = IconSettings().Scale;

	// Clicking nothing clears the selection, which is how a click becomes a way
	// to deselect rather than a thing that can only ever select.
	const PickResult hit = PickEntity(*m_Scene, ray, options);
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

	Physics::DrawColliders(*m_Scene, selected);
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

	// Mid-build, the game module is unloaded so the linker can write it. A
	// play session started now would run without the project's C++ scripts
	// and look like they were broken -- so the press is queued, and the scene
	// starts the moment the build lands.
	if (m_BuildInFlight)
	{
		RV_INFO("A build is running; Play starts when it finishes");
		m_ResumePlayAfterBuild = true;
		return;
	}

	SceneSerializer serializer(m_Scene);
	m_SceneSnapshot = serializer.SerializeToString();

	m_SceneState = SceneState::Play;
	// Whatever the cursor was doing before Play was pressed is not the start of
	// a click on anything.
	UI::ResetPointer(*m_Scene);
	m_Pointer = {};

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

	// Before the restore replaces every entity: a capture naming an entity that
	// no longer exists would be a press nobody could ever complete.
	UI::ResetPointer(*m_Scene);
	m_Pointer = {};

	SceneSerializer serializer(m_Scene);
	if (!serializer.DeserializeFromString(m_SceneSnapshot))
		RV_ERROR("Could not restore the scene; what is on screen is the state Play left behind");

	m_SceneSnapshot.clear();
	m_Commands.Clear();
	RV_INFO("Stop");

	// A C# build that finished mid-play parked its assembly here: the swap
	// needs every instance gone, and OnRuntimeStop above is what destroys
	// them. This is the earliest moment the reload is safe.
	if (!m_PendingAssemblyLoad.empty() && Managed::Interop::IsReady())
	{
		LoadScriptAssembly(m_PendingAssemblyLoad);
		m_PendingAssemblyLoad.clear();
	}
}

void EditorLayer::OnScenePause(bool paused)
{
	if (m_SceneState == SceneState::Edit)
		return;

	m_SceneState = paused ? SceneState::Paused : SceneState::Play;

	// The scene holds the pause, not just this layer: the frame pass is what
	// re-blends physics transforms, and it has to know to hold them -- gating
	// the fixed step alone left paused bodies jittering along their last step.
	if (m_Scene)
		m_Scene->SetPaused(paused);
}

void EditorLayer::OnImGuiRender()
{
	// This module's ImGuizmo, not the engine's -- they are separate globals, and
	// every other ImGuizmo call in the editor is against this one.
	ImGuizmo::BeginFrame();

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
		m_LastDockSize = viewport->WorkSize;
	}
	else
	{
		// Keep the arrangement proportional when the window changes size.
		//
		// ImGui stores a dock split's size in *pixels*, so shrinking the window
		// leaves every side panel at its old width and takes the entire
		// shortfall out of the viewport in the middle -- maximise, un-maximise,
		// and the layout is a different layout. Scaling every node's stored
		// size by the resize ratio keeps the panels at the same fraction of the
		// window instead.
		//
		// The reference size persists in panels.ini, so the correction also
		// applies across runs: a layout saved maximised opens sensibly in a
		// small window, which the pixel sizes in imgui.ini alone cannot do.
		if (m_LastDockSize.x > 0.0f && m_LastDockSize.y > 0.0f &&
			(std::fabs(m_LastDockSize.x - viewport->WorkSize.x) > 0.5f ||
			 std::fabs(m_LastDockSize.y - viewport->WorkSize.y) > 0.5f))
		{
			const ImVec2 scale(viewport->WorkSize.x / m_LastDockSize.x,
							   viewport->WorkSize.y / m_LastDockSize.y);
			RescaleDockTree(ImGui::DockBuilderGetNode(dockspaceID), scale);
		}
		m_LastDockSize = viewport->WorkSize;
	}

	// Before the dock space, not after it.
	//
	// DockSpace() takes the whole remaining content region, so anything drawn
	// after it starts past the bottom of the window and is clipped away. That
	// is where the toolbar was, which is why it has never appeared in a single
	// screenshot of this editor -- gizmo mode, Play, Stop and the camera toggle
	// were all being drawn into nothing. Drawn first, it claims its row and the
	// dock space fills what is left.
	DrawToolbar();

	// NoCloseButton because every docked panel was showing *two* of them: one
	// on its tab, and one the dock node draws on the right for whichever
	// window is focused. Two controls for one action, on every panel, is the
	// kind of duplication that reads as clutter without anybody being able to
	// say why. The tab's own X is the one that stays -- it names what it
	// closes.
	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f),
					 ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton);
	style.WindowMinSize.x = minWindowSize;

	DrawMenuBar();

	ImGui::End();

	// --- panels -------------------------------------------------------------
	m_SceneHierarchyPanel.OnImGuiRender(&m_ShowHierarchy, &m_ShowProperties);

	if (m_ShowStatistics)      DrawStatisticsPanel();
	if (m_ShowRenderSettings)  DrawRenderSettingsPanel();
	DrawScriptBuildPanel();
	if (m_ShowViewport)        DrawViewportPanel();
	if (m_ShowGameViewport)    DrawGameViewportPanel();
	if (m_ShowContentBrowser)
	{
		// Applied here rather than where it was read: the panel resolves the
		// folder against the asset root, and the registry is not up when
		// panels.ini is parsed. Cleared either way, so a folder that has since
		// been deleted is tried once and then forgotten.
		if (!m_PendingContentFolder.empty())
		{
			m_ContentBrowser.SetCurrentFolder(m_PendingContentFolder);
			m_PendingContentFolder.clear();
		}

		m_ContentBrowser.OnImGuiRender(&m_ShowContentBrowser);
	}
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
// Which panels are open, saved beside imgui.ini and layout.version -- the
// other two pieces of "how my editor looked", which already live in the
// working directory.
//
// Written as key = value in the ragev.ini style. A key that is missing keeps
// its compiled default, which is what makes adding a panel later safe: old
// files simply do not mention it.
//
// The About and restart popups are deliberately absent. They are modal
// responses to something the user just did, and reopening one at startup
// because it was up at shutdown would be noise.
// Every node's stored size, scaled by a window-resize ratio. SizeRef is what
// the docking layout is actually computed from; Size is what is on screen this
// frame. Both are scaled so the layout does not lurch for a frame.
void EditorLayer::RescaleDockTree(ImGuiDockNode* node, const ImVec2& scale)
{
	if (!node)
		return;

	node->SizeRef.x *= scale.x;
	node->SizeRef.y *= scale.y;
	node->Size.x *= scale.x;
	node->Size.y *= scale.y;

	RescaleDockTree(node->ChildNodes[0], scale);
	RescaleDockTree(node->ChildNodes[1], scale);
}

void EditorLayer::LoadPanelState()
{
	std::ifstream file("panels.ini");
	if (!file)
		return;

	std::string line;
	while (std::getline(file, line))
	{
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;

		auto trim = [](std::string text)
		{
			const size_t first = text.find_first_not_of(" \t\r");
			const size_t last = text.find_last_not_of(" \t\r");
			return first == std::string::npos ? std::string{}
											  : text.substr(first, last - first + 1);
		};

		const std::string key = trim(line.substr(0, equals));
		const bool value = trim(line.substr(equals + 1)) == "1";

		if      (key == "hierarchy")       m_ShowHierarchy = value;
		else if (key == "properties")      m_ShowProperties = value;
		else if (key == "viewport")        m_ShowViewport = value;
		else if (key == "game")            m_ShowGameViewport = value;
		else if (key == "content")         m_ShowContentBrowser = value;
		else if (key == "statistics")      m_ShowStatistics = value;
		else if (key == "render-settings") m_ShowRenderSettings = value;
		else if (key == "build-log")       m_ShowScriptBuild = value;
		else if (key == "colliders")       m_ShowColliders = value;
		else if (key == "grid")            m_ShowGrid = value;
		// Not a bool like the rest, so it reads the raw text rather than `value`.
		else if (key == "theme")           m_Theme = EditorTheme::Parse(trim(line.substr(equals + 1)).c_str());
		else if (key == "content-folder")   m_PendingContentFolder = trim(line.substr(equals + 1));
		// The window size the dock layout was saved against, so the first
		// frame can rescale imgui.ini's pixel sizes to today's window.
		else if (key == "layout-width")    m_LastDockSize.x = (float)std::atof(trim(line.substr(equals + 1)).c_str());
		else if (key == "layout-height")   m_LastDockSize.y = (float)std::atof(trim(line.substr(equals + 1)).c_str());
	}
}

RageV::ViewportGridSettings EditorLayer::GridSettings() const
{
	const auto& colors = EditorTheme::Colors();

	ViewportGridSettings settings;

	// The axes come from the theme so the grid and the transform widget name
	// them the same way -- a red line on the floor and a green handle for the
	// same axis is worse than no colour at all.
	settings.AxisXColor = Vec3(colors.AxisX.x, colors.AxisX.y, colors.AxisX.z);
	settings.AxisZColor = Vec3(colors.AxisZ.x, colors.AxisZ.y, colors.AxisZ.z);

	// The lines themselves do not, and that is not an oversight.
	//
	// The first version darkened them in the light theme, on the reasoning that
	// a light theme has a light background. It does not -- the *panels* go pale
	// and the viewport does not, because what is behind the grid is the scene's
	// own sky and ground. A dark line picked for a pale panel was then drawn
	// over a dark horizon and nearly vanished there.
	//
	// This is the mistake EditorTheme.h warns about (inverting a palette rather
	// than authoring one) reaching a surface the theme does not own. The mid
	// grey below is the one value that reads against both a bright sky and a
	// dark ground, which is what the grid is actually drawn on.
	return settings;
}

RageV::EditorIconSettings EditorLayer::IconSettings() const
{
	EditorIconSettings settings;

	// The selected mark takes the theme's accent, so the icon agrees with the
	// hierarchy row and the inspector header about what is selected. The
	// unselected one does *not* follow the theme, for the reason the grid's
	// lines do not: what a mark is drawn against is the scene, not a panel,
	// and a colour picked for a pale panel disappears against a dark horizon.
	const auto& accent = EditorTheme::Colors().Accent;
	settings.SelectedTint = Vec4(accent.x, accent.y, accent.z, 1.0f);

	settings.Selected = m_SceneHierarchyPanel.GetSelectedEntity()
					  ? m_SceneHierarchyPanel.GetSelectedEntity().GetUUID()
					  : UUID::Invalid();

	return settings;
}

// Switching theme is a style change and nothing more: no resources are
// rebuilt, no layout is touched, and it is written straight to disk so the
// choice survives a crash as well as a clean exit.
void EditorLayer::SetTheme(EditorTheme::Theme theme)
{
	if (theme == m_Theme)
		return;

	m_Theme = theme;
	EditorTheme::Apply(m_Theme);
	SavePanelState();
}

void EditorLayer::SavePanelState()
{
	std::ofstream file("panels.ini");
	if (!file)
		return;

	file << "# Which editor panels are open. Rewritten on every clean exit.\n";
	file << "hierarchy = "       << (m_ShowHierarchy ? 1 : 0) << "\n";
	file << "properties = "      << (m_ShowProperties ? 1 : 0) << "\n";
	file << "viewport = "        << (m_ShowViewport ? 1 : 0) << "\n";
	file << "game = "            << (m_ShowGameViewport ? 1 : 0) << "\n";
	file << "content = "         << (m_ShowContentBrowser ? 1 : 0) << "\n";
	file << "statistics = "      << (m_ShowStatistics ? 1 : 0) << "\n";
	file << "render-settings = " << (m_ShowRenderSettings ? 1 : 0) << "\n";
	file << "build-log = "       << (m_ShowScriptBuild ? 1 : 0) << "\n";
	file << "colliders = "       << (m_ShowColliders ? 1 : 0) << "\n";
	file << "grid = "            << (m_ShowGrid ? 1 : 0) << "\n";
	file << "theme = "           << EditorTheme::Name(m_Theme) << "\n";
	file << "content-folder = "  << m_ContentBrowser.GetCurrentFolder() << "\n";
	file << "layout-width = "    << (int)m_LastDockSize.x << "\n";
	file << "layout-height = "   << (int)m_LastDockSize.y << "\n";
}

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
	// Tabbed with Content rather than left to float. Without this the Build
	// Log opens as a free window over whatever is behind it, which on a fresh
	// layout is the hierarchy.
	ImGui::DockBuilderDockWindow("Build Log", bottom);

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
		if (ImGui::MenuItem("New Project...")) NewProject();
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Pick a name and a place; a folder of that name is\n"
							  "made there, with assets/, Scripts/, Source/ and a\n"
							  "starter scene already in it.");
		}
		if (ImGui::MenuItem("Open Project...")) OpenProject();
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("A project is a folder with a .rvproject in it.\n"
							  "Its assets folder is where handles are minted, which\n"
							  "is why they survive a rebuild.");
		}

		if (Project::GetActive())
		{
			if (ImGui::MenuItem("Build Game"))
				BuildGame();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Package this project into a folder someone else\n"
								  "can run: the runtime, the assets, and a config\n"
								  "file, with no editor.\n\n"
								  "Goes into this project's own bin/.");
			}

			if (ImGui::MenuItem("Build Game As..."))
				BuildGameAs();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("The same build, somewhere other than bin/.");

			ImGui::Separator();

			if (ImGui::MenuItem("Build Scripts", "Ctrl+B"))
				BuildScripts();
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Compiles this project's C# in Scripts/ and loads it.\n"
								  "Needs the .NET 8 SDK; the engine itself does not.");
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
		ImGui::MenuItem("Build Log",       nullptr, &m_ShowScriptBuild);
		ImGui::Separator();

		ImGui::MenuItem("Show Grid", "F2", &m_ShowGrid);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("The ground plane at y = 0, in the scene view.\n\n"
							  "Lines every unit and every ten, fading out as they\n"
							  "get too close together to draw. The two coloured\n"
							  "lines are the X and Z axes.");
		}

		ImGui::MenuItem("Show Colliders", "F3", &m_ShowColliders);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Wireframe every collider in the scene view.\n\n"
							  "Green is static, bright green dynamic, blue kinematic,\n"
							  "amber a trigger. While playing, a body the simulation\n"
							  "has put to sleep is drawn dimmed.");
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("Theme"))
		{
			// Radio rather than two checkboxes: these are two states of one
			// setting, and a checkbox each would suggest both could be off.
			if (ImGui::MenuItem("Dark", nullptr, m_Theme == EditorTheme::Theme::Dark))
				SetTheme(EditorTheme::Theme::Dark);
			if (ImGui::MenuItem("Light", nullptr, m_Theme == EditorTheme::Theme::Light))
				SetTheme(EditorTheme::Theme::Light);
			ImGui::EndMenu();
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

	// --- what is being edited, after the menus -------------------------------
	//
	// The menu bar said nothing about the document. A project name and a scene
	// name lived only in the OS title bar, which is the one part of the window
	// nobody looks at while working -- and "is this saved" was not shown
	// anywhere at all.
	//
	// Caption weight and secondary colour, because this is a readout and the
	// menus beside it are controls. It is information, not a place to click.
	{
		ImGui::SameLine(0.0f, EditorTheme::Space::Wide);

		const std::string project = Project::GetActive() ? Project::Config().Name
														: std::string("No project");
		const std::string scene = m_ScenePath.empty()
								? std::string("Untitled scene")
								: m_ScenePath.filename().string();

		UI::TextCaption("%s", project.c_str());

		ImGui::SameLine(0.0f, EditorTheme::Space::Snug);
		UI::PushTextScale(EditorTheme::Type::Caption);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextDisabled);
		ImGui::TextUnformatted("/");
		ImGui::PopStyleColor();
		UI::PopTextScale();

		ImGui::SameLine(0.0f, EditorTheme::Space::Snug);
		UI::TextCaption("%s", scene.c_str());

		// A dot rather than an asterisk in the name: the name is the name, and
		// a mark beside it can be looked at or ignored without re-reading the
		// word it was glued to.
		if (m_Commands.CanUndo())
		{
			ImGui::SameLine(0.0f, EditorTheme::Space::Snug);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float radius = ImGui::GetTextLineHeight() * 0.16f;
			ImGui::GetWindowDrawList()->AddCircleFilled(
				{ at.x + radius, at.y + ImGui::GetTextLineHeight() * 0.5f }, radius,
				ImGui::GetColorU32(EditorTheme::Colors().Accent));
			ImGui::Dummy({ radius * 2.0f, ImGui::GetTextLineHeight() });
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Unsaved changes since the last save.");
		}
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
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, EditorTheme::Colors().AccentMuted);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, EditorTheme::Colors().Accent);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextPrimary);

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
	// The same inset on the left as the right edge gets, so the row is
	// centred in its own bar. The right side had an explicit gap and the left
	// had none, which left the gizmo buttons clinging to the window edge while
	// the camera toggle sat comfortably off it.
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + EditorTheme::Space::Roomy);

	auto Gap = [](float width = 10.0f)
	{
		ImGui::SameLine(0.0f, width);
	};

	const bool running = m_SceneState != SceneState::Edit;

	// --- transform tools, left ----------------------------------------------
	// Icons rather than "Mov"/"Rot"/"Scl". Three-letter abbreviations were the
	// last place in the editor still speaking a different visual language from
	// everything around them, and an abbreviation is worse than either an icon
	// or the whole word: it has to be decoded and it still is not the label.
	// The tooltip carries the name and the shortcut.
	auto ModeButton = [&](const char* id, UI::IconKind icon, ImGuizmo::OPERATION op,
						  const char* tip)
	{
		if (UI::IconButton(id, icon, tip, m_GizmoOperation == op))
			m_GizmoOperation = op;
	};

	ModeButton("##translate", UI::IconKind::ToolTranslate,
			   ImGuizmo::OPERATION::TRANSLATE, "Translate (W)");
	Gap(4.0f);
	ModeButton("##rotate", UI::IconKind::ToolRotate,
			   ImGuizmo::OPERATION::ROTATE, "Rotate (E)");
	Gap(4.0f);
	ModeButton("##scale", UI::IconKind::ToolScale,
			   ImGuizmo::OPERATION::SCALE, "Scale (R)");

	// Widths measured from the label rather than typed in pixels.
	//
	// The toolbar used fixed numbers -- 52 for this, 96 for the camera toggle
	// -- which are correct at 100% and too narrow at every other UI scale. At
	// --ui-scale=2 the font doubles and the button does not, so "Local" became
	// "Loc" and "Editor Cam" became "Editor".
	auto FitButton = [](const char* label)
	{
		return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f
			 + EditorTheme::Space::Base;
	};

	Gap();
	if (ImGui::Button(m_GizmoLocal ? "Local" : "World",
					  ImVec2(FitButton("World"), 0.0f)))
		m_GizmoLocal = !m_GizmoLocal;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Gizmo space. Scaling is always local.");

	Gap();

	// A toggle button, not a checkbox.
	//
	// ImGui draws a checkbox as an empty framed square with its label to the
	// right, so in a row of icon buttons it read as a button with nothing in
	// it -- the one control in the toolbar that looked broken. A toggle that
	// fills with the accent when it is on says the same thing in the same
	// language as the three gizmo buttons beside it.
	if (UI::IconButton("##snap", UI::IconKind::SnapGrid,
					   "Snap to increments while dragging.\n\n"
					   "Hold Ctrl for the same effect without leaving it on.",
					   m_SnapEnabled))
		m_SnapEnabled = !m_SnapEnabled;

	// A full gap rather than the tight one, because this starts a different
	// group: everything to the left of it changes the scene, and this only
	// changes what is drawn.
	Gap();
	if (UI::IconButton("##grid", UI::IconKind::GroundGrid,
					   "Show the ground grid in the scene view.\n\n"
					   "Scene view only -- the game view is what a player would see.",
					   m_ShowGrid))
		m_ShowGrid = !m_ShowGrid;

	// --- transport, centred -------------------------------------------------
	// Centred because it is the control people reach for most, and because that
	// is where every other engine puts it.
	{
		const float kPlayWidth = FitButton("Stop");
		const float kPauseWidth = FitButton("Resume");
		const float transportWidth = kPlayWidth + kPauseWidth + 4.0f;

		const float centre = (ImGui::GetWindowWidth() - transportWidth) * 0.5f;
		// Never behind what is already drawn: at a narrow window the tools on
		// the left win and the transport simply sits after them.
		ImGui::SameLine(ImMax(centre, ImGui::GetCursorPosX() + 10.0f));

		// Accent-filled while it is the active one, and through the helper so
		// the label's colour comes with the fill. See UI::AccentButton.
		const bool pressed = running
			? UI::AccentButton(running ? "Stop" : "Play", ImVec2(kPlayWidth, 0.0f))
			: ImGui::Button(running ? "Stop" : "Play", ImVec2(kPlayWidth, 0.0f));
		if (pressed)
		{
			if (running) OnSceneStop();
			else         OnScenePlay();
		}

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
		const float width = FitButton("Editor Cam");

		const float rightEdge = ImGui::GetWindowWidth() - width - EditorTheme::Space::Roomy;
		ImGui::SameLine(ImMax(rightEdge, ImGui::GetCursorPosX() + 10.0f));

		// Filled while the viewport is showing the scene's camera rather than
		// the editor's, because that is the state somebody can be surprised by.
		const bool pressed = !m_UseEditorCamera
			? UI::AccentButton(label, ImVec2(width, 0.0f))
			: ImGui::Button(label, ImVec2(width, 0.0f));
		if (pressed)
			m_UseEditorCamera = !m_UseEditorCamera;

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

	// The headline is the **median** of the history, not the last frame.
	//
	// The instantaneous value was the headline, and one number with no context
	// cannot tell a one-off spike from a sustained problem. A startup frame
	// carrying a 133 ms environment prefilter -- or any frame a screenshot
	// readback stalled -- rendered identically to an engine that had genuinely
	// fallen over, and read as ~600 ms with nothing on screen to say otherwise.
	//
	// A median ignores the spike; the p95 beside it is where the spike shows up
	// without being able to shout; the sparkline still draws it. Between them
	// the panel can say "mostly fine, occasionally not" instead of picking one
	// frame and presenting it as the truth.
	float sorted[IM_ARRAYSIZE(m_FrameHistory)];
	int samples = 0;
	for (float value : m_FrameHistory)
		if (value > 0.0f)
			sorted[samples++] = value;

	float median = m_FrameTimeMs;
	float p95 = m_FrameTimeMs;
	if (samples > 0)
	{
		std::sort(sorted, sorted + samples);
		median = sorted[samples / 2];
		p95 = sorted[(int)((samples - 1) * 0.95f)];
	}

	const float fps = median > 0.0f ? 1000.0f / median : 0.0f;

	UI::PushTextScale(EditorTheme::Type::Display);
	ImGui::Text("%.2f ms", median);
	UI::PopTextScale();

	ImGui::SameLine(0.0f, EditorTheme::Space::Base);
	ImGui::AlignTextToFramePadding();
	UI::TextCaption("median  %.0f FPS", fps);

	UI::TextCaption("p95 %.2f ms    last %.2f ms    %d frames",
					p95, m_FrameTimeMs, samples);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Median over the last %d frames, so a single slow frame -- "
						  "shader compilation, an asset load, the first frame after "
						  "Play -- does not become the number you read.\n\n"
						  "p95 is where those spikes show. If p95 is far above the "
						  "median, something stutters even though the average looks "
						  "fine.", samples);
	}

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
		float gpuAccounted = 0.0f;
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			const auto phase = (FramePhase)i;
			const float cpu = FrameProfiler::LivePhaseMs(phase);
			const float gpu = FrameProfiler::LiveGpuPhaseMs(phase);
			cpuTotal += cpu;
			if (gpu > 0.0f)
				gpuAccounted += gpu;

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
		// Two different kinds of number, so two rows rather than one.
		//
		// "accounted" is the sum of the phases above it. "whole frame" is a
		// single span from the first timestamp to the last, which includes any
		// gap between passes and is not the sum of anything. Putting them in
		// one row labelled "total" made the CPU column drop with its phases
		// while the GPU column did not, which reads as a bug in the numbers.
		const float gpuFrame = FrameProfiler::LiveGpuFrameMs();

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled("accounted");
		ImGui::TableNextColumn(); ImGui::TextDisabled("%.3f", cpuTotal);
		ImGui::TableNextColumn(); ImGui::TextDisabled("%.3f", gpuAccounted);

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled("whole frame");
		ImGui::TableNextColumn(); ImGui::TextDisabled("%.3f", m_FrameTimeMs);
		ImGui::TableNextColumn();
		if (gpuFrame > 0.0f)
			ImGui::TextDisabled("%.3f", gpuFrame);
		else
			ImGui::TextDisabled("--");

		// The line the panel was missing, and the reason the numbers looked
		// like they should add up to the frame and did not. On a vsync-locked
		// frame this is most of it.
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled("waiting");
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%.3f", Math::Max(m_FrameTimeMs - cpuTotal, 0.0f));
		ImGui::TableNextColumn(); ImGui::TextDisabled("--");

		ImGui::EndTable();
	}

	const bool vsync = Renderer::HasDevice() && Renderer::GetDevice().IsVSync();

	if (!FrameProfiler::HasGpuTimings())
		ImGui::TextDisabled("No GPU timings: this device has no timestamp queries.");
	else if (vsync)
	{
		// Read from the device, not from the startup config: the config is
		// what was asked for at launch and says nothing about what the
		// swapchain is presenting with now.
		ImGui::TextDisabled("Vsync is on, so \"waiting\" is the display holding the "
							"frame. Turn it off in Render Settings to see the cost.");
	}
	else
	{
		ImGui::TextDisabled("Vsync is off; \"waiting\" is the CPU idle on the GPU "
							"or the present.");
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

	UI::SectionHeader("Presentation");

	// Seeded from the device every frame rather than remembered, so a window
	// that started with --vsync=off does not show a ticked box, and so the
	// control and the readout above can never disagree.
	if (Renderer::HasDevice())
		m_VSync = Renderer::GetDevice().IsVSync();

	if (UI::RowCheckbox("VSync",
		&m_VSync,
		"Vulkan swaps present mode between FIFO and immediate; OpenGL sets the swap interval.\n"
			   "Off tears, and is saved for the next start.") && Renderer::HasDevice())
	{
		Renderer::GetDevice().SetVSync(m_VSync);

		// Saved as well as applied, so a plain restart keeps the choice --
		// the same contract as the backend picker above.
		EngineConfig::SaveVSyncPreference(m_VSync);
	}

	UI::SectionHeader("Debug");

	if (UI::RowCheckbox("Wireframe",
		&m_Wireframe,
		"Rebuilds the pipeline with a line polygon mode. Polygon mode is baked into "
			   "the pipeline on Vulkan, so this is not a free state toggle."))
		Renderer::SetWireframe(m_Wireframe);

	UI::RowColor3("Clear colour", Math::ValuePtr(m_ClearColor));

	UI::SectionHeader("Environment");

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

		UI::RowColor3("Ambient colour", Math::ValuePtr(environment.AmbientColor));
		trackAmbient("Ambient colour");
		UI::RowDragFloat("Ambient intensity",
		&environment.AmbientIntensity,
		0.005f,
		0.0f,
		4.0f,
		"%.3f",
		"A single colour arriving from every direction. It cannot vary with view "
				   "angle or roughness the way a real environment does -- image-based lighting "
				   "replaces it, and falls back to it for scenes with no environment map.\n\n"
				   "Set the intensity to 0 for pure direct lighting.");
		trackAmbient("Ambient intensity");

		UI::SectionHeader("Sky");

		const char* skyModes[] = { "Colour", "Gradient", "Environment map" };
		int sky = (int)environment.Sky;
		if (UI::RowCombo("Background",
		&sky,
		skyModes,
		IM_ARRAYSIZE(skyModes),
		"Colour draws nothing and leaves the clear colour, which is what a 2D or "
				   "UI-only scene wants.\n\nGradient costs no asset.\n\nAn environment map is "
				   "a panorama -- .hdr for values brighter than white -- or one face of a "
				   "six-file set, in which case the other five come with it."))
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

		if (environment.Sky == SkyType::Gradient)
		{
			UI::RowColor3("Horizon", Math::ValuePtr(environment.SkyHorizon));
			trackAmbient("Sky horizon");
			UI::RowColor3("Zenith", Math::ValuePtr(environment.SkyZenith));
			trackAmbient("Sky zenith");
			UI::RowColor3("Ground", Math::ValuePtr(environment.SkyGround));
			trackAmbient("Sky ground");
		}

		if (environment.Sky == SkyType::Cubemap)
		{
			const std::string name = Assets::Manager::GetDisplayName(environment.SkyTexture);
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
					if (Assets::Registry::GetMetadata(dropped).Type == AssetType::Texture)
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

			float degrees = Math::Degrees(environment.SkyRotation);
			if (UI::RowDragFloat("Sky rotation",
		&degrees,
		0.5f,
		-360.0f,
		360.0f,
		"%.1f deg",
		"A panorama points wherever it was shot, and the scene was not built "
					   "to match it."))
				environment.SkyRotation = Math::Radians(degrees);
			trackAmbient("Sky rotation");
		}

		if (environment.Sky != SkyType::Color)
		{
			UI::RowDragFloat("Sky intensity", &environment.SkyIntensity, 0.01f, 0.0f, 16.0f);
			trackAmbient("Sky intensity");
		}

		UI::SectionHeader("Post processing");

		UI::RowDragFloat("Exposure",
		&environment.Exposure,
		0.01f,
		0.01f,
		16.0f,
		"%.3f",
		"Applied before the tone curve, which is what makes this an exposure "
				   "control rather than a brightness one: it slides the scene along the "
				   "response curve instead of scaling the result of it.");
		trackAmbient("Exposure");

		UI::RowCheckbox("Bloom", &environment.BloomEnabled);
		trackAmbient("Bloom");

		if (environment.BloomEnabled)
		{
			UI::RowDragFloat("Threshold",
		&environment.BloomThreshold,
		0.01f,
		0.0f,
		16.0f,
		"%.3f",
		"Brightness at which a pixel starts to bleed. Above 1, only things "
					   "genuinely brighter than white glow.");
			trackAmbient("Bloom threshold");

			UI::RowDragFloat("Knee",
		&environment.BloomKnee,
		0.01f,
		0.0f,
		4.0f,
		"%.3f",
		"Width of the ramp around the threshold. Zero is a hard cut, which "
					   "pops as something crosses it and reads as flickering.");
			trackAmbient("Bloom knee");

			UI::RowDragFloat("Intensity", &environment.BloomIntensity, 0.002f, 0.0f, 2.0f);
			trackAmbient("Bloom intensity");
		}

		// Only the modes that exist. Offering TAA here and doing nothing would
		// be worse than not offering it; the roadmap is where "not yet"
		// belongs.
		const char* aaModes[] = { "None", "FXAA", "SMAA" };
		int aa = (int)environment.AA;
		if (UI::RowCombo("Anti-aliasing",
		&aa,
		aaModes,
		IM_ARRAYSIZE(aaModes),
		"FXAA is one pass over the tone-mapped image: cheap, no prerequisites, "
				   "and it softens the picture slightly.\n\n"
				   "SMAA reconstructs the edge instead of guessing at it -- it finds the "
				   "run of pixels an edge spans, works out which way the real line sloped, "
				   "and computes the coverage from that. Three passes over two small "
				   "intermediates, and sharper for it. It has no diagonal pass yet, so a "
				   "45-degree edge is the one case FXAA can still win.\n\n"
				   "TAA is better than either and needs motion vectors -- every mesh "
				   "carrying its previous transform and the renderer writing a velocity "
				   "target. That is a renderer feature with its own prerequisites, not a "
				   "post pass."))
			environment.AA = (AntiAliasing)aa;
	}

	UI::SectionHeader("Lighting");

	// Shadows are not implemented. A toggle wired to nothing would be worse
	// than no toggle, so it is present, disabled, and says why.
	bool shadows = false;
	ImGui::BeginDisabled();
	UI::RowCheckbox("Shadows",
		&shadows,
		"Meshes use pbr.rvshader with the metallic-roughness parameterisation. "
			   "Surfaces reflect the scene's environment, with roughness picking a mip "
			   "rather than a real prefiltered convolution; the diffuse ambient is still "
			   "the flat term above. The shader writes linear HDR and the tone curve is "
			   "its own pass.");
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Not implemented yet.\n\nThe RHI already carries what shadows need -- depth-only\n"
						  "render targets, comparison samplers, slope-scaled depth bias,\n"
						  "cubemap and array textures -- but no shadow pass exists.");

	ImGui::TextDisabled("Shading: Cook-Torrance PBR");

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

	// Only while the scene is actually running, and only when this panel is the
	// one showing it. Hovering a button in edit mode must not light it up: the
	// rectangle under the cursor there is something being *authored*, and the
	// click belongs to picking.
	CapturePointer(imageOrigin, viewportSize, m_ViewportSize,
				   m_IsViewportHovered && m_SceneState != SceneState::Edit);

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
		// Captured before the image is drawn, which is where the cursor still
		// reports this panel's own top-left corner.
		const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();

		const ImTextureID handle = (ImTextureID)color->GetImGuiHandle();
		const bool flip = Renderer::GetDevice().GetBackend() == RHI::Backend::OpenGL;
		ImGui::Image(handle, size,
					 ImVec2{ 0, flip ? 1.0f : 0.0f }, ImVec2{ 1, flip ? 0.0f : 1.0f });

		// This panel wins over the scene view when the cursor is in it, because
		// it is drawn second and the two cannot both be hovered.
		CapturePointer(imageOrigin, size, m_GameViewportSize,
					   ImGui::IsWindowHovered() && m_SceneState != SceneState::Edit);

		// Which camera won, and why -- otherwise a scene with several cameras
		// gives no clue about what is being looked through.
		ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
		ImGui::TextColored(EditorTheme::Colors().Accent, "%s", camera.GetName().c_str());
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
	Mat4 cameraView;
	Mat4 cameraProjection;

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

		cameraView = Math::Inverse(m_Scene->GetWorldTransform(cameraEntity));
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
	Mat4 transform = m_Scene->GetWorldTransform(selected);

	const bool snap = m_SnapEnabled || Input::IsKeyPressed(RV_KEY_LEFT_CONTROL);
	float snapValue = m_SnapTranslate;
	if (m_GizmoOperation == ImGuizmo::OPERATION::ROTATE) snapValue = m_SnapRotate;
	if (m_GizmoOperation == ImGuizmo::OPERATION::SCALE)  snapValue = m_SnapScale;
	const float snapValues[3] = { snapValue, snapValue, snapValue };

	// Scale is meaningless in world space, so force local for it.
	const ImGuizmo::MODE mode = (m_GizmoLocal || m_GizmoOperation == ImGuizmo::OPERATION::SCALE)
							  ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

	ImGuizmo::Manipulate(Math::ValuePtr(cameraView), Math::ValuePtr(cameraProjection),
						 m_GizmoOperation, mode, Math::ValuePtr(transform),
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
		const Mat4 local = Math::Inverse(m_Scene->GetParentWorldTransform(selected)) * transform;

		Vec3 position, scale;
		Quat rotation;
		Math::Decompose(local, position, rotation, scale);

		const Vec3 euler = Math::ToEuler(rotation);
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

	// The name at title weight rather than in the accent. Red means "you can
	// act on this", and a product name is not a control -- it was the one
	// remaining place the accent was being used as decoration.
	UI::PushTextScale(EditorTheme::Type::Title);
	ImGui::TextUnformatted("RageV Engine");
	UI::PopTextScale();

	UI::TextCaption("A Vulkan/OpenGL renderer with an EnTT scene system.");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (Renderer::HasDevice())
	{
		const auto& caps = Renderer::GetDevice().GetCaps();
		if (UI::BeginProperties("##about"))
		{
			UI::RowText("Backend", caps.APIName.c_str());
			UI::RowText("Device", caps.DeviceName.c_str());
			UI::EndProperties();
		}
	}

	ImGui::Spacing();
	UI::TextCaption("Switch backend with --rhi=vulkan|opengl, or by editing "
					"ragev.ini next to the executable.");
	ImGui::Spacing();

	// Accent-filled: a modal with one button should say which one it is, and
	// this is the only action in the dialog.
	if (UI::AccentButton("Close", ImVec2(-1.0f, 0.0f)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

void EditorLayer::DrawBackendRestartPopup()
{
	if (m_ShowBackendRestart)
	{
		ImGui::OpenPopup("Restart required");
		m_ShowBackendRestart = false;
		m_BackendSaveAttempted = false;
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

	// Written once, on the frame the popup opens, and not on every frame it is
	// visible -- which is what a call in the draw path means, and what put two
	// "preference saved" lines in the log for one change.
	//
	// Still before either button: whichever the user picks, the preference is
	// what they asked for, and restarting later must not mean forgetting.
	if (!m_BackendSaveAttempted)
	{
		m_BackendSaved = EngineConfig::SaveBackendPreference(m_PendingBackend);
		m_BackendSaveAttempted = true;
	}

	if (!m_BackendSaved)
	{
		ImGui::TextColored(EditorTheme::Colors().Warning,
						   "Could not write ragev.ini; the choice will not survive.");
		ImGui::Spacing();
	}

	const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

	// The destructive one is the accented one, because in a two-button dialog
	// the reader needs to know which is the commitment before reading either
	// label. "Restart Later" stays plain: doing nothing should never be the
	// loudest thing on screen.
	if (UI::AccentButton("Restart Now", ImVec2(width, 0.0f)))
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

		// Function keys rather than letters: both are toggled while looking at
		// the scene, often with the other hand on the camera controls, and every
		// unmodified letter near WASD is already a gizmo.
		case RV_KEY_F2: m_ShowGrid = !m_ShowGrid; return true;
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
	const float radius = Math::MaxComponent(Math::Abs(transform.Scale)) * 1.2f;
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
		transform.Rotation = Math::Radians(Vec3(-12.0f, 0.0f, 0.0f));
	}

	auto place = [&](PrimitiveType primitive, const Vec3& position, const Vec3& scale,
					 const Vec4& color, float metallic, float roughness, const char* name,
					 Entity parent = {})
	{
		Entity entity = m_Scene->CreateEntity(name);
		auto& mesh = entity.AddComponent<MeshComponent>(primitive);

		mesh.OverrideBaseColor = true;
		mesh.BaseColor = color;
		mesh.OverrideMetallic = true;
		mesh.Metallic = metallic;
		mesh.OverrideRoughness = true;
		mesh.Roughness = roughness;

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

	place(PrimitiveType::Cube,     { -3.0f, 0.0f, 0.0f }, Vec3(1.5f),
		  { 0.85f, 0.17f, 0.19f, 1.0f }, 0.0f, 0.35f, "Cube (dielectric)");
	place(PrimitiveType::Sphere,   {  0.0f, 0.1f, 0.0f }, Vec3(1.8f),
		  { 0.94f, 0.78f, 0.38f, 1.0f }, 1.0f, 0.20f, "Sphere (gold)");
	place(PrimitiveType::Cylinder, {  3.0f, 0.0f, 0.0f }, Vec3(1.4f),
		  { 0.90f, 0.91f, 0.92f, 1.0f }, 1.0f, 0.45f, "Cylinder (brushed metal)");

	// A rough/smooth pair behind, to make the roughness axis visible directly.
	place(PrimitiveType::Sphere, { -1.6f, -0.5f, -3.0f }, Vec3(0.9f),
		  { 0.80f, 0.80f, 0.82f, 1.0f }, 0.0f, 0.08f, "Sphere (smooth)");
	place(PrimitiveType::Sphere, {  1.6f, -0.5f, -3.5f }, Vec3(0.9f),
		  { 0.80f, 0.80f, 0.82f, 1.0f }, 0.0f, 0.95f, "Sphere (rough)");

	// A parented arrangement, so the hierarchy is exercised by the scene the
	// editor opens on rather than only by the round-trip test. Dragging the
	// pedestal moves the whole stack; the children keep their local offsets.
	Entity pedestal = m_Scene->CreateEntity("Pedestal");
	pedestal.GetComponent<TransformComponent>().Position = { -6.5f, -1.0f, -1.0f };

	place(PrimitiveType::Cylinder, { -6.5f, -0.6f, -1.0f }, { 1.2f, 0.6f, 1.2f },
		  { 0.22f, 0.23f, 0.26f, 1.0f }, 0.0f, 0.7f, "Pedestal Base", pedestal);
	place(PrimitiveType::Cube, { -6.5f, 0.2f, -1.0f }, Vec3(0.55f),
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
		sun.GetComponent<TransformComponent>().Rotation = Math::Radians(Vec3(-55.0f, -30.0f, 0.0f));
	}

	// An imported glTF alongside the generated primitives, so the demo scene
	// exercises the asset path rather than only the built-in one. Absent
	// quietly if the file is not there -- a missing sample should not stop the
	// editor opening.
	if (const AssetHandle model = Assets::Registry::GetHandle("models/testcube.gltf"); model.IsValid())
	{
		if (Entity imported = Assets::Manager::InstantiateModel(*m_Scene, model))
		{
			auto& transform = imported.GetComponent<TransformComponent>();
			transform.Position = { 6.0f, 0.0f, -1.0f };
			transform.Scale = Vec3(1.6f);
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
		mesh.OverrideBaseColor = true;
		mesh.BaseColor = { 0.30f, 0.55f, 0.85f, 1.0f };
		mesh.OverrideRoughness = true;
		mesh.Roughness = 0.4f;

		auto& transform = box.GetComponent<TransformComponent>();
		// Offset slightly on each axis so they topple rather than landing in a
		// perfect column, which reads as nothing happening.
		transform.Position = { -0.4f + i * 0.22f, 4.0f + i * 1.6f, 0.35f - i * 0.18f };
		transform.Scale = Vec3(0.7f);

		box.AddComponent<RigidBodyComponent>(BodyType::Dynamic);
		box.AddComponent<ColliderComponent>().HalfExtents = Vec3(0.5f);

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
			sound.Clip = Assets::Registry::GetHandle("audio/impact.wav");
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
		chime.Clip = Assets::Registry::GetHandle("audio/chime.wav");
		chime.PlayOnAwake = false;
		chime.Volume = 0.7f;
	}

	// A looping spatial drone on the pedestal, off to one side, so the demo
	// scene proves the parts of audio that a one-shot cannot: that a sound
	// loops without a seam, that it starts with the scene, and that it is
	// panned and attenuated by where it is relative to the camera.
	{
		auto& ambience = pedestal.AddComponent<AudioSourceComponent>();
		ambience.Clip = Assets::Registry::GetHandle("audio/hum.wav");
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
	Assets::Registry::Refresh();

	const std::filesystem::path absolute = std::filesystem::absolute(filepath);
	std::error_code error;
	const std::filesystem::path relative =
		std::filesystem::relative(absolute, Assets::Registry::Root(), error);

	if (error || relative.empty() || relative.native().rfind(L"..", 0) == 0)
	{
		RV_WARN("'{0}' is outside the assets folder. Copy it in first -- an asset "
				"the registry cannot see has no handle, so nothing referring to it "
				"would survive a reload.", filepath);
		return;
	}

	const AssetHandle handle = Assets::Registry::GetHandle(relative.generic_string());
	if (!handle.IsValid())
	{
		RV_WARN("No asset handle for '{0}'", relative.generic_string());
		return;
	}

	Entity root = Assets::Manager::InstantiateModel(*m_Scene, handle);
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

	const AssetHandle handle = Assets::Manager::CreatePrefab(*m_Scene, selected, relative);
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
		case AssetType::Mesh:   root = Assets::Manager::InstantiateModel(*m_Scene, handle); break;
		case AssetType::Prefab: root = Assets::Manager::InstantiatePrefab(*m_Scene, handle); break;
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
// Create a project folder, fill it in, and open it.
//
// The dialog asks for a file because the platform layer only has file dialogs,
// but what gets made is a *folder* named after the project with the .rvproject
// inside it. Creating the project file directly wherever the user pointed would
// scatter assets/ and bin/ into whatever directory that happened to be, which
// is how a Downloads folder ends up with a game engine in it.
void EditorLayer::NewProject()
{
	const std::string chosen = FileDialogs::SaveFile("RageV Project (*.rvproject)\0*.rvproject\0");
	if (chosen.empty())
		return;

	CreateProjectAt(chosen);
}

// The dialog's result names both the project and the folder to put it in --
// split from the dialog so the creation itself can be driven without one.
void EditorLayer::CreateProjectAt(const std::filesystem::path& picked)
{
	const std::string name = picked.stem().string();
	if (name.empty())
	{
		RV_WARN("A project needs a name");
		return;
	}

	const std::filesystem::path directory = picked.parent_path() / name;

	if (!Project::Create(directory, name))
		return;

	// The new project's assets are empty, but the registry still has to be
	// pointed at them before anything can be saved into it -- a scene written
	// while the registry points at the *old* project mints handles that mean
	// nothing here.
	Assets::Manager::ClearCache();
	Assets::Registry::Init(Project::AssetRoot());

	NewScene();
	PopulateStarterScene();

	const std::filesystem::path scene = Project::AssetPath("scenes/Main.rage");
	SceneSerializer serializer(m_Scene);
	if (!serializer.Serialize(scene.string()))
	{
		RV_ERROR("Created the project but could not write its first scene");
		return;
	}

	m_ScenePath = scene;
	Project::Config().StartScene = "scenes/Main.rage";
	Project::Save();

	RV_INFO("Created project '{0}' at {1}", name, directory.string());
}

// What a new project opens on.
//
// Deliberately not an empty scene. An engine that opens on nothing makes the
// first five minutes an exercise in finding out which of the six things you
// need is missing -- there is no light, so everything is black, and that reads
// as a broken install rather than an empty scene. A ground plane, a light and
// two objects means Play does something immediately and every part of the
// pipeline has proved itself before the user has touched anything.
void EditorLayer::PopulateStarterScene()
{
	if (Entity camera = m_Scene->GetPrimaryCameraEntity())
	{
		auto& transform = camera.GetComponent<TransformComponent>();
		transform.Position = { 0.0f, 2.0f, 6.0f };
		transform.Rotation = Math::Radians(Vec3(-12.0f, 0.0f, 0.0f));
	}

	const auto place = [&](PrimitiveType primitive, const char* name, const Vec3& position,
						   const Vec3& scale, const Vec4& colour, float metallic, float roughness)
	{
		Entity entity = m_Scene->CreateEntity(name);
		auto& mesh = entity.AddComponent<MeshComponent>(primitive);

		mesh.OverrideBaseColor = true;
		mesh.BaseColor = colour;
		mesh.OverrideMetallic = true;
		mesh.Metallic = metallic;
		mesh.OverrideRoughness = true;
		mesh.Roughness = roughness;

		auto& transform = entity.GetComponent<TransformComponent>();
		transform.Position = position;
		transform.Scale = scale;
		return entity;
	};

	place(PrimitiveType::Plane, "Ground", { 0.0f, 0.0f, 0.0f }, { 12.0f, 1.0f, 12.0f },
		  { 0.16f, 0.16f, 0.18f, 1.0f }, 0.0f, 0.9f);
	place(PrimitiveType::Cube, "Cube", { -1.2f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f },
		  { 0.78f, 0.22f, 0.22f, 1.0f }, 0.0f, 0.45f);
	place(PrimitiveType::Sphere, "Sphere", { 1.2f, 0.6f, 0.0f }, { 1.2f, 1.2f, 1.2f },
		  { 0.85f, 0.85f, 0.88f, 1.0f }, 1.0f, 0.2f);

	// A directional light, angled rather than straight down: a light pointing
	// along an axis produces flat shading and a shadow directly underneath,
	// which makes it hard to tell whether shadows work at all.
	Entity sun = m_Scene->CreateEntity("Sun");
	auto& light = sun.AddComponent<LightComponent>();
	light.Light.Type = Light::LightType::Directional;
	light.Light.Intensity = 3.0f;
	sun.GetComponent<TransformComponent>().Rotation = Math::Radians(Vec3(-55.0f, -35.0f, 0.0f));
}

void EditorLayer::OpenProject()
{
	const std::string filepath = FileDialogs::OpenFile("RageV Project (*.rvproject)\0*.rvproject\0");
	if (filepath.empty())
		return;

	// Before Project::Load, which unloads the old project's game module: a
	// playing scene may hold instances whose code lives in that module, and
	// they must be gone before the DLL is.
	if (m_SceneState != SceneState::Edit)
		OnSceneStop();

	if (!Project::Load(filepath))
		return;

	// The cache is keyed by handle, and handles from the old project mean
	// something else -- or nothing -- in the new one.
	Assets::Manager::ClearCache();
	Assets::Registry::Init(Project::AssetRoot());

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
// Into the project's own bin/ by default, because that is where the output of a
// project belongs: the folder can be zipped, moved or handed over with its build
// intact, and nobody has to remember where they put the last one.
void EditorLayer::BuildGame()
{
	if (!Project::GetActive())
		return;

	BuildInto(Project::BinaryRoot() / Project::Config().Name);
}

// Somewhere else, for when the default is not what is wanted -- a network share,
// a folder an installer picks up from.
//
// A folder dialog would be better than a file one, but the platform layer only
// has file dialogs; asking for a name inside the target folder and using its
// parent is the honest version of that until it grows one.
void EditorLayer::BuildGameAs()
{
	if (!Project::GetActive())
		return;

	const std::string chosen = FileDialogs::SaveFile("Build folder\0*.*\0");
	if (chosen.empty())
		return;

	BuildInto(std::filesystem::path(chosen).parent_path());
}

void EditorLayer::BuildInto(const std::filesystem::path& output)
{
	PackageDesc desc;
	desc.OutputDirectory = output;
	// The target usually already holds the previous build. Refusing here would
	// make the menu item unusable after its first use; the CLI keeps the guard,
	// where a scripted build could otherwise flatten a directory nobody looked
	// at.
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


// Start building the project's scripts -- the C++ module, then C# -- on a
// worker thread.
//
// Asynchronous because a module build is tens of seconds the first time, and
// a person should be able to keep arranging a scene while it runs. What is
// *not* asynchronous is the hand-back: results are published by FinishBuild
// on the main thread, so nothing downstream of a build ever runs concurrently
// with the editor's own state.
void EditorLayer::BuildScripts()
{
	if (!Project::GetActive())
		return;

	// One build at a time. Pressing Ctrl+B during a build brings the console
	// to front rather than queueing a second compiler behind the first.
	if (m_BuildInFlight)
	{
		m_ShowScriptBuild = true;
		return;
	}

	m_ShowScriptBuild = true;
	m_BuildInFlight = true;
	m_BuildCancel = false;
	m_BuildDone = false;
	m_WorkerRanModule = false;
	m_WorkerRanScripts = false;
	{
		std::lock_guard<std::mutex> lock(m_BuildLogMutex);
		m_BuildLiveLog.clear();
	}

	// Everything the worker needs, captured by value. It must not touch
	// Project::* -- the main thread can close or switch projects while it
	// runs, and half of one project with half of another is worse than a
	// build against a stale snapshot.
	const std::filesystem::path root = Project::Root();
	const std::string name = Project::Config().Name;
	const std::filesystem::path csproj = Managed::ScriptBuild::ProjectFileFor(root, name);
	const std::filesystem::path scriptsOut = root / "Scripts" / "bin";
	// The engine's own class library, staged beside the executable. Absolute,
	// because MSBuild resolves a relative HintPath against the .csproj's
	// directory, not against this process's working directory -- passed
	// relative, every editor C# build failed with a missing-type cascade.
	const std::filesystem::path scriptCore =
		std::filesystem::absolute("managed/RageV.ScriptCore.dll");

	// The loaded module holds its own DLL open, so the linker cannot write the
	// new one: it has to be unloaded before the build starts, and that is only
	// safe while nothing instantiated from it is alive. Instances exist only
	// during Play -- so a build started mid-play stops the scene first and
	// resumes it when the build lands. The Unity loop: change, Ctrl+B, and the
	// scene restarts on the new code.
	if (m_SceneState != SceneState::Edit)
	{
		RV_INFO("Stopping the scene to swap scripts; Play resumes when the build lands");
		m_ResumePlayAfterBuild = true;
		OnSceneStop();
	}

	const bool buildModule = ModuleBuild::ProjectHasModule(root);
	if (buildModule)
		GameModule::Unload();

	// The console's supply line. Called from the worker for every chunk the
	// compiler prints; the panel reads under the same lock each frame.
	auto tee = [this](const char* text, size_t length)
	{
		std::lock_guard<std::mutex> lock(m_BuildLogMutex);
		m_BuildLiveLog.append(text, length);
	};

	m_BuildThread = std::thread([this, root, name, csproj, scriptsOut, scriptCore, tee, buildModule]()
	{
		// C++ first: it is the slower half, so cancelling early saves the
		// most, and its output is what the console mostly exists to show.
		if (buildModule)
		{
			m_WorkerRanModule = true;
			m_WorkerModule = ModuleBuild::Build(root, name, &m_BuildCancel, tee);
		}

		std::error_code ec;
		if (!m_BuildCancel && std::filesystem::exists(csproj, ec))
		{
			m_WorkerRanScripts = true;
			m_WorkerScripts = Managed::ScriptBuild::Build(csproj, scriptsOut, scriptCore,
														  &m_BuildCancel, tee);
		}

		// Last, after every result is in place: this is what tells the main
		// thread it may read them.
		m_BuildDone = true;
	});
}

// The frame a build finishes: join the worker, publish its results, and do
// the one thing that must not happen off-thread -- loading the C# assembly
// into the runtime the editor is actively using.
void EditorLayer::FinishBuild()
{
	if (!m_BuildInFlight || !m_BuildDone)
		return;

	m_BuildThread.join();
	m_BuildInFlight = false;

	const bool cancelled = m_BuildCancel;

	// Whether the scene resumes below: only when everything that ran
	// succeeded. After a failed build the person is mid-fix, and auto-playing
	// the *old* code under them would misreport their change as having no
	// effect -- the panel with the errors is the right next thing to see.
	const bool resume = m_ResumePlayAfterBuild;
	m_ResumePlayAfterBuild = false;

	if (m_WorkerRanModule)
	{
		m_ModuleBuild = std::move(m_WorkerModule);
		m_ModuleBuildRan = true;

		if (m_ModuleBuild.Cancelled)
			RV_INFO("Module build cancelled");
		else if (m_ModuleBuild.SdkMissing)
			RV_ERROR("No CMake found; the C++ module was not built");
		else if (!m_ModuleBuild.Success)
			RV_ERROR("Module build failed: {0} error(s)", m_ModuleBuild.ErrorCount());
		else
			RV_INFO("Module built in {0:.1f}s, {1} warning(s)",
					m_ModuleBuild.Seconds, m_ModuleBuild.WarningCount());

		// Reload whatever DLL exists now -- the new one after a success, the
		// previous one after a failure or cancel, since the build only
		// replaces the file when it links. Either way the scripts come back;
		// a failed build must not leave the project with none.
		if (Project::GetActive())
			GameModule::Load(Project::Root(), Project::Config().Name);
	}

	if (m_WorkerRanScripts)
	{
		m_ScriptBuild = std::move(m_WorkerScripts);
		m_ScriptBuildRan = true;

		if (m_ScriptBuild.Cancelled)
			RV_INFO("Script build cancelled");
		else if (m_ScriptBuild.SdkMissing)
			RV_ERROR("No .NET SDK found. Install the .NET 8 SDK to build C# scripts.");
		else if (!m_ScriptBuild.Success)
			RV_ERROR("Script build failed: {0} error(s)", m_ScriptBuild.ErrorCount());
		else
		{
			RV_INFO("Scripts built in {0:.1f}s, {1} warning(s)",
					m_ScriptBuild.Seconds, m_ScriptBuild.WarningCount());

			// Loading is separate from building on purpose: a build can succeed
			// and still produce an assembly the runtime refuses. The swap must
			// not happen under live instances -- the same rule the C++ module
			// has -- so mid-play it parks and OnSceneStop does it.
			if (!Managed::Interop::IsReady())
				RV_WARN("Built, but C# scripting is not running -- the assembly was not loaded");
			else if (m_SceneState != SceneState::Edit)
			{
				m_PendingAssemblyLoad = m_ScriptBuild.Assembly;
				RV_INFO("Built; the scene is playing, so the new scripts load when it stops.");
			}
			else
			{
				LoadScriptAssembly(m_ScriptBuild.Assembly);
			}
		}
	}
	else if (!cancelled && !m_WorkerRanModule
			 && !ModuleBuild::ProjectHasModule(Project::Root()))
	{
		RV_INFO("Nothing to build: this project has no Source/ and no .csproj");
	}

	// The scene this build interrupted, resumed -- but only when everything
	// that ran succeeded. After a failure the person is mid-fix, and
	// auto-playing the old code under them would misreport their change as
	// having no effect; the panel with the errors is the right next thing.
	if (resume)
	{
		const bool moduleOk = !m_WorkerRanModule || m_ModuleBuild.Success;
		const bool scriptsOk = !m_WorkerRanScripts || m_ScriptBuild.Success;

		if (!cancelled && moduleOk && scriptsOk)
		{
			RV_INFO("Resuming Play on the new scripts");
			OnScenePlay();
		}
		else
		{
			RV_INFO("Staying stopped: the build did not land cleanly");
		}
	}
}

// The actual swap: retire the old collectible context, load the new bytes.
// Shared by FinishBuild (edit mode) and OnSceneStop (a build that finished
// mid-play and waited).
void EditorLayer::LoadScriptAssembly(const std::filesystem::path& assembly)
{
	const int32_t scripts =
		Managed::Interop::Managed().LoadAssembly(assembly.string().c_str());

	if (scripts < 0)
		RV_ERROR("The script assembly built but could not be loaded");
	else
		RV_INFO("Loaded {0} script type(s) from {1}", scripts, assembly.filename().string());
}

// The compiler's output, where somebody will actually read it.
void EditorLayer::DrawScriptBuildPanel()
{
	if (!m_ShowScriptBuild)
		return;

	if (!ImGui::Begin("Build Log", &m_ShowScriptBuild))
	{
		ImGui::End();
		return;
	}

	// A build in progress: the panel is a console. Live output with the
	// cancel above it, because the moment somebody wants to cancel is the
	// moment they are reading the output.
	if (m_BuildInFlight)
	{
		// Through the token. A literal amber here was the last hardcoded colour
		// in the editor -- it was tuned against the old near-black surface and
		// has no idea a light theme exists.
		ImGui::TextColored(EditorTheme::Colors().Warning, "Building%.*s",
						   1 + (int)(ImGui::GetTime() * 2.0) % 3, "...");
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel"))
			m_BuildCancel = true;
		if (m_BuildCancel)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("stopping...");
		}

		ImGui::Separator();

		ImGui::BeginChild("##buildconsole", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
						  ImGuiWindowFlags_HorizontalScrollbar);
		{
			std::lock_guard<std::mutex> lock(m_BuildLogMutex);
			ImGui::TextUnformatted(m_BuildLiveLog.c_str());
		}
		// Follow the output, but only while already at the bottom -- somebody
		// scrolled up is reading, and yanking the view away is how a console
		// stops being trusted.
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		ImGui::End();
		return;
	}

	if (!m_ScriptBuildRan && !m_ModuleBuildRan)
	{
		ImGui::TextDisabled("Nothing built yet.");
		ImGui::End();
		return;
	}

	// The C++ module, when one was built. First for the same reason it builds
	// first: it is where the long wait went.
	if (m_ModuleBuildRan)
	{
		ImGui::SeparatorText("C++ module");
		if (m_ModuleBuild.SdkMissing)
		{
			ImGui::TextColored(EditorTheme::Colors().Danger, "No CMake");
			ImGui::TextWrapped("The C++ module needs CMake. The engine's own build has one; "
							   "install CMake or put it on PATH.");
		}
		else
		{
			DrawBuildResult(m_ModuleBuild);
		}
	}

	// Only when it actually ran: a project with no .csproj has no C# section,
	// not an empty green one.
	if (m_ScriptBuildRan)
	{
		if (m_ModuleBuildRan)
			ImGui::SeparatorText("C# scripts");

		if (m_ScriptBuild.SdkMissing)
		{
			ImGui::TextColored(EditorTheme::Colors().Danger, "No .NET SDK");
			ImGui::TextWrapped("C# scripts need the .NET 8 SDK. The engine itself does not -- "
							   "it finds the runtime at startup and reports C# as unavailable "
							   "when there is none.");
		}
		else
		{
			DrawBuildResult(m_ScriptBuild);
		}
	}

	ImGui::End();
}

// One build's outcome, whichever language produced it: the summary line, the
// diagnostics with errors first, and the raw log folded away. Shared between
// the two halves of the panel so a C++ error and a C# error read identically.
void EditorLayer::DrawBuildResult(const Managed::BuildResult& result)
{
	// Scoped IDs: the two halves render the same widgets, and ImGui would
	// otherwise conflate their collapsing headers.
	ImGui::PushID(&result);

	const size_t errors = result.ErrorCount();
	const size_t warnings = result.WarningCount();

	// Cancelled is its own state, not a failure dressed as one: "0 errors and
	// yet broken" is the reading being avoided.
	if (result.Cancelled)
	{
		ImGui::TextColored(EditorTheme::Colors().Warning, "Cancelled");
		if (ImGui::CollapsingHeader("Output up to the cancel"))
			ImGui::TextUnformatted(result.Output.c_str());
		ImGui::PopID();
		return;
	}

	if (errors > 0)
		ImGui::TextColored(EditorTheme::Colors().Danger, "%zu error(s), %zu warning(s)", errors, warnings);
	else
		ImGui::TextColored(EditorTheme::Colors().Success, "Built in %.1fs, %zu warning(s)",
						   result.Seconds, warnings);

	ImGui::Separator();

	// Errors first. A build with forty warnings and one error should not make
	// somebody scroll to find the thing that stopped it.
	for (int pass = 0; pass < 2; pass++)
	{
		const bool wantErrors = (pass == 0);
		for (const Managed::BuildDiagnostic& diagnostic : result.Diagnostics)
		{
			if (diagnostic.IsError != wantErrors)
				continue;

			const ImVec4 colour = diagnostic.IsError ? EditorTheme::Colors().Danger
													 : EditorTheme::Colors().Warning;

			// A linker diagnostic has no line at all; printing "(0,0)" after
			// every unresolved symbol would just be noise.
			if (diagnostic.Line > 0)
				ImGui::TextColored(colour, "%s(%d,%d)", diagnostic.File.filename().string().c_str(),
								   diagnostic.Line, diagnostic.Column);
			else
				ImGui::TextColored(colour, "%s", diagnostic.File.filename().string().c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", diagnostic.Code.c_str());
			ImGui::TextWrapped("    %s", diagnostic.Message.c_str());
			ImGui::Spacing();
		}
	}

	// The whole log, folded away. Parsing is best-effort -- a failure that is
	// not a compiler diagnostic still has to be readable by someone.
	if (result.Diagnostics.empty() || errors > 0)
	{
		if (ImGui::CollapsingHeader("Full output"))
			ImGui::TextUnformatted(result.Output.c_str());
	}

	ImGui::PopID();
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

