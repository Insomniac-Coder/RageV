#include "RageV/Renderer/UIRenderer.h"
#include "RuntimeLayer.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Project/Project.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Renderer/RayCounters.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/UI/Canvas.h"
#include "RageV/UI/Interaction.h"
#include "RageV/Core/Input.h"
#include "RageV/Core/MouseButtonCodes.h"
#include "imgui.h"
#include "RageV/ImGui/ImGuiBinding.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

using namespace RageV;

namespace
{
	// An IEEE half, as a R16G16B16A16_SFLOAT texel stores each channel.
	float HalfToFloat(uint16_t half)
	{
		const uint32_t exponent = (half >> 10) & 0x1Fu;
		const uint32_t mantissa = half & 0x3FFu;
		float value;
		if (exponent == 0)
			value = std::ldexp((float)mantissa, -24);
		else if (exponent == 31)
			value = mantissa != 0 ? std::numeric_limits<float>::quiet_NaN()
								  : std::numeric_limits<float>::infinity();
		else
			value = std::ldexp((float)(mantissa | 0x400u), (int)exponent - 25);
		return (half & 0x8000u) != 0 ? -value : value;
	}

	// **A float array as numpy writes one** (.npy, version 1.0): the magic, a
	// little-endian header length, a Python dict, spaces to a multiple of 64,
	// then the values. The format a measurement script opens in one call.
	bool WriteNpy(const std::string& path, const std::vector<float>& values,
				  const std::vector<uint32_t>& shape)
	{
		std::string dims;
		for (size_t i = 0; i < shape.size(); ++i)
			dims += (i ? ", " : "") + std::to_string(shape[i]);
		std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (" + dims + "), }";
		const size_t unpadded = 10 + header.size() + 1;
		header.append((64 - unpadded % 64) % 64, ' ');
		header.push_back('\n');
		std::ofstream out(path, std::ios::binary);
		if (!out)
			return false;
		const char magic[8] = { '\x93', 'N', 'U', 'M', 'P', 'Y', '\x01', '\x00' };
		out.write(magic, sizeof(magic));
		const uint16_t length = (uint16_t)header.size();
		const char lengthBytes[2] = { (char)(length & 0xFF), (char)(length >> 8) };
		out.write(lengthBytes, 2);
		out.write(header.data(), (std::streamsize)header.size());
		out.write((const char*)values.data(), (std::streamsize)(values.size() * sizeof(float)));
		return (bool)out;
	}
}

RuntimeLayer::RuntimeLayer()
	: Layer("RuntimeLayer")
{
}

void RuntimeLayer::OnAttach()
{
	// The engine is a DLL and ImGui's state is a global, so this executable
	// starts with its own empty one. See ImGuiBinding.h.
	ImGuiBinding::Bind();

	// The scene draws into a linear HDR target, not the swapchain: bloom needs
	// values from before the tone curve compresses them, and there are none
	// left once an 8-bit backbuffer has been written. The tonemap pass is what
	// reaches the swapchain.
	// The velocity format is named here as well as in BuildFrame, and that is
	// not redundancy. A reflection probe captures the scene *before* the frame
	// graph is built on the very first frame, so pipelines that learn the
	// target's shape only from BuildFrame are bound into a probe face that
	// already has the extra attachment. ENGINE-NOTES 7r.
	Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT, RHI::Format::D32_SFLOAT,
							   1, RHI::Format::R16G16_SFLOAT, RHI::Format::R8G8B8A8_UNORM,
							   RHI::Format::R16G16B16A16_SFLOAT);
	// The UI's world layer draws in the scene pass too, and learns the shape
	// from the same two places for the same reason.
	UIRenderer::SetWorldTargetFormats(RHI::Format::R16G16B16A16_SFLOAT,
									  RHI::Format::D32_SFLOAT, 1, RHI::Format::R16G16_SFLOAT,
									  RHI::Format::R8G8B8A8_UNORM,
									  RHI::Format::R16G16B16A16_SFLOAT);

	m_Graph = std::make_unique<RenderGraph>(Renderer::GetDevice());
	m_Graph->SetName("scene");

	// So the UI pass does not wipe the frame this layer just drew.
	//
	// Set here rather than after the scene loads, because the loading screen
	// is drawn through that same pass and needs the opposite: it *is* the
	// whole frame while it runs, and the boot loop draws nothing underneath
	// it. Turning the clear off before there is anything to preserve would
	// leave the bar compositing over whatever the swapchain last held.
	Application::Get().GetImGuiLayer()->SetClearsBackbuffer(true);
}

