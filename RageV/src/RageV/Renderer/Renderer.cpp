#include <rvpch.h>
#include "Renderer.h"
#include "Renderer2D.h"
#include "Renderer3D.h"
#include "DebugRenderer.h"
#include "ParticleRenderer.h"
#include "LightGlow.h"
#include "RageV/Particles/GpuParticles.h"
#include "Skybox.h"
#include "ViewportGrid.h"
#include "UIRenderer.h"
#include "ShadowMap.h"
#include "VoxelGI.h"
#include "EnvironmentIBL.h"
#include "ProbeArray.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Project/Project.h"
#include "PostProcess.h"
#include "AutoExposure.h"
#include "Water.h"
#include "RayCounters.h"

namespace RageV
{
	namespace
	{
		RHI::RHIDevice*      s_Device = nullptr;
		RHI::RHICommandList* s_CommandList = nullptr;

		// Frames drawn since the main loop started, and the sub-pixel offset
		// the current one is being drawn with. See the header for why the
		// count is not process-wide.
		uint64_t s_FrameCount = 0;
		Vec2     s_Jitter{ 0.0f, 0.0f };

		// Whose history the scene being drawn will be reprojected into. Null
		// outside a scene pass, which is most of the frame.
		CameraMotion* s_CameraMotion = nullptr;

		// Last frame's reflection trace for the scene being drawn. Null
		// outside a scene pass, like the two above.
		const Renderer::ScreenReflections* s_ScreenReflections = nullptr;
	}

	void Renderer::Init(RHI::RHIDevice& device)
	{
		s_Device = &device;

		ResetRayBudget();
		if (EngineConfig::Get().RayBudgetMs > 0.0f)
		{
			RV_CORE_INFO("Ray budget: holding {0} ms of ray-pass GPU time (command "
						 "line, overrides the project's mode); counts scale between "
						 "{1} and 1.0", EngineConfig::Get().RayBudgetMs, kMinRayScale);
		}

		Renderer2D::Init(device);
		Renderer3D::Init(device);
		DebugRenderer::Init(device);
		ParticleRenderer::Init(device);
		LightGlow::Init(device);
		// After the renderer: an emitter that cannot simulate on the GPU still
		// draws, and Init logs which of the two it got.
		Particles::Gpu::Init(device);
		Skybox::Init(device);
		ViewportGrid::Init(device);
		UIRenderer::Init(device);
		ShadowMap::Init(device);
		// After the shadow maps: the voxel grid is lit from their cascades.
		VoxelGI::Init(device);
		EnvironmentIBL::Init(device);
		// After it: the arrays are filled by its two convolutions, and Begin
		// asks it how many roughness levels a face size can carry.
		ProbeArray::Init(device);
		PostProcess::Init(device);
		AutoExposure::Init(device);
	}

	void Renderer::Shutdown()
	{
		// Before the renderers: the water's material, foam buffers and compute
		// pipeline hold device resources, and this was not called from
		// anywhere at all until the foam work made the leak big enough to
		// matter.
		Water::Shutdown();
		AutoExposure::Shutdown();
		PostProcess::Shutdown();
		ProbeArray::Shutdown();
		EnvironmentIBL::Shutdown();
		VoxelGI::Shutdown();
		ShadowMap::Shutdown();
		UIRenderer::Shutdown();
		ViewportGrid::Shutdown();
		Skybox::Shutdown();
		Particles::Gpu::Shutdown();
		LightGlow::Shutdown();
		ParticleRenderer::Shutdown();
		DebugRenderer::Shutdown();
		Renderer3D::Shutdown();
		Renderer2D::Shutdown();
		s_CommandList = nullptr;
		s_Device = nullptr;
	}

	void Renderer::BeginFrame(RHI::RHICommandList* commandList)
	{
		s_CommandList = commandList;
		s_FrameCount++;

		// Belt and braces: nothing outside a scene pass should ever read a
		// non-zero jitter, and a frame that threw one away half-drawn would
		// otherwise hand it to the next one's probe captures.
		s_Jitter = Vec2(0.0f, 0.0f);
		s_CameraMotion = nullptr;
		s_ScreenReflections = nullptr;

		// Frees the per-frame buffer pools. Anything that draws a scene more
		// than once per frame depends on this having run first.
		Renderer2D::BeginFrame();
		Renderer3D::BeginFrame();
		// Takes back last time round's ray counts and zeroes this frame's
		// buffer, outside any render pass, before anything traces (WR-16 S0).
		if (commandList)
			RayCounters::BeginFrame(*commandList);
		DebugRenderer::BeginFrame();
		ParticleRenderer::BeginFrame();
		Skybox::BeginFrame();
		UIRenderer::BeginFrame();
		// Cascades are rendered per frame, if at all. Anything left from the
		// last one describes a camera that has since moved.
		ShadowMap::Invalidate();
		EnvironmentIBL::BeginFrame();
		PostProcess::BeginFrame();
		AutoExposure::BeginFrame();
	}

