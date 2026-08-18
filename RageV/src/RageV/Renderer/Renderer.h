#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"
#include "RageV/Renderer/TemporalHistory.h"

namespace RageV
{
	// Owns the frame lifecycle and hands the active command list to whoever
	// needs it. Layers have no command-list parameter, so this plays the same
	// role the old RenderCommand static did -- one well-known place to reach
	// the current recording context -- but scoped to a frame.
	class Renderer
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void BeginFrame(RHI::RHICommandList* commandList);
		static void EndFrame();

		// How many frames have been drawn since the main loop started.
		//
		// **Not since the process started**, and the difference is the whole
		// reason this is here. Loading draws frames too, and how many depends
		// on whether the import cache was warm -- so anything indexed by a
		// process-wide count would land on a different value between two runs
		// of the same build. The temporal jitter is indexed by this, and a
		// jitter that moves between runs makes every screenshot check in
		// tools/scripts irreproducible with a failure that looks like noise.
		// ENGINE-NOTES 7r.
		static uint64_t GetFrameCount();

		// Called once when the main loop begins, for the reason above. The
		// same idea as the fixed step's Reset beside it: this is where the
		// clock starts.
		static void ResetFrameCount();

		// The scene camera's sub-pixel offset for this frame, in NDC.
		//
		// Set around the passes that draw into the scene target and cleared
		// after them, so it is zero everywhere else -- which is what keeps it
		// out of the two places it must never reach: a shadow cascade, which
		// is reused across frames and would shimmer along every edge, and a
		// reflection probe, which assembles one cube out of six frames and
		// would assemble six differently-offset faces.
		//
		// Zero for every mode but TAA.
		static void SetJitter(const Vec2& ndcOffset);
		static Vec2 GetJitter();

		// Which frame chain is being drawn, for the one thing that has to know:
		// motion vectors, which are the difference between this camera and the
		// camera that drew the history this chain is about to reproject into.
		//
		// Set and cleared around the scene draw exactly as the jitter is, and
		// null everywhere else. Null means "no chain", which BeginScene renders
		// as zero velocity -- correct for a shadow cascade, for the six faces
		// of a probe capture, and for any pass with no history to reproject.
		//
		// A pointer rather than a value because BeginScene *updates* it: what
		// this frame draws with is what the next frame reprojects from.
		static void SetCameraMotion(CameraMotion* motion);
		static CameraMotion* GetCameraMotion();

		// Last frame's screen-space reflection trace, for the lighting that
		// draws this frame: RGB the traced radiance, A how far to trust it,
		// full resolution in the chain's output space. ENGINE-NOTES 7af.
		//
		// The PBR shader mixes it into the probe's reflected radiance *inside*
		// the lighting integral -- the same weight, the same occlusion, the
		// same F0 the probe term gets -- which is what makes the replacement
		// exact rather than a post-pass guess at what the probe contributed.
		// The price is one frame of latency, which the reprojection through
		// each surface's own motion vector hides.
		//
		// Set and cleared around the scene draw exactly as the jitter is, and
		// null everywhere else: null means "no reflections", which BeginScene
		// binds as an empty texture at intensity zero. Correct for a shadow
		// cascade, a probe capture, a chain with the feature off, and the
		// first frame of a chain that has not traced anything yet.
		struct ScreenReflections
		{
			RHI::Ref<RHI::RHITexture> Texture;
			float Intensity = 0.0f;
		};
		static void SetScreenReflections(const ScreenReflections* reflections);
		static const ScreenReflections* GetScreenReflections();

		// Ray-traced global illumination's dial and its per-frame die
		// (ENGINE-NOTES 7at). The frame graph sets it when the traced form
		// resolves on; the lit shader's RV_RAY_GI block reads both. Intensity
		// is the profile's GiIntensity -- one dial serves both forms -- and
		// the counter is what makes each frame's four rays a different four,
		// which is what TAA needs to converge them.
		static void SetGlobalIllumination(float intensity);
		static float GetGlobalIllumination();

		// How many times a traced bounce ray's light bounces again before the
		// probe answers (ENGINE-NOTES 7ax). 1 or 2; the frame sets it, the lit
		// shader reads it out of the scene block.
		static void SetGiBounces(int bounces);
		static int GetGiBounces();

		// Last frame's indirect diffuse, for the lighting that draws this
		// frame: RGB the irradiance arriving from the scene, **albedo-free**,
		// A how far to trust it. ENGINE-NOTES 7av.
		//
		// The same shape as ScreenReflections above and for the same reasons,
		// one level down the integral: the lit shader adds this to the probe's
		// irradiance *before* the diffuse term multiplies by albedo, so the
		// bounce is tinted by what the surface is rather than by what it
		// already looked like. **That multiply moving back in here is what
		// retires SSGI's albedo stand-in** -- a post pass has no albedo and
		// this shader has it as a local.
		//
		// Whichever form is enabled fills it: the screen-space gather, or the
		// traced ray pass. One buffer, one writer, so the exclusivity rule is
		// a choice of writer rather than two code paths.
		//
		// One frame late, reprojected through the same previous-frame NDC the
		// reflections use. Indirect diffuse is the cheapest thing in the frame
		// to be late with: low frequency, no hard edges, and a reprojection
		// error in it is a slightly wrong soft gradient.
		//
		// Null everywhere it does not apply -- a shadow cascade, a probe face,
		// a chain with GI off, the first frame of a chain -- and null binds a
		// 1x1 transparent black at intensity zero, which adds nothing.
		struct ScreenIndirect
		{
			RHI::Ref<RHI::RHITexture> Texture;
			float Intensity = 0.0f;
		};
		static void SetScreenIndirect(const ScreenIndirect* indirect);
		static const ScreenIndirect* GetScreenIndirect();

		// Null outside a frame, and between BeginFrame returning nullptr and
		// the next successful frame.
		static RHI::RHICommandList* GetCommandList();
		static RHI::RHIDevice& GetDevice();
		static bool HasDevice();

		static void OnWindowResize(unsigned int width, unsigned int height);

		// Applied to both the 2D and 3D renderers, since a wireframe view that
		// only covered half the scene would be misleading.
		static void SetWireframe(bool enabled);
		static bool IsWireframe();

		// Formats of whatever both renderers are currently drawing into.
		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined,
									 RHI::Format indirect = RHI::Format::Undefined);

		// What the last SetTargetFormats said. Anything that renders the scene
		// into a target of its own -- a reflection probe face -- has to match
		// it, because the pipelines are built once for one count.
		static uint32_t GetTargetSamples();
	};
}