// On the boot worker. Files and CPU only -- see Layer::OnLoad.
void RuntimeLayer::OnLoad(Boot::Progress& progress)
{
	if (!Project::GetActive())
	{
		RV_ERROR("No project. Pass --project=<folder>, or put a .rvproject "
				 "beside the executable.");
		return;
	}

	// --scene wins over the project's own, so a benchmark or a bug report can
	// name a scene without changing what everyone else opens.
	const std::string& override = EngineConfig::Get().ScenePath;
	const std::string startScene = override.empty() ? Project::Config().StartScene : override;

	if (startScene.empty())
	{
		RV_ERROR("Project '{0}' has no start scene. Set one in the editor: "
				 "File > Set Start Scene.", Project::Config().Name);
		return;
	}

	const std::filesystem::path scenePath = Project::AssetPath(startScene);

	progress.BeginPhase("Opening scene", 0.0f, 0.15f);
	progress.SetDetail(scenePath.filename().string());

	auto scene = std::make_shared<Scene>();
	SceneSerializer serializer(scene);
	if (!serializer.Deserialize(scenePath.string()))
	{
		RV_ERROR("Could not load the start scene {0}", scenePath.string());
		return;
	}

	m_Scene = std::move(scene);

	// **--camera, so a measurement can be repeated.** Applied to the scene's
	// own primary camera rather than a second one -- shadows, culling and
	// reflections all read the camera entity's transform, and a runtime with
	// two notions of where the eye is lets them disagree with the picture.
	// Nothing is written back to disk: this is the loaded copy. The orbit is
	// unpacked exactly as EditorCamera::SetOrbit unpacks it -- Euler
	// (-pitch, -yaw, 0) in radians, eye at focus minus forward times distance
	// -- so the same six numbers frame the same shot in editor and runtime.
	if (EngineConfig::Get().HasCameraPose)
	{
		if (Entity camera = m_Scene->GetPrimaryCameraEntity())
		{
			const EngineConfig& config = EngineConfig::Get();
			const Vec3 rotation(-Math::Radians(config.CameraPitch),
								-Math::Radians(config.CameraYaw), 0.0f);
			const Vec3 forward = Math::Rotate(Math::FromEuler(rotation),
											  Vec3(0.0f, 0.0f, -1.0f));
			TransformComponent& transform =
				camera.GetComponent<TransformComponent>();
			transform.Rotation = rotation;
			transform.Position = config.CameraFocus
							   - forward * Math::Max(config.CameraDistance, 0.01f);
		}
	}

	if (progress.Cancelled())
		return;

	progress.BeginPhase("Loading assets", 0.15f, 0.95f);
	Assets::Manager::PrepareScene(*m_Scene, progress);

	m_SceneName = startScene;
}

// Main thread, one slice per loading-screen frame.
bool RuntimeLayer::OnLoadStep(Boot::Progress& progress)
{
	if (!m_Scene)
		return false;

	progress.BeginPhase("Uploading assets", 0.95f, 1.0f);
	return Assets::Manager::UploadPrepared(progress, 1.0f / 12.0f);
}

// Back on the main thread, after the uploads.
void RuntimeLayer::OnLoaded()
{
	// A game with nothing to run says so and exits, rather than presenting an
	// empty window that is indistinguishable from a broken one. Decided here
	// because OnLoad is where it becomes knowable, and acted on here because
	// this is the last point before the first frame.
	if (!m_Scene)
	{
		Application::Get().Close();
		return;
	}

	auto& device = Renderer::GetDevice();

	m_Width = device.GetSwapchainWidth();
	m_Height = device.GetSwapchainHeight();
	m_Scene->OnViewportResize((float)m_Width, (float)m_Height);

	// The scene owns the frame from here, so the UI pass must stop clearing.
	Application::Get().GetImGuiLayer()->SetClearsBackbuffer(false);

	// A game starts running. There is no Play button to press, and that
	// difference is most of what separates this from the editor.
	m_Scene->OnRuntimeStart();
	m_Ready = true;

	RV_INFO("Running '{0}' -- {1}", Project::Config().Name, m_SceneName);
}

void RuntimeLayer::OnFixedUpdate(Timestep dt)
{
	if (m_Ready)
		m_Scene->OnFixedUpdateRuntime(dt);
}