	void Renderer::EndFrame()
	{
		s_CommandList = nullptr;
	}

	uint64_t Renderer::GetFrameCount()
	{
		return s_FrameCount;
	}

	void Renderer::ResetFrameCount()
	{
		s_FrameCount = 0;
	}

	void Renderer::SetJitter(const Vec2& ndcOffset)
	{
		s_Jitter = ndcOffset;
	}

	Vec2 Renderer::GetJitter()
	{
		return s_Jitter;
	}

	void Renderer::SetCameraMotion(CameraMotion* motion)
	{
		s_CameraMotion = motion;
	}

	CameraMotion* Renderer::GetCameraMotion()
	{
		return s_CameraMotion;
	}

	namespace
	{
		float s_GlobalIllumination = 0.0f;
		float s_ReflectionFloor = 0.05f;
		int s_GiBounces = 1;
		float s_GiReach = 0.0f;
		Vec2 s_ReflectionGloss{ 0.25f, 0.60f };
		Renderer::Features s_Features;
	}

	void Renderer::SetGlobalIllumination(float intensity)
	{
		s_GlobalIllumination = intensity;
	}

	void Renderer::SetReflectionFloor(float floor)
	{
		s_ReflectionFloor = Math::Max(floor, 0.0f);
	}

	float Renderer::GetReflectionFloor()
	{
		return s_ReflectionFloor;
	}

	float Renderer::GetGlobalIllumination()
	{
		return s_GlobalIllumination;
	}

	void Renderer::SetGiBounces(int bounces)
	{
		s_GiBounces = Math::Clamp(bounces, 1, 2);
	}

	void Renderer::SetGiReach(float metres)
	{
		s_GiReach = Math::Max(metres, 0.0f);
	}

	float Renderer::GetGiReach()
	{
		return s_GiReach;
	}

	namespace
	{
		float s_RayScale = 1.0f;

		// Frames of measurement seen since the target was set. The controller
		// ignores the first few: see UpdateRayBudget.
		int  s_RayBudgetSamples = 0;
		bool s_RayBudgetEngaged = false;

		// **Discrete levels, because the thing being set is an integer.**
		//
		// Ray counts are whole numbers -- eight taps, or seven, never 7.4 --
		// so a scale that drifts continuously lands either side of a rounding
		// boundary and the count flips between two values frame after frame.
		// Each flip is a different amount of ambient-occlusion noise over the
		// whole image, and the eye reads that as flicker. It is not the
		// controller being unstable; it is a continuous control on a quantised
		// output, which flickers no matter how gently the control moves.
		//
		// So the levels are named, they are far enough apart to be worth
		// moving between, and nothing lands between them. Eight taps, six,
		// four, two.
		// Floored at half -- see kMinRayScale. The steps are also gentler
		// than they were: a level change is visible, so the fewer levels a
		// scene crosses and the less each one costs, the better.
		const float kRayLevels[] = { 1.0f, 0.85f, 0.7f, 0.5f };
		constexpr int kRayLevelCount = 4;
		int s_RayLevel = 0;

		// Frames to wait after a change before considering another.
		//
		// The reading is smoothed, so it describes the frames *before* the
		// last change for a while after it. A controller that acts on that
		// sees its own correction as not having worked, corrects again, and
		// overshoots -- which is the second half of the flicker. Waiting for
		// the measurement to catch up costs nothing: the level is already
		// close, and holding it is exactly what stability looks like.
		// Frames a level must hold before another change is allowed. Raised
		// with the dead band above and for the same reason: settling slowly is
		// invisible, settling repeatedly is not.
		constexpr int kRayCooldown = 90;
		int s_RayCooldown = 0;
	}

	void Renderer::ResetRayBudget()
	{
		s_RayBudgetSamples = 0;
		s_RayBudgetEngaged = false;
		s_RayLevel = 0;
		s_RayCooldown = 0;
		s_RayScale = 1.0f;
	}

	float Renderer::GetRayScale()        { return s_RayScale; }

