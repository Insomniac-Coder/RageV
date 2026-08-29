#include <rvpch.h>
#include "FrameGraphBuilder.h"
#include "PostProcess.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Core/FrameProfiler.h"
#include "RayShadows.h"
#include "VoxelGI.h"
#include "Renderer3D.h"
#include "RageV/Asset/AssetManager.h"
#include "Renderer.h"
#include "UIRenderer.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// How far down the bloom chain goes. Five levels at half-resolution
		// steps reaches 1/32 of the frame, which is a wide enough blur to read
		// as a glow rather than a halo, and stops well before the levels get
		// small enough to shimmer.
		constexpr int kBloomLevels = 5;

		// The surface-description attachment: octahedral normal, roughness,
		// metallic. Eight bits per octahedral component is about a degree,
		// which is ample for a reflection direction. ENGINE-NOTES 7ad.
		constexpr Format kNormalFormat = Format::R8G8B8A8_UNORM;
		// Traced indirect diffuse (7av): irradiance, unbounded and positive,
		// so half floats rather than the normal's eight bits. Named here
		// because five places have to agree about it -- the target, the six
		// renderers, the UI world layer, the probe face and the resolve.
		constexpr Format kIndirectFormat = Format::R16G16B16A16_SFLOAT;

		// Radius of the tent filter on the way back up, in texels of the level
		// being read. Wider is smoother and starts to look like a box.
		constexpr float kUpsampleRadius = 1.0f;

		// Below this the chain would be sampling a handful of texels, and the
		// filter stops meaning anything.
		constexpr uint32_t kMinBloomSize = 8;


		// The largest SSAA factor offered. Four means sixteen times the pixels
		// shaded, and at a 4K output a 16K scene target -- past what a lot of
		// hardware will allocate and all of what is sensible.
		constexpr int kMaxSupersample = 4;

		// The bounds on the jitter sequence's length, now that it is a setting
		// rather than a constant.
		//
		// One at the bottom because a phase of zero is a modulo by zero, and a
		// project file is a text file somebody can type into. Sixteen at the
		// top for the reason the default is eight: a temporal filter that has
		// to reject its history -- because the camera cut, or a silhouette
		// moved -- starts again from nothing, and the shorter the phase the
		// sooner it has covered the pixel evenly again. Longer converges on a
		// finer image and recovers more slowly, and past sixteen the finer
		// image stops being visible while the slower recovery does not.
		constexpr int kMinJitterPhase = 1;
		constexpr int kMaxJitterPhase = 16;

		// The radical-inverse sequence, which is what "low discrepancy" means
		// in practice: successive points fall in the gaps the earlier ones
		// left, rather than wherever a random generator puts them. Eight
		// random offsets can easily leave a quarter of the pixel unsampled;
		// eight Halton offsets cannot.
		//
		// One-based, because Halton's first point is 0 and an offset of zero
		// contributes nothing -- it renders the frame the unjittered path
		// would have rendered.
		float Halton(uint32_t index, uint32_t base)
		{
			float result = 0.0f;
			float fraction = 1.0f;

			while (index > 0)
			{
				fraction /= (float)base;
				result += fraction * (float)(index % base);
				index /= base;
			}

			return result;
		}
	}

	uint32_t TemporalJitterPhase(int phase)
	{
		return (uint32_t)Math::Clamp(phase, kMinJitterPhase, kMaxJitterPhase);
	}

	Vec2 TemporalJitter(uint64_t frame, uint32_t width, uint32_t height,
						float scale, int phase)
	{
		if (width == 0 || height == 0)
			return Vec2(0.0f, 0.0f);

		const uint32_t index = (uint32_t)(frame % TemporalJitterPhase(phase)) + 1;

		// Centred on the pixel, then scaled: a width of 0 has to give exactly
		// zero on both axes, and it does only because the centring happens
		// first. Scaling a point that had not been centred would shrink the
		// offsets towards the pixel's corner rather than towards its middle,
		// which is a filter biased down and to the left -- an image that
		// converges half a pixel off, everywhere, with nothing to see.
		const float x = (Halton(index, 2) - 0.5f) * scale;
		const float y = (Halton(index, 3) - 0.5f) * scale;

		return Vec2(2.0f * x / (float)width, 2.0f * y / (float)height);
	}

	Mat4 JitterProjection(const Mat4& projection, const Vec2& ndcOffset)
	{
		// A translation in *clip* space: clip.xy += offset * clip.w, which is
		// a constant shift once the perspective divide has run. Applied as a
		// matrix on the left rather than by editing two entries of the
		// projection, because which two entries those are depends on whether
		// the projection is perspective or orthographic -- and an editor
		// camera can be either.
		Mat4 shift(1.0f);
		shift[3].x = ndcOffset.x;
		shift[3].y = ndcOffset.y;

		return shift * projection;
	}

	AntiAliasing ResolveAntiAliasing(const RenderSettings& render)
	{
		// Resolved in one place, because the alternative has already cost a
		// day: the SMAA passes branched on the scene's stored mode while the
		// rest of the frame branched on the resolved one, so --aa=smaa built
		// FXAA's chain and the two modes came out byte-identical. Anything
		// that needs to know which filter is running asks here.
		//
		// This is also the whole of the render override chain: the project's
		// answer, then `ragev.ini`, then `--aa=`. Nothing else in the engine
		// knows those last two exist.
		const EngineConfig& config = EngineConfig::Get();
		const AntiAliasing requested = config.HasAAOverride ? config.AAOverride
															: render.AA;

		// Every mode but None is a pass PostProcess owns. Without it the chain
		// would tone map into an intermediate that nothing then reads, and the
		// window would be black with no error anywhere.
		return PostProcess::IsReady() ? requested : AntiAliasing::None;
	}

	bool ResolveRayTracing(const RenderSettings& render)
	{
		// The rays ride on the shadow pass: the structure they trace into is
		// built in Scene::RenderShadows, and the lit shader declares it under
		// RV_RAY_SHADOWS. Shadows off is rays off -- reflections and occlusion
		// included, since they resolve through this -- and the panel hides
		// the whole block accordingly.
		if (!render.ShadowsEnabled)
			return false;
		const EngineConfig& config = EngineConfig::Get();
		const bool requested = config.HasRayTracingOverride ? config.RayTracingOverride
															: render.RayTracing;
		if (requested && !RayShadows::IsAvailable())
		{
			static bool reported = false;
			if (!reported)
			{
				RV_CORE_INFO("Ray tracing requested but this device has no ray queries; "
							 "using shadow maps");
				reported = true;
			}
			return false;
		}
		return requested;
	}

	RayDetail ResolveRayTracedReflections(const RenderSettings& render)
	{
		if (!ResolveRayTracing(render))
			return RayDetail::Off;
		const EngineConfig& config = EngineConfig::Get();
		RayDetail requested = render.RayTracedReflections;
		if (config.HasRayReflectionsOverride)
		{
			// --rt-reflections=on predates the level, and means what it meant.
			requested = !config.RayReflectionsOverride ? RayDetail::Off
					  : (requested == RayDetail::Off ? RayDetail::High : requested);
		}
		if (requested != RayDetail::Off && !Renderer3D::IsBindless())
		{
			static bool reported = false;
			if (!reported)
			{
				RV_CORE_INFO("Ray-traced reflections requested but materials are not bindless "
							 "on this device; a hit cannot be shaded, so screen-space reflections stay");
				reported = true;
			}
			return RayDetail::Off;
		}
		return requested;
	}

	// Where a mirror ray stops being the answer, per level: below the first
	// number the ray is taken whole, above the second the probe's blur is what
	// many jittered rays would have converged to anyway. High is the window
	// the shader used to hold as a constant.
	Vec2 RayDetailGloss(RayDetail detail)
	{
		switch (detail)
		{
			case RayDetail::Low:    return Vec2(0.05f, 0.20f);
			case RayDetail::Medium: return Vec2(0.15f, 0.40f);
			default:                return Vec2(0.25f, 0.60f);
		}
	}

	AoDetail ResolveRayTracedAmbientOcclusion(const RenderSettings& render)
	{
		if (!ResolveRayTracing(render))
			return AoDetail::Off;
		const EngineConfig& config = EngineConfig::Get();
		if (config.HasRayAoOverride)
		{
			// --rt-ao=on predates the level, so it means "at whatever level
			// the settings hold" and picks the one the flag used to mean when
			// they hold none. Every check script that passes on|off keeps
			// working and keeps measuring what it measured.
			if (!config.RayAoOverride)
				return AoDetail::Off;
			return render.RayTracedAmbientOcclusion == AoDetail::Off
				 ? AoDetail::Half : render.RayTracedAmbientOcclusion;
		}
		return render.RayTracedAmbientOcclusion;
	}

	// How many bounces the traced form runs (ENGINE-NOTES 7ax). Clamped to
	// 1 or 2 here as well as at the flag, because the setting is an int in a
	// serialized struct and a scene file can hold anything.
	int ResolveGiBounces(const PostSettings& post)
	{
		const EngineConfig& config = EngineConfig::Get();
		const int requested = config.GiBouncesOverride != 0 ? config.GiBouncesOverride
															: post.GiBounces;
		return Math::Clamp(requested, 1, 2);
	}

	bool ResolveVoxelGlobalIllumination(const PostSettings& post)
	{
		const EngineConfig& config = EngineConfig::Get();
		const bool requested = config.HasVoxelGiOverride ? config.VoxelGiOverride
														: post.VoxelGlobalIllumination;
		if (requested && !VoxelGI::IsReady())
		{
			static bool reported = false;
			if (!reported)
			{
				RV_CORE_INFO("Voxel global illumination requested but this device cannot "
							 "write storage images from a fragment stage, or the voxel "
							 "shaders did not compile; the screen-space form stays");
				reported = true;
			}
			return false;
		}
		return requested;
	}

	RayDetail ResolveRayTracedGlobalIllumination(const RenderSettings& render)
	{
		if (!ResolveRayTracing(render))
			return RayDetail::Off;
		const EngineConfig& config = EngineConfig::Get();
		RayDetail requested = render.RayTracedGlobalIllumination;
		if (config.HasRayGiOverride)
		{
			// --rt-gi=on predates the level: it means "at whatever the
			// settings hold", and the level the flag used to mean when they
			// hold none. Every check script passing on|off keeps measuring
			// what it measured.
			requested = !config.RayGiOverride ? RayDetail::Off
					  : (requested == RayDetail::Off ? RayDetail::High : requested);
		}
		// Shading a hit reads the material heap, exactly as a reflection's
		// does: without bindless there is nothing to shade with, so the
		// screen-space form stays and the log says so once (7at).
		if (requested != RayDetail::Off && !Renderer3D::IsBindless())
		{
			static bool reported = false;
			if (!reported)
			{
				RV_CORE_INFO("Ray-traced global illumination requested but materials are not "
							 "bindless on this device; a bounce cannot be shaded, so the "
							 "screen-space form stays");
				reported = true;
			}
			return RayDetail::Off;
		}
		return requested;
	}

	// What a level costs per pixel, in one place so the target and the shader
	// cannot be given different answers -- the trap 7az records for the
	// rasterised dial, where a pass ran at one resolution and read a texel
	// size for another.
	// **Resolution is the dial, ray count is the trim.** Halving the trace
	// resolution is a clean 4x on the pass and the images do not separate --
	// measured 9.71 -> 2.84 ms on the showroom with a max per-pixel difference
	// of 11/255 and 0.07% of pixels differing by more than two levels. Halving
	// the ray count changes the *character* of the noise instead, which the eye
	// finds far more readily than softness. So no level traces at full
	// resolution any more; High is four rays at half, and the two below it
	// spend their saving on resolution first.
	//
	//   Low     2 rays, quarter    Medium  2 rays, half    High  4 rays, half
	//
	// The divisor and the ray count are stated together here because giving the
	// target one answer and the shader another is the trap 7az records.
	uint32_t RayDetailDivisor(RayDetail detail)
	{
		return detail == RayDetail::Low ? 4u : 2u;
	}
	int RayDetailRays(RayDetail detail)
	{
		return detail == RayDetail::High ? 4 : 2;
	}

	void BuildFrame(RenderGraph& graph, const FrameDesc& desc)
	{
		if (desc.Output == kRGInvalid || desc.Width == 0 || desc.Height == 0)
			return;

		// Which filter, resolved once, by the function everything else asks.
		const EngineConfig& config = EngineConfig::Get();
		const AntiAliasing aa = ResolveAntiAliasing(desc.Render);

		// SSAA is decided here rather than with the other two, because it is
		// the only one that changes the size of the scene target -- everything
		// else in the frame reacts to something that has already been drawn.
		const int requestedFactor = config.SupersampleOverride > 0
			? config.SupersampleOverride
			: desc.Render.SupersampleFactor;
		const int supersample = aa == AntiAliasing::SSAA
			? Math::Clamp(requestedFactor, 1, kMaxSupersample)
			: 1;

		// --- the scene, in linear HDR -----------------------------------------
		// RGBA16F rather than the 11-11-10 alternative: bloom reads this back
		// and the smallest levels accumulate a lot of energy into few texels,
		// where 10 bits of blue starts to show as a colour cast.
		RGTargetDesc sceneDesc;
		sceneDesc.Name = "SceneHDR";
		sceneDesc.Color = Format::R16G16B16A16_SFLOAT;
		sceneDesc.Depth = Format::D32_SFLOAT;
		// Larger for SSAA, which is the whole of what SSAA does on the way in.
		// The camera's aspect is unchanged, so nothing downstream of the
		// projection needs to know.
		sceneDesc.Scale = (float)supersample;

		// And multisampled for MSAA, which is the whole of what *that* does.
		// The RHI resolves each attachment when a pass ends and hands out the
		// resolve, so nothing below this line can tell either -- see
		// ENGINE-NOTES 7q. What cannot be hidden is the pipeline state: a
		// pipeline's sample count has to equal the attachment's, so the
		// renderers are told before anything is recorded.
		//
		// **Sanitised, not clamped.** A sample count is a bit flag in both
		// APIs, so the failure mode for a bad one is not a bad picture -- five
		// set two bits of VkSampleCountFlagBits, every pipeline failed to
		// create, and the submit lost the device. Clamping to a range is no
		// defence against that: five is inside every range anyone would pick.
		// SanitiseMsaaSamples answers with a count that exists, and with one
		// the *device* says it can do. ENGINE-NOTES 7ci.
		int msaa = 1;
		if (aa == AntiAliasing::MSAA)
		{
			const int asked = config.MsaaOverride > 0 ? config.MsaaOverride
													  : desc.Render.MsaaSamples;
			const uint32_t deviceMax = Renderer::GetDevice().GetCaps().MaxSampleCount;
			msaa = SanitiseMsaaSamples(asked, deviceMax);

			// Said once per change rather than every rebuild: this runs on
			// every resize, and a line per frame is a line nobody reads.
			static int s_LastSaid = 0;
			if (msaa != asked && asked != s_LastSaid)
			{
				s_LastSaid = asked;
				RV_CORE_WARN("MSAA at {0}x is not a sample count this device can use; "
							 "using {1}x. Legal counts are 2, 4 and 8, and this device "
							 "tops out at {2}x.", asked, msaa, deviceMax);
			}
		}
		sceneDesc.Samples = (uint32_t)msaa;

		// Depth of field reads this. Colour is always sampleable; depth costs
		// an extra usage flag and, on some hardware, a compression mode -- so
		// it was off until something wanted it.
		//
		// **On unconditionally**, not only when depth of field is enabled, for
		// the reason the velocity attachment gives a few lines down: a target
		// whose *shape* depends on a setting is a target every pipeline and
		// every reflection probe has to agree with about that setting too, and
		// the pool would reallocate it every time a checkbox moved. 7q is the
		// record of how that goes. ENGINE-NOTES 7z.
		sceneDesc.SampleDepth = true;
		Renderer::SetTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
								   Format::R16G16_SFLOAT, kNormalFormat, kIndirectFormat);
		// The UI renderer's *world* layer draws inside the scene pass -- world
		// text, and the editor's light and camera marks -- so it takes the
		// scene's sample count. Its screen-space layer is set separately, down
		// with the UI pass, and stays at one.
		UIRenderer::SetWorldTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
										  Format::R16G16_SFLOAT, kNormalFormat, kIndirectFormat);

		// Accumulation and revealage live on the *scene's* target rather than
		// one of their own, so the transparent pass depth-tests against the
		// opaque geometry it is drawn over. Three separate targets would each
		// own a depth buffer and the particles would ignore the world.
		//
		// Float accumulation because it is a sum that is meant to exceed one;
		// a single channel of revealage because it is one number.
		const bool wantTransparent = desc.DrawTransparent && desc.ResolveTransparent;
		if (wantTransparent)
		{
			sceneDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT, Format::R8_UNORM };
		}

		// Motion vectors, in screen space, two half floats a pixel.
		//
		// **Appended**, so transparency keeps attachments 1 and 2 and nothing
		// that already binds them has to learn a new number. Half float rather
		// than 8-bit because a velocity is signed and routinely a small
		// fraction of a pixel, which is exactly where 8 bits has nothing left.
		//
		// Always present rather than only when a temporal filter wants it: a
		// target whose *shape* depends on a setting is a target the reflection
		// probes and every pipeline have to agree with about that setting too,
		// and 7q is the record of how that goes.
		const uint32_t velocityIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(Format::R16G16_SFLOAT);

		// The surface description SSR reads: octahedral normal in RG, roughness
		// in B, metallic in A. Appended after velocity, so velocity keeps its
		// number. Always present for the same shape-not-setting reason as the
		// velocity; only the PBR shaders write real values, and a clear of zero
		// decodes to "no surface", which the resolve reads as "no reflection".
		// ENGINE-NOTES 7ad records the sweep this attachment cost.
		const uint32_t normalIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kNormalFormat);

		// Traced indirect diffuse (ENGINE-NOTES 7av). Appended last, so every
		// number above keeps its meaning, and **always present** for the same
		// shape-not-setting reason velocity and the surface are. Only the lit
		// shaders compiled with RV_RAY_GI write anything but zero, and zero
		// resolves to no bounce.
		//
		// This is the attachment whose absence made the first attempt at the
		// traced pass measure +0.00 while every graph assertion passed: the
		// pipelines are built once for one count, so declaring it here is only
		// a third of the job -- the six renderers, the UI world layer and the
		// probe face all had to be told as well.
		const uint32_t indirectIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kIndirectFormat);

		const RGResource sceneHDR = graph.CreateTarget(sceneDesc);

		// The sub-pixel offset this frame is drawn with, in the scene target's
		// own pixels -- which are the supersampled ones when SSAA is on, not
		// the output's. Zero for every mode but TAA, so every other mode
		// renders exactly the frame it rendered before this existed.
		//
		// Indexed by the frame *count*, never by elapsed time. A clock-driven
		// sequence would make --screenshot-frame=30 produce a different image
		// on every run, and the failure would look like noise rather than like
		// a mistake. ENGINE-NOTES 7r.
		//
		// Only when there is somewhere to accumulate into. Jittering without a
		// history is a wobble and nothing else -- strictly worse than not
		// jittering -- so a caller with no TemporalHistory gets the unjittered
		// frame rather than the worse half of a feature.
		const bool wantTemporal = aa == AntiAliasing::TAA && desc.History != nullptr
							   && PostProcess::IsReady();

		const Vec2 jitter = wantTemporal
			? TemporalJitter(Renderer::GetFrameCount(),
							 desc.Width * (uint32_t)supersample,
							 desc.Height * (uint32_t)supersample,
							 desc.Render.TemporalJitterScale,
							 desc.Render.TemporalJitterPhase)
			: Vec2(0.0f, 0.0f);

		// A history left over from before the mode changed describes a frame
		// this chain is no longer producing. Switching to FXAA for ten seconds
		// and back must not resume from a ten-second-old image.
		if (!wantTemporal && desc.History)
			desc.History->Invalidate();

		// --- SSR: what this frame's lighting reads --------------------------------
		//
		// Screen-space reflections are traced at the end of a frame and read
		// by the *next* frame's lighting, inside the PBR shader, where the
		// probe's reflected radiance is swapped for the traced one under the
		// exact weight the probe would have had. So the scene pass samples
		// last frame's trace, and the SSR passes below write this frame's
		// into the other half of the pair. Only when there is somewhere to
		// keep it: a caller with no Reflections history gets the probe alone,
		// the same shape as TAA with no History. ENGINE-NOTES 7af.
		// And not at all when the traced form is on (ENGINE-NOTES 7ao): the
		// lit shader then casts the mirror ray itself, this frame, and a
		// screen walk in front of it would only be another way to be wrong
		// on-screen. The profile's toggle is not consulted; its row says so.
		const RayDetail reflectionDetail = ResolveRayTracedReflections(desc.Render);
		const bool rayReflections = reflectionDetail != RayDetail::Off;
		// The window the lit shader weighs a mirror ray in over.
		// **Reflections spend the budget too, and their currency is the
		// window.** A mirror ray is cast per glossy *fragment*, so the count
		// is decided by how much of the screen is smooth enough to qualify --
		// and the window is the only thing that moves that while the ray is
		// cast inside the lit shader.
		//
		// Narrowing pulls the upper bound down toward the lower, so the
		// roughest surfaces still taking a ray give theirs up first and a
		// mirror stays a mirror. It pays into the transparent pass as well as
		// the lit one: glass casts these rays too, which is why that pass
		// nearly halves between a close view of the car and a far one.
		//
		// **Weak on a car, and honestly so.** Automotive paint sits near 0.1
		// roughness, below even the narrowest window's lower bound, so it
		// keeps its ray at any scale -- narrowing High to Medium moved the
		// showroom's lit pass 7.28 ms to 6.78 and no further. What this
		// reclaims is the mid-gloss majority of a scene, not the hero object.
		{
			// **The budget does not touch this window.**
			//
			// It used to: the upper bound was pulled toward the lower one in
			// proportion to the ray scale, so at the floor a High window of
			// 0.25-0.60 collapsed to 0.25-0.34. That does not make reflections
			// cheaper-looking, it makes them *absent* -- every surface rougher
			// than 0.34 simply stopped having one. Observed on the showroom
			// (2026-08-28): a metallic bar at the back of the room lost its
			// reflection a few seconds in and never got it back.
			//
			// A budget may spend fewer rays on a thing. It may not decide the
			// thing is no longer in the picture.
			Renderer::SetReflectionGloss(RayDetailGloss(reflectionDetail));
		}
		const AoDetail rayAo = ResolveRayTracedAmbientOcclusion(desc.Render);
		const bool rayOcclusion = rayAo != AoDetail::Off;
		// The third twin (7at). Where it runs, the lit shader casts the bounce
		// itself and the screen-space chain below is not added at all --
		// whatever the profile holds; its row says so. The dial goes to the
		// renderer here, because the shader reads it out of the scene block:
		// zero when the traced form is not running, so the block costs
		// nothing where it is compiled in but idle.
		const RayDetail giDetail = ResolveRayTracedGlobalIllumination(desc.Render);

		// **And the pass has to have a shader.** Without one the traced branch
		// below cannot run, and leaving this true would suppress the
		// screen-space chain as well -- a frame with no indirect light at all,
		// which is what happened before CompileLitShaders learned to turn the
		// renderer's own flag off in the same case.
		//
		// Asked here rather than inside ResolveRayTracedGlobalIllumination on
		// purpose: that function is also what *decides* whether to put the
		// renderer into traced mode, so consulting the shader there would mean
		// no shader, so no traced mode, so no shader.
		const bool rayGi = giDetail != RayDetail::Off &&
						   Renderer3D::CanTraceGlobalIllumination();
		Renderer::SetGlobalIllumination(rayGi ? Math::Max(desc.Post.GiIntensity, 0.0f) : 0.0f);
		// One while the traced form is off, so the uniform never claims a
		// depth nothing is tracing -- the same shape as the intensity above.
		Renderer::SetGiBounces(rayGi ? ResolveGiBounces(desc.Post) : 1);
		// **Unbounded, and GiRadius is not read here.** 79035ab bounded the
		// bounce ray by the profile's GiRadius on the reasoning that a long
		// ray only ever found "a hit that a miss would have handled
		// identically", because a miss contributes nothing (7bb). That is true
		// of the camp scene it was measured on -- outdoors, where a ray that
		// travels far leaves the geometry and sees sky. It is false indoors,
		// where a ray that travels far hits a *wall*, and the bound turns a
		// real bounce source into a miss: at the 2 m default, gi_corner's red
		// wall contributes exactly nothing above the skirting and the frame
		// carries a dull patch where the bleed should be.
		//
		// And the win it bought is gone. Those rays were cast inside the lit
		// fragment then; 7bs moved them into a pass with a resolution of its
		// own, and today reach 2.5 against 200 is 2.538 ms against 2.443 on
		// the same camp scene, and 6.55 against 6.78 on the showroom -- noise,
		// in both directions. So the bound costs correctness and saves
		// nothing, which makes it a straight removal rather than a trade.
		//
		// GiRadius keeps its meaning for the screen-space gather, which is the
		// form it was written for: how far *that* searches, on screen. The
		// voxel form has always ignored it for the same reason a cone runs to
		// the cascade's edge. Conflating the two is what made a dial tuned for
		// a bleed width decide how far light may travel.
		//
		// **Bounded generously rather than not at all**, because unbounded is
		// not free either: on the camp scene a 10 km ray costs 3.654 ms
		// against 2.443 at 200 m, which is the tail spent traversing empty sky
		// no bounce will ever come back from. 250 m is past the far end of any
		// room and past the camp's clearing, so it changes no picture that
		// 10 km would have drawn -- it only stops paying for the emptiness.
		// Not a dial, and that was measured twice. A per-scene reach was built
		// and benchmarked interleaved on camp -- the scene the old bound was
		// tuned for -- at 2.5 m against this constant: 3.67/3.81/3.97 versus
		// 3.64/3.92/3.91 ms at 1600x900, and 9.89/9.89 versus 9.86/9.87 at
		// native. No signal at either size; the "+1.2 ms camp regression" that
		// motivated the dial was thermal drift across a rebuild gap. A knob
		// that measures as pure noise is a knob someone will one day lower
		// into gi_corner's bug, so it went back out.
		constexpr float kGiReach = 250.0f;
		Renderer::SetGiReach(rayGi ? kGiReach : 0.0f);
		const bool wantReflections = desc.Post.ScreenSpaceReflections
								  && !rayReflections
								  && desc.Reflections != nullptr
								  && PostProcess::IsReady();


		// **What actually runs, published for anything that reports it.**
		//
		// Every line here is a resolution rather than a setting, and the two
		// differ in the case that matters: a project with RayTracing on and
		// reflections at High still runs the screen-space chain on a device
		// with no ray query, which OpenGL never has. Anything that read the
		// settings instead would name a traced feature on a backend that
		// cannot trace, and there would be no way to tell from the screen.
		//
		// It also means nothing downstream needs to know *why* a feature is
		// off -- no device check, no "if OpenGL". The reasons live in the
		// Resolve* functions above, once. ENGINE-NOTES 7cm.
		// **A Baked source that can be honoured drops the whole indirect
		// chain, under every form.** No gather, no voxel cones, no traced
		// bounce, no history buffer: the lit pass reads the stored field per
		// pixel and that is the frame's entire indirect cost -- which is what
		// baked means. The transport the traced chain used to add at runtime
		// is baked into the field itself now (the solve's sweeps are
		// bounces), so dropping the chain no longer trades quality for the
		// saving.
		//
		// The renderer is told this by the scene rather than reading the
		// setting itself, because the setting says what the author *wants*
		// and only the scene knows whether a bake exists to honour it.
		const bool bakedOnly = Renderer3D::IsBakedIrradianceOnly();

		// The voxel form (ENGINE-NOTES 7bc) replaces the screen gather at the
		// head of the same chain, where the profile asks for GI and rays do
		// not win. Only with a grid lit this frame: the scene updates it
		// beside the shadow maps, and a frame without one -- a probe capture,
		// no camera -- adds no chain rather than reading a stale grid.
		//
		// Declared here rather than beside the chain below because the
		// feature report needs the same answer, and two expressions for one
		// question is how a report starts disagreeing with the frame.
		const bool voxelGi = !rayGi && ResolveVoxelGlobalIllumination(desc.Post)
						  && VoxelGI::HasGrid();
		const bool voxelWanted = !rayGi && ResolveVoxelGlobalIllumination(desc.Post);

		{
			Renderer::Features active;
			active.Shadows = desc.Render.ShadowsEnabled;
			active.RayTracing = ResolveRayTracing(desc.Render);
			active.RayTracedReflections = rayReflections;
			active.RayTracedAmbientOcclusion = rayOcclusion;
			// **None of the three GI lines survives a honoured bake**, which
			// is what put "RT GI" and "RT GI (baked)" side by side in the
			// stats overlay: this struct is what the frame *ran*, and a baked
			// frame ran none of them. The baked case is reported by the
			// RTGIBaked / SSGIBaked keys, split by the dropdown that owned
			// the frame.
			active.RayTracedGlobalIllumination = rayGi && !bakedOnly;
			active.ScreenSpaceReflections = wantReflections;
			active.ScreenSpaceGlobalIllumination =
				desc.Post.GlobalIllumination && !rayGi && !voxelGi && !bakedOnly;
			active.VoxelGlobalIllumination = voxelGi && !bakedOnly;
			active.AmbientOcclusion =
				(rayOcclusion ? rayAo : desc.Post.AmbientOcclusion) != AoDetail::Off;
			Renderer::SetActiveFeatures(active);
		}

		Renderer::ScreenReflections reflectionsForScene;
		RGResource previousReflections = kRGInvalid;
		RGResource currentReflections = kRGInvalid;

		if (wantReflections)
		{
			TemporalHistory& reflections = *desc.Reflections;
			reflections.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,
								Format::R16G16B16A16_SFLOAT, "ScreenReflections");

			if (reflections.Current() && reflections.Previous())
			{
				previousReflections = graph.Import(reflections.Previous(), "ReflectionsPrevious");
				currentReflections = graph.Import(reflections.Current(), "ReflectionsCurrent");

				// Nothing to read on the first frame of a chain, or after a
				// resize: the pair holds whatever the driver left in it, and a
				// confidence read out of that would mix somebody else's memory
				// into every metal. The scene draws with the probe alone and
				// the trace below starts the history.
				if (reflections.HasHistory())
				{
					reflectionsForScene.Texture = reflections.Previous()->GetColorTexture(0);
					reflectionsForScene.Intensity = Math::Max(desc.Post.SsrIntensity, 0.0f);
				}
			}
		}
		else if (desc.Reflections)
		{
			// Off, or nowhere to run: a trace left over from before must not
			// be resumed from when the feature comes back.
			desc.Reflections->Invalidate();
		}

		// --- the indirect buffer (ENGINE-NOTES 7av) -------------------------
		//
		// The same one-frame-late shape as the reflections above, one level
		// down the integral: whichever GI form is enabled writes albedo-free
		// irradiance here, and the lit shader reads it next frame and
		// multiplies by the surface's own base colour. That multiply moving
		// into the shader is what retired SSGI's lit-pixel stand-in.
		// Either form fills it -- that is the point of one buffer -- so this
		// asks whether *anything* will, not which. `voxelGi`, `voxelWanted`
		// and `bakedOnly` are resolved above, beside the feature report that
		// has to agree with them.
		const bool wantIndirect = (desc.Post.GlobalIllumination || rayGi)
							   && !bakedOnly
							   && desc.Indirect != nullptr
							   && PostProcess::IsReady()
							   && (!voxelWanted || voxelGi);

		Renderer::ScreenIndirect indirectForScene;
		RGResource previousIndirect = kRGInvalid;
		RGResource currentIndirect = kRGInvalid;
		bool indirectHasHistory = false;
		// How much of last frame's indirect survives into this one (7av).
		// Far higher than TAA's 0.6: irradiance is low frequency, and the
		// estimate underneath it is four rays wide.
		const float giFeedback = Math::Clamp(desc.Post.GiDenoise, 0.0f, 0.98f);

		if (wantIndirect)
		{
			TemporalHistory& indirect = *desc.Indirect;
			// **A second attachment, for what the denoiser remembers.** Frames
			// accumulated in .x and the first two luminance moments in .yz --
			// per-pixel state that has to survive to the next frame and has
			// nowhere to live in the colour target, whose alpha is a validity
			// flag the lit shader multiplies into the bounce. Ping-ponged with
			// the colour by the same pair, so the two cannot get out of step.
			indirect.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,
							 Format::R16G16B16A16_SFLOAT, "Indirect",
							 Format::R16G16B16A16_SFLOAT);

			if (indirect.Current() && indirect.Previous())
			{
				previousIndirect = graph.Import(indirect.Previous(), "IndirectPrevious");
				currentIndirect = graph.Import(indirect.Current(), "IndirectCurrent");

				// Nothing to read on a chain's first frame or after a resize:
				// the pair holds whatever the driver left, and Advance() marks
				// a target valid unconditionally because it means "what was
				// just written". Reading that as light would add uninitialised
				// memory to every surface.
				if (indirect.HasHistory())
				{
					indirectForScene.Texture = indirect.Previous()->GetColorTexture(0);
					indirectForScene.Intensity = Math::Max(desc.Post.GiIntensity, 0.0f);
					// The same fact the denoiser needs: there is a frame
					// behind this one worth accumulating onto.
					indirectHasHistory = true;
				}
			}
		}
		else if (desc.Indirect)
		{
			desc.Indirect->Invalidate();
		}

		graph.AddPass("Scene",
			[&](RGPassBuilder& builder)
			{
				// Colour, velocity and the surface description. Not the
				// transparency attachments: a pipeline's declared colour
				// formats have to match what the pass binds, and the pass
				// that accumulates transparency binds a different pair --
				// which is what WriteAttachments is for. Every pipeline
				// drawing here declares all three of these.
				builder.WriteAttachments(sceneHDR,
					{ { 0, desc.ClearColor },
					  // Zero is "did not move", which is what anything that
					  // never writes velocity should read back as.
					  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  // And zero here decodes to "no surface": SSR reads it as
					  // "no reflection", so sky, grid and text never reflect.
					  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  // Traced indirect diffuse (7av). **The subset is what
					  // `layout(location = N)` counts, not the target** --
					  // which is what made the first attempt measure +0.00
					  // with the attachment declared, the six renderers swept
					  // and the probe face widened: location 3 had nothing
					  // behind it because this list stopped at three.
					  { indirectIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
				builder.SetClearColor(desc.ClearColor);

				// Declared here even though the PBR shader binds it through
				// the scene set rather than through the graph: the graph is
				// what moves the imported target into a readable layout
				// before this pass, and it can only do that for a read it
				// knows about.
				if (previousReflections != kRGInvalid)
					builder.Sample(previousReflections);
				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
			},
			[draw = desc.DrawScene, jitter, reflectionsForScene, indirectForScene,
			 motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)
			{
				// Last frame's reflection trace, for the lighting. Set and
				// cleared on the same edges as the two below and for the
				// same reason: a probe face or a shadow caster reaching this
				// would light itself from a trace made for another camera.
				Renderer::SetScreenReflections(&reflectionsForScene);
				// Last frame's indirect diffuse, on the same edges and for the
				// same reason (7av).
				Renderer::SetScreenIndirect(&indirectForScene);

				// Set here and cleared immediately after, so that the only
				// code able to see a non-zero jitter is code drawing the
				// scene. Reflection probe captures run *outside* the graph,
				// earlier in the frame, and a cube assembled from six
				// differently-offset faces would not close at the seams; a
				// shadow cascade is reused across frames and would shimmer
				// along every edge it casts.
				//
				// The scene pass is also where the particles' view-projection
				// is captured, even though they are drawn two passes later --
				// so they jitter with the geometry rather than sliding a
				// half-pixel against it.
				Renderer::SetJitter(jitter);

				// Set and cleared on the same edges, and for the same reason:
				// the only code that may difference a camera against last
				// frame's is the code drawing this chain's scene. A probe
				// capture or a shadow cascade reaching this would be a velocity
				// measured between two things that were never consecutive
				// frames of anything.
				//
				// Keyed on the history rather than on whether TAA is on this
				// frame: the chain's identity does not come and go with the
				// anti-aliasing mode, and keeping the record current means
				// switching *to* TAA starts from last frame rather than from
				// whenever it was last enabled.
				Renderer::SetCameraMotion(motion);

				if (draw)
					draw(context);

				Renderer::SetCameraMotion(nullptr);
				Renderer::SetJitter(Vec2(0.0f, 0.0f));
				Renderer::SetScreenReflections(nullptr);
			});

		// The overlay goes into the HDR target rather than over the finished
		// image, because it depth-tests against the scene it annotates. The
		// cost is that its colours go through the tone curve like everything
		// else, which shifts them slightly -- acceptable for a diagnostic, and
		// the alternative needs the depth buffer in a second pass.
		// Transparency goes in before the overlay, so a collider wireframe is
		// still drawn over the smoke it describes rather than under it.
		if (wantTransparent)
		{
			graph.AddPass("Transparent",
				[&](RGPassBuilder& builder)
				{
					// Accumulation starts at zero and revealage at one: what
					// survives is the product of everything that missed, so
					// "nothing has covered this pixel yet" is one, not zero.
					builder.WriteAttachments(sceneHDR,
						{ { 1, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { 2, Vec4(1.0f, 1.0f, 1.0f, 1.0f) } });

					// Clear these two, keep the depth the scene wrote --
					// which is the whole reason they share a target.
					builder.PreserveDepth();
				},
				[draw = desc.DrawTransparent](RGPassContext& context) { draw(context); });

			graph.AddPass("ResolveTransparent",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(sceneHDR, { { 0, desc.ClearColor } },
											 RGLoad::Preserve);
					builder.Sample(sceneHDR);
					// A fullscreen composite has nothing to test against, and
					// testing would reject it everywhere the scene is nearer
					// than the far plane -- which is everywhere.
					builder.DisableDepth();
				},
				[resolve = desc.ResolveTransparent, sceneHDR](RGPassContext& context)
				{
					resolve(context, context.Color(sceneHDR, 1), context.Color(sceneHDR, 2));
				});
		}

		if (desc.DrawOverlay)
		{
			graph.AddPass("Overlay",
				[&](RGPassBuilder& builder)
				{
					// Preserve: the scene is already in there. Velocity, the
					// surface description and the indirect bounce are bound
					// too, because the debug renderer's pipeline is built for
					// the scene target's shape and this is the scene target.
					builder.WriteAttachments(sceneHDR,
						{ { 0, desc.ClearColor },
						  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { indirectIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } },
						RGLoad::Preserve);
				},
				[draw = desc.DrawOverlay](RGPassContext& context) { draw(context); });
		}

		// --- SSAA resolve --------------------------------------------------------
		//
		// Before bloom and before tone mapping, both deliberately. Averaging is
		// only meaningful where the numbers add up, and after the tone curve
		// they no longer do; and bloom thresholding the *supersampled* image
		// would let a single bright subsample light a whole output pixel, which
		// is the firefly SSAA is supposed to remove.
		RGResource shaded = sceneHDR;
		if (supersample > 1)
		{
			RGTargetDesc resolvedDesc;
			resolvedDesc.Name = "SceneResolved";
			resolvedDesc.Color = Format::R16G16B16A16_SFLOAT;
			resolvedDesc.Depth = Format::Undefined;
			shaded = graph.CreateTarget(resolvedDesc);

			graph.AddPass("SSAA resolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(shaded);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, supersample](RGPassContext& context)
				{
					PostProcess::SsaaResolve(context.Cmd, context.Color(sceneHDR),
											 context.Width * supersample,
											 context.Height * supersample,
											 Format::R16G16B16A16_SFLOAT, supersample);
				});
		}

		// --- TAA resolve ---------------------------------------------------------
		//
		// The same slot as the SSAA resolve above, and mutually exclusive with
		// it: both are a mode of anti-aliasing that produces the shaded image
		// the rest of the chain consumes, and both belong before bloom and
		// tone mapping because averaging is only meaningful in linear light.
		//
		// Bloom reading the *accumulated* image rather than the jittered one
		// is not incidental. A threshold applied to a frame that is wobbling
		// by half a pixel flickers along every bright edge, and a glow that
		// shimmers is more obvious than the aliasing it was hiding.
		if (wantTemporal)
		{
			TemporalHistory& history = *desc.History;
			history.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,
							Format::R16G16B16A16_SFLOAT);

			// Null only if the device refused the allocation, which is a
			// bigger problem than anti-aliasing; the frame still renders.
			if (history.Current() && history.Previous())
			{
				// The target written this frame *is* next frame's history, so
				// the accumulated image is never copied anywhere -- the rest
				// of the chain reads the same image the next frame will
				// reproject.
				const RGResource current = graph.Import(history.Current(), "TemporalCurrent");
				const RGResource previous = graph.Import(history.Previous(), "TemporalPrevious");
				const RGResource source = shaded;
				const float feedback = desc.Render.TemporalFeedback;
				const bool hasHistory = history.HasHistory();

				graph.AddPass("TAA resolve",
					[&](RGPassBuilder& builder)
					{
						builder.Write(current);
						builder.Sample(source);
						builder.Sample(previous);
						builder.DisableDepth();
					},
					[source, previous, velocityIndex, feedback, hasHistory](RGPassContext& context)
					{
						PostProcess::TemporalResolve(
							context.Cmd,
							context.Color(source),
							context.Color(previous),
							// The velocity attachment of the scene target,
							// which is the same target `source` is when SSAA
							// is off -- and SSAA and TAA cannot both be on.
							context.Color(source, velocityIndex),
							context.Width, context.Height,
							Format::R16G16B16A16_SFLOAT, feedback, hasHistory);
					});

				shaded = current;

				// Swapped here rather than by the caller. A ping-pong that
				// somebody has to remember to advance is a ping-pong that
				// spends a session reading the target it is writing.
				history.Advance();
			}
		}

		// What SSR and SSAO both reconstruct view space from: the clip planes,
		// the projection's inverse diagonal, and the view rotation that
		// brings the scene's world normal into that reconstruction. One
		// value, so the two passes cannot disagree about it. ENGINE-NOTES 7ae.
		PostProcess::ViewReconstruction reconstruction;
		reconstruction.NearClip = desc.NearClip;
		reconstruction.FarClip = desc.FarClip;
		reconstruction.InvProjection0 = desc.InvProjection0;
		reconstruction.InvProjection1 = desc.InvProjection1;
		reconstruction.View = desc.View;
		// What the scene was drawn through, so a pass that reconstructs a
		// position from its depth can take it back out again (7bq).
		reconstruction.JitterX = jitter.x;
		reconstruction.JitterY = jitter.y;


		// --- SSAO --------------------------------------------------------------
		//
		// First after the resolve, before depth of field and motion blur:
		// occlusion is lighting, and it belongs on the sharp image the
		// temporal filter produced -- a corner's darkness should then defocus
		// and smear like darkness rather than being painted over the finished
		// frame. Depth from the scene target, same normalised-coordinate
		// arrangement as everything since 7z. ENGINE-NOTES 7ac.
		//
		// The ray-traced form (7ao) is the same chain with a different first
		// pass -- the taps cast as rays into the frame's structure -- and it
		// runs on the render setting alone: the profile's AmbientOcclusion is
		// then not consulted, its radius and intensity still are.
		// --- SSGI (9.12): one bounce, gathered off the screen ---------------
		//
		// The same four-pass shape as the occlusion chain below, and for the
		// same reasons: half resolution because a bounce is low frequency,
		// the separable depth-aware blur because a gather of twelve taps is
		// noisy, and the add on the linear HDR image before defocus and bloom
		// because indirect light is lighting. Blurs 2 and 3 are literally
		// SSAO's shader -- the packing is RGB and depth in alpha for exactly
		// that. Before the occlusion chain, so a corner that receives a bounce
		// also has that bounce darkened by its own occlusion rather than the
		// other way about. Not added at all when the traced form runs, and not
		// added at all when the profile's toggle is off. ENGINE-NOTES 7at.
		// ------------------------------------------------------------------
		// **The ray budget.** Three passes and a reduction, here because this is
		// after the scene pass -- depth, the surface buffer and the lit colour
		// all exist -- and before the tracing passes that spend what it
		// allocates.
		//
		// A screen-space importance map dividing a *fixed* budget, per ray type.
		// What it replaces is a single global scalar stepping between four
		// levels on frame time: that could not tell a crease from a flat wall,
		// and it stepped for the whole screen at once, visibly. Nothing here
		// reads frame time; only the distribution moves.
		// ------------------------------------------------------------------
		RGResource rayBudgetMap = kRGInvalid;
		bool hasRayBudget = false;
		uint32_t budgetTilesX = 0;
		uint32_t budgetTilesY = 0;

		if (desc.RayBudget && PostProcess::IsReady())
		{
			// Sixteen: one number per 256 pixels. Small enough that a wave never
			// straddles two allocations, large enough that the map is a few
			// thousand texels rather than a few million.
			constexpr uint32_t kTileSize = 16;
			budgetTilesX = Math::Max((desc.Width + kTileSize - 1) / kTileSize, 1u);
			budgetTilesY = Math::Max((desc.Height + kTileSize - 1) / kTileSize, 1u);

			TemporalHistory& budget = *desc.RayBudget;
			budget.Prepare(Renderer::GetDevice(), budgetTilesX, budgetTilesY,
				   Format::R16G16B16A16_SFLOAT, "RayBudget");

			if (budget.Current() && budget.Previous())
			{
				const bool hasHistory = budget.HasHistory();
				const RGResource previous =
					graph.Import(budget.Previous(), "RayBudgetPrevious");
				const RGResource current =
					graph.Import(budget.Current(), "RayBudgetCurrent");

				RGTargetDesc tileDesc;
				tileDesc.Name = "ImportanceTiles";
				tileDesc.Color = Format::R16G16B16A16_SFLOAT;
				tileDesc.Depth = Format::Undefined;
				tileDesc.Width = budgetTilesX;
				tileDesc.Height = budgetTilesY;
				const RGResource tiles = graph.CreateTarget(tileDesc);

				graph.AddPass("Budget importance",
					[&](RGPassBuilder& builder)
					{
						builder.Write(tiles);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[sceneHDR, normalIndex, velocityIndex,
					 tilesX = budgetTilesX, tilesY = budgetTilesY,
					 width = desc.Width, height = desc.Height,
					 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
					{
						PostProcess::ImportanceTiles(
							context.Cmd,
							context.Color(sceneHDR, normalIndex),
							context.Depth(sceneHDR),
							context.Color(sceneHDR, velocityIndex),
					tilesX, tilesY, kTileSize, width, height,
							nearZ, farZ, Format::R16G16B16A16_SFLOAT);
					});

				// **Halve until nothing is left, and what remains is the mean.** The
				// allocator divides by it, so this is the only place the whole screen
				// becomes one number -- and it enters as a divisor of a per-tile
				// weight rather than as a level every tile is set to, which is the
				// structural difference between this and the dial it replaces.
				RGResource reduced = tiles;
				uint32_t reduceX = budgetTilesX;
				uint32_t reduceY = budgetTilesY;
				while (reduceX > 1 || reduceY > 1)
				{
					reduceX = Math::Max(reduceX / 2, 1u);
					reduceY = Math::Max(reduceY / 2, 1u);

					RGTargetDesc stepDesc;
					stepDesc.Name = "BudgetReduce";
					stepDesc.Color = Format::R16G16B16A16_SFLOAT;
					stepDesc.Depth = Format::Undefined;
					stepDesc.Width = reduceX;
					stepDesc.Height = reduceY;
					const RGResource next = graph.CreateTarget(stepDesc);
					const RGResource from = reduced;

					graph.AddPass("Budget reduce",
						[&](RGPassBuilder& builder)
						{
							builder.Write(next);
							builder.Sample(from);
							builder.DisableDepth();
						},
						[from, reduceX, reduceY](RGPassContext& context)
						{
							PostProcess::TileReduce(context.Cmd, context.Color(from),
								reduceX, reduceY,
								Format::R16G16B16A16_SFLOAT);
						});

					reduced = next;
				}

				const RGResource mean = reduced;

				graph.AddPass("Budget allocate",
					[&](RGPassBuilder& builder)
					{
						builder.Write(current);
						builder.Sample(tiles);
						builder.Sample(mean);
						if (hasHistory)
							builder.Sample(previous);
						builder.DisableDepth();
					},
					[tiles, mean, previous, hasHistory,
					 tilesX = budgetTilesX, tilesY = budgetTilesY,
					 aoAverage = desc.Render.RayBudgetAoAverage,
					 giAverage = desc.Render.RayBudgetGiAverage,
					 spread = desc.Render.RayBudgetSpread](RGPassContext& context)
					{
						// One dial, and it is the honest one: the ratio between the
						// cheapest tile and the dearest. Floor and ceiling move together.
						const float maxFactor = Math::Max(spread, 1.0f);
						const float minFactor = 1.0f / maxFactor;

						PostProcess::TileBudget(
							context.Cmd, context.Color(tiles), context.Color(mean),
							hasHistory ? context.Color(previous) : nullptr,
							tilesX, tilesY, aoAverage, giAverage,
							minFactor, maxFactor,
							// One ray a frame. Any target is reached inside a fifth
							// of a second -- faster than the filters downstream
							// converge -- so nothing waits on this and nothing pops.
							1.0f,
							16.0f, 16.0f,
							Format::R16G16B16A16_SFLOAT);
					});

				rayBudgetMap = current;
				hasRayBudget = true;

				// **`budget.Advance()` belongs here and is deliberately absent.
				// Do not add it as the one-line fix it looks like.**
				//
				// What it would do is real: without it m_Valid never becomes
				// true, HasHistory() above is permanently false, the previous
				// map is imported and never sampled, TileBudget is handed a
				// null history, and tile_budget's whole +/-1 ray a frame block
				// is skipped. So per-tile counts are re-derived from scratch
				// every frame, undamped. On paper that is a bug and this is
				// its fix.
				//
				// **It has been enabled and reverted three times, on three
				// different versions of the surrounding code, and every time it
				// made the picture worse.** The last was 2026-08-29, after the
				// allocator's inputs, the GI resolution, the runtime cache and
				// the denoiser had all changed -- which was the argument for
				// trying again, and it still came back: flicker and jitter
				// lower than they once were, but returning. Frame time is
				// untouched either way (4.38 vs 4.44 ms, inside this machine's
				// drift), so there is nothing on the other side of the trade.
				//
				// The reason is structural rather than a tuning miss. A damped
				// integrator feeding a quantiser -- `floor(x + 0.5)` in
				// tile_budget -- is the classic shape for a slow limit cycle,
				// and this engine has already met it once, breathing at about a
				// hertz (importance_tiles.rvshader:46-52). Damping the input to
				// a stair does not stop the stair being climbed; it just makes
				// the climbing rhythmic.
				//
				// **What it needs before it can come back: a dead band on the
				// tile count**, so a tile near a rounding boundary holds its
				// current value instead of stepping whenever the estimate
				// crosses. Fix the shape, then restore the line.
			}
		}
		else if (desc.RayBudget)
		{
			desc.RayBudget->Invalidate();
		}
		if (wantIndirect && !rayGi && currentIndirect != kRGInvalid)
		{
			// ENGINE-NOTES 7az. High gathers at full resolution; the two below
			// it halve. The blur that follows is handed *these* dimensions, so
			// its radius narrows with them instead of smearing the extra
			// detail straight back off -- which is the whole of what makes the
			// dial worth having, and the one thing easy to get wrong.
			const bool giFull = desc.Post.GiQuality == GiDetail::High;
			const uint32_t giWidth = giFull ? desc.Width
										    : Math::Max(desc.Width / 2u, 1u);
			const uint32_t giHeight = giFull ? desc.Height
											 : Math::Max(desc.Height / 2u, 1u);
			// Twelve at Low, twenty-four above it. A push constant rather than
			// a shader define: a fork earns its place by removing work from the
			// inner loop (8.2), and a loop bound does not.
			const float giTaps = desc.Post.GiQuality == GiDetail::Low ? 12.0f : 24.0f;

			RGTargetDesc giDesc;
			giDesc.Name = "SsgiRaw";
			giDesc.Color = Format::R16G16B16A16_SFLOAT;
			giDesc.Depth = Format::Undefined;
			// The *targets* follow the quality dial, not only the numbers
			// handed to the shader (ENGINE-NOTES 7az). Changing one without
			// the other leaves the gather running at half resolution and
			// reading a texel size for a grid twice as fine -- which changes
			// the picture, costs nothing, and improves nothing.
			giDesc.Scale = giFull ? 1.0f : 0.5f;
			const RGResource giRaw = graph.CreateTarget(giDesc);

			RGTargetDesc giBlurDesc = giDesc;
			giBlurDesc.Name = "SsgiBlurX";
			const RGResource giBlurX = graph.CreateTarget(giBlurDesc);
			giBlurDesc.Name = "SsgiBlurred";
			const RGResource giBlurred = graph.CreateTarget(giBlurDesc);

			const RGResource giSource = shaded;
			const float giRadius = desc.Post.GiRadius;

			if (voxelGi)
			{
				// The voxel gather (ENGINE-NOTES 7bc): the same inputs the
				// screen gather takes, minus the lit image -- it reads the lit
				// grid instead -- and the same packing out, so the blur and
				// the denoise below do not know which ran.
				graph.AddPass("Voxel GI gather",
					[&](RGPassBuilder& builder)
					{
						builder.Write(giRaw);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[sceneHDR, normalIndex, giWidth, giHeight,
					 reconstruction](RGPassContext& context)
					{
						VoxelGI::Gather(context.Cmd, context.Depth(sceneHDR),
										context.Color(sceneHDR, normalIndex),
										giWidth, giHeight, reconstruction,
										Format::R16G16B16A16_SFLOAT);
					});
			}
			else
			{
			graph.AddPass("SSGI compute",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giRaw);
					builder.Sample(sceneHDR);
					builder.Sample(giSource);
					builder.DisableDepth();
				},
				[sceneHDR, giSource, normalIndex, indirectIndex, giWidth, giHeight,
				 reconstruction, giRadius, giTaps](RGPassContext& context)
				{
					PostProcess::SsgiCompute(context.Cmd, context.Depth(sceneHDR),
											 context.Color(sceneHDR, normalIndex),
											 context.Color(giSource),
											 context.Color(sceneHDR, indirectIndex),
											 giWidth, giHeight, reconstruction,
											 giRadius, giTaps, Format::R16G16B16A16_SFLOAT);
				});
			}

			graph.AddPass("SSGI blur x",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giBlurX);
					builder.Sample(giRaw);
					builder.DisableDepth();
				},
				[giRaw, giWidth, giHeight](RGPassContext& context)
				{
					PostProcess::SsgiBlur(context.Cmd, context.Color(giRaw),
										  giWidth, giHeight, 1.0f, 0.0f,
										  Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("SSGI blur y",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giBlurred);
					builder.Sample(giBlurX);
					builder.DisableDepth();
				},
				[giBlurX, giWidth, giHeight](RGPassContext& context)
				{
					PostProcess::SsgiBlur(context.Cmd, context.Color(giBlurX),
										  giWidth, giHeight, 0.0f, 1.0f,
										  Format::R16G16B16A16_SFLOAT);
				});

			// The resolve, not an apply: the gather lands in the frame's
			// Indirect buffer and the scene image is not touched. `shaded` is
			// deliberately NOT reassigned -- the bounce reaches the picture
			// through the lit shader next frame, multiplied by albedo.
			//
			// **And it accumulates, which it could not before** (ENGINE-NOTES
			// 7ay). 7av had to give the denoiser to the traced form alone: the
			// gather read the lit image, the lit image carried last frame's
			// indirect, and blending its own output back in compounded it to
			// +16.98 against a calibrated +1.71, with the backends 2.03 apart.
			// The gather now subtracts what the lit shader added, so its output
			// no longer depends on its own history and the two forms take the
			// same pass -- one name for it, because they are one thing again.
			//
			// The *gather's* dimensions, not the buffer's: `TexelSize` places
			// the neighbourhood taps on `u_Current`, and this chain runs at
			// half resolution. Passing the output size would put all nine taps
			// inside one source texel, which is a clamp box of nothing, which
			// is an accumulation that clips itself straight back to the frame
			// it was supposed to be smoothing.
			graph.AddPass("GI denoise",
				[&](RGPassBuilder& builder)
				{
					builder.Write(currentIndirect);
					builder.Sample(giBlurred);
					builder.Sample(sceneHDR);
					if (previousIndirect != kRGInvalid)
						builder.Sample(previousIndirect);
					builder.DisableDepth();
				},
				[giBlurred, sceneHDR, velocityIndex, previousIndirect,
				 giWidth, giHeight, feedback = giFeedback,
				 has = indirectHasHistory](RGPassContext& context)
				{
					PostProcess::GiDenoise(context.Cmd, context.Color(giBlurred),
										   previousIndirect != kRGInvalid
											   ? context.Color(previousIndirect) : nullptr,
										   context.Color(sceneHDR, velocityIndex),
										   giWidth, giHeight, feedback, has,
										   Format::R16G16B16A16_SFLOAT,
										   // Attachment 1: last frame's count
										   // and luminance moments.
										   previousIndirect != kRGInvalid
											   ? context.Color(previousIndirect, 1) : nullptr,
										   Format::R16G16B16A16_SFLOAT);
				});

			// Swapped once the pass is declared, for the reason the
			// reflections pair is: what was written this frame is what the
			// next frame reads.
			desc.Indirect->Advance();
		}
		else if (wantIndirect && rayGi && currentIndirect != kRGInvalid
				 && Renderer3D::CanTraceGlobalIllumination())
		{
			// **The traced form, in a pass of its own** (ENGINE-NOTES 7bs).
			// It used to be four rays inside the lit fragment, which is one
			// resolution -- the frame's -- because an attachment has one size.
			// Here it has a target, and a target has a scale.
			RGTargetDesc traceDesc;
			traceDesc.Name = "RtGiRaw";
			traceDesc.Color = Format::R16G16B16A16_SFLOAT;
			traceDesc.Depth = Format::Undefined;
			// **The dial, which is the whole reason this became a pass.** The
			// target follows it and so do the dimensions handed to the denoise
			// below, because setting one without the other leaves a pass
			// running at one resolution and reading a texel size for another.
			const uint32_t giDivisor = RayDetailDivisor(giDetail);
			traceDesc.Scale = 1.0f / (float)giDivisor;
			const uint32_t giTraceWidth = Math::Max(desc.Width / giDivisor, 1u);
			const uint32_t giTraceHeight = Math::Max(desc.Height / giDivisor, 1u);
			const RGResource giTraced = graph.CreateTarget(traceDesc);

			Renderer3D::GiTraceView traceView;
			traceView.NearClip = desc.NearClip;
			traceView.FarClip = desc.FarClip;
			traceView.InvProjection0 = desc.InvProjection0;
			traceView.InvProjection1 = desc.InvProjection1;
			traceView.View = desc.View;

			graph.AddPass("RT GI trace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giTraced);
					// Depth and the surface description, which is where the
					// position and the normal come back from.
					builder.Sample(sceneHDR);
					// Declared, not merely read -- the graph checks.
					if (rayBudgetMap != kRGInvalid)
						builder.Sample(rayBudgetMap);
					builder.DisableDepth();
				},
				// Scaled by the same budget, floored at one: a bounce pass
				// that casts no rays writes black and the field it feeds
				// darkens the whole scene, which is a worse failure than a
				// noisy bounce.
				[sceneHDR, normalIndex, traceView, budgetMap = rayBudgetMap,
				 rays = hasRayBudget ? RayDetailRays(giDetail)
										  : Math::Max((int)(RayDetailRays(giDetail)
											   * Renderer::GetRayScale() + 0.5f), 1)]
				(RGPassContext& context)
				{
					RayGpuScope rayTime(context.Cmd);
					Renderer3D::TraceGlobalIllumination(context.Cmd,
														context.Depth(sceneHDR),
														context.Color(sceneHDR, normalIndex),
														budgetMap != kRGInvalid
															? context.Color(budgetMap) : nullptr,
														Format::R16G16B16A16_SFLOAT,
														traceView, rays);
				});

			// Accumulated through the same pass the screen-space chain ends
			// on, because at this point the two carry the same quantity and,
			// since 7ay closed the gather's loop, neither carries its own
			// answer back into itself.
			graph.AddPass("GI denoise",
				[&](RGPassBuilder& builder)
				{
					builder.Write(currentIndirect);
					builder.Sample(giTraced);
					builder.Sample(sceneHDR);
					if (previousIndirect != kRGInvalid)
						builder.Sample(previousIndirect);
					builder.DisableDepth();
				},
				[giTraced, sceneHDR, previousIndirect, velocityIndex,
				 width = giTraceWidth, height = giTraceHeight,
				 feedback = giFeedback, has = indirectHasHistory](RGPassContext& context)
				{
					PostProcess::GiDenoise(context.Cmd,
										   context.Color(giTraced),
										   previousIndirect != kRGInvalid
											   ? context.Color(previousIndirect) : nullptr,
										   context.Color(sceneHDR, velocityIndex),
										   width, height, feedback, has,
										   Format::R16G16B16A16_SFLOAT,
										   // Attachment 1 of the same history:
										   // last frame's count and moments.
										   previousIndirect != kRGInvalid
											   ? context.Color(previousIndirect, 1) : nullptr,
										   Format::R16G16B16A16_SFLOAT);
				});

			desc.Indirect->Advance();
		}

		// **A field waiting to be solved**, filled here and not in the scene.
		//
		// Here means three things at once, and each of them rules out
		// somewhere else. It is **after the scene pass**, because the solve
		// traces and the set it traces through is built by BeginScene -- which
		// is why the scene walk can only ask for this and not do it. It is
		// **outside a render pass**, because it begins one of its own and
		// fences its writes with barriers, neither of which is legal inside
		// another; that is what the standalone kind is. And it is **once per
		// frame**, not once per viewport: the request clears when it is
		// solved, so the editor's second graph finds nothing to do.
		//
		// The field it writes is read by the lit pass of the *next* frame,
		// which is the one frame of latency this design accepts. A field is
		// solved when it is created or invalidated and held after that, so the
		// alternative -- solving before the scene pass, against the previous
		// frame's set -- would buy one frame of freshness for a scene that is
		// one frame stale. This way the rays see the scene as it is.
		if (Renderer3D::HasPendingIrradianceSolve())
		{
			graph.AddStandalonePass("Irradiance fill",
				[&](RGPassBuilder&)
				{
					// Nothing declared: what it reads is the scene's traced
					// structure and what it writes is a volume texture, and
					// the graph owns neither.
				},
				[](RGPassContext& context)
				{
					Renderer3D::SolvePendingIrradiance(context.Cmd);
				});
		}

		// The traced form answers where it runs, exactly as it always has --
		// what is new is that both forms say at what resolution. The blur that
		// follows is handed these dimensions, so its radius narrows with them
		// rather than smearing the extra detail back off, which is the rule
		// the GI dial already follows (7az).
		const AoDetail aoLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;
		if (aoLevel != AoDetail::Off && PostProcess::IsReady())
		{
			// **What the rung buys depends on which form is running.** The
			// traced one spends rays and always runs at the frame's own
			// resolution; the screen-space one has no rays to trade and
			// spends resolution, which is what this dial has always meant for
			// it. See AoDetail.
			uint32_t aoTaps = aoLevel == AoDetail::Full    ? 8u
							: aoLevel == AoDetail::Quarter ? 2u
														   : 4u;

			// **The budget is spent here first, because this is where the
			// variation is.** With the depth prepass in, ambient occlusion is
			// 58% of what remains of the showroom's close-versus-far swing --
			// 3.30 ms against 2.17 -- and it is the cheapest term to trade:
			// the count is already a runtime argument, and the result is
			// blurred by a 9x9 separable filter afterwards, so fewer samples
			// cost less than they would anywhere else in the frame.
			//
			// Only the traced form. The screen-space one spends resolution
			// rather than rays (see AoDetail), so scaling its tap count would
			// be changing a different quantity than the one under budget.
			// **Not when the allocator is running.** The budget is fixed by
			// design: a controller reacting to frame time is what stepped the
			// whole screen's quality at once, and running it underneath a
			// per-tile allocation is the same rays taken twice -- the second
			// time uniformly. Here the count is the ceiling the allocator works
			// below, and the average it works toward is a setting.
			if (rayOcclusion && !hasRayBudget)
			{
				const float scale = Renderer::GetRayScale();
				if (scale < 1.0f)
					aoTaps = Math::Max((uint32_t)(aoTaps * scale + 0.5f), 2u);
			}
			const uint32_t divisor = rayOcclusion                  ? 1u
								   : aoLevel == AoDetail::Full     ? 1u
								   : aoLevel == AoDetail::Quarter  ? 4u
																   : 2u;
			const float aoScale = 1.0f / (float)divisor;
			const uint32_t halfWidth = Math::Max(desc.Width / divisor, 1u);
			const uint32_t halfHeight = Math::Max(desc.Height / divisor, 1u);

			RGTargetDesc aoDesc;
			aoDesc.Name = "SsaoRaw";
			aoDesc.Color = Format::R16G16B16A16_SFLOAT;
			aoDesc.Depth = Format::Undefined;
			// The target follows the dial and not only the numbers handed to
			// the shader: changing one without the other leaves the pass
			// running at one resolution and reading a texel size for another.
			aoDesc.Scale = aoScale;
			const RGResource raw = graph.CreateTarget(aoDesc);

			RGTargetDesc blurredDesc = aoDesc;
			blurredDesc.Name = "SsaoBlurX";
			const RGResource blurredX = graph.CreateTarget(blurredDesc);

			blurredDesc.Name = "SsaoBlurred";
			const RGResource blurred = graph.CreateTarget(blurredDesc);

			RGTargetDesc shadedDesc;
			shadedDesc.Name = "SsaoApplied";
			shadedDesc.Color = Format::R16G16B16A16_SFLOAT;
			shadedDesc.Depth = Format::Undefined;
			const RGResource occluded = graph.CreateTarget(shadedDesc);

			// **Decided before the compute pass, because it changes the rays.**
			// Varying the spiral per frame is only right when something is
			// averaging the result; with no history it is noise that crawls.
			const bool aoAccumulating = desc.Occlusion && PostProcess::IsReady();
			// Wrapped so the float keeps its precision over a long session; the
			// spiral only needs successive frames to differ, not to be unique
			// forever.
			const float aoFrame = aoAccumulating
								? (float)(Renderer::GetFrameCount() % 64u) : 0.0f;

			const RGResource lit = shaded;
			const float radius = desc.Post.AoRadius;
			const float intensity = desc.Post.AoIntensity;

			graph.AddPass("SSAO compute",
				[&](RGPassBuilder& builder)
				{
					builder.Write(raw);
					builder.Sample(sceneHDR);
					// Declared, not merely read -- the graph checks.
					if (rayBudgetMap != kRGInvalid)
						builder.Sample(rayBudgetMap);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, halfWidth, halfHeight, reconstruction,
				 radius, rayOcclusion, aoTaps, aoFrame,
				 budgetMap = rayBudgetMap](RGPassContext& context)
				{
					// Depth and the surface attachment: the real normal where
					// the scene wrote one, reconstruction where it did not.
					// Timed for the ray budget, always -- see FrameProfiler's
					// ClaimRayGpuScope. Scoped so it closes before the pass does.
					if (rayOcclusion)
					{
						RayGpuScope rayTime(context.Cmd);
						PostProcess::RtaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 RayShadows::GetStructure(),
												 budgetMap != kRGInvalid
												 	 ? context.Color(budgetMap) : nullptr,
												 halfWidth, halfHeight, reconstruction,
												 radius, aoTaps, Format::R16G16B16A16_SFLOAT,
												 aoFrame);
					}
					else
					{
						PostProcess::SsaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 halfWidth, halfHeight, reconstruction,
												 radius, Format::R16G16B16A16_SFLOAT);
					}
				});

			// **Accumulate the occlusion across frames, before it is blurred.**
			//
			// RTAO was the one noisy term in the renderer with no temporal
			// filter of any kind: it is applied after the TAA resolve, so
			// nothing downstream averages it, and the separable blur below was
			// its whole defence. A blur trades detail for quietness at a fixed
			// rate; accumulation buys quietness with *time* and costs no detail
			// at all, which is why it goes first and the blur cleans up what is
			// left rather than doing the whole job.
			//
			// Its own history rather than TAA's, on the owner's argument: this
			// engine ships MSAA and FXAA as well, and a filter that lives
			// inside TAA only helps the people running TAA.
			//
			// Reuses the temporal resolve TAA uses -- reproject through the
			// velocity buffer, clamp to the neighbourhood, blend -- because
			// that is exactly the shape wanted and occlusion is a greyscale
			// signal it handles without special-casing.
			RGResource aoAccumulated = raw;
			bool aoHasHistory = false;
			if (desc.Occlusion && PostProcess::IsReady())
			{
				TemporalHistory& occlusion = *desc.Occlusion;
				occlusion.Prepare(Renderer::GetDevice(), halfWidth, halfHeight,
								  Format::R16G16B16A16_SFLOAT, "Occlusion");

				if (occlusion.Current() && occlusion.Previous())
				{
					aoHasHistory = occlusion.HasHistory();
					const RGResource previousAo =
						graph.Import(occlusion.Previous(), "OcclusionPrevious");
					const RGResource currentAo =
						graph.Import(occlusion.Current(), "OcclusionCurrent");

					graph.AddPass("SSAO accumulate",
						[&](RGPassBuilder& builder)
						{
							builder.Write(currentAo);
							builder.Sample(raw);
							builder.Sample(sceneHDR);
							if (aoHasHistory)
								builder.Sample(previousAo);
							builder.DisableDepth();
						},
						[raw, sceneHDR, previousAo, velocityIndex,
						 halfWidth, halfHeight, has = aoHasHistory](RGPassContext& context)
						{
							// The same feedback the indirect buffer uses.
							// Occlusion is low frequency and has no highlights,
							// so it can afford a long tail -- and it needs one,
							// because underneath is a handful of rays.
							PostProcess::TemporalResolve(
								context.Cmd, context.Color(raw),
								has ? context.Color(previousAo) : nullptr,
								context.Color(sceneHDR, velocityIndex),
								halfWidth, halfHeight,
								Format::R16G16B16A16_SFLOAT, 0.9f, has);
						});

					aoAccumulated = currentAo;
					desc.Occlusion->Advance();
				}
			}
			else if (desc.Occlusion)
			{
				desc.Occlusion->Invalidate();
			}

			graph.AddPass("SSAO blur x",
				[&](RGPassBuilder& builder)
				{
					builder.Write(blurredX);
					builder.Sample(aoAccumulated);
					builder.DisableDepth();
				},
				[aoAccumulated, halfWidth, halfHeight](RGPassContext& context)
				{
					PostProcess::SsaoBlur(context.Cmd, context.Color(aoAccumulated),
										  halfWidth, halfHeight, 1.0f, 0.0f,
										  Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("SSAO blur y",
				[&](RGPassBuilder& builder)
				{
					builder.Write(blurred);
					builder.Sample(blurredX);
					builder.DisableDepth();
				},
				[blurredX, halfWidth, halfHeight](RGPassContext& context)
				{
					PostProcess::SsaoBlur(context.Cmd, context.Color(blurredX),
										  halfWidth, halfHeight, 0.0f, 1.0f,
										  Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("SSAO apply",
				[&](RGPassBuilder& builder)
				{
					builder.Write(occluded);
					builder.Sample(lit);
					builder.Sample(blurred);
					builder.DisableDepth();
				},
				[lit, blurred, intensity](RGPassContext& context)
				{
					PostProcess::SsaoApply(context.Cmd, context.Color(lit),
										   context.Color(blurred), intensity,
										   Format::R16G16B16A16_SFLOAT);
				});

			shaded = occluded;
		}

		// --- fog ---------------------------------------------------------------
		//
		// **After occlusion and before depth of field**, which is the same
		// argument SSAO's apply pass makes one line above: fog is light, not a
		// filter over a finished picture. Put it here and a fogged headlamp
		// blooms as the fog's colour and defocuses with the air it is in; put
		// it after the bloom and tone map and it is grey paint over a finished
		// frame, which is what every fog that reads as a filter has done.
		//
		// Before SSR's trace as well, for the reason the trace's own comment
		// gives about occlusion: what a reflection ray finds should already
		// carry the haze of the distance it was found at.
		if (desc.Post.Fog && PostProcess::IsReady())
		{
			RGTargetDesc foggedDesc;
			foggedDesc.Name = "Fogged";
			foggedDesc.Color = Format::R16G16B16A16_SFLOAT;
			foggedDesc.Depth = Format::Undefined;
			const RGResource fogged = graph.CreateTarget(foggedDesc);

			FogSettings fogSettings;
			fogSettings.Enabled = true;
			fogSettings.Color = desc.Post.FogColor;
			fogSettings.Density = desc.Post.FogDensity;
			fogSettings.HeightFalloff = desc.Post.FogHeightFalloff;
			fogSettings.Height = desc.Post.FogHeight;
			fogSettings.StartDistance = desc.Post.FogStartDistance;
			fogSettings.MaxOpacity = desc.Post.FogMaxOpacity;

			PostProcess::FogView fogView;
			fogView.NearClip = desc.NearClip;
			fogView.FarClip = desc.FarClip;
			fogView.InvProjection0 = desc.InvProjection0;
			fogView.InvProjection1 = desc.InvProjection1;
			fogView.View = desc.View;

			const RGResource clear = shaded;

			graph.AddPass("Fog",
				[&](RGPassBuilder& builder)
				{
					builder.Write(fogged);
					builder.Sample(clear);
					// The scene's depth, which is where every pixel's distance
					// and height come from.
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[clear, sceneHDR, fog = fogSettings, fogView](RGPassContext& context)
				{
					PostProcess::Fog(context.Cmd, context.Color(clear),
									 context.Depth(sceneHDR), fog, fogView,
									 Format::R16G16B16A16_SFLOAT);
				});

			shaded = fogged;
		}

		// --- SSR: this frame's trace, for next frame's lighting -----------------
		//
		// After SSAO and before depth of field: the radiance a ray finds
		// should carry the occlusion of the corner it landed in, and should
		// not carry a defocus that belongs to the reflector's own depth, not
		// the reflected surface's. Trace at half resolution against this
		// frame's depth and surface, resolve at full into the other half of
		// the reflections pair -- which the *next* frame's scene pass reads
		// inside the lighting. Nothing here touches `shaded`: this is a side
		// chain whose output is a frame late by design. ENGINE-NOTES 7ad for
		// the march, 7af for why the blend is not here any more.
		if (wantReflections && currentReflections != kRGInvalid)
		{
			const uint32_t halfWidth = Math::Max(desc.Width / 2u, 1u);
			const uint32_t halfHeight = Math::Max(desc.Height / 2u, 1u);

			// The hi-Z atlas: a min-depth pyramid over the trace's pixels, one
			// target sized by hand because its shape is level 0 plus a
			// half-width column of the levels after it, which is not a
			// fraction of the frame. ENGINE-NOTES 7ag.
			uint32_t atlasWidth = 1, atlasHeight = 1;
			PostProcess::SsrHiZSize(halfWidth, halfHeight, atlasWidth, atlasHeight);

			RGTargetDesc hiZDesc;
			hiZDesc.Name = "SsrHiZFine";
			hiZDesc.Color = Format::R32G32_SFLOAT;
			hiZDesc.Depth = Format::Undefined;
			hiZDesc.Width = atlasWidth;
			hiZDesc.Height = atlasHeight;
			const RGResource hiZFine = graph.CreateTarget(hiZDesc);
			hiZDesc.Name = "SsrHiZCoarse";
			const RGResource hiZCoarse = graph.CreateTarget(hiZDesc);

			RGTargetDesc traceDesc;
			traceDesc.Name = "SsrTrace";
			traceDesc.Color = Format::R16G16B16A16_SFLOAT;
			traceDesc.Depth = Format::Undefined;
			traceDesc.Scale = 0.5f;
			const RGResource trace = graph.CreateTarget(traceDesc);

			PostProcess::SsrParams ssr;
			ssr.View = reconstruction;
			ssr.MaxDistance = desc.Post.SsrMaxDistance;
			ssr.Thickness = desc.Post.SsrThickness;

			const RGResource lit = shaded;
			const uint32_t width = desc.Width;
			const uint32_t height = desc.Height;
			const float nearClip = desc.NearClip;
			const float farClip = desc.FarClip;

			graph.AddPass("SSR hi-Z fine",
				[&](RGPassBuilder& builder)
				{
					builder.Write(hiZFine);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, halfWidth, halfHeight, nearClip, farClip](RGPassContext& context)
				{
					PostProcess::SsrHiZFine(context.Cmd, context.Depth(sceneHDR),
											halfWidth, halfHeight, nearClip, farClip,
											Format::R32G32_SFLOAT);
				});

			graph.AddPass("SSR hi-Z coarse",
				[&](RGPassBuilder& builder)
				{
					builder.Write(hiZCoarse);
					builder.Sample(hiZFine);
					builder.DisableDepth();
				},
				[hiZFine, halfWidth, halfHeight, farClip](RGPassContext& context)
				{
					PostProcess::SsrHiZCoarse(context.Cmd, context.Color(hiZFine),
											  halfWidth, halfHeight, farClip,
											  Format::R32G32_SFLOAT);
				});

			graph.AddPass("SSR trace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(trace);
					builder.Sample(hiZFine);
					builder.Sample(hiZCoarse);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[hiZFine, hiZCoarse, sceneHDR, normalIndex, halfWidth, halfHeight,
				 ssr](RGPassContext& context)
				{
					PostProcess::SsrTrace(context.Cmd, context.Color(hiZFine),
										  context.Color(hiZCoarse),
										  context.Color(sceneHDR, normalIndex),
										  halfWidth, halfHeight, ssr,
										  Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("SSR resolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(currentReflections);
					builder.Sample(lit);
					builder.Sample(trace);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[lit, trace, sceneHDR, normalIndex, width, height](RGPassContext& context)
				{
					PostProcess::SsrResolve(context.Cmd, context.Color(lit),
											context.Color(trace),
											context.Color(sceneHDR, normalIndex),
											width, height,
											Format::R16G16B16A16_SFLOAT);
				});

			// Swapped here rather than by the caller, for the reason the TAA
			// history is: what was written this frame is what the next frame
			// reads, and a ping-pong somebody has to remember to advance is
			// one that spends a session reading the target it is writing.
			desc.Reflections->Advance();
		}


		// --- depth of field ----------------------------------------------------
		//
		// **After the resolve and before bloom**, and both halves matter.
		//
		// After, because reprojecting a temporal filter over an already
		// defocused image asks its neighbourhood clamp to reconcile a blur
		// with a history blurred differently -- the motion vectors describe
		// where the *sharp* geometry went.
		//
		// Before, because a bright out-of-focus highlight should glow as the
		// disc it has become rather than as the point it was.
		//
		// Depth comes from the scene target, which after an SSAA resolve is a
		// different size than `shaded`. Sampled with normalised coordinates,
		// so the mismatch does not need handling. ENGINE-NOTES 7z.
		if (desc.Post.DepthOfField && PostProcess::IsReady())
		{
			const uint32_t halfWidth = Math::Max(desc.Width / 2u, 1u);
			const uint32_t halfHeight = Math::Max(desc.Height / 2u, 1u);

			RGTargetDesc cocDesc;
			cocDesc.Name = "DofCoC";
			cocDesc.Color = Format::R16G16B16A16_SFLOAT;
			cocDesc.Depth = Format::Undefined;
			cocDesc.Scale = 0.5f;
			const RGResource coc = graph.CreateTarget(cocDesc);

			RGTargetDesc blurDesc = cocDesc;
			blurDesc.Name = "DofBlurred";
			const RGResource blurred = graph.CreateTarget(blurDesc);

			RGTargetDesc focusedDesc;
			focusedDesc.Name = "DofComposited";
			focusedDesc.Color = Format::R16G16B16A16_SFLOAT;
			focusedDesc.Depth = Format::Undefined;
			const RGResource focused = graph.CreateTarget(focusedDesc);

			PostProcess::FocusParams focus;
			focus.FocusDistance = desc.Post.FocusDistance;
			// Millimetres in the inspector, because that is how lenses are
			// sold; metres here, because that is what the scene is in.
			focus.FocalLength = desc.Post.FocalLength * 0.001f;
			focus.FNumber = desc.Post.Aperture;
			focus.MaxRadius = desc.Post.MaxBokehRadius;
			focus.NearClip = desc.NearClip;
			focus.FarClip = desc.FarClip;

			const RGResource sharp = shaded;
			const uint32_t frameHeight = desc.Height;

			graph.AddPass("DoF circle of confusion",
				[&](RGPassBuilder& builder)
				{
					builder.Write(coc);
					builder.Sample(sharp);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sharp, sceneHDR, focus, frameHeight](RGPassContext& context)
				{
					PostProcess::DofPrepass(context.Cmd, context.Color(sharp),
											context.Depth(sceneHDR), frameHeight,
											Format::R16G16B16A16_SFLOAT, focus);
				});

			const float maxRadius = desc.Post.MaxBokehRadius;
			graph.AddPass("DoF gather",
				[&](RGPassBuilder& builder)
				{
					builder.Write(blurred);
					builder.Sample(coc);
					builder.DisableDepth();
				},
				[coc, halfWidth, halfHeight, maxRadius](RGPassContext& context)
				{
					PostProcess::DofGather(context.Cmd, context.Color(coc),
										   halfWidth, halfHeight,
										   Format::R16G16B16A16_SFLOAT, maxRadius);
				});

			graph.AddPass("DoF composite",
				[&](RGPassBuilder& builder)
				{
					builder.Write(focused);
					builder.Sample(sharp);
					builder.Sample(blurred);
					builder.DisableDepth();
				},
				[sharp, blurred](RGPassContext& context)
				{
					PostProcess::DofComposite(context.Cmd, context.Color(sharp),
											  context.Color(blurred),
											  Format::R16G16B16A16_SFLOAT);
				});

			shaded = focused;
		}

		// --- motion blur -------------------------------------------------------
		//
		// After depth of field -- the defocused disc smearing along the motion
		// is nearer the truth than a smear being defocused -- and before bloom,
		// so a bright streak glows as the streak it became. Velocity and depth
		// both come from the scene target, a different size under SSAA and
		// jittered under TAA; normalised coordinates handle the first and the
		// half-float velocity stores the second as zero. ENGINE-NOTES 7ab.
		if (desc.Post.MotionBlur && PostProcess::IsReady())
		{
			const float tileSize = Math::Clamp(desc.Post.MotionBlurMaxRadius, 4.0f, 64.0f);
			// Rounded *up*: a floor would leave the last partial row and
			// column of the frame outside every tile, and a fast object
			// there would not smear at all.
			const uint32_t tilesX = Math::Max((desc.Width + (uint32_t)tileSize - 1u)
											  / (uint32_t)tileSize, 1u);
			const uint32_t tilesY = Math::Max((desc.Height + (uint32_t)tileSize - 1u)
											  / (uint32_t)tileSize, 1u);

			RGTargetDesc packDesc;
			packDesc.Name = "MotionPack";
			packDesc.Color = Format::R16G16B16A16_SFLOAT;
			packDesc.Depth = Format::Undefined;
			const RGResource packed = graph.CreateTarget(packDesc);

			RGTargetDesc tileDesc;
			tileDesc.Name = "MotionTileMax";
			tileDesc.Color = Format::R16G16B16A16_SFLOAT;
			tileDesc.Depth = Format::Undefined;
			tileDesc.Width = tilesX;
			tileDesc.Height = tilesY;
			const RGResource tiles = graph.CreateTarget(tileDesc);

			RGTargetDesc dilatedDesc = tileDesc;
			dilatedDesc.Name = "MotionNeighborMax";
			const RGResource dilated = graph.CreateTarget(dilatedDesc);

			RGTargetDesc blurredDesc;
			blurredDesc.Name = "MotionBlurred";
			blurredDesc.Color = Format::R16G16B16A16_SFLOAT;
			blurredDesc.Depth = Format::Undefined;
			const RGResource smeared = graph.CreateTarget(blurredDesc);

			const RGResource sharp = shaded;
			const uint32_t width = desc.Width;
			const uint32_t height = desc.Height;
			const float nearClip = desc.NearClip;
			const float farClip = desc.FarClip;
			const float shutter = desc.Post.MotionBlurShutter;
			const float maxRadius = desc.Post.MotionBlurMaxRadius;

			graph.AddPass("Motion pack",
				[&](RGPassBuilder& builder)
				{
					builder.Write(packed);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, velocityIndex, width, height, nearClip, farClip](RGPassContext& context)
				{
					PostProcess::MotionBlurPack(context.Cmd,
												context.Color(sceneHDR, velocityIndex),
												context.Depth(sceneHDR),
												width, height, nearClip, farClip,
												Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("Motion tile max",
				[&](RGPassBuilder& builder)
				{
					builder.Write(tiles);
					builder.Sample(packed);
					builder.DisableDepth();
				},
				[packed, width, height, tileSize](RGPassContext& context)
				{
					PostProcess::MotionBlurTileMax(context.Cmd, context.Color(packed),
												   width, height, tileSize,
												   Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("Motion neighbor max",
				[&](RGPassBuilder& builder)
				{
					builder.Write(dilated);
					builder.Sample(tiles);
					builder.DisableDepth();
				},
				[tiles, tilesX, tilesY](RGPassContext& context)
				{
					PostProcess::MotionBlurNeighborMax(context.Cmd, context.Color(tiles),
													   tilesX, tilesY,
													   Format::R16G16B16A16_SFLOAT);
				});

			graph.AddPass("Motion gather",
				[&](RGPassBuilder& builder)
				{
					builder.Write(smeared);
					builder.Sample(sharp);
					builder.Sample(packed);
					builder.Sample(dilated);
					builder.DisableDepth();
				},
				[sharp, packed, dilated, width, height, shutter, maxRadius](RGPassContext& context)
				{
					PostProcess::MotionBlurGather(context.Cmd, context.Color(sharp),
												  context.Color(packed), context.Color(dilated),
												  width, height, shutter, maxRadius,
												  Format::R16G16B16A16_SFLOAT);
				});

			shaded = smeared;
		}

		// --- bloom -------------------------------------------------------------
		RGResource bloom = kRGInvalid;
		const bool wantBloom = desc.Post.BloomEnabled && PostProcess::IsReady();

		std::vector<RGResource> levels;
		if (wantBloom)
		{
			for (int i = 0; i < kBloomLevels; i++)
			{
				const float scale = 1.0f / (float)(1 << (i + 1));
				if ((uint32_t)(desc.Width * scale) < kMinBloomSize ||
					(uint32_t)(desc.Height * scale) < kMinBloomSize)
					break;

				RGTargetDesc level;
				level.Name = "Bloom" + std::to_string(i);
				level.Color = Format::R16G16B16A16_SFLOAT;
				// No depth: nothing in the chain tests or writes it, and
				// attaching one would force every post pipeline to declare a
				// matching depth format.
				level.Depth = Format::Undefined;
				level.Scale = scale;

				levels.push_back(graph.CreateTarget(level));
			}
		}

		if (!levels.empty())
		{
			const PostSettings post = desc.Post;

			// Down: threshold into the first level, then halve repeatedly.
			graph.AddPass("Bloom prefilter",
				[&](RGPassBuilder& builder)
				{
					builder.Write(levels[0]);
					builder.Sample(shaded);
				},
				[shaded, post](RGPassContext& context)
				{
					PostProcess::Prefilter(context.Cmd, context.Color(shaded),
										   context.Width * 2, context.Height * 2,
										   Format::R16G16B16A16_SFLOAT,
										   post.BloomThreshold, post.BloomKnee,
										   post.BloomClamp);
				});

			for (size_t i = 1; i < levels.size(); i++)
			{
				const RGResource source = levels[i - 1];
				graph.AddPass(("Bloom down " + std::to_string(i)).c_str(),
					[&](RGPassBuilder& builder)
					{
						builder.Write(levels[i]);
						builder.Sample(source);
					},
					[source](RGPassContext& context)
					{
						PostProcess::Downsample(context.Cmd, context.Color(source),
												context.Width * 2, context.Height * 2,
												Format::R16G16B16A16_SFLOAT);
					});
			}

			// Up: blur each level onto the one above it, additively, so the
			// chain accumulates in place and needs no second set of targets.
			for (size_t i = levels.size() - 1; i > 0; i--)
			{
				const RGResource source = levels[i];
				const RGResource destination = levels[i - 1];

				graph.AddPass(("Bloom up " + std::to_string(i)).c_str(),
					[&](RGPassBuilder& builder)
					{
						builder.Write(destination, RGLoad::Preserve);
						builder.Sample(source);
					},
					[source](RGPassContext& context)
					{
						PostProcess::Upsample(context.Cmd, context.Color(source),
											  context.Width / 2, context.Height / 2,
											  Format::R16G16B16A16_SFLOAT, kUpsampleRadius);
					});
			}

			bloom = levels[0];
		}

		// --- tone mapping -------------------------------------------------------
		//
		// SSAA is absent from this test on purpose: its work is already done by
		// here, so tone mapping writes the output directly and the frame ends
		// one pass shorter than either morphological filter.
		const bool wantAA = aa == AntiAliasing::FXAA || aa == AntiAliasing::SMAA;

		// With a post filter on, tone mapping lands in an intermediate that the
		// filter then reads. Both filters work on perceived brightness, so they
		// have to run after the transfer function, not before.
		RGResource tonemapped = desc.Output;
		if (wantAA)
		{
			RGTargetDesc ldr;
			ldr.Name = "Tonemapped";
			ldr.Color = desc.OutputFormat;
			ldr.Depth = Format::Undefined;
			tonemapped = graph.CreateTarget(ldr);
		}

		// --- auto exposure, measured from the linear image ----------------------
		//
		// Before tone mapping, because that is the only place the numbers mean
		// anything: after the curve every scene is correctly exposed by
		// construction, so metering there would measure the curve.
		//
		// A *compute* pass, which is why RenderGraph has them -- the thing it
		// reads is a target the graph owns and pools, and a dispatch may not be
		// recorded inside a render pass. ENGINE-NOTES 7y.
		ExposureState* exposure = nullptr;
		if (desc.Post.AutoExposure && desc.Exposure && AutoExposure::IsReady())
		{
			exposure = desc.Exposure;

			// Allocated **here**, while the frame is being described, and not
			// inside the pass.
			//
			// Creating these buffers seeds them from the CPU, and a seed issued
			// from inside command-buffer recording is a staging copy with no
			// ordering against the dispatch that reads it. On OpenGL that
			// happened to be fine; on Vulkan the seed landed *after* the first
			// dispatch and overwrote the value it had just adopted, so the
			// exposure started from the buffer's initial state and crawled
			// toward the right answer over about four seconds. It was
			// reproducible, which is what made it look like a slow adaptation
			// rather than a race. ENGINE-NOTES 7y.
			desc.Exposure->Prepare(Renderer::GetDevice());

			const PostSettings post = desc.Post;
			const float delta = desc.DeltaSeconds;

			graph.AddComputePass("Auto exposure",
				[&](RGPassBuilder& builder)
				{
					builder.Sample(shaded);
				},
				[shaded, exposure, post, delta](RGPassContext& context)
				{
					const RHI::Ref<RHI::RHITexture> scene = context.Color(shaded);
					if (!scene)
						return;

					AutoExposure::Params params;
					params.MinLogLuminance = post.AutoExposureMinLog;
					params.MaxLogLuminance = post.AutoExposureMaxLog;
					params.LowPercentile = post.AutoExposureLowPercent;
					params.HighPercentile = post.AutoExposureHighPercent;
					params.MiddleGrey = post.AutoExposureKey;
					params.MinExposure = post.AutoExposureMin;
					params.MaxExposure = post.AutoExposureMax;
					params.SpeedUp = post.AutoExposureSpeed;
					params.SpeedDown = post.AutoExposureSpeed;
					params.DeltaSeconds = delta;

					AutoExposure::Dispatch(context.Cmd, scene,
										   scene->GetWidth(), scene->GetHeight(),
										   *exposure, params);
				});
		}
		else if (desc.Exposure)
		{
			// Off this frame, so what it adapted to is stale. Resuming from a
			// ten-second-old exposure when the feature is switched back on is
			// the mistake TemporalHistory::Invalidate exists to prevent, and it
			// is the same mistake here.
			desc.Exposure->Invalidate();
		}

		{
			const PostSettings post = desc.Post;
			const RGResource bloomSource = bloom;
			const Format format = desc.OutputFormat;

			// Resolved here rather than inside the pass, because a graph pass
			// is a lambda that runs later and the asset manager is not
			// something to reach into from execute time. Null when there is no
			// LUT, when the handle is unknown, or when the `.cube` would not
			// parse -- all of which grade nothing.
			const RHI::Ref<RHI::RHITexture> lut =
				Assets::Manager::GetColorLut(AssetHandle(post.ColorLut));
			const uint32_t lutSize = lut ? lut->GetWidth() : 0;

			graph.AddPass("Tonemap",
				[&](RGPassBuilder& builder)
				{
					builder.Write(tonemapped);
					builder.Sample(shaded);
					if (bloomSource != kRGInvalid)
						builder.Sample(bloomSource);
					builder.DisableDepth();
				},
				[shaded, bloomSource, post, format, lut, lutSize, exposure](RGPassContext& context)
				{
					PostProcess::LensParams lens;
					lens.Aberration = post.ChromaticAberration;
					lens.Vignette = post.VignetteIntensity;
					lens.VignetteSmoothness = post.VignetteSmoothness;
					lens.Grain = post.FilmGrain;
					lens.GrainSize = post.FilmGrainSize;

					// Null unless the compute pass above ran, and the tone
					// mapping pass then takes the profile's exposure whole.
					// With it, the manual value becomes exposure *compensation*
					// -- a multiplier on what the metering worked out, the same
					// control a camera has and for the same reason. ENGINE-NOTES 7y.
					lens.Exposure = exposure ? exposure->Exposure() : nullptr;

					PostProcess::Tonemap(context.Cmd, context.Color(shaded),
										 bloomSource != kRGInvalid ? context.Color(bloomSource)
																   : nullptr,
										 format, post.Exposure,
										 bloomSource != kRGInvalid ? post.BloomIntensity : 0.0f,
										 lut, lutSize, post.ColorLutStrength, lens);
				});
		}

		if (aa == AntiAliasing::FXAA)
		{
			const RGResource source = tonemapped;
			const Format format = desc.OutputFormat;

			graph.AddPass("FXAA",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output);
					builder.Sample(source);
					builder.DisableDepth();
				},
				[source, format](RGPassContext& context)
				{
					// The thresholds are the reference implementation's
					// defaults: 1/16 of a full-range step is where an edge
					// stops being worth touching, and 1/8 keeps near-black
					// regions -- where contrast is tiny but banding is most
					// visible -- from being smeared.
					PostProcess::FXAA(context.Cmd, context.Color(source),
									  context.Width, context.Height, format,
									  0.0625f, 0.125f);
				});
		}
		else if (aa == AntiAliasing::SMAA)
		{
			const RGResource source = tonemapped;
			const Format format = desc.OutputFormat;

			// Two intermediates, both full resolution and both small: two
			// bytes a pixel for the edge flags and four for the weights. They
			// are 8-bit because what they hold is a classification and a
			// coverage fraction that never leaves [0, 1/2] -- and because the
			// pass that reads them is bandwidth bound, not precision bound.
			RGTargetDesc edgesDesc;
			edgesDesc.Name = "SMAAEdges";
			edgesDesc.Color = Format::R8G8_UNORM;
			edgesDesc.Depth = Format::Undefined;
			const RGResource edges = graph.CreateTarget(edgesDesc);

			RGTargetDesc weightsDesc;
			weightsDesc.Name = "SMAAWeights";
			weightsDesc.Color = Format::R8G8B8A8_UNORM;
			weightsDesc.Depth = Format::Undefined;
			const RGResource weights = graph.CreateTarget(weightsDesc);

			// Cleared to zero, and the edge pass *relies* on it: it discards
			// rather than writing where there is no edge, so the clear value
			// is what the majority of the frame ends up holding. A clear of
			// anything else would read as an edge everywhere flat.
			const Vec4 empty(0.0f, 0.0f, 0.0f, 0.0f);

			graph.AddPass("SMAA edges",
				[&](RGPassBuilder& builder)
				{
					builder.Write(edges);
					builder.SetClearColor(empty);
					builder.Sample(source);
					builder.DisableDepth();
				},
				[source](RGPassContext& context)
				{
					// 0.1 of full-range luma is the reference's default for
					// its quality preset, and 2.0 is its local contrast
					// factor: an edge survives only if it is at least half
					// the strongest edge beside it, which is what keeps a
					// soft gradient next to a hard silhouette from being
					// treated as one.
					PostProcess::SmaaEdges(context.Cmd, context.Color(source),
										   context.Width, context.Height,
										   Format::R8G8_UNORM, 0.1f, 2.0f);
				});

			graph.AddPass("SMAA weights",
				[&](RGPassBuilder& builder)
				{
					builder.Write(weights);
					builder.SetClearColor(empty);
					builder.Sample(edges);
					builder.DisableDepth();
				},
				[edges](RGPassContext& context)
				{
					PostProcess::SmaaWeights(context.Cmd, context.Color(edges),
											 context.Width, context.Height,
											 Format::R8G8B8A8_UNORM);
				});

			graph.AddPass("SMAA blend",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output);
					builder.Sample(source);
					builder.Sample(weights);
					builder.DisableDepth();
				},
				[source, weights, format](RGPassContext& context)
				{
					PostProcess::SmaaBlend(context.Cmd, context.Color(source),
										   context.Color(weights),
										   context.Width, context.Height, format);
				});
		}

		// --- the UI, last ------------------------------------------------------
		//
		// After everything, into the finished image. Preserve, obviously -- the
		// frame is already in there.
		//
		// The pass declares no depth: UI layering is the order quads were
		// submitted, which is the order somebody authored, and a depth buffer
		// would make it depend on numbers nobody set.
		if (desc.DrawUI)
		{
			// The UI pipeline is built against the *output* format, not the
			// scene's HDR one. Renderer::SetTargetFormats speaks for the scene
			// and would give this the wrong answer.
			UIRenderer::SetTargetFormats(desc.OutputFormat, Format::Undefined);

			graph.AddPass("UI",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output, RGLoad::Preserve);
					builder.DisableDepth();
				},
				[draw = desc.DrawUI](RGPassContext& context) { draw(context); });
		}
	}
}