void RuntimeLayer::CaptureSignals()
{
	const EngineConfig& config = EngineConfig::Get();
	if (config.CaptureSignals.empty() || config.ScreenshotPath.empty())
		return;

	// This frame's number is last frame's plus one, and last frame's is the
	// one whose histories Previous() now holds: Advance swapped the pair when
	// the graph was described, before the frame ran.
	const uint64_t frame = Renderer::GetFrameCount();
	if (frame == 0)
		return;
	const uint64_t written = frame - 1;
	const uint64_t first = config.ScreenshotFrame;
	const uint64_t last = first + Math::Max(config.ScreenshotCount, 1u) - 1;
	if (written < first || written > last)
		return;

	struct Named
	{
		const char* Name;
		TemporalHistory* History;
		std::vector<uint32_t> Attachments;
	};
	// What each attachment is: direct 0 the diffuse before the albedo and 3 the
	// specular; the others' 0 the signal; taa 0 the resolved colour before the
	// tone curve.
	const Named table[] = {
		{ "direct", &m_DirectLight, { 0u, 3u } },
		{ "reflections", &m_Reflections, { 0u } },
		{ "occlusion", &m_Occlusion, { 0u } },
		{ "gi", &m_GiLight, { 0u } },
		{ "taa", &m_History, { 0u } },
	};

	// **crop=x:y:w:h**, beside the names: every frame's values in that
	// rectangle as well as the mean, so what a filter does to a pixel can be
	// followed frame by frame instead of inferred from an average. Colons, not
	// commas, because the commas separate the names.
	uint32_t cropX = 0, cropY = 0, cropW = 0, cropH = 0;
	{
		std::stringstream scan(config.CaptureSignals);
		std::string token;
		while (std::getline(scan, token, ','))
			if (token.rfind("crop=", 0) == 0
				&& std::sscanf(token.c_str() + 5, "%u:%u:%u:%u", &cropX, &cropY, &cropW, &cropH) != 4)
				cropW = cropH = 0;
	}

	auto& device = Renderer::GetDevice();
	std::stringstream names(config.CaptureSignals);
	std::string name;
	while (std::getline(names, name, ','))
	{
		if (name.rfind("crop=", 0) == 0)
			continue;
		const Named* entry = nullptr;
		for (const Named& candidate : table)
			if (name == candidate.Name)
				entry = &candidate;
		if (!entry)
		{
			if (written == first)
				RV_WARN("capture-signals: no history named '{0}' (direct, reflections, occlusion, gi, taa)", name);
			continue;
		}
		const RHI::Ref<RHI::RHIRenderTarget>& target = entry->History->Previous();
		if (!target || !entry->History->HasHistory())
			continue;
		for (uint32_t attachment : entry->Attachments)
		{
			const RHI::Ref<RHI::RHITexture> texture = target->GetColorTexture(attachment);
			if (!texture)
				continue;
			const RHI::Format format = texture->GetFormat();
			const bool half = format == RHI::Format::R16G16B16A16_SFLOAT;
			if (!half && format != RHI::Format::R32G32B32A32_SFLOAT)
			{
				if (written == first)
					RV_WARN("capture-signals: {0}{1} is not a four-channel float target", name, attachment);
				continue;
			}
			std::vector<uint8_t> bytes;
			if (!device.ReadTexture(texture, bytes))
				continue;
			const uint32_t width = texture->GetWidth();
			const uint32_t height = texture->GetHeight();
			const size_t values = (size_t)width * height * 4;
			if (bytes.size() < values * (half ? 2u : 4u))
				continue;

			SignalCapture& capture = m_SignalCaptures[name + std::to_string(attachment)];
			if (capture.Width != width || capture.Height != height)
			{
				capture = SignalCapture{};
				capture.Width = width;
				capture.Height = height;
				capture.Sum.assign(values, 0.0);
			}
			const bool cropped = cropW > 0 && cropH > 0 && cropX + cropW <= width && cropY + cropH <= height;
			for (size_t i = 0; i < values; ++i)
			{
				float v;
				if (half)
				{
					uint16_t raw;
					std::memcpy(&raw, bytes.data() + i * 2, 2);
					v = HalfToFloat(raw);
				}
				else
				{
					std::memcpy(&v, bytes.data() + i * 4, 4);
				}
				capture.Sum[i] += (double)v;
				if (cropped)
				{
					const size_t texel = i / 4;
					const uint32_t x = (uint32_t)(texel % width);
					const uint32_t y = (uint32_t)(texel / width);
					if (x >= cropX && x < cropX + cropW && y >= cropY && y < cropY + cropH)
						capture.Crop.push_back(v);
				}
			}
			capture.Frames++;
			if (cropped)
			{
				capture.CropW = cropW;
				capture.CropH = cropH;
			}
		}
	}

	if (written != last)
		return;
	const std::filesystem::path shot(config.ScreenshotPath);
	for (const auto& [key, capture] : m_SignalCaptures)
	{
		if (capture.Frames == 0)
			continue;
		std::vector<float> mean(capture.Sum.size());
		for (size_t i = 0; i < mean.size(); ++i)
			mean[i] = (float)(capture.Sum[i] / (double)capture.Frames);
		const std::string path =
			(shot.parent_path() / (shot.stem().string() + "_" + key + ".npy")).string();
		if (WriteNpy(path, mean, { capture.Height, capture.Width, 4u }))
			RV_INFO("capture-signals: {0}, the mean of {1} frames -> {2}", key, capture.Frames, path);
		else
			RV_ERROR("capture-signals: could not write {0}", path);
		const size_t perFrame = (size_t)capture.CropW * capture.CropH * 4;
		if (perFrame > 0 && capture.Crop.size() == perFrame * capture.Frames)
		{
			const std::string cropPath =
				(shot.parent_path() / (shot.stem().string() + "_" + key + "_crop.npy")).string();
			if (!WriteNpy(cropPath, capture.Crop, { capture.Frames, capture.CropH, capture.CropW, 4u }))
				RV_ERROR("capture-signals: could not write {0}", cropPath);
		}
	}
}