	void Renderer::UpdateRayBudget(float rayGpuMs, float frameGpuMs)
	{
		const RenderSettings& render = Project::Render();

		// **The command line wins, and forces Absolute.** It exists so a
		// benchmark can pin a target without editing the project, which is the
		// only way to A/B a controller against itself.
		const float override = EngineConfig::Get().RayBudgetMs;
		const RayBudgetMode mode = override > 0.0f ? RayBudgetMode::Absolute
												   : render.RayBudget;

		if (mode == RayBudgetMode::Off)
		{
			s_RayScale = 1.0f;
			return;
		}

		// **What the rays may cost, in milliseconds either way.**
		//
		// Fractional resolves to a number here rather than being compared as a
		// ratio, so both modes share one controller and one set of constants.
		// A frame that has not been measured yet gives Fractional nothing to
		// take a share of, so it waits rather than guessing.
		float targetMs = 0.0f;
		if (mode == RayBudgetMode::Absolute)
		{
			targetMs = override > 0.0f ? override : render.RayBudgetMs;
		}
		else
		{
			if (frameGpuMs <= 0.0f || rayGpuMs <= 0.0f)
				return;

			// **Solved, not chased.**
			//
			// The obvious form -- fraction times the frame -- is a target that
			// moves when the thing it controls moves. Cutting rays shortens
			// the frame, which lowers the target, which asks for fewer rays
			// still: it ratchets down through the levels instead of settling,
			// and every step of that ratchet is a visible change in ambient
			// occlusion. Measured on the showroom's ceiling: two drops in
			// under a second, ending two levels below where it belonged, with
			// the corners visibly under-occluded.
			//
			// The rest of the frame is what does not move. Asking for
			//     rays / (fixed + rays) = f
			// gives rays = f * fixed / (1 - f) directly -- a fixed point,
			// reached in one step and stable once there.
			const float fraction = Math::Clamp(render.RayBudgetFraction, 0.05f, 0.9f);
			const float fixedMs = Math::Max(frameGpuMs - rayGpuMs, 0.01f);
			targetMs = fraction * fixedMs / (1.0f - fraction);
		}

		if (targetMs <= 0.0f)
		{
			s_RayScale = 1.0f;
			return;
		}

		// **Measured on the ray passes, not the frame.**
		//
		// Budgeting the whole frame is a category error: most of a frame is
		// raster, shadow maps and post, and no ray count can pay for any of
		// it. A machine whose fixed costs already exceeded the target would
		// pin the rays at their floor forever and still miss it -- stripped of
		// quality and slow, with the controller cutting the one thing that was
		// not the cause.
		if (rayGpuMs <= 0.0f)
			return;

		// **The first frames of a scene are not a measurement of it.**
		//
		// Shader compilation, first-frame allocation, the first environment
		// prefilter and the first probe capture all land in the frames just
		// after a load, and the reading this controller gets is smoothed, so
		// it arrives at the truth over several frames rather than at once. A
		// controller that believed those frames pulled the ray count down to
		// its floor and then walked it back up as the scene settled -- a
		// visible dip and recovery right when somebody is first looking.
		//
		// So it watches without acting for long enough that both are past.
		// Holding at 1.0 while it waits is deliberate: full quality is the
		// right thing to show when you do not yet know what you can afford.
		constexpr int kSettleFrames = 10;
		if (s_RayBudgetSamples < kSettleFrames)
		{
			++s_RayBudgetSamples;
			return;
		}

		// Let the last change show up in the measurement before judging it.
		if (s_RayCooldown > 0)
		{
			--s_RayCooldown;
			return;
		}

		const float ratio = rayGpuMs / targetMs;

		// **Asymmetric, and deliberately so.** Over budget is a problem now,
		// so it drops as soon as the overshoot is real. Under budget is not a
		// problem at all, so it only climbs back when there is enough headroom
		// that the higher level will still fit -- a level costs about a third
		// more than the one below it, so anything less than that margin buys a
		// climb followed immediately by a drop, which is the flicker again
		// wearing a different hat.
		// **A wider dead band, because a level change is a visible event.**
		//
		// Ten per cent over budget was tight enough that ordinary frame-to-frame
		// variation crossed it, and the controller spent a session stepping: on
		// the showroom, eight changes in one short run, including a 3-2-3 that
		// is a drop and an immediate undo. Every one of those is the whole
		// screen changing quality at once, which is exactly what somebody
		// watching reports as flicker -- and the cost of tolerating a little
		// overshoot is a fraction of a millisecond, where the cost of moving is
		// something they can see.
		constexpr float kDropAbove = 1.35f;

		int wanted = s_RayLevel;
		if (ratio > kDropAbove)
		{
			// **The first move goes straight to the right level.** Nobody has
			// seen a settled image yet, so there is nothing to be gradual
			// from, and stepping one level at a time would spend a cooldown
			// on each -- most of a second of visibly wrong quality. Later
			// moves are one level, because by then the picture is one somebody
			// is already looking at.
			if (!s_RayBudgetEngaged)
			{
				wanted = kRayLevelCount - 1;
				for (int i = 0; i < kRayLevelCount; i++)
				{
					if (kRayLevels[i] <= 1.0f / ratio)
					{
						wanted = i;
						break;
					}
				}
			}
			else
			{
				wanted = s_RayLevel + 1;
			}
		}
		else if (s_RayLevel > 0)
		{
			// **Climb only if the climb would hold.**
			//
			// A fixed "under budget by this much" test is what makes a
			// controller on discrete levels oscillate, and the levels here are
			// not evenly spaced: the last step halves the rays. Sitting at 0.5
			// and ten per cent over budget, dropping to 0.25 puts the ratio at
			// about 0.56 -- under any sensible raise threshold -- so it climbs
			// straight back, is over budget again, and drops. A limit cycle
			// with the cooldown for a period, which at a hundred and thirty
			// frames a second is flicker at about six hertz.
			//
			// So the test is not "is there room now" but "would there still be
			// room after". Ray time is close to linear in ray count, so the
			// ratio at the level above is this one scaled by the ratio of
			// their counts. Climb only if that predicted ratio clears the drop
			// threshold with a margin -- otherwise the climb is a drop waiting
			// for its cooldown to expire.
			//
			// This is correct for any spacing, which is what makes it the fix
			// rather than a tuning of the numbers.
			// Small, so a level lost to a brief spike is climbed back out of.
			// The dead band above is what stops the oscillation now; a large
			// margin here would instead make every drop close to permanent,
			// which is how a scene ends up sitting at the floor all session.
			constexpr float kClimbMargin = 0.03f;
			const float step = kRayLevels[s_RayLevel - 1] / kRayLevels[s_RayLevel];
			if (ratio * step < kDropAbove - kClimbMargin)
				wanted = s_RayLevel - 1;
		}

		wanted = Math::Clamp(wanted, 0, kRayLevelCount - 1);
		if (wanted == s_RayLevel)
			return;

		RV_CORE_INFO("Ray budget: level {0} -> {1} (scale {2}), rays {3} ms against {4} ms",
					 s_RayLevel, wanted, kRayLevels[wanted], rayGpuMs, targetMs);
		s_RayLevel = wanted;
		s_RayScale = kRayLevels[s_RayLevel];
		s_RayBudgetEngaged = true;
		s_RayCooldown = kRayCooldown;
	}

