#include <rvpch.h>
#include "FrameGraphBuilder.h"
#include "LightGlow.h"
#include "PostProcess.h"
#include "TextureLoader.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Core/FrameProfiler.h"
#include "RayShadows.h"
#include "RayCounters.h"
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
		//
		// **Sixteen since WR-16 S5** (2026-09-05). A degree is ample to point a
		// reflection ray and not ample to *test* one: S5 reconstructs a
		// half-resolution trace by rejecting neighbours whose normals disagree,
		// and at eight bits the quantisation is the same size as the angle the
		// test is trying to measure -- so a flat surface reads as a spread of
		// distinct normals and the reconstruction rejects its own neighbours.
		// The design names this as S5's prerequisite and notes the history's
		// normal test wants it too. Half floats rather than sixteen-bit unorm
		// because the octahedral encode already lands in [0, 1] and every other
		// target in this chain is SFLOAT; the cost is four more bytes a pixel
		// on a buffer that RTAO, the bounce, the reflections, the importance
		// pass and the water's four passes all read, so the frame-time A/B is
		// part of S5's acceptance rather than an afterthought.
		constexpr Format kNormalFormat = Format::R16G16B16A16_SFLOAT;
		// Traced indirect diffuse (7av): irradiance, unbounded and positive,
		// so half floats rather than the normal's eight bits. Named here
		// because five places have to agree about it -- the target, the six
		// renderers, the UI world layer, the probe face and the resolve.
		constexpr Format kIndirectFormat = Format::R16G16B16A16_SFLOAT;
		// The G-buffer's albedo + metallic and the surface id (RT-first step
		// 1a). Albedo in eight bits linear for now (sRGB storage once the
		// attachment path is checked); the id a float, exact to 2^24, so the
		// graph's Vec4 clear serves it.
		constexpr Format kAlbedoFormat = Format::R8G8B8A8_UNORM;
		// r: the object's id, negative for a Static surface; g: the roughness
		// the lit shader shades analytic lights with (specular-antialiased,
		// RT-first T5) -- o_Surface keeps the raw one for the traced lobes.
		constexpr Format kSurfaceIdFormat = Format::R32G32_SFLOAT;

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

		// **`--debug-view` (WR-16 S0)**, decided once: the composite runs
		// where the counters exist (the ray-query path), and the per-pixel
		// count buffer is sized and zeroed only for the two modes that read
		// it. Renderer3D decided whether the lit shaders write it when they
		// were compiled; DebugCountsBuffer is null otherwise.
		const bool debugView = config.DebugView != EngineConfig::DebugViewMode::None
							&& RayCounters::IsAvailable();
		// **The level's own numbers**, resolved once. `--rt-optimisation`
		// overrides the project for a run, exactly as it does in Renderer3D.
		const RayOptimisationPreset rtPreset = RayOptimisationPresetFor(
			config.HasRayOptimisationOverride
				? (RayOptimisation)config.RayOptimisationOverride
				: desc.Render.RtOptimisation);
		// The lamp count the water passes are gated on: the level's, unless a
		// measurement run asked for another.
		const int rtLamps = config.HasRaysPerPixelOverride
							   ? config.RaysPerPixel : rtPreset.RaysPerPixel;

		const bool debugCounts = debugView
							  && (config.DebugView == EngineConfig::DebugViewMode::Rays
								  || config.DebugView == EngineConfig::DebugViewMode::Lights);

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
		// **And under TAA when the command line asks** (`--msaa=N`): coverage
		// samples answer the one thing a temporal resolve cannot -- geometry
		// thinner than a pixel is present in every frame's resolved image
		// instead of only in the frames the jitter lands on it -- and the
		// resolve then converges on shading. A measurement flag first; a
		// project setting is the owner's call.
		if (aa == AntiAliasing::MSAA
			|| (aa == AntiAliasing::TAA && config.MsaaOverride > 0))
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
								   Format::R16G16_SFLOAT, kNormalFormat, kIndirectFormat,
								   kAlbedoFormat, kSurfaceIdFormat);
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
		// **RT-6.9: a camera cut throws every temporal history away.**
		//
		// Reprojecting across a teleport, a scene load or a cut between two
		// viewpoints is meaningless: the velocity buffer describes a motion that
		// never happened, and what comes out is a whole frame of smear that then
		// takes thirty frames to fade. It is the one temporal failure with no
		// gradual version, so it gets the one blunt response.
		//
		// A speed rather than a distance: a dolly and a jump cover the same
		// ground given enough frames, and it is the *rate* that makes a
		// reprojection nonsense. A hundred metres a second is 360 km/h, past
		// anything a camera travels here; forty-five degrees in one frame is
		// 2700 degrees a second at sixty.
		//
		// The exposure is deliberately left alone -- a cut into a brighter room
		// should still adapt rather than snap, and how fast is a look decision.
		if (desc.History && desc.DeltaSeconds > 1.0e-5f)
		{
			const CameraMotion& was = desc.History->Motion();
			if (was.Eye.w > 0.5f && desc.History->HasHistory())
			{
				const Mat4 toWorld = Math::Inverse(desc.View);
				const Vec3 eyeNow(toWorld[3][0], toWorld[3][1], toWorld[3][2]);
				const Vec3 facingNow = Math::Normalize(
					Vec3(-toWorld[2][0], -toWorld[2][1], -toWorld[2][2]));
				const Vec3 eyeWas(was.Eye.x, was.Eye.y, was.Eye.z);
				const Vec3 facingWas = Math::Normalize(
					Vec3(was.Forward.x, was.Forward.y, was.Forward.z));
				constexpr float kCutMetresPerSecond = 100.0f;
				constexpr float kCutFacing = 0.7071f;   // forty-five degrees
				const float speed = Math::Length(eyeNow - eyeWas) / desc.DeltaSeconds;
				const float turned = Math::Dot(facingNow, facingWas);
				if (speed > kCutMetresPerSecond || turned < kCutFacing)
				{
					RV_CORE_TRACE("Camera cut: {0:.1f} m/s, facing dot {1:.3f}; "
								  "temporal histories dropped", speed, turned);
					desc.History->Invalidate();
					if (desc.Reflections)  desc.Reflections->Invalidate();
					if (desc.Indirect)     desc.Indirect->Invalidate();
					if (desc.TaaGuide)     desc.TaaGuide->Invalidate();
					if (desc.DirectLight)  desc.DirectLight->Invalidate();
					if (desc.GiLight)      desc.GiLight->Invalidate();
					if (desc.Occlusion)    desc.Occlusion->Invalidate();
					if (desc.RayBudget)    desc.RayBudget->Invalidate();
				}
			}
		}

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
		// The G-buffer's two (RT-first step 1a).
		const uint32_t albedoIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kAlbedoFormat);
		const uint32_t surfaceIdIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kSurfaceIdFormat);

		// **The sea's surface** (WR-16 S4b): the octahedral normal, the
		// roughness the footprint block chose and the wind angle in one
		// attachment; the colour and the specular dial in the other. Water is
		// drawn as a transparent surface, so it writes neither the normal
		// attachment above nor a velocity, and the pass that chooses and
		// shades its lamps in one place would otherwise have nothing to read.
		//
		// **Appended last, and only where the transparent pass exists**, which
		// is the only place water is drawn. Appending keeps every index above
		// exactly what it was -- the lesson the indirect attachment's own
		// comment records -- and no pipeline but the surface pass's names
		// them, so no other renderer has to learn a new count.
		// **A target of its own, and the depth is the whole reason** (measured
		// 2026-09-04). These three began as attachments appended to the scene
		// target, which made them share its depth -- tested against the opaque
		// scene and never written, because writing there would make the water
		// pass's own fragments fail their test afterwards. But a sea seen
		// nearly edge on covers itself: at 400 m on the Glitter camera three
		// to five water fragments land on one pixel, all of them in front of
		// the opaque scene and so all of them passing. With no depth write and
		// no blending the survivor is whichever was rasterised last, not the
		// nearest -- a probed pixel there stood 29 m from the water the draw
		// actually shows, with different lamps visible, and the brightest band
		// of the sea came out at a tenth of its brightness.
		//
		// With a depth of their own, cleared and written, the nearest fragment
		// wins. What that depth cannot do is reject water behind the pier, so
		// the surface shader does that itself, against the backdrop's own view
		// depth -- which this pass already samples for the refraction.
		const bool wantWaterSurface = wantTransparent && desc.DrawWaterSurface;

		const RGResource sceneHDR = graph.CreateTarget(sceneDesc);

		RGResource waterSurface = kRGInvalid;
		if (wantWaterSurface)
		{
			RGTargetDesc surfaceDesc;
			surfaceDesc.Name = "WaterSurface";
			// The normal's two horizontal components with the roughness and
			// the wind angle; the colour with the specular dial; and the world
			// position in full floats, because the sea is a kilometre wide
			// here and a half's step at that distance is half a metre.
			surfaceDesc.Color = Format::R32G32B32A32_SFLOAT;
			// **RT-8: and the wave's own motion.** The vertex stage has always
			// evaluated the wave at last frame's time as well as this one's and
			// built a previous clip position from it -- and until now there was
			// no attachment to write the difference into, so every temporal
			// filter read the sea as standing still and the long still feedback
			// had to be turned off for it. xy the screen motion in the same
			// units the scene's velocity lane uses, z the mask again so a
			// reader needs one fetch rather than two, w spare. Half floats: a
			// motion vector is a fraction of the screen and a half resolves a
			// thousandth of it.
			surfaceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT,
										Format::R32G32B32A32_SFLOAT,
										Format::R16G16B16A16_SFLOAT };
			surfaceDesc.Depth = Format::D32_SFLOAT;
			surfaceDesc.Scale = sceneDesc.Scale;
			// **One sample, whatever the scene uses.** This target holds a
			// description of the sea, not a picture of it, and everything that
			// reads it is a fullscreen pass working one sample to a pixel. A
			// multisampled copy would cost a 1440p D32 and three float
			// attachments times the sample count, resolve them all, and hand
			// the readers back exactly what one sample would have given:
			// measured at 7 ms of the Glitter frame, a fifth of it.
			surfaceDesc.Samples = 1;
			waterSurface = graph.CreateTarget(surfaceDesc);
		}

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
			Renderer::SetMirrorRays((int)rtPreset.MirrorRays);
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
		Renderer::SetReflectionFloor(desc.Post.ReflectionFloor);

		// The lights' glow (WR-5): the profile's dials, handed over once per
		// frame the way the reflection floor is.
		{
			LightGlowSettings glow;
			glow.Enabled = desc.Post.LightGlow;
			glow.GlowPixels = desc.Post.LightGlowPixels;
			glow.Intensity = desc.Post.LightGlowIntensity;
			glow.FlareShare = desc.Post.LightFlare;
			glow.FlarePixels = desc.Post.LightFlareSize;
			glow.FlareRays = desc.Post.LightFlareRays;
			LightGlow::SetSettings(glow);
		}
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

		// The pair serves two producers that never run together: the screen-space
		// trace, or the traced glossy pass and its accumulator (below the
		// allocator). Either way the lit shader reads last frame's through the
		// same hook; the intensity is the share of it to trust per frame behind
		// the value for the traced form, whose alpha counts frames.
		const bool tracedReflections = rayReflections && desc.Reflections != nullptr
									  && !(config.HasReflectionPassOverride
										   && !config.ReflectionPassOverride);
		if (wantReflections || tracedReflections)
		{
			TemporalHistory& reflections = *desc.Reflections;
			// The traced form keeps a second attachment beside the picture:
			// the reflector under each texel, which the accumulator tests a
			// history against before averaging with it. The screen-space
			// trace has no such test and keeps the one it had.
			// RT-6.1: the fourth lane is the virtual image's screen motion, which
			// the composite hands to the temporal resolve so a reflection can be
			// reprojected by its own movement rather than by the floor's.
			reflections.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,
								Format::R16G16B16A16_SFLOAT, "ScreenReflections",
								tracedReflections ? Format::R16G16B16A16_SFLOAT
												  : Format::Undefined,
								tracedReflections ? Format::R16G16B16A16_SFLOAT
												  : Format::Undefined,
								tracedReflections ? Format::R16G16B16A16_SFLOAT
												  : Format::Undefined,
								// RT-6.5: the object id under each texel, so the next frame's
								// history test can ask whether it is the same object -- the
								// one thing position, facing and material cannot answer,
								// because the plane test's tolerance is a quarter of a metre
								// at twenty.
								tracedReflections ? Format::R16G16B16A16_SFLOAT
												  : Format::Undefined);

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
					reflectionsForScene.Surface = tracedReflections
												 ? reflections.Previous()->GetColorTexture(1)
												 : nullptr;
					reflectionsForScene.Intensity = tracedReflections
												   ? 1.0f / 4.0f
												   : Math::Max(desc.Post.SsrIntensity, 0.0f);
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

		// RT-3: the traced bounce as a signal of *this* frame -- traced from the
		// G-buffer between it and the lit pass, upsampled to the lit pass's
		// resolution and settled on the same contract the direct light and the
		// occlusion take. The traced form only: the screen-space gather reads the
		// lit image, so it cannot run before the pass that makes it and stays on
		// the one-frame-late buffer of 7av. `--gi-signal=off` is the reference
		// arm and puts the traced form back on that buffer too.
		//
		// Resolved *here*, above the block that fills the intensity, and not
		// beside RT-2's gate further down: what that block decides is whether
		// the lit shader believes there is a bounce at all, and under the
		// signal the answer cannot come from the old buffer's history.
		const bool giSignal = Renderer3D::GBufferPassAvailable() && rayGi && wantIndirect
						   && config.GiSignal && PostProcess::IsReady()
						   && desc.GiLight != nullptr
						   && Renderer3D::CanTraceGlobalIllumination();

		Renderer::ScreenIndirect indirectForScene;
		RGResource previousIndirect = kRGInvalid;
		RGResource currentIndirect = kRGInvalid;
		bool indirectHasHistory = false;
		// How much of last frame's indirect survives into this one (7av).
		// Far higher than TAA's 0.6: irradiance is low frequency, and the
		// estimate underneath it is four rays wide.
		const float giFeedback = Math::Clamp(desc.Post.GiDenoise, 0.0f, 0.98f);

		// **RT-3: and the intensity is live at once, with no history behind
		// it.** Indirect.x is what the lit shader reads as "there is a bounce
		// to add, at this strength"; the block below sets it only when the
		// one-frame-late pair has a frame in it, which is right for a buffer
		// written last frame and wrong for a signal computed for this one.
		// The texture stays null on purpose -- binding 16 is overwritten with
		// the settled signal in DrawLit, and what is bound before that is the
		// 1x1 transparent black every other set gets.
		if (giSignal)
			indirectForScene.Intensity = Math::Max(desc.Post.GiIntensity, 0.0f);

		if (wantIndirect && !giSignal)
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
			// Invalidated under the signal too: the one-frame-late chain is not
			// running, and a history left standing would be resumed as truth the
			// frame --gi-signal=off puts it back in service.
			desc.Indirect->Invalidate();
		}

		// --- water foam --------------------------------------------------------
		//
		// Before the scene pass, because a compute dispatch may not sit inside
		// a render pass and the transparent pass reads what this steps. The
		// callback guards its own once-per-frame, so the editor's second view
		// re-runs nothing.
		if (desc.UpdateWater)
		{
			graph.AddComputePass("WaterFoam",
				[](RGPassBuilder&) {},
				[update = desc.UpdateWater](RGPassContext& context) { update(context); });
		}

		// **The debug view's per-pixel counts, sized and zeroed before the
		// scene draws** (WR-16 S0). The lit shaders add into the buffer
		// under RV_DEBUG_VIEW; a transfer zeroes its two planes every frame,
		// past the header, and the barrier orders the zero before the first
		// atomic. Outside any render pass, which is what a standalone pass
		// is for.
		if (debugCounts)
		{
			Renderer3D::EnsureDebugCounts(desc.Width * (uint32_t)supersample,
										  desc.Height * (uint32_t)supersample);
			if (const Ref<RHIBuffer> counts = Renderer3D::DebugCountsBuffer())
			{
				graph.AddStandalonePass("Debug counts clear",
					[&](RGPassBuilder&) {},
					[counts](RGPassContext& context)
					{
						context.Cmd.FillBuffer(counts, Renderer3D::kDebugCountsHeaderBytes,
											   counts->GetSize() - Renderer3D::kDebugCountsHeaderBytes,
											   0u);
						context.Cmd.BufferBarrier(counts, BufferSync::TransferWrite,
												  BufferSync::ShaderWrite);
					});
			}
		}

		// **RT-first step 1a: the G-buffer pass, then the lit pass.** The scene
		// callback (uploads, the cull, the G-buffer draw) runs in "GBuffer",
		// which binds velocity, normal, albedo, id and the depth; "Scene"
		// preserves that depth and draws the lighting through DrawLit. Any
		// ray-traced signal pass added between the two reads a finished
		// G-buffer. Without the G-buffer shader the old single pass stands.
		auto drawScene = 			[draw = desc.DrawScene, jitter, reflectionsForScene, indirectForScene,
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

				// The glow's viewport, on the same edges and for the same
				// reason as the jitter: only the scene pass has pixels for a
				// disc to be sized in. A probe face or a cascade sees zero
				// and draws none.
				LightGlow::SetViewport(context.Width, context.Height);

				if (draw)
					draw(context);

				LightGlow::SetViewport(0, 0);
				Renderer::SetCameraMotion(nullptr);
				Renderer::SetJitter(Vec2(0.0f, 0.0f));
				Renderer::SetScreenReflections(nullptr);
			};
		// The reconstruction contract as passes (RT-first T4), for any signal:
		// the accumulate and the three a-trous blurs. Above the G-buffer pass
		// because the direct light (T5) runs between it and the lit pass.
		struct SignalPassNames { const char* Accumulate; const char* Blur[3]; };
		// **RT-3.1: which buffers the contract validates against, and at what
		// scale.** The accumulate and the blurs read the surface under each texel
		// with `texelFetch(..., ivec2(gl_FragCoord.xy))`, so the lanes they read
		// have to be on the signal's own grid. At full resolution that is the
		// G-buffer itself and `Divisor` is one -- which is what the reflections
		// and the direct light pass, so nothing changes for them. A signal traced
		// at half or quarter passes the downsampled lanes and its divisor, and the
		// whole contract runs there instead of at four or sixteen times the texels.
		struct SignalGuidance
		{
			RGResource Depth = kRGInvalid;      // kRGInvalid means the G-buffer's own
			RGResource Surface = kRGInvalid;
			RGResource Velocity = kRGInvalid;
			uint32_t   Divisor = 1;
			// **RT-8: which lane of each, because a layer that already exists
			// is cheaper than one laid out to suit.** The downsampled guidance
			// puts its three in lanes 0, 1 and 2, which is what these default
			// to; the sea's surface pass already writes its normal at 0, its
			// position at 2 and its motion at 3, so it needs no pass of its own.
			uint32_t   DepthLane = 0;
			uint32_t   NormalLane = 1;
			uint32_t   VelocityLane = 2;
			// **And what the depth lane means.** True where it is the layer's
			// own position and mask rather than clip depth -- see
			// SignalParams::PositionLane, which this sets so the two cannot
			// disagree about one binding.
			bool       PositionLane = false;
		};
		auto addSignal = [&](const SignalPassNames& names, Renderer3D::SignalParams params,
							 RGResource fresh, RGResource current, RGResource previous, bool hasHistory,
							 CameraMotion* motion, RGTargetDesc blurDesc, bool pair,
							 SignalGuidance guide = {}) -> RGResource
		{
			// **The texel-denominated tuning follows the grid.** Every one of these
			// four is counted in texels of the signal's own target, and a texel at
			// half resolution covers twice the screen: unscaled, a signal moved down
			// would hold its history through twice the camera motion before the
			// smear cap bit, and blur half as far across the picture. Scaling them
			// keeps what they mean -- a distance on screen -- the same.
			if (guide.Divisor > 1)
			{
				const float scale = 1.0f / (float)guide.Divisor;
				params.Slack *= scale;
				params.SmearTexels *= scale;
				params.YoungRadius *= scale;
				params.MaxRadius *= scale;
			}
			const RGResource guideDepth = guide.Depth != kRGInvalid ? guide.Depth : sceneHDR;
			const RGResource guideSurface = guide.Surface != kRGInvalid ? guide.Surface : sceneHDR;
			const RGResource guideVelocity = guide.Velocity != kRGInvalid ? guide.Velocity : sceneHDR;
			// The lane index inside whichever target: the G-buffer keeps its
			// attachments, the guidance target has one lane apiece.
			const uint32_t guideDepthLane = guide.Depth != kRGInvalid ? guide.DepthLane : 0u;
			const uint32_t guideNormalLane =
				guide.Surface != kRGInvalid ? guide.NormalLane : normalIndex;
			const uint32_t guideVelocityLane =
				guide.Velocity != kRGInvalid ? guide.VelocityLane : velocityIndex;
			const bool ownGuide = guide.Depth != kRGInvalid;
			// RT-8: the guidance says what its depth lane is, and the signal
			// carries it to both shaders. Set here rather than trusted to the
			// caller, so a signal cannot describe one binding two ways.
			params.PositionLane = params.PositionLane || guide.PositionLane;
			graph.AddPass(names.Accumulate,
				[&](RGPassBuilder& builder)
				{
					builder.Write(current);
					builder.Sample(fresh);
					builder.Sample(sceneHDR);
					if (ownGuide)
						builder.Sample(guideDepth);
					if (hasHistory)
						builder.Sample(previous);
					builder.DisableDepth();
				},
				[params, fresh, sceneHDR, current, previous, hasHistory, motion, pair,
				 surfaceIdIndex,
				 specular = params.Type != Renderer3D::SignalParams::Kind::Diffuse,
				 guideDepth, guideSurface, guideVelocity, guideDepthLane, guideNormalLane,
				 guideVelocityLane, ownGuide]
				(RGPassContext& context)
				{
					Renderer3D::AccumulateSignal(params,
						context.Color(fresh),
						// The guidance target keeps its depth in a colour lane; the
						// G-buffer's is a depth attachment. Both are a sampler2D whose
						// red is clip depth as written, which is all the shader reads.
						ownGuide ? context.Color(guideDepth, guideDepthLane)
								 : context.Depth(sceneHDR),
						context.Color(guideSurface, guideNormalLane),
						hasHistory ? context.Color(previous) : nullptr,
						hasHistory ? context.Color(previous, 1) : nullptr,
						hasHistory ? context.Color(previous, 2) : nullptr,
						context.Color(guideVelocity, guideVelocityLane),
						*motion, hasHistory,
						pair ? context.Color(fresh, 1) : nullptr,
						pair && hasHistory ? context.Color(previous, 3) : nullptr,
						// RT-6.5: the G-buffer's object id, and this signal's own copy of
						// it from last frame. Only the specular accumulate keeps a fifth
						// attachment for it; the diffuse kinds pass null and the binding
						// falls back to the surface, which is what it did before.
						specular ? context.Color(sceneHDR, surfaceIdIndex) : nullptr,
						specular && hasHistory ? context.Color(previous, 4) : nullptr);
				});
			// Three blur passes at strides 1, 2, 4: each reads the previous
			// pass's output; the first reads the history itself, which is
			// never written here.
			blurDesc.ExtraColors.clear();
			if (pair)
				blurDesc.ExtraColors.push_back(blurDesc.Color);
			RGResource blurred = kRGInvalid;
			RGResource blurInput = current;
			const std::string base = blurDesc.Name;
			for (int pass = 0; pass < 3; ++pass)
			{
				blurDesc.Name = base + (pass == 0 ? "" : pass == 1 ? "2" : "4");
				const RGResource output = graph.CreateTarget(blurDesc);
				const RGResource input = blurInput;
				const int stride = 1 << pass;
				graph.AddPass(names.Blur[pass],
					[&](RGPassBuilder& builder)
					{
						builder.Write(output);
						builder.Sample(input);
						if (input != current)
							builder.Sample(current);
						builder.Sample(sceneHDR);
						if (ownGuide)
							builder.Sample(guideDepth);
						builder.DisableDepth();
					},
					[params, input, current, sceneHDR, stride, pair,
					 guideDepth, guideSurface, guideDepthLane, guideNormalLane,
					 ownGuide](RGPassContext& context)
					{
						Renderer3D::BlurSignal(params, context.Color(input),
											   ownGuide ? context.Color(guideDepth, guideDepthLane)
													: context.Depth(sceneHDR),
											   context.Color(guideSurface, guideNormalLane),
											   context.Color(current, 1),
											   stride,
											   // the twin: attachment 3 of the accumulated target, 1 of a blurred one
											   pair ? context.Color(input, input == current ? 3 : 1) : nullptr);
					});
				blurInput = output;
				blurred = output;
			}
			return blurred;
		};
		// **RT-3.1: the guidance lanes at a divisor, built once and shared.** GI
		// and the occlusion can sit on different rungs of their own dials; two
		// signals on the same rung should pay for one downsample between them.
		// Keyed by divisor, and the pass is added the first time one is asked for,
		// so a frame with no reduced-resolution signal adds nothing at all.
		std::map<uint32_t, SignalGuidance> guidanceCache;
		auto guidanceFor = [&](uint32_t divisor) -> SignalGuidance
		{
			if (divisor <= 1 || !PostProcess::IsReady())
				return {};
			const auto found = guidanceCache.find(divisor);
			if (found != guidanceCache.end())
				return found->second;

			RGTargetDesc guideDesc;
			guideDesc.Name = "Guidance";
			// Depth in full precision: it is fed straight to the inverse
			// view-projection, and a half float near the far plane rebuilds a
			// position metres from where the surface is.
			guideDesc.Color = Format::R32_SFLOAT;
			guideDesc.ExtraColors = { kNormalFormat, Format::R16G16_SFLOAT };
			guideDesc.Depth = Format::Undefined;
			guideDesc.Scale = 1.0f / (float)divisor;
			const RGResource guide = graph.CreateTarget(guideDesc);

			graph.AddPass("Guidance downsample",
				[&](RGPassBuilder& builder)
				{
					builder.Write(guide);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, velocityIndex, divisor](RGPassContext& context)
				{
					PostProcess::GuideDownsample(context.Cmd,
												 context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 context.Color(sceneHDR, velocityIndex),
												 divisor, Format::R32_SFLOAT,
												 kNormalFormat, Format::R16G16_SFLOAT);
				});

			SignalGuidance made;
			made.Depth = guide;
			made.Surface = guide;
			made.Velocity = guide;
			made.Divisor = divisor;
			guidanceCache.emplace(divisor, made);
			return made;
		};
		// **RT-6: the identity lanes, kept for next frame's temporal resolve.**
		// Declared here and filled by a pass added after the G-buffer below; the
		// resolve reads `Previous` and, for this frame's side of the comparison,
		// `Current` -- so the pass has to have run by then, which it has, being
		// hundreds of lines earlier in the graph.
		RGResource taaGuideCurrent = kRGInvalid;
		RGResource taaGuidePrevious = kRGInvalid;
		bool taaGuideHasHistory = false;

		// The depth-to-view reconstruction every screen-space pass takes; above
		// the G-buffer pass since RT-2, because the occlusion signal runs there.
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
		// The ray budget's tile map, prepared and imported here so the passes
		// before the lit pass (RT-2's occlusion) can read last frame's allocation;
		// the allocator's own passes run after TAA as before.
		// Sixteen: one number per 256 pixels. Small enough that a wave never
		// straddles two allocations, large enough that the map is a few
		// thousand texels rather than a few million.
		constexpr uint32_t kTileSize = 16;
		uint32_t budgetTilesX = 0;
		uint32_t budgetTilesY = 0;
		RGResource budgetPrevious = kRGInvalid;
		RGResource budgetCurrent = kRGInvalid;
		bool budgetHasHistory = false;
		if (desc.RayBudget && PostProcess::IsReady())
		{
			budgetTilesX = Math::Max((desc.Width + kTileSize - 1) / kTileSize, 1u);
			budgetTilesY = Math::Max((desc.Height + kTileSize - 1) / kTileSize, 1u);
			TemporalHistory& budget = *desc.RayBudget;
			budget.Prepare(Renderer::GetDevice(), budgetTilesX, budgetTilesY,
						   Format::R16G16B16A16_SFLOAT, "RayBudget");
			if (budget.Current() && budget.Previous())
			{
				budgetHasHistory = budget.HasHistory();
				budgetPrevious = graph.Import(budget.Previous(), "RayBudgetPrevious");
				budgetCurrent = graph.Import(budget.Current(), "RayBudgetCurrent");
			}
		}
		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		// The flag is up only around the scene callback, so a probe face or a
		// shadow caster drawn outside the graph keeps the single-pass path.
		// RT-first T5: the direct light as a signal -- the DirectTrace pass
		// between the G-buffer and the lit pass, K lights per pixel (the
		// preset's count, or --rays-per-pixel), the lit shader adding its two
		// pictures. Needs the split, rays, a place to keep the history, the
		// shader, and not --direct-signal=off (the reference arm).
		const bool directSignal = gbufferPass && ResolveRayTracing(desc.Render)
							   && config.DirectSignal && desc.DirectLight != nullptr
							   && Renderer3D::CanTraceDirectLight();
		const int directRays = config.HasRaysPerPixelOverride ? config.RaysPerPixel
															   : rtPreset.RaysPerPixel;
		// RT-2: the ambient occlusion as a signal -- computed from the G-buffer
		// before the lit pass, settled on the contract, applied to the ambient
		// terms in the lit shader. RTAO under rays, SSAO in raster; needs the
		// split, a place to keep the history, and not --ao-signal=off.
		const AoDetail aoSignalLevel = rayOcclusion ? rayAo : desc.Post.AmbientOcclusion;
		const bool aoSignal = gbufferPass && aoSignalLevel != AoDetail::Off && PostProcess::IsReady()
						   && config.AoSignal && desc.Occlusion != nullptr;
		auto drawGBuffer = [drawScene, directSignal, aoSignal, giSignal](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			// Told before BeginScene fills the scene block, whose RayRates.w
			// bits 22, 23 and 24 are the lit shader's switches.
			Renderer3D::SetDirectSignal(directSignal);
			Renderer3D::SetAoSignal(aoSignal);
			Renderer3D::SetGiSignal(giSignal);
			drawScene(context);
			Renderer3D::SetDirectSignal(false);
			Renderer3D::SetAoSignal(false);
			Renderer3D::SetGiSignal(false);
			Renderer3D::SetGBufferPassActive(false);
		};
		if (gbufferPass)
		{
			graph.AddPass("GBuffer",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(sceneHDR,
						{ { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { albedoIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { surfaceIdIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
					builder.SetClearColor(desc.ClearColor);
					if (previousReflections != kRGInvalid)
						builder.Sample(previousReflections);
					if (previousIndirect != kRGInvalid)
						builder.Sample(previousIndirect);
				},
				drawGBuffer);
		}
		// **RT-6: written here, straight after the G-buffer.** Everything the
		// resolve validates against is final by this point and nothing has yet
		// drawn over it -- the water and the transparent kinds write none of
		// these lanes, so waiting would keep the same values at more risk.
		// Only under TAA: no other filter reads a history per pixel, and a
		// full-resolution RGBA32F pair is not something to allocate for nobody.
		if (gbufferPass && desc.TaaGuide && PostProcess::IsReady()
			&& aa == AntiAliasing::TAA && config.TaaGeometry)
		{
			TemporalHistory& guide = *desc.TaaGuide;
			guide.Prepare(Renderer::GetDevice(),
						  desc.Width * (uint32_t)supersample,
						  desc.Height * (uint32_t)supersample,
						  Format::R32G32B32A32_SFLOAT, "TaaGuide");
			if (guide.Current() && guide.Previous())
			{
				taaGuideCurrent = graph.Import(guide.Current(), "TaaGuideCurrent");
				taaGuidePrevious = graph.Import(guide.Previous(), "TaaGuidePrevious");
				taaGuideHasHistory = guide.HasHistory();
				graph.AddPass("TAA guide",
					[&](RGPassBuilder& builder)
					{
						builder.Write(taaGuideCurrent);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[sceneHDR, normalIndex, surfaceIdIndex](RGPassContext& context)
					{
						PostProcess::TaaGuide(context.Cmd,
											  context.Depth(sceneHDR),
											  context.Color(sceneHDR, normalIndex),
											  context.Color(sceneHDR, surfaceIdIndex),
											  Format::R32G32B32A32_SFLOAT);
					});
				// Swapped once the pass is declared, like every other pair here:
				// what was written this frame is what the next frame reads.
				guide.Advance();
			}
		}
		else if (desc.TaaGuide)
		{
			// A history left standing would be resumed as truth the frame TAA or
			// the test comes back on, and it would describe another camera.
			desc.TaaGuide->Invalidate();
		}
		RGResource directTraced = kRGInvalid;
		RGResource directLit = kRGInvalid;      // what the lit pass adds: the blurred pair
		RGResource currentDirect = kRGInvalid;  // the accumulated pair, for the debug views
		if (directSignal)
		{
			RGTargetDesc directDesc;
			directDesc.Name = "DirectTrace";
			directDesc.Color = Format::R16G16B16A16_SFLOAT;
			directDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
			directDesc.Depth = Format::Undefined;
			directDesc.Scale = (float)supersample;
			directTraced = graph.CreateTarget(directDesc);
			Renderer3D::GiTraceView directView;
			directView.NearClip = desc.NearClip;
			directView.FarClip = desc.FarClip;
			directView.InvProjection0 = desc.InvProjection0;
			directView.InvProjection1 = desc.InvProjection1;
			directView.View = desc.View;
			graph.AddPass("DirectTrace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(directTraced);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, albedoIndex, surfaceIdIndex, directView, directRays]
				(RGPassContext& context)
				{
					Renderer3D::TraceDirectLight(context.Cmd,
												 context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 context.Color(sceneHDR, albedoIndex),
												 context.Color(sceneHDR, surfaceIdIndex),
												 Format::R16G16B16A16_SFLOAT,
												 directView, directRays);
				});
			// The contract: the pair accumulated over the frames behind it
			// (surface reprojection, the tests, the bound, the motion-capped
			// memory) and blurred while young. Four attachments: the diffuse,
			// the surface, the extra, the specular twin.
			directLit = directTraced;
			TemporalHistory& direct = *desc.DirectLight;
			direct.Prepare(Renderer::GetDevice(),
						   desc.Width * (uint32_t)supersample, desc.Height * (uint32_t)supersample,
						   Format::R16G16B16A16_SFLOAT, "DirectLight",
						   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT,
						   Format::R16G16B16A16_SFLOAT);
			if (direct.Current() && direct.Previous())
			{
				const RGResource previousDirect = graph.Import(direct.Previous(), "DirectPrevious");
				currentDirect = graph.Import(direct.Current(), "DirectCurrent");
				const bool directHistory = direct.HasHistory();
				RGTargetDesc directBlurDesc = directDesc;
				directBlurDesc.Name = "DirectBlurred";
				static const SignalPassNames kDirectPasses =
					{ "DirectAccumulate", { "DirectBlur", "DirectBlur2", "DirectBlur4" } };
				directLit = addSignal(kDirectPasses, Renderer3D::DirectSignal(),
									  directTraced, currentDirect, previousDirect, directHistory,
									  &direct.Motion(), directBlurDesc, true);
				direct.Advance();
			}
		}
		else if (desc.DirectLight)
		{
			desc.DirectLight->Invalidate();
		}
		RGResource occlusionLit = kRGInvalid;       // what the lit pass reads
		RGResource currentOcclusion = kRGInvalid;   // the accumulated signal, for the debug view
		if (aoSignal)
		{
			// The compute at its own resolution (RTAO's half, SSAO's rung), from
			// the G-buffer's depth and normal, exactly as the post chain did it.
			const uint32_t aoTaps = aoSignalLevel == AoDetail::Full    ? 8u
								  : aoSignalLevel == AoDetail::Quarter ? 2u
																	   : 4u;
			const uint32_t aoDivisor = rayOcclusion                       ? 2u
									 : aoSignalLevel == AoDetail::Full     ? 2u
									 : aoSignalLevel == AoDetail::Quarter  ? 4u
																		   : 2u;
			const uint32_t aoWidth = Math::Max(desc.Width / aoDivisor, 1u);
			const uint32_t aoHeight = Math::Max(desc.Height / aoDivisor, 1u);
			RGTargetDesc aoRawDesc;
			aoRawDesc.Name = "OcclusionRaw";
			aoRawDesc.Color = Format::R16G16B16A16_SFLOAT;
			aoRawDesc.Depth = Format::Undefined;
			aoRawDesc.Scale = 1.0f / (float)aoDivisor;
			const RGResource aoRaw = graph.CreateTarget(aoRawDesc);
			const float aoRadius = desc.Post.AoRadius;
			const float aoIntensity = desc.Post.AoIntensity;
			const float aoFrame = (float)(Renderer::GetFrameCount() % 64u);
			const bool aoBudget = rayOcclusion && budgetPrevious != kRGInvalid && budgetHasHistory;
			graph.AddPass("OcclusionCompute",
				[&](RGPassBuilder& builder)
				{
					builder.Write(aoRaw);
					builder.Sample(sceneHDR);
					if (aoBudget)
						builder.Sample(budgetPrevious);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, aoWidth, aoHeight, reconstruction, aoRadius,
				 rayOcclusion, aoTaps, aoFrame, aoBudget, budgetPrevious](RGPassContext& context)
				{
					if (rayOcclusion)
					{
						RayGpuScope rayTime(context.Cmd);
						PostProcess::RtaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 RayShadows::GetStructure(),
												 aoBudget ? context.Color(budgetPrevious) : nullptr,
												 aoWidth, aoHeight, reconstruction,
												 aoRadius, aoTaps, Format::R16G16B16A16_SFLOAT,
												 aoFrame);
					}
					else
					{
						PostProcess::SsaoCompute(context.Cmd, context.Depth(sceneHDR),
												 context.Color(sceneHDR, normalIndex),
												 aoWidth, aoHeight, reconstruction,
												 aoRadius, Format::R16G16B16A16_SFLOAT);
					}
				});
			// **RT-3.1: the resolve at the occlusion's own resolution, not the
			// frame's.** Against a white scene the apply shader is a joint
			// bilateral resample with the intensity curve folded in; run at 1:1 the
			// resample collapses to a passthrough and what is left is the part the
			// contract needs -- the depth stripped out of the green channel, where
			// the compute pass keeps it for its own tap weights, and the scalar
			// replicated into RGB. Without that the contract's bound and moments,
			// which are built from Luma(rgb), would be almost entirely a distance in
			// metres. Doing it here also keeps RT-2's order: the intensity curve is
			// applied before accumulation, as it was when the look was accepted.
			const SignalGuidance aoGuide = guidanceFor(aoDivisor);
			RGTargetDesc aoDesc = aoRawDesc;
			aoDesc.Name = "OcclusionFresh";
			const RGResource aoFresh = graph.CreateTarget(aoDesc);
			graph.AddPass("OcclusionResolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(aoFresh);
					builder.Sample(aoRaw);
					builder.Sample(sceneHDR);
					if (aoGuide.Depth != kRGInvalid)
						builder.Sample(aoGuide.Depth);
					builder.DisableDepth();
				},
				[aoRaw, sceneHDR, aoWidth, aoHeight, aoIntensity, aoGuide,
				 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
				{
					// The depth on this pass's own grid: the guidance lane where there
					// is one, and it holds clip depth exactly as the G-buffer wrote it,
					// which is what LinearDepth in the shader expects either way.
					PostProcess::SsaoApply(context.Cmd, TextureLoader::White(Renderer::GetDevice()),
										   context.Color(aoRaw),
										   aoGuide.Depth != kRGInvalid
											   ? context.Color(aoGuide.Depth)
											   : context.Depth(sceneHDR),
										   aoWidth, aoHeight, nearZ, farZ, aoIntensity,
										   Format::R16G16B16A16_SFLOAT);
				});
			RGResource aoSettled = aoFresh;
			TemporalHistory& occlusion = *desc.Occlusion;
			occlusion.Prepare(Renderer::GetDevice(), aoWidth, aoHeight,
							  Format::R16G16B16A16_SFLOAT, "OcclusionSignal",
							  Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);
			if (occlusion.Current() && occlusion.Previous())
			{
				const RGResource previousOcclusion = graph.Import(occlusion.Previous(), "OcclusionPrevious");
				currentOcclusion = graph.Import(occlusion.Current(), "OcclusionCurrent");
				RGTargetDesc aoBlurDesc = aoDesc;
				aoBlurDesc.Name = "OcclusionBlurred";
				static const SignalPassNames kOcclusionPasses =
					{ "OcclusionAccumulate", { "OcclusionBlur", "OcclusionBlur2", "OcclusionBlur4" } };
				aoSettled = addSignal(kOcclusionPasses, Renderer3D::AoSignal(),
									  aoFresh, currentOcclusion, previousOcclusion, occlusion.HasHistory(),
									  &occlusion.Motion(), aoBlurDesc, false, aoGuide);
				occlusion.Advance();
			}

			// And up to the lit pass's grid, once, at the end.
			RGTargetDesc aoFullDesc;
			aoFullDesc.Name = "OcclusionFull";
			aoFullDesc.Color = Format::R16G16B16A16_SFLOAT;
			aoFullDesc.Depth = Format::Undefined;
			aoFullDesc.Scale = (float)supersample;
			const RGResource aoFull = graph.CreateTarget(aoFullDesc);
			graph.AddPass("OcclusionUpsample",
				[&](RGPassBuilder& builder)
				{
					builder.Write(aoFull);
					builder.Sample(aoSettled);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[aoSettled, sceneHDR, aoWidth, aoHeight,
				 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
				{
					PostProcess::SignalUpsample(context.Cmd, context.Color(aoSettled),
											   context.Depth(sceneHDR),
											   aoWidth, aoHeight, nearZ, farZ,
											   Format::R16G16B16A16_SFLOAT);
				});
			occlusionLit = aoFull;
		}
		// --- RT-3: the bounce, traced and settled before the lighting ------
		//
		// The same three stages RT-2 gave the occlusion, for the same reason.
		// The trace runs at the quality dial's own resolution (half, at Medium
		// and below) because indirect light is the lowest-frequency thing in
		// the frame and was the only term that ever paid full rate for itself.
		// The upsample brings it to the lit pass's grid, where the contract's
		// surface tests are honest -- a half-resolution texel sits on the
		// corner of four full-resolution ones and 'the surface under this
		// texel' has four answers there. Then the contract, unchanged.
		RGResource giLit = kRGInvalid;        // what the lit pass reads
		RGResource currentGi = kRGInvalid;    // the accumulated signal, for the debug view
		if (giSignal)
		{
			const uint32_t giDivisor = RayDetailDivisor(giDetail);
			const uint32_t giTraceWidth = Math::Max(desc.Width / giDivisor, 1u);
			const uint32_t giTraceHeight = Math::Max(desc.Height / giDivisor, 1u);
			RGTargetDesc giRawDesc;
			giRawDesc.Name = "GiRaw";
			giRawDesc.Color = Format::R16G16B16A16_SFLOAT;
			giRawDesc.Depth = Format::Undefined;
			giRawDesc.Scale = 1.0f / (float)giDivisor;
			const RGResource giRaw = graph.CreateTarget(giRawDesc);

			Renderer3D::GiTraceView giView;
			giView.NearClip = desc.NearClip;
			giView.FarClip = desc.FarClip;
			giView.InvProjection0 = desc.InvProjection0;
			giView.InvProjection1 = desc.InvProjection1;
			giView.View = desc.View;

			// Last frame's allocation, the way RT-2's occlusion reads it: the
			// allocator's own passes still run after TAA, so what is available
			// this early is the previous map.
			const bool giBudget = budgetPrevious != kRGInvalid && budgetHasHistory;
			graph.AddPass("GI trace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giRaw);
					builder.Sample(sceneHDR);
					if (giBudget)
						builder.Sample(budgetPrevious);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, giView, giBudget, budgetPrevious,
				 rays = RayDetailRays(giDetail)](RGPassContext& context)
				{
					RayGpuScope rayTime(context.Cmd);
					Renderer3D::TraceGlobalIllumination(context.Cmd,
													   context.Depth(sceneHDR),
													   context.Color(sceneHDR, normalIndex),
													   giBudget ? context.Color(budgetPrevious) : nullptr,
													   Format::R16G16B16A16_SFLOAT,
													   giView, rays);
				});

			// **RT-3.1: the contract runs here, at the trace's own resolution.**
			// It used to run after a full-resolution upsample, which paid four
			// times the texels for a signal that carries information at one in
			// four -- 0.85 ms of RT-3's 0.89 ms chain. Filtering where the signal
			// was traced and upsampling once at the end is the arrangement every
			// real-time denoiser uses, and the guidance downsample above is what
			// keeps the contract's surface tests honest on the coarser grid.
			const SignalGuidance giGuide = guidanceFor(giDivisor);
			RGTargetDesc giDesc = giRawDesc;
			giDesc.Name = "GiSettled";
			RGResource giSettled = giRaw;
			TemporalHistory& gi = *desc.GiLight;
			gi.Prepare(Renderer::GetDevice(), giTraceWidth, giTraceHeight,
					   Format::R16G16B16A16_SFLOAT, "GiSignal",
					   Format::R16G16B16A16_SFLOAT, Format::R16G16B16A16_SFLOAT);
			if (gi.Current() && gi.Previous())
			{
				const RGResource previousGi = graph.Import(gi.Previous(), "GiPrevious");
				currentGi = graph.Import(gi.Current(), "GiCurrent");
				RGTargetDesc giBlurDesc = giDesc;
				giBlurDesc.Name = "GiBlurred";
				static const SignalPassNames kGiPasses =
					{ "GiAccumulate", { "GiBlur", "GiBlur2", "GiBlur4" } };
				giSettled = addSignal(kGiPasses, Renderer3D::GiSignal(),
									  giRaw, currentGi, previousGi, gi.HasHistory(),
									  &gi.Motion(), giBlurDesc, false,
									  giGuide);
				gi.Advance();
			}

			// And one joint bilateral upsample at the end, onto the lit pass's
			// grid, which is the only place the lit shader can read it by texel.
			RGTargetDesc giFullDesc;
			giFullDesc.Name = "GiFull";
			giFullDesc.Color = Format::R16G16B16A16_SFLOAT;
			giFullDesc.Depth = Format::Undefined;
			giFullDesc.Scale = (float)supersample;
			const RGResource giFull = graph.CreateTarget(giFullDesc);
			graph.AddPass("GI upsample",
				[&](RGPassBuilder& builder)
				{
					builder.Write(giFull);
					builder.Sample(giSettled);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[giSettled, sceneHDR, giTraceWidth, giTraceHeight,
				 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
				{
					PostProcess::SignalUpsample(context.Cmd, context.Color(giSettled),
											   context.Depth(sceneHDR),
											   giTraceWidth, giTraceHeight, nearZ, farZ,
											   Format::R16G16B16A16_SFLOAT);
				});
			giLit = giFull;
		}
		else if (desc.GiLight)
		{
			desc.GiLight->Invalidate();
		}
		graph.AddPass("Scene",
			[&](RGPassBuilder& builder)
			{
				builder.WriteAttachments(sceneHDR,
					{ { 0, desc.ClearColor },
					  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { indirectIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
				builder.SetClearColor(desc.ClearColor);
				if (gbufferPass)
					builder.PreserveDepth();
				if (previousReflections != kRGInvalid)
					builder.Sample(previousReflections);
				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
				if (directLit != kRGInvalid)
					builder.Sample(directLit);
				if (occlusionLit != kRGInvalid)
					builder.Sample(occlusionLit);
				if (giLit != kRGInvalid)
					builder.Sample(giLit);
			},
			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter, directLit, occlusionLit, giLit,
							   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)
							  {
								  // The lit half: the same edges the scene callback
								  // had (viewport, jitter, camera motion), for the
								  // glow, the sky and the particles it draws.
								  LightGlow::SetViewport(context.Width, context.Height);
								  Renderer::SetJitter(jitter);
								  Renderer::SetCameraMotion(motion);
								  // RT-first T5: the direct light for the lit draw.
								  Renderer3D::SetDirectLight(
									  directLit != kRGInvalid ? context.Color(directLit, 0) : nullptr,
									  directLit != kRGInvalid ? context.Color(directLit, 1) : nullptr);
								  Renderer3D::SetScreenOcclusion(
									  occlusionLit != kRGInvalid ? context.Color(occlusionLit) : nullptr);
								  // RT-3: this frame's bounce, onto binding 16 in place of
								  // last frame's buffer. Null leaves that binding alone.
								  Renderer3D::SetScreenIndirectSignal(
									  giLit != kRGInvalid ? context.Color(giLit) : nullptr);
								  if (drawLit)
									  drawLit(context);
								  else
									  Renderer3D::DrawLit();
								  Renderer3D::SetDirectLight(nullptr, nullptr);
								  Renderer3D::SetScreenOcclusion(nullptr);
								  Renderer3D::SetScreenIndirectSignal(nullptr);
								  Renderer::SetCameraMotion(nullptr);
								  Renderer::SetJitter(Vec2(0.0f, 0.0f));
								  LightGlow::SetViewport(0, 0);
							  })
						: std::function<void(RGPassContext&)>(drawScene));

		// The overlay goes into the HDR target rather than over the finished
		// image, because it depth-tests against the scene it annotates. The
		// cost is that its colours go through the tone curve like everything
		// else, which shifts them slightly -- acceptable for a diagnostic, and
		// the alternative needs the depth buffer in a second pass.
		// Transparency goes in before the overlay, so a collider wireframe is
		// still drawn over the smoke it describes rather than under it.
		if (wantTransparent)
		{
			// --- the water's backdrop -------------------------------------------
			//
			// Between the opaque passes and the transparent one, which is the
			// only place it can be: after this the scene target is being drawn
			// over, and during the transparent pass its depth is bound for
			// testing -- the layout conflict HANDOFF recorded. The copy
			// conflicts with nothing: the scene pass has ended, so its colour
			// and depth (or their single-sampled twins, under MSAA) are
			// sampleable, and what comes out is a pair the water can read
			// while the transparent pass owns the real attachments.
			//
			// At the output size, not the supersampled one: refraction is
			// smooth content, and the water addresses it through NDC either
			// way.
			RGResource waterBackdrop = kRGInvalid;
			if (desc.WaterSeeThrough && PostProcess::IsReady())
			{
				RGTargetDesc backdropDesc;
				backdropDesc.Name = "WaterBackdrop";
				backdropDesc.Color = Format::R16G16B16A16_SFLOAT;
				// The depth beside it, linearised to view metres by the copy.
				backdropDesc.ExtraColors = { Format::R32_SFLOAT };
				backdropDesc.Depth = Format::Undefined;
				waterBackdrop = graph.CreateTarget(backdropDesc);

				graph.AddPass("WaterBackdrop",
					[&](RGPassBuilder& builder)
					{
						builder.Write(waterBackdrop);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[sceneHDR, waterBackdrop, nearClip = desc.NearClip,
					 farClip = desc.FarClip](RGPassContext& context)
					{
						PostProcess::WaterBackdrop(context.Cmd,
												   context.Color(sceneHDR, 0),
												   context.Depth(sceneHDR),
												   nearClip, farClip,
												   Format::R16G16B16A16_SFLOAT,
												   Format::R32_SFLOAT);
					});
			}

			// --- the sea's surface, before anything shades it ------------------
			//
			// The same water, the same waves, the same shader stopped as soon
			// as the surface is final. After the backdrop copy, because the
			// colour gradient reads the depth behind the water to know how deep
			// it is; before the transparent pass, because the pass that shades
			// the sea's lamps sits between the two.
			if (waterSurface != kRGInvalid)
			{
				graph.AddPass("WaterSurface",
					[&](RGPassBuilder& builder)
					{
						// Its own colour and its own depth, both cleared: the
						// depth is what makes the nearest of several water
						// fragments on one pixel the one that survives.
						builder.Write(waterSurface);
						// **Cleared to nothing, alpha included.** The position
						// attachment's w is the mask that says a wave was drawn
						// here, and the graph's default clear has alpha one --
						// which reads as valid water on every pixel of the
						// screen and put the two lamp passes over the whole
						// frame instead of over the sea. Measured: the choose
						// pass 3.8 ms to 7.6.
						builder.SetClearColor(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
						if (waterBackdrop != kRGInvalid)
							builder.Sample(waterBackdrop);
					},
					[draw = desc.DrawWaterSurface, waterBackdrop](RGPassContext& context)
					{
						if (waterBackdrop != kRGInvalid)
							Renderer3D::SetWaterBackdrop(context.Color(waterBackdrop, 0),
														 context.Color(waterBackdrop, 1));
						draw(context);
						Renderer3D::SetWaterBackdrop(nullptr, nullptr);
					});
			}

			// --- the sea's lamps: which four, then their light ------------------
			//
			// Between the surface pass and the water draw, because the water
			// draw reads what they produce. Two passes rather than one: every
			// pixel has to have finished choosing before any pixel can read
			// what its neighbour chose, and a fragment cannot see its
			// neighbours' work inside its own pass.
			RGResource waterLamps = kRGInvalid;
			// WR-16 S5's half-resolution mirror picture, when that pass ran.
			RGResource waterTraced = kRGInvalid;
			// Only where lamps are being sampled at all, and only where the
			// run did not ask for the sampler inside the water shader instead.
			if (waterSurface != kRGInvalid && desc.WaterReservoirs
				&& rtLamps > 0
				&& EngineConfig::Get().WaterLampPass)
			{
				TemporalHistory& choices = *desc.WaterReservoirs;
				// Four choices a pixel: the light's index and the confidence
				// as whole integers in one attachment, the weight in the
				// other. Whole integers because a weight through a half would
				// quantise the one number the estimate divides by, and an
				// index is not a thing to interpolate at all.
				// One choice per n x n block where a run asks for it: the
				// sweep is what the choose pass costs, and this is the only
				// thing that divides it.
				const uint32_t chooseScale = (uint32_t)EngineConfig::ChooseBlock(
					EngineConfig::Get().WaterLampReuse);
				choices.Prepare(Renderer::GetDevice(),
								desc.Width * (uint32_t)supersample / chooseScale,
								desc.Height * (uint32_t)supersample / chooseScale,
								Format::R32G32B32A32_UINT, "WaterLampChoices",
								Format::R32G32B32A32_UINT);

				if (choices.Current() && choices.Previous())
				{
					const RGResource pastChoices =
						graph.Import(choices.Previous(), "WaterChoicesPrevious");
					const RGResource newChoices =
						graph.Import(choices.Current(), "WaterChoicesCurrent");

					graph.AddPass("WaterChooseLamps",
						[&](RGPassBuilder& builder)
						{
							builder.Write(newChoices);
							builder.Sample(pastChoices);
							builder.Sample(waterSurface);
							builder.DisableDepth();
						},
						[waterSurface, pastChoices, &choices](RGPassContext& context)
						{
							Renderer3D::ChooseWaterLamps(
								context.Color(waterSurface, 0),
								context.Color(waterSurface, 1),
								context.Color(waterSurface, 2),
								context.Color(pastChoices, 0),
								context.Color(pastChoices, 1),
								choices.Motion(), choices.HasHistory());
						});

					RGTargetDesc lampDesc;
					lampDesc.Name = "WaterLampLight";
					lampDesc.Color = Format::R16G16B16A16_SFLOAT;
					lampDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
					lampDesc.Depth = Format::Undefined;
					lampDesc.Scale = (float)supersample;
					waterLamps = graph.CreateTarget(lampDesc);

					graph.AddPass("WaterShadeLamps",
						[&](RGPassBuilder& builder)
						{
							builder.Write(waterLamps);
							builder.Sample(newChoices);
							builder.Sample(waterSurface);
							builder.DisableDepth();
						},
						[waterSurface, newChoices](RGPassContext& context)
						{
							Renderer3D::ShadeWaterLamps(
								context.Color(waterSurface, 0),
								context.Color(waterSurface, 1),
								context.Color(waterSurface, 2),
								context.Color(newChoices, 0),
								context.Color(newChoices, 1));
						});

					// --- and the light averaged with the frames behind it ---
					//
					// WR-16 S4c. The pair above is the light of four lamps
					// drawn at random: right on average, different every frame,
					// and that difference is the fizz the flicker protocol
					// counts. This blends each pixel with what it read last
					// frame for the same patch of sea. It is the *light* that
					// is kept, not the choice -- the half of S4b that reused
					// the choice was measured to lose, because a wave turning a
					// patch makes last frame's choice wrong immediately, while
					// a brightness is a property of the patch and survives the
					// turn.
					//
					// From here on the water draw reads this pair instead, so
					// `waterLamps` becomes the accumulated one.
					// **RT-8 job 3: the same averaging, on the contract.** The
					// sea's surface pass already writes the three lanes the
					// contract validates against -- its normal, its position
					// and its motion -- so nothing is downsampled or copied to
					// get here; the guidance points straight at them. The
					// private pass below is the other arm of --water-contract,
					// kept so the two can be compared rather than argued over.
					if (desc.WaterLampLight && EngineConfig::Get().WaterLampAccumulate
						&& EngineConfig::Get().WaterContract)
					{
						TemporalHistory& light = *desc.WaterLampLight;
						// The pair's four: the scattered half, the surface, the
						// extra, the glinting twin. The same shape the direct
						// light's history has, because it is the same signal.
						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampSignal",
									  Format::R16G16B16A16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT,
									  Format::R16G16B16A16_SFLOAT);
						if (light.Current() && light.Previous())
						{
							const RGResource pastLight =
								graph.Import(light.Previous(), "WaterLightPrevious");
							const RGResource newLight =
								graph.Import(light.Current(), "WaterLightCurrent");
							SignalGuidance seaGuide;
							seaGuide.Depth = waterSurface;
							seaGuide.Surface = waterSurface;
							seaGuide.Velocity = waterSurface;
							seaGuide.DepthLane = 2;      // world position, w the mask
							seaGuide.NormalLane = 0;     // octahedral normal, roughness
							seaGuide.VelocityLane = 3;   // the wave's own screen motion
							seaGuide.PositionLane = true;
							RGTargetDesc seaBlurDesc = lampDesc;
							seaBlurDesc.Name = "WaterLampBlurred";
							static const SignalPassNames kWaterPasses =
								{ "WaterLampAccumulate",
								  { "WaterLampBlur", "WaterLampBlur2", "WaterLampBlur4" } };
							waterLamps = addSignal(kWaterPasses,
												   Renderer3D::WaterLampSignal(),
												   waterLamps, newLight, pastLight,
												   light.HasHistory(), &light.Motion(),
												   seaBlurDesc, true, seaGuide);
							light.Advance();
						}
					}
					else if (desc.WaterLampLight && EngineConfig::Get().WaterLampAccumulate)
					{
						TemporalHistory& light = *desc.WaterLampLight;
						// RT-8 job 3: and a third attachment, the sea's surface a
						// frame back -- octahedral normal, plane distance, mask.
						// Full float because the plane distance is a world
						// coordinate and the bay is a kilometre across.
						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampAverage",
									  Format::R16G16B16A16_SFLOAT,
									  Format::R32G32B32A32_SFLOAT);

						if (light.Current() && light.Previous())
						{
							const RGResource pastLight =
								graph.Import(light.Previous(), "WaterLightPrevious");
							const RGResource newLight =
								graph.Import(light.Current(), "WaterLightCurrent");
							const RGResource rawLamps = waterLamps;

							graph.AddPass("WaterAccumulateLamps",
								[&](RGPassBuilder& builder)
								{
									builder.Write(newLight);
									builder.Sample(pastLight);
									builder.Sample(rawLamps);
									builder.Sample(waterSurface);
									builder.DisableDepth();
								},
								[rawLamps, waterSurface, pastLight, &light]
								(RGPassContext& context)
								{
									Renderer3D::AccumulateWaterLamps(
										context.Color(rawLamps, 0),
										context.Color(rawLamps, 1),
										context.Color(waterSurface, 2),
										context.Color(pastLight, 0),
										context.Color(pastLight, 1),
										light.Motion(), light.HasHistory(),
										// RT-8: attachment 3 is the wave's own motion.
										context.Color(waterSurface, 3),
										// RT-8 job 3's measurement: the sea's normal
										// now, and its surface a frame back.
										context.Color(waterSurface, 0),
										context.Color(pastLight, 2));
								});

							waterLamps = newLight;
							light.Advance();
						}
					}

					// The swap. The camera that drew the choices is recorded
					// by the pass itself, when it has used the one before it:
					// a history of choices is per chain, and the editor draws
					// two chains from two cameras in one frame.
					choices.Advance();
				}
			}

			// --- WR-16 S5: the sea's mirror ray, in a pass of its own -------
			//
			// At 1/n the width and height, so the ray density the quad share
			// already gives costs what it should: a smaller pass has one
			// fragment where the quad has one working lane and three waiting.
			// Nothing reads this yet -- the water draw still traces its own,
			// and pointing it here is the next step -- so it is off unless a
			// run asks, because an unread pass is only cost.
			// The level's, unless a run asked for another (--water-reflection).
			const int traceScale = config.HasWaterReflectionOverride
									   ? config.WaterReflectionScale
									   : rtPreset.ReflectionScale;
			if (traceScale > 1 && waterSurface != kRGInvalid)
			{
				// Declared outside so the water draw below can sample it.
				RGTargetDesc traceDesc;
				traceDesc.Name = "WaterReflection";
				traceDesc.Color = Format::R16G16B16A16_SFLOAT;
				traceDesc.Depth = Format::Undefined;
				traceDesc.Scale = (float)supersample / (float)traceScale;
				waterTraced = graph.CreateTarget(traceDesc);
				const RGResource traced = waterTraced;

				graph.AddPass("WaterReflection",
					[&](RGPassBuilder& builder)
					{
						builder.Write(traced);
						builder.Sample(waterSurface);
						builder.DisableDepth();
					},
					[waterSurface, traceScale](RGPassContext& context)
					{
						Renderer3D::TraceWaterReflection(
							context.Color(waterSurface, 0),
							context.Color(waterSurface, 1),
							context.Color(waterSurface, 2),
							(float)traceScale);
					});
			}

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

					if (waterBackdrop != kRGInvalid)
						builder.Sample(waterBackdrop);
					if (waterLamps != kRGInvalid)
						builder.Sample(waterLamps);
					if (waterTraced != kRGInvalid)
						builder.Sample(waterTraced);
				},
				[draw = desc.DrawTransparent, waterBackdrop, waterLamps,
				 waterTraced](RGPassContext& context)
				{
					// Handed over around the draw and taken back after it, the
					// ScreenReflections shape: the renderer must not carry a
					// texture the pool may hand to somebody else next frame.
					if (waterBackdrop != kRGInvalid)
						Renderer3D::SetWaterBackdrop(context.Color(waterBackdrop, 0),
													 context.Color(waterBackdrop, 1));
					if (waterLamps != kRGInvalid)
						Renderer3D::SetWaterLamps(context.Color(waterLamps, 0),
												  context.Color(waterLamps, 1));
					if (waterTraced != kRGInvalid)
						Renderer3D::SetWaterReflection(context.Color(waterTraced));
					draw(context);
					Renderer3D::SetWaterReflection(nullptr);
					Renderer3D::SetWaterLamps(nullptr, nullptr);
					Renderer3D::SetWaterBackdrop(nullptr, nullptr);
				});

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
		// RT-6.1: the composite's velocity lane, when it ran.
		RGResource reflectionMotion = kRGInvalid;
		// --- the opaque glossy reflection, traced and averaged -----------------
		//
		// **RT-6.1: before the temporal resolve, and it hands the resolve the
		// motion to reproject by.** The chain reads only the G-buffer and the
		// scene's structure, never the lit colour, so it lifts above the resolve
		// cleanly; the tile map it reads becomes last frame's, as RT-2's
		// occlusion and RT-3's bounce already read it. Moving it without the
		// motion lane was tried on 2026-09-07 and smeared the wet floor into
		// horizontal bands, because the resolve dragged the reflection along the
		// floor's velocity; the composite now says which motion each pixel has.
		// What it writes is still read a frame late through the screen-reflection
		// hook (reflection_trace.rvshader), which this does not affect.
		// Gated on the traced form being on; inside, a pixel with no glossy
		// surface casts nothing, which is the check a scene with none passes.
		if (tracedReflections && currentReflections != kRGInvalid)
		{
			RGTargetDesc traceDesc;
			traceDesc.Name = "ReflectionTrace";
			traceDesc.Color = Format::R16G16B16A16_SFLOAT;
			// The ray's direction and pdf, for the resolve's ratio estimator.
			traceDesc.ExtraColors = { Format::R16G16B16A16_SFLOAT };
			traceDesc.Depth = Format::Undefined;
			traceDesc.Scale = (float)supersample;
			const RGResource traced = graph.CreateTarget(traceDesc);
			const bool budgetBound = budgetPrevious != kRGInvalid && budgetHasHistory;

			graph.AddPass("ReflectionTrace",
				[&](RGPassBuilder& builder)
				{
					builder.Write(traced);
					builder.Sample(sceneHDR);
					if (budgetBound)
						builder.Sample(budgetPrevious);
					builder.DisableDepth();
				},
				[sceneHDR, normalIndex, albedoIndex, budgetMap = budgetPrevious, budgetBound,
				 giAverage = rtPreset.GiRays](RGPassContext& context)
				{
					Renderer3D::TraceReflections(context.Color(sceneHDR, normalIndex),
												 context.Depth(sceneHDR),
												 budgetBound ? context.Color(budgetMap) : nullptr,
												 context.Color(sceneHDR, albedoIndex),
												 giAverage);
				});

			// The rough surfaces' rays, shared across their neighbourhood
			// before any frame is averaged (reflection_resolve.rvshader).
			RGTargetDesc resolveDesc = traceDesc;
			resolveDesc.Name = "ReflectionResolve";
			resolveDesc.ExtraColors.clear();
			const RGResource resolved = graph.CreateTarget(resolveDesc);
			graph.AddPass("ReflectionResolve",
				[&](RGPassBuilder& builder)
				{
					builder.Write(resolved);
					builder.Sample(traced);
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[traced, sceneHDR, normalIndex](RGPassContext& context)
				{
					Renderer3D::ResolveReflections(context.Color(traced),
												   context.Color(traced, 1),
												   context.Depth(sceneHDR),
												   context.Color(sceneHDR, normalIndex));
				});
			// The previous frame's picture, surface and moments, when there is one.
			// **The reconstruction contract (RT-first T4).** The accumulate and
			// the three young-history blurs are one helper for any signal:
			// reflections here; shadows, occlusion and irradiance to follow.
			const bool reflectionHistory = desc.Reflections && desc.Reflections->HasHistory() && previousReflections != kRGInvalid;
			RGTargetDesc reflectionBlurDesc = traceDesc;
			reflectionBlurDesc.Name = "ReflectionBlurred";
			static const SignalPassNames kReflectionPasses = { "ReflectionAccumulate", { "ReflectionBlur", "ReflectionBlur2", "ReflectionBlur4" } };
			const RGResource blurredReflections = addSignal(kReflectionPasses, Renderer3D::ReflectionSignal(),
															resolved, currentReflections, previousReflections,
															reflectionHistory, &desc.Reflections->Motion(),
															reflectionBlurDesc, false);
			{
				const RGResource blurred = blurredReflections;

				RGTargetDesc compositeDesc;
				compositeDesc.Name = "ReflectionComposited";
				compositeDesc.Color = Format::R16G16B16A16_SFLOAT;
				// RT-6.1: and the velocity the resolve reprojects by, per pixel.
				compositeDesc.ExtraColors = { Format::R16G16_SFLOAT };
				compositeDesc.Depth = Format::Undefined;
				const RGResource composited = graph.CreateTarget(compositeDesc);
				const RGResource before = shaded;
				graph.AddPass("ReflectionComposite",
					[&](RGPassBuilder& builder)
					{
						builder.Write(composited);
						builder.Sample(before);
						builder.Sample(blurred);
						builder.Sample(currentReflections);
						builder.Sample(sceneHDR);
						builder.DisableDepth();
					},
					[before, blurred, currentReflections, sceneHDR, velocityIndex]
					(RGPassContext& context)
					{
						PostProcess::ReflectionComposite(context.Cmd, context.Color(before),
														 context.Color(blurred),
														 Format::R16G16B16A16_SFLOAT,
														 // RT-6.1: the accumulator's fourth lane and
														 // the scene's, and the lane it writes.
														 context.Color(currentReflections, 3),
														 context.Color(sceneHDR, velocityIndex),
														 Format::R16G16_SFLOAT);
					});
				shaded = composited;
				// The resolve reads this instead of the scene's lane.
				reflectionMotion = composited;
			}

			// Swapped here for the reason the SSR chain swaps: what was written
			// this frame is what the next frame reads.
			desc.Reflections->Advance();
		}

		// This frame's temporal history, for the debug view's confidence
		// map: attachment 1 of it carries the validity lane (WR-16 S0).
		RGResource temporalCurrent = kRGInvalid;
		if (wantTemporal)
		{
			TemporalHistory& history = *desc.History;
			// **A second attachment, for what the resolve remembers about
			// each pixel**: frames accumulated and the first two luminance
			// moments of the arriving sample, which is what keeps the
			// neighbourhood clamp from rejecting sub-pixel detail every frame
			// the jitter misses it. Ping-ponged with the colour by the same
			// pair, so the two cannot get out of step. See taa_resolve.
			history.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,
							Format::R16G16B16A16_SFLOAT, "TemporalHistory",
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
				temporalCurrent = current;
				const RGResource source = shaded;
				const float feedback = desc.Render.TemporalFeedback;
				const float stillFeedback = desc.Render.TemporalStillFeedback;
				const bool hasHistory = history.HasHistory();

				graph.AddPass("TAA resolve",
					[&](RGPassBuilder& builder)
					{
						builder.Write(current);
						builder.Sample(source);
						builder.Sample(sceneHDR);
						builder.Sample(previous);
						if (reflectionMotion != kRGInvalid)
							builder.Sample(reflectionMotion);
						if (taaGuideCurrent != kRGInvalid)
						{
							builder.Sample(taaGuideCurrent);
							builder.Sample(taaGuidePrevious);
						}
						// RT-8: the sea's own motion, which this pass prefers over the
						// geometry's wherever a wave covered the pixel. Declared here
						// as well as passed below: without the declaration the graph
						// hands the pass nothing and the branch reads black, which is
						// a mask of zero and so silently no water at all.
						if (waterSurface != kRGInvalid)
							builder.Sample(waterSurface);
						builder.DisableDepth();
					},
					[source, sceneHDR, previous, velocityIndex, normalIndex, feedback, stillFeedback,
					 hasHistory, jitter, taaGuideCurrent, taaGuidePrevious, taaGuideHasHistory,
					 boxGeometry = config.TaaBoxGeometry,
					 reflectionMotion, waterSurface](RGPassContext& context)
					{
						PostProcess::TemporalResolve(
							context.Cmd,
							context.Color(source),
							context.Color(previous),
							// **RT-6.1: the composite's lane where the reflection ran**,
							// which is the scene's velocity everywhere the pixel is
							// mostly its surface and the virtual image's where it is
							// mostly reflection. Otherwise the scene's own attachment,
							// which is the same target `source` is when SSAA is off --
							// and SSAA and TAA cannot both be on.
							reflectionMotion != kRGInvalid
								? context.Color(reflectionMotion, 1)
								: context.Color(sceneHDR, velocityIndex),
							context.Width, context.Height,
							Format::R16G16B16A16_SFLOAT, feedback, hasHistory,
							// Attachment 1 of the same history: last frame's
							// count and moments.
							hasHistory ? context.Color(previous, 1) : nullptr,
							Format::R16G16B16A16_SFLOAT, jitter, stillFeedback,
							// RT-6: the identity lanes. Only once the pair holds a real
							// frame -- on the first frame `Previous` is whatever the
							// driver left, and comparing against that refuses every
							// pixel, which would look like the resolve having stopped.
							taaGuideCurrent != kRGInvalid && taaGuideHasHistory
								? context.Color(taaGuideCurrent) : nullptr,
							taaGuidePrevious != kRGInvalid && taaGuideHasHistory
								? context.Color(taaGuidePrevious) : nullptr,
							// RT-6.2: the material under each pixel -- the G-buffer's
							// normal attachment, whose B and A are the roughness and the
							// metallic that decide how far the clamp may open.
							// **From the G-buffer, not from `source`** -- since RT-6.1 put the
							// reflection composite above this pass, `source` is the composite's
							// target and carries a colour and a motion lane and nothing else.
							// A G-buffer lane index into it reads nothing at all, which is a
							// black material and a clamp that never opens -- measured
							// bit-identical, at 2 and at 40, before this was corrected.
							context.Color(sceneHDR, normalIndex),
							// RT-6.8: and whether the box may be built from this
							// surface's taps alone.
							boxGeometry,
							// **RT-8: the water layer's motion.** Attachment 3 of the sea's
							// own surface pass: where a wave covers a pixel that is the
							// motion to reproject by, and the geometry lane under it holds
							// the seabed instead.
							waterSurface != kRGInvalid ? context.Color(waterSurface, 3)
													   : nullptr);
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
		// **The most rays a tile may be given, stated once.** The allocator
		// clamps to it and the debug view's ramp tops out at it. They were
		// written separately -- 16 at the call below, AoAverage * spread (24
		// at the defaults) in the ramp -- so the map's top third was
		// unreachable and a tile handed the maximum read as two thirds of the
		// way up the scale. A map that cannot show the ceiling is a map that
		// hides the clamp binding, which is exactly when one is worth reading.
		constexpr float kTileRayCeiling = 16.0f;

		RGResource rayBudgetMap = kRGInvalid;
		bool hasRayBudget = false;

		if (desc.RayBudget && PostProcess::IsReady())
		{
			// Sixteen: one number per 256 pixels. Small enough that a wave never
			// straddles two allocations, large enough that the map is a few
			// thousand texels rather than a few million.

			TemporalHistory& budget = *desc.RayBudget;

			if (budget.Current() && budget.Previous())
			{
				const bool hasHistory = budgetHasHistory;
				// Imported above the G-buffer pass (RT-2), where the occlusion
				// signal reads last frame's allocation.
				const RGResource previous = budgetPrevious;
				const RGResource current = budgetCurrent;

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

				// The stillness levers, the preset's unless a measurement run
				// says otherwise. Resolved here rather than in the capture so
				// the override is read once, where it can be seen.
				const float deadBand = config.HasTileDeadBandOverride
										 ? config.TileDeadBandOverride
										 : desc.Render.RayBudgetDeadBand;
				const float dwell = config.HasTileDwellOverride
									  ? config.TileDwellOverride
									  : desc.Render.RayBudgetDwell;
				const float smoothing = config.HasTileSmoothOverride
										  ? config.TileSmoothOverride
										  : desc.Render.RayBudgetImportanceSmoothing;

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
					 aoAverage = rtPreset.AoRays,
					 giAverage = rtPreset.GiRays,
					 spread = rtPreset.Spread,
					 deadBand, dwell, smoothing](RGPassContext& context)
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
							// The distance a target must move before a whole number
							// follows it, and the frames it must then sit still. Not
							// an easing -- see the shader's note, and the one below on
							// why the line that feeds it back had to wait for this.
							deadBand,
							kTileRayCeiling, kTileRayCeiling,
							dwell, smoothing,
							Format::R16G16B16A16_SFLOAT);
					});

				rayBudgetMap = current;
				hasRayBudget = true;

				// **`budget.Advance()`, restored** (WR-16 S3). Without it m_Valid
				// never becomes true, HasHistory() above is permanently false, the
				// previous map is imported and never sampled, and every tile's counts
				// are re-derived from scratch every frame. On paper that was always a
				// bug -- and the line was still enabled and reverted three times, on
				// three different versions of the surrounding code, making the picture
				// worse every time. The last was 2026-08-29, after the allocator's
				// inputs, the GI resolution, the runtime cache and the denoiser had all
				// changed, which was the argument for trying again.
				//
				// **The fault was never this line; it was what the history fed.** An
				// easing of one ray a frame into a `floor(x + 0.5)` is a damped
				// integrator feeding a quantiser, the classic shape for a slow limit
				// cycle, and this engine has already met one breathing at about a hertz
				// (importance_tiles.rvshader:46-52). Damping the input to a stair does
				// not stop the stair being climbed; it makes the climbing rhythmic.
				// Frame time was untouched either way (4.38 vs 4.44 ms, inside this
				// machine's drift), so there was never anything on the other side of
				// the trade to weigh the flicker against.
				//
				// **So the shape was fixed first.** tile_budget now holds a whole
				// number until the continuous target has moved a whole dead band away
				// from it, takes the rounded target in one step when it has, and may
				// not step again for the dwell -- RAY-BUDGET-DESIGN 4.2, and the
				// shader's own note on why the band's 0.75 makes a step unable to
				// provoke the step back. The judge is
				// `tools/scripts/tile_transitions.py`: how often a tile changes its
				// allocation with the camera and the scene still, where the honest
				// answer is "almost never" and the bar is 0.01 changes per tile per
				// second.
				budget.Advance();
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
		// Only when the signal did not run: with `--gi-signal=on` the trace and
		// its accumulation happen above the lit pass and this whole chain --
		// the trace, gi_denoise, and the one frame of latency they carry -- is
		// what RT-3 replaced. Kept whole as the reference arm.
		else if (wantIndirect && rayGi && !giSignal && currentIndirect != kRGInvalid
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
				 rays = RayDetailRays(giDetail)]
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
		// Under the occlusion signal (RT-2) the lit shader has applied it already;
		// this post chain is the old path, kept as the A/B (--ao-signal=off).
		if (aoLevel != AoDetail::Off && PostProcess::IsReady() && !aoSignal)
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
			// **Nothing runs occlusion at full resolution any more.** It is a
			// low-frequency term multiplied into lighting, it now has a
			// temporal filter behind it and a depth-aware upsample in front,
			// and the ray-traced form additionally interleaves its sub-position
			// so four frames cover the full-resolution grid anyway. Measured on
			// the showroom at 2560x1440: 1.86 -> 0.47 ms on the pass and
			// 154 -> 199 FPS on the frame.
			const uint32_t divisor = rayOcclusion                  ? 2u
								   : aoLevel == AoDetail::Full     ? 2u
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
				// The same second attachment the TAA history carries: the
				// resolve they share writes it on every path, and occlusion
				// is exactly the kind of noisy input the temporal floor was
				// built for.
				occlusion.Prepare(Renderer::GetDevice(), halfWidth, halfHeight,
								  Format::R16G16B16A16_SFLOAT, "Occlusion",
								  Format::R16G16B16A16_SFLOAT);

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
						 halfWidth, halfHeight, has = aoHasHistory, jitter](RGPassContext& context)
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
								Format::R16G16B16A16_SFLOAT, 0.9f, has,
								has ? context.Color(previousAo, 1) : nullptr,
								Format::R16G16B16A16_SFLOAT, jitter, 0.0f);
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
					// The upsample weighs each half-resolution tap against this
					// pixel's own surface, so it reads the frame's depth too.
					builder.Sample(sceneHDR);
					builder.DisableDepth();
				},
				[lit, blurred, sceneHDR, intensity, halfWidth, halfHeight,
				 nearZ = desc.NearClip, farZ = desc.FarClip](RGPassContext& context)
				{
					PostProcess::SsaoApply(context.Cmd, context.Color(lit),
										   context.Color(blurred),
										   context.Depth(sceneHDR),
										   halfWidth, halfHeight, nearZ, farZ,
										   intensity,
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
			fogSettings.Floor = desc.Post.FogFloor;
			fogSettings.MaxOpacity = desc.Post.FogMaxOpacity;
			// WR-3. The two dials are the profile's; the two sky terms are the
			// scene's, and they have to come from the environment rather than
			// the profile because they describe the sky being drawn, not the
			// grade -- a camera that changed SkyIntensity would be fogging
			// toward a sky nobody rendered.
			fogSettings.SkyAffect = desc.Post.FogSkyAffect;
			fogSettings.SkyOcclusion = desc.Post.FogSkyOcclusion;
			fogSettings.SkyIntensity = desc.Environment.SkyIntensity;
			fogSettings.SkyRotation = desc.Environment.SkyRotation;

			// **A flat background is not a sky.** SkyType::Color draws nothing
			// at all, so there is no colour in any direction to take -- and
			// the cube the scene would hand over for it is black, which as an
			// inscatter colour is worse than the constant it replaced.
			const Ref<RHITexture> skyCube =
				desc.Environment.Sky == SkyType::Color ? nullptr : desc.SkyCube;

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
				[clear, sceneHDR, fog = fogSettings, fogView, skyCube](RGPassContext& context)
				{
					PostProcess::Fog(context.Cmd, context.Color(clear),
									 context.Depth(sceneHDR), fog, fogView,
									 Format::R16G16B16A16_SFLOAT, skyCube);
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
		// And under the debug view (WR-16 S0), which draws the output last
		// from the tone-mapped frame and cannot sample the target it writes.
		if (wantAA || debugView)
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

		// --- the debug view, over everything but the UI (WR-16 S0) ---------------
		//
		// The output written again, from the tone-mapped frame and one number
		// per pixel: the rays or lights the lit shaders counted into the debug
		// buffer, the temporal resolve's validity lane, or the ray budget's
		// tile map. Whatever anti-aliasing wrote into the output before this
		// is replaced; a heat map is not a picture to smooth.
		if (debugView)
		{
			const EngineConfig::DebugViewMode view = config.DebugView;
			const int mode = (int)view - 1;
			const Format format = desc.OutputFormat;
			// The composite declares the count buffer whichever mode runs,
			// and a declared binding is filled or the draw is undefined: the
			// texture-backed modes, which never read it, bind the frame's
			// ray counters in its place.
			const Ref<RHIBuffer> counts = debugCounts ? Renderer3D::DebugCountsBuffer()
													  : RayCounters::Buffer();
			// The ramp's top: rays saturate at the busiest cluster's worth of
			// shadow rays plus a few, lights at the busiest cluster, the
			// allocation at three times the AO average (RayBudgetSpread's
			// ceiling); confidence is two colours and needs no scale.
			// **RT-12: one place per view.** Where its number comes from, which
			// attachment and channel it is, and how it is displayed -- together,
			// because they were four parallel ternary chains and a hardcoded
			// ordinal in the shader, and three of fourteen views had drifted out
			// of agreement without anything failing.
			//
			// Display: 0 a ramp, 1 the picture, 2 two colours, 3 a vector in rg,
			// 4 an octahedral normal. Channel: 0 r, 1 g, 2 b, 3 a, 4 the standard
			// deviation the moments in g and b describe.
			struct ViewSpec
			{
				float       Scale = 1.0f;
				RGResource  Aux = kRGInvalid;
				// RT-12 §11: the same attachment one frame back, for the
				// direction difference. Every other view leaves it invalid.
				RGResource  AuxPrev = kRGInvalid;
				uint32_t    Attachment = 0u;
				int         Display = 0;
				int         Channel = 3;
				bool        FromCounts = false;
				const char* Name = "";
				const char* Missing = "";
			};

			const float busiest = (float)Math::Max(Renderer3D::GetMaxCellLoad(), 1u);
			const RGResource reflectionAux = tracedReflections ? currentReflections
															   : kRGInvalid;
			static constexpr const char* kMissingReflection = "the traced reflection pass is off";
			static constexpr const char* kMissingDirect = "the direct-light signal is off (--direct-signal)";
			static constexpr const char* kMissingGi = "the GI signal is off (--gi-signal, or the scene bakes its GI)";
			static constexpr const char* kMissingAo = "the occlusion signal is off (--ao-signal)";
			static constexpr const char* kMissingBudget = "the ray budget's tile allocator is off";
			static constexpr const char* kMissingTaa = "the temporal resolve runs under TAA only";
			static constexpr const char* kMissingWater = "this scene has no water";

			ViewSpec spec;
			switch (view)
			{
			case EngineConfig::DebugViewMode::Rays:
				spec.Scale = busiest + 8.0f; spec.FromCounts = true; spec.Name = "rays";
				break;
			case EngineConfig::DebugViewMode::Lights:
				spec.Scale = busiest; spec.FromCounts = true; spec.Name = "lights";
				break;
			// The resolve's validity as two colours: kept or refused. Zero means
			// kept, in the accumulator's encoding, which RT-12 gave the resolve
			// too -- so this is now the same question as taa-refusal asked as a
			// yes or no.
			case EngineConfig::DebugViewMode::Confidence:
				spec.Aux = temporalCurrent; spec.Attachment = 1; spec.Channel = 3;
				spec.Display = 2; spec.Name = "confidence"; spec.Missing = kMissingTaa;
				break;
			// **RT-12: and the same lane as a ramp, which is the new part.** The
			// clause that refused the reprojected texel, 0..6, plus a half where
			// the nine-tap search found nothing either.
			case EngineConfig::DebugViewMode::TaaRefusal:
				spec.Aux = temporalCurrent; spec.Attachment = 1; spec.Channel = 3;
				spec.Scale = 7.0f; spec.Name = "taa-refusal"; spec.Missing = kMissingTaa;
				break;
			case EngineConfig::DebugViewMode::Importance:
				spec.Aux = rayBudgetMap; spec.Channel = 0;
				spec.Scale = Math::Min(rtPreset.AoRays * Math::Max(rtPreset.Spread, 1.0f),
									   kTileRayCeiling);
				spec.Name = "importance"; spec.Missing = kMissingBudget;
				break;
			case EngineConfig::DebugViewMode::GiImportance:
				spec.Aux = rayBudgetMap; spec.Channel = 1;
				spec.Scale = Math::Min(rtPreset.GiRays * Math::Max(rtPreset.Spread, 1.0f),
									   kTileRayCeiling);
				spec.Name = "importance-gi"; spec.Missing = kMissingBudget;
				break;
			// The reflection accumulator's own four, unchanged in meaning.
			case EngineConfig::DebugViewMode::Reflection:
				spec.Aux = reflectionAux; spec.Attachment = 0; spec.Channel = 3;
				spec.Scale = Math::Max(Renderer3D::ReflectionSignal().Memory, 1.0f);
				spec.Name = "reflection"; spec.Missing = kMissingReflection;
				break;
			case EngineConfig::DebugViewMode::ReflectionImage:
				spec.Aux = reflectionAux; spec.Attachment = 1; spec.Channel = 3;
				spec.Scale = 20.0f; spec.Name = "reflection-image";
				spec.Missing = kMissingReflection;
				break;
			case EngineConfig::DebugViewMode::ReflectionChoice:
				spec.Aux = reflectionAux; spec.Attachment = 2; spec.Channel = 3;
				spec.Scale = 6.0f; spec.Name = "reflection-choice";
				spec.Missing = kMissingReflection;
				break;
			case EngineConfig::DebugViewMode::ReflectionPicture:
				spec.Aux = reflectionAux; spec.Attachment = 0; spec.Display = 1;
				spec.Scale = 4.0f; spec.Name = "reflection-picture";
				spec.Missing = kMissingReflection;
				break;
			// RT-12: new. The stored reflector normal, and the virtual image's
			// motion in the velocity lane's units -- RT-6.1 has written the
			// second since it landed and nothing has ever looked at it.
			case EngineConfig::DebugViewMode::ReflectionNormal:
				spec.Aux = reflectionAux; spec.Attachment = 1; spec.Display = 4;
				spec.Name = "reflection-normal"; spec.Missing = kMissingReflection;
				break;
			// **§11.** The direction the accumulator stores octahedrally in
			// the motion lane's spare channels, and the swing since last
			// frame against the previous history. Both need the reflection
			// pass; the difference needs a previous frame as well, so it
			// reports "no source" rather than differencing against black.
			case EngineConfig::DebugViewMode::ReflectionDirection:
				spec.Aux = reflectionAux; spec.Attachment = 3; spec.Display = 5;
				spec.Name = "reflection-direction"; spec.Missing = kMissingReflection;
				break;
			case EngineConfig::DebugViewMode::ReflectionDirectionDelta:
				spec.Aux = (tracedReflections && previousReflections != kRGInvalid)
						 ? currentReflections : kRGInvalid;
				spec.AuxPrev = previousReflections; spec.Attachment = 3;
				spec.Display = 6; spec.Scale = 0.1f;
				spec.Name = "reflection-direction-delta";
				spec.Missing = kMissingReflection;
				break;
			// **RT-8: the water's own layer**, attachment 3 of the sea's surface
			// pass: xy the wave's screen motion, z the mask. Drawn about grey
			// like the reflection's motion, and at the same scale, so the two
			// read the same way. "No source" where the scene has no water at
			// all, which is a different statement from a black frame.
			case EngineConfig::DebugViewMode::WaterMotion:
				spec.Aux = waterSurface; spec.Attachment = 3; spec.Display = 3;
				spec.Scale = 0.02f; spec.Name = "water-motion";
				spec.Missing = kMissingWater;
				break;
			// The mask alone, as a number: one where the layer holds a wave.
			// The first thing to ask when the sea behaves as though its layer
			// were empty -- as it did on 2026-09-08, when it was.
			case EngineConfig::DebugViewMode::WaterMask:
				spec.Aux = waterSurface; spec.Attachment = 3; spec.Channel = 2;
				spec.Scale = 1.0f; spec.Name = "water-mask";
				spec.Missing = kMissingWater;
				break;
			case EngineConfig::DebugViewMode::ReflectionMotion:
				spec.Aux = reflectionAux; spec.Attachment = 3; spec.Display = 3;
				spec.Scale = 0.02f; spec.Name = "reflection-motion";
				spec.Missing = kMissingReflection;
				break;
			// **The three that were wrong.** direct-light and ao rendered the
			// frame count instead of the picture, and direct-refusal rendered raw
			// radiance instead of its refusal ramp.
			case EngineConfig::DebugViewMode::DirectLight:
				spec.Aux = currentDirect; spec.Attachment = 0; spec.Display = 1;
				spec.Scale = 64.0f; spec.Name = "direct-light";
				spec.Missing = kMissingDirect;
				break;
			case EngineConfig::DebugViewMode::DirectRefusal:
				spec.Aux = currentDirect; spec.Attachment = 2; spec.Channel = 3;
				spec.Scale = 6.0f; spec.Name = "direct-refusal";
				spec.Missing = kMissingDirect;
				break;
			case EngineConfig::DebugViewMode::Occlusion:
				spec.Aux = currentOcclusion; spec.Attachment = 0; spec.Display = 1;
				spec.Scale = 1.0f; spec.Name = "ao"; spec.Missing = kMissingAo;
				break;
			case EngineConfig::DebugViewMode::AoRefusal:
				spec.Aux = currentOcclusion; spec.Attachment = 2; spec.Channel = 3;
				spec.Scale = 6.0f; spec.Name = "ao-refusal"; spec.Missing = kMissingAo;
				break;
			case EngineConfig::DebugViewMode::GiLight:
				spec.Aux = currentGi; spec.Attachment = 0; spec.Display = 1;
				spec.Scale = 1.0f; spec.Name = "gi-light"; spec.Missing = kMissingGi;
				break;
			case EngineConfig::DebugViewMode::GiRefusal:
				spec.Aux = currentGi; spec.Attachment = 2; spec.Channel = 3;
				spec.Scale = 6.0f; spec.Name = "gi-refusal"; spec.Missing = kMissingGi;
				break;
			case EngineConfig::DebugViewMode::ReflectionSigma:
				spec.Aux = reflectionAux; spec.Attachment = 2; spec.Channel = 4;
				spec.Scale = 0.25f; spec.Name = "reflection-sigma"; spec.Missing = kMissingReflection;
				break;
			case EngineConfig::DebugViewMode::DirectHistory:
				spec.Aux = currentDirect; spec.Attachment = 0; spec.Channel = 3;
				spec.Scale = Math::Max(Renderer3D::DirectSignal().Memory, 1.0f);
				spec.Name = "direct-history"; spec.Missing = kMissingDirect;
				break;
			case EngineConfig::DebugViewMode::DirectSigma:
				spec.Aux = currentDirect; spec.Attachment = 2; spec.Channel = 4;
				spec.Scale = 0.25f; spec.Name = "direct-sigma"; spec.Missing = kMissingDirect;
				break;
			case EngineConfig::DebugViewMode::GiHistory:
				spec.Aux = currentGi; spec.Attachment = 0; spec.Channel = 3;
				spec.Scale = Math::Max(Renderer3D::GiSignal().Memory, 1.0f);
				spec.Name = "gi-history"; spec.Missing = kMissingGi;
				break;
			case EngineConfig::DebugViewMode::GiSigma:
				spec.Aux = currentGi; spec.Attachment = 2; spec.Channel = 4;
				spec.Scale = 0.25f; spec.Name = "gi-sigma"; spec.Missing = kMissingGi;
				break;
			case EngineConfig::DebugViewMode::AoHistory:
				spec.Aux = currentOcclusion; spec.Attachment = 0; spec.Channel = 3;
				spec.Scale = Math::Max(Renderer3D::AoSignal().Memory, 1.0f);
				spec.Name = "ao-history"; spec.Missing = kMissingAo;
				break;
			case EngineConfig::DebugViewMode::AoSigma:
				spec.Aux = currentOcclusion; spec.Attachment = 2; spec.Channel = 4;
				spec.Scale = 0.10f; spec.Name = "ao-sigma"; spec.Missing = kMissingAo;
				break;
			default:
				break;
			}

			const float scale = spec.Scale;
			const RGResource auxResource = spec.Aux;
			const RGResource auxPrevResource = spec.AuxPrev;
			const uint32_t auxAttachment = spec.Attachment;
			// Said once: a view whose source is not running draws a dark map,
			// and the log should say why rather than leave it to be guessed.
			// RT-12: the name and the reason come from the view's own row, so a
			// new view cannot be described as whichever one the chain ended on --
			// which is what happened before RT-3 added two of its own.
			static EngineConfig::DebugViewMode s_Said = EngineConfig::DebugViewMode::None;
			if (!spec.FromCounts && auxResource == kRGInvalid && s_Said != view)
			{
				s_Said = view;
				RV_CORE_WARN("Debug view: {0} has no source this frame ({1}); the map stays dark",
							 spec.Name, spec.Missing);
			}

			if (debugCounts && counts)
			{
				// The counts were written by the scene passes' fragments;
				// the composite reads them.
				graph.AddStandalonePass("Debug view sync",
					[&](RGPassBuilder&) {},
					[counts](RGPassContext& context)
					{
						context.Cmd.BufferBarrier(counts, BufferSync::ShaderWrite,
												  BufferSync::ShaderRead);
					});
			}

			graph.AddPass("Debug view",
				[&](RGPassBuilder& builder)
				{
					builder.Write(desc.Output);
					builder.Sample(tonemapped);
					if (auxResource != kRGInvalid)
						builder.Sample(auxResource);
					if (auxPrevResource != kRGInvalid)
						builder.Sample(auxPrevResource);
					builder.DisableDepth();
				},
				[tonemapped, auxResource, auxPrevResource, auxAttachment, counts,
				 mode, scale, format,
				 display = spec.Display, channel = spec.Channel,
				 fromCounts = spec.FromCounts, logRamp = config.DebugViewLog,
				 frameMix = config.DebugViewMix]
				(RGPassContext& context)
				{
					PostProcess::DebugView(context.Cmd, context.Color(tonemapped),
										   auxResource != kRGInvalid
											   ? context.Color(auxResource, auxAttachment) : nullptr,
										   auxPrevResource != kRGInvalid
											   ? context.Color(auxPrevResource, auxAttachment) : nullptr,
										   counts, mode, scale, frameMix,
										   display, channel, fromCounts, logRamp, format);
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