void RuntimeLayer::OnUpdate(Timestep ts)
{
	if (!m_Ready)
		return;

	// Before anything this frame touches a history: what they hold is last
	// frame's, submitted and finished.
	CaptureSignals();

	// **A forced bake run ends itself when there is nothing left to store.**
	//
	// `--bake=force` exists to produce files -- the editor's Bake button runs
	// exactly this, as a child process -- and a producer that keeps rendering
	// after the last file lands is a producer somebody has to watch and kill.
	// Settled has to *hold*, not merely happen: a script sweeping a scene's
	// lightings (the showroom's mode pill) drops it back to false when it
	// switches, and the hold is what keeps the run alive across that gap.
	// Two seconds of frames is far past any sweep's switch and costs nothing.
	//
	// Not when a screenshot was asked for -- that run is a measurement and
	// its frame numbers are its protocol. A benchmark ceiling still applies
	// as the backstop for a scene that can never settle (a light animating
	// changes the lighting every frame, and no bake can ever match it).
	if (EngineConfig::Get().ForceLightingBake && m_Scene
		&& EngineConfig::Get().ScreenshotPath.empty())
	{
		if (m_Scene->BakedLightingSettled())
			m_BakeSettledFrames++;
		else
			m_BakeSettledFrames = 0;

		if (m_BakeSettledFrames == 120)
		{
			RV_INFO("Bake complete; exiting.");
			Application::Get().Close();
			return;
		}
	}

	// A rolling average rather than the instantaneous value, which is
	// unreadable at any frame rate worth having.
	m_FrameTimeAccum += ts.GetSeconds() * 1000.0f;
	if (++m_FrameTimeSamples >= 30)
	{
		m_FrameTimeMs = m_FrameTimeAccum / (float)m_FrameTimeSamples;
		m_FrameTimeAccum = 0.0f;
		m_FrameTimeSamples = 0;
	}

	auto& device = Renderer::GetDevice();

	// The window drives the aspect ratio here, where in the editor a panel
	// does. Aspect is a property of the surface being drawn into, which is why
	// it is passed per pass rather than stored on the camera.
	const uint32_t width = device.GetSwapchainWidth();
	const uint32_t height = device.GetSwapchainHeight();
	if (width != m_Width || height != m_Height)
	{
		m_Width = width;
		m_Height = height;
		m_Scene->OnViewportResize((float)width, (float)height);
	}

	// The pointer, before the scripts that read it.
	//
	// In a shipped game the UI layer *is* the window, so a cursor position
	// needs no mapping at all -- which is the whole reason this is two lines
	// here and a dozen in the editor, where the same layer is an image inside
	// a panel.
	{
		const std::pair<float, float> cursor = Input::GetMousePosition();

		UI::PointerInput pointer;
		pointer.X = cursor.first;
		pointer.Y = cursor.second;
		pointer.Down = Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_LEFT);
		pointer.Inside = cursor.first >= 0.0f && cursor.second >= 0.0f
					  && cursor.first < (float)m_Width && cursor.second < (float)m_Height;

		UI::UpdatePointer(*m_Scene, (float)m_Width, (float)m_Height, pointer);
	}

	m_Scene->OnUpdateRuntime(ts);

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd || m_Height == 0)
		return;

	// Before the frame graph: both of these open render passes of their own,
	// and nothing may do that inside another one.
	{
		RV_PROFILE_PHASE(FramePhase::EnvironmentPrefilter);
		m_Scene->PrepareEnvironment();
	}

	{
		RV_PROFILE_PHASE(FramePhase::Probes);
		m_Scene->CaptureReflectionProbes();
	}

	if (Entity camera = m_Scene->GetPrimaryCameraEntity())
	{
		RV_PROFILE_PHASE(FramePhase::Shadows);
		m_Scene->RenderShadows(camera.GetComponent<CameraComponent>().Camera,
							   camera.GetComponent<TransformComponent>().World);
	}

	RV_PROFILE_PHASE(FramePhase::Graph);

	m_Graph->Begin(m_Width, m_Height);

	// The scene, bloom, tone mapping and anti-aliasing, described in one place
	// and shared with the editor -- the two differ only in where the finished
	// image goes.
	FrameDesc frame;
	frame.Output = m_Graph->Backbuffer();
	frame.Width = m_Width;
	frame.Height = m_Height;
	frame.Environment = m_Scene->GetEnvironment();
	// WR-3's fog reads its inscatter colour from this. See FrameDesc::SkyCube.
	frame.SkyCube = m_Scene->ResolveSky();
	frame.Render = Project::Render();
	frame.Post = m_Scene->GetPostSettings();
	frame.OutputFormat = device.GetSwapchainFormat();
	{
		const RageV::Vec2 clips = m_Scene->GetCameraClipPlanes();
		frame.NearClip = clips.x;
		frame.FarClip = clips.y;

		const RageV::Vec2 inverse = m_Scene->GetCameraProjectionInverse();
		frame.InvProjection0 = inverse.x;
		frame.InvProjection1 = inverse.y;
		frame.View = m_Scene->GetCameraView();
	}
	// One frame chain, so one history and one adapted exposure. See
	// TemporalHistory for why the graph cannot own either.
	frame.History = &m_History;
	frame.Exposure = &m_Exposure;
	frame.Reflections = &m_Reflections;
	frame.DirectLight = &m_DirectLight;
	frame.DirectChange = &m_DirectChange;
	frame.ReflectionChange = &m_ReflectionChange;
	frame.GiChange = &m_GiChange;
	frame.Indirect = &m_Indirect;
	frame.RayBudget = &m_RayBudget;
	frame.Occlusion = &m_Occlusion;
	frame.GiLight = &m_GiLight;
	frame.TaaGuide = &m_TaaGuide;

	// The loop's frame time, straight through. Not a clock read here: this is
	// the number --frame-time pins, and driving the adaptation from it is the
	// whole of how auto exposure stays reproducible. ENGINE-NOTES 7y.
	frame.DeltaSeconds = ts.GetSeconds();
	frame.DrawScene = [this](RGPassContext& context)
	{
		m_Scene->OnRenderRuntime((float)context.Width / (float)context.Height);
	};
	frame.DrawSceneLit = [this](RGPassContext&)
	{
		m_Scene->OnRenderLit();
	};

	// The game's own UI, over the finished image. The whole reason the UI layer
	// exists is that a shipped game can say something, so this is not
	// conditional on anything -- a scene with no canvas resolves to nothing and
	// the pass costs a compare.
	frame.DrawUI = [this](RGPassContext& context)
	{
		UI::DrawScene(*m_Scene, context.Width, context.Height);
	};

	// Asked of the scene rather than of the renderer: the graph is described
	// before anything draws, so the renderer would answer for last frame.
	const bool hasWater = m_Scene->HasWater();
	if (Particles::System::HasWeightedEmitters(*m_Scene) || m_Scene->HasBlendedMeshes())
	{
		frame.DrawTransparent = [](RGPassContext&)
		{
			// Meshes then particles -- the same order and the same reason as
			// the editor's two graphs. See EditorLayer.
			Renderer3D::FlushTransparent();
			ParticleRenderer::FlushWeighted();
		};
		frame.ResolveTransparent = [](RGPassContext&, const RHI::Ref<RHI::RHITexture>& accumulate,
									  const RHI::Ref<RHI::RHITexture>& revealage)
		{
			ParticleRenderer::ResolveWeighted(accumulate, revealage);
		};
		// WR-16 S4b: the sea's surface, drawn before the pass that shades its
		// lamps. The water runs of the same list, and the list stands --
		// **and only where there is a sea.** A scene with blended meshes and no
		// water -- a windscreen is the ordinary case -- still set these three up,
		// and paid for the surface draw, the two lamp passes and the mirror trace:
		// five passes with nothing to run on, 0.26 ms of the showroom's 8 ms frame.
		if (hasWater)
		{
			frame.DrawWaterSurface = [](RGPassContext&)
			{
				Renderer3D::FlushWaterSurface();
			};
			frame.WaterReservoirs = &m_WaterChoices;
			frame.WaterLampLight = &m_WaterLampLight;
			frame.WaterReflectionLight = &m_WaterReflectionLight;
		}
	}

	// The water's two extras, on the transparent block's own terms: only
	// when a body exists to pay for them.
	if (hasWater)
	{
		frame.WaterSeeThrough = true;
		frame.UpdateWater = [this](RGPassContext& context)
		{
			m_Scene->UpdateWaterFoam(context.Cmd);
		};
	}

	BuildFrame(*m_Graph, frame);

	if (!m_Graph->Compile())
	{
		// Loudly and once. A frame that cannot be described is a bug in the
		// description, and it will be the same bug every frame.
		for (const std::string& error : m_Graph->Errors())
			RV_ERROR("Render graph: {0}", error);
		return;
	}

	m_Graph->Execute(*cmd);
}