	void Renderer::SetReflectionGloss(const Vec2& window)
	{
		s_ReflectionGloss = window;
	}

	void Renderer::SetActiveFeatures(const Features& features)
	{
		s_Features = features;
	}

	const Renderer::Features& Renderer::GetActiveFeatures()
	{
		return s_Features;
	}

	Vec2 Renderer::GetReflectionGloss()
	{
		return s_ReflectionGloss;
	}

	int Renderer::GetGiBounces()
	{
		return s_GiBounces;
	}

	namespace
	{
		const Renderer::ScreenIndirect* s_ScreenIndirect = nullptr;
	}

	void Renderer::SetScreenIndirect(const ScreenIndirect* indirect)
	{
		s_ScreenIndirect = indirect;
	}

	const Renderer::ScreenIndirect* Renderer::GetScreenIndirect()
	{
		return s_ScreenIndirect;
	}

	void Renderer::SetScreenReflections(const ScreenReflections* reflections)
	{
		s_ScreenReflections = reflections;
	}

	const Renderer::ScreenReflections* Renderer::GetScreenReflections()
	{
		return s_ScreenReflections;
	}

	RHI::RHICommandList* Renderer::GetCommandList()
	{
		return s_CommandList;
	}

	RHI::RHIDevice& Renderer::GetDevice()
	{
		RV_CORE_ASSERT(s_Device, "Renderer has no device");
		return *s_Device;
	}

	bool Renderer::HasDevice()
	{
		return s_Device != nullptr;
	}

	void Renderer::OnWindowResize(unsigned int width, unsigned int height)
	{
		if (s_Device)
			s_Device->OnResize(width, height);
	}

	void Renderer::SetWireframe(bool enabled)
	{
		Renderer2D::SetWireframe(enabled);
		Renderer3D::SetWireframe(enabled);
	}

	bool Renderer::IsWireframe()
	{
		return Renderer2D::IsWireframe();
	}

	namespace
	{
		uint32_t s_TargetSamples = 1;
	}

	uint32_t Renderer::GetTargetSamples()
	{
		return s_TargetSamples;
	}

	void Renderer::SetTargetFormats(RHI::Format color, RHI::Format depth, uint32_t samples,
									RHI::Format velocity, RHI::Format normal,
									RHI::Format indirect)
	{
		s_TargetSamples = samples;

		Renderer2D::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		Renderer3D::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		DebugRenderer::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		ParticleRenderer::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		LightGlow::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		Skybox::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
		ViewportGrid::SetTargetFormats(color, depth, samples, velocity, normal, indirect);
	}
}