void RuntimeLayer::OnImGuiRender()
{
	if (!m_ShowStats)
		return;

	ImGui::SetNextWindowBgAlpha(0.6f);
	ImGui::SetNextWindowPos({ 12.0f, 12.0f }, ImGuiCond_Always);

	if (ImGui::Begin("##runtimestats", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
					 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
					 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
	{
		ImGui::Text("%.2f ms  (%.0f fps)", m_FrameTimeMs,
					m_FrameTimeMs > 0.0f ? 1000.0f / m_FrameTimeMs : 0.0f);
		ImGui::Text("%u x %u", m_Width, m_Height);
		ImGui::Text("%u Hz simulation", Application::GetFixedHz());

		// The frame's rays, a frame or two behind like the GPU timings
		// (WR-16 S0). Millions per frame by kind, then the per-pixel numbers
		// the owner's document asks for first.
		const RayCounters::Sample& rays = RayCounters::Last();
		if (rays.Valid)
		{
			ImGui::Separator();
			ImGui::Text("rays %.1f M: shadow %.1f  water %.1f  refl %.1f  GI %.1f  AO %.1f",
						rays.TotalRays() / 1.0e6, rays.Lanes[RayCounters::ShadowRays] / 1.0e6,
						rays.Lanes[RayCounters::WaterRays] / 1.0e6,
						rays.Lanes[RayCounters::ReflectionRays] / 1.0e6,
						rays.Lanes[RayCounters::GiRays] / 1.0e6,
						rays.Lanes[RayCounters::AoRays] / 1.0e6);
			ImGui::Text("per fragment: %.1f rays, %.1f lights (max %u); %.1f lights per hit",
						rays.RaysPerFragment(), rays.LightsPerFragment(),
						rays.Lanes[RayCounters::LightsMax], rays.LightsPerHit());
			const float confidence = rays.TemporalConfidence();
			if (confidence >= 0.0f)
				ImGui::Text("temporal confidence %.1f%%", confidence * 100.0f);
		}
	}
	ImGui::End();
}

void RuntimeLayer::OnEvent(Event& e)
{
	EventDispatcher dispatcher(e);
	dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& key) { return OnKeyPressed(key); });
}

bool RuntimeLayer::OnKeyPressed(KeyPressedEvent& e)
{
	if (e.GetRepeatCount() > 0)
		return false;

	switch (e.GetKeyCode())
	{
		case RV_KEY_F1:
			m_ShowStats = !m_ShowStats;
			return true;

		// Escape quits. A game with no way out but the window's close button is
		// a game that traps anyone who launches it full-screen.
		case RV_KEY_ESCAPE:
			Application::Get().Close();
			return true;
	}

	return false;
}

void RuntimeLayer::ResizeTarget(uint32_t width, uint32_t height)
{
	m_Width = width;
	m_Height = height;
}
