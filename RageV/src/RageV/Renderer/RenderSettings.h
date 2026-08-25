#pragma once
#include <cstdint>
#include "RageV/Renderer/PostSettings.h"

namespace RageV
{
	// How the image is anti-aliased.
	//
	//   FXAA looks at one pixel's neighbourhood and guesses. One pass, no
	//   prerequisites, and it softens the picture slightly -- which is its
	//   well-known cost and the reason it was first rather than best.
	//
	//   SMAA reconstructs the silhouette instead: it finds the run of pixels
	//   an edge spans, works out which way the real line sloped, and computes
	//   the coverage the rasterizer should have produced. Three passes over
	//   two small intermediates, and sharper for it. ENGINE-NOTES 7n.
	//
	//   MSAA takes several coverage samples per pixel and keeps a depth for
	//   each, but runs the fragment shader **once**. So geometry edges get
	//   several levels of coverage for close to the price of one shaded
	//   pixel -- and shading aliasing gets nothing at all, because a specular
	//   highlight is one shaded sample either way. It resolves in linear
	//   light, before the tone curve, which is the correct place and a visible
	//   difference on high-contrast edges. ENGINE-NOTES 7q.
	//
	//   SSAA renders the whole scene larger and averages it down. It is the
	//   only one of the three that anti-aliases *shading* rather than
	//   geometry: a specular highlight sparkling across a curved surface, or a
	//   texture aliasing into moire, is a signal the frame never sampled
	//   finely enough, and no filter working on the finished image can invent
	//   what was missed. It costs the square of the factor in fill.
	//
	//   TAA is the only one that is not a function of the frame it runs in.
	//   It offsets the projection by a different fraction of a pixel every
	//   frame and accumulates the results, which converges on a supersampled
	//   image for the cost of one sample -- and puts every one of its costs
	//   into the same place: what happens when the accumulation is wrong,
	//   because something moved. ENGINE-NOTES 7r.
	//
	//   **TAA is finished, and was measured in motion.** A scene with one
	//   textured patch falling sixteen pixels a frame beside an identical
	//   still one, judged against a supersampled render of the same instant,
	//   settled the two claims that used to rest on argument: the
	//   reprojection's vertical sign is right (19.8 RMS as shipped against
	//   29.9 inverted, both backends), and the default feedback was wrong
	//   -- see TemporalFeedback below for the curve that replaced it.
	enum class AntiAliasing : uint32_t
	{
		None = 0,
		FXAA = 1,
		SMAA = 2,
		SSAA = 3,
		MSAA = 4,
		TAA  = 5,
	};

	// What the frame costs to render, as opposed to what it looks like.
	//
	// **These belong to the project**, not to a scene and not to a camera.
	// They are a judgement about the hardware -- how many samples it can
	// afford, how much depth-map memory to spend on shadows -- and that
	// judgement does not change because a different level loaded. Storing
	// them per scene meant a project with forty scenes stored its shadow
	// resolution forty times and changed it forty times. ENGINE-NOTES 7s.
	//
	// Two things layer on top, both narrower and both about *this machine*
	// rather than this project: the `AntiAliasing` key in `ragev.ini`, which
	// is what a viewing preference needs to survive a restart without saving
	// a project file, and `--aa=` on the command line, which is what the
	// checks use to render the same scene six ways. `ResolveAntiAliasing`
	// applies both, in one place, so nothing else has to know they exist.
	// ---------------------------------------------------------------------
	// Sample counts
	// ---------------------------------------------------------------------
	//
	// **A sample count is a bit flag, not a number.** `VkSampleCountFlagBits`
	// is 1, 2, 4, 8, 16, 32, 64 -- one bit each -- and OpenGL's is the same
	// set by another name. Five is not "one more than four", it is
	// `VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT`: two bits, which is not
	// a value the enum has.
	//
	// This mattered. The count used to be a slider from 1 to 8, so five was
	// two pixels of mouse travel away, and it did not fail cleanly: every
	// graphics pipeline failed to create with
	// `VUID-VkPipelineMultisampleStateCreateInfo-rasterizationSamples-parameter`,
	// the frame was recorded against pipelines that did not exist, and the
	// submit took the **device** down -- an INVALID WRITE at address 0, and
	// the application closing because a lost device cannot be recovered.
	// OpenGL, meanwhile, quietly rounded five up to eight and rendered, so the
	// same project worked on one backend and killed the other.
	//
	// Hence: the legal set is stated once, here, and everything that can
	// produce a sample count goes through the function below.
	inline constexpr int MsaaCounts[] = { 2, 4, 8 };

	// The nearest legal count at or below `requested`, and never above
	// `deviceMax`.
	//
	// Rounds **down** rather than to the nearest, because every step up is a
	// step up in bandwidth and target memory: somebody who asked for five and
	// silently got eight paid for something they did not choose. Five becomes
	// four, which is the largest thing they asked for that exists.
	//
	// `deviceMax` is RHI::DeviceCaps::MaxSampleCount -- what the physical
	// device reports for the scene target's colour, depth, and being sampled.
	// Zero means "not known yet", which answers the smallest legal count
	// rather than guessing at the hardware.
	constexpr int SanitiseMsaaSamples(int requested, uint32_t deviceMax)
	{
		int best = MsaaCounts[0];
		for (int count : MsaaCounts)
		{
			if (count <= requested && (deviceMax == 0 || (uint32_t)count <= deviceMax))
				best = count;
		}

		// A device that cannot manage even the smallest is a device that does
		// not get MSAA at all; the caller turns it off rather than asking for
		// something the driver will refuse.
		if (deviceMax != 0 && (uint32_t)best > deviceMax)
			return 1;
		return best;
	}

	struct RenderSettings
	{
		AntiAliasing AA = AntiAliasing::FXAA;

		// How many times larger each axis is drawn when AA is SSAA. Ignored
		// otherwise.
		//
		// **Cost is the square of this.** Two means four times the pixels
		// shaded, four means sixteen -- which is why it is a number the person
		// paying for it chooses rather than a fixed part of the mode.
		//
		// Clamped to something a texture can be: at 4 a 4K output would ask
		// for a 16K target, which is past what a lot of hardware will allocate
		// and all of what it would be sensible to.
		int SupersampleFactor = 2;

		// How many coverage samples per pixel when AA is MSAA. Ignored
		// otherwise.
		//
		// **2, 4 or 8, and nothing else** -- MsaaCounts above is the list, and
		// SanitiseMsaaSamples is the only way this reaches a render target.
		//
		// **Not the square of anything.** Unlike SupersampleFactor above, this
		// costs bandwidth and a little rasterizer work rather than shading, so
		// 4 is an ordinary choice where supersampling at 4 is a statement.
		int MsaaSamples = 4;

		// How much of TAA's accumulated image survives each frame. Ignored by
		// every other mode.
		//
		// **This is the ghosting-versus-sharpness dial, and there is no correct
		// value** -- but there is a measured curve, which is better than the
		// guess that used to be here.
		//
		// Against a 4x supersampled render of the same frame, on a scene with
		// one textured patch falling 16 px a frame and an identical one
		// standing still (RMS, lower better):
		//
		//               moving    still
		//   no filter    16.08    16.73
		//   0.0          17.15    17.95
		//   0.3          15.38    10.14
		//   0.6          16.32     5.39
		//   0.9          19.83     3.82
		//
		// Still content improves all the way up. Moving content has a minimum
		// near 0.3 and is *worse than no filter at all* by 0.9. The default
		// was 0.9 -- chosen when every scene in this repository was static,
		// which is exactly the regime that number is best in and the one
		// nobody complains about.
		//
		// 0.6 rather than the moving optimum: it is within a quarter of a unit
		// of no filter under motion while still being three times better than
		// it standing still, where 0.3 gives up half of that. A project that
		// knows it is mostly still should raise it, and one that is mostly
		// motion should lower it -- which is the whole reason this is a number
		// and not a constant. ENGINE-NOTES 7r.
		float TemporalFeedback = 0.6f;

		// How far the per-frame sub-pixel offset reaches, as a fraction of a
		// pixel. Ignored by every other mode.
		//
		// **This is the reconstruction filter's width, and it is a different
		// dial from feedback.** Feedback decides how much of the past is kept;
		// this decides how far apart the samples being accumulated were taken.
		// Widening it covers more of the pixel's footprint -- fewer edges left
		// unsampled, and a softer result, because the average is taken over a
		// larger area than the pixel. Narrowing it keeps the samples near the
		// centre: sharper, and closer to no jitter at all, which is to say
		// closer to no anti-aliasing at all.
		//
		// 1.0 is the whole pixel, which is what a box filter over the pixel's
		// own area means and what makes the accumulation converge on the
		// supersampled image the mode is aiming at. Values under 1 trade
		// coverage for sharpness; over 1 samples outside the pixel, which is
		// a deliberate blur and the reason the range does not stop at 1.
		//
		// 0 disables the offset entirely. That is not "TAA off": the history
		// still accumulates, it just accumulates the same sample every frame,
		// which converges on the unjittered image and buys nothing. It exists
		// because being able to switch one half of the mode off is how the
		// checks tell the two halves apart.
		float TemporalJitterScale = 1.0f;

		// How many frames the offset sequence runs before repeating.
		//
		// The sequence is Halton, so successive points fall in the gaps the
		// earlier ones left rather than wherever a random generator puts them
		// -- eight random offsets can easily leave a quarter of the pixel
		// unsampled, eight Halton offsets cannot. Longer converges on a finer
		// image and takes longer to recover from a cut; past sixteen the
		// difference stops being visible while the recovery still costs.
		//
		// It also bounds the index, which is what makes "frame 30 and frame 38
		// are drawn with the same offset" a property a check can assert --
		// see check_taa_jitter.py, which reads this rather than assuming 8.
		int TemporalJitterPhase = 8;

		// --- shadows -----------------------------------------------------------
		bool ShadowsEnabled = true;

		// Trace rays instead of rendering shadow maps (ENGINE-NOTES 7am, 7an):
		// one ray per pixel toward every casting light -- sun, spot and point
		// -- into an acceleration structure of the scene. No acne, no
		// detachment, no distance limit, no cap on how many lights cast, and
		// skinned casters cast their pose; the edge is hard. Off by default,
		// so no existing project changes appearance. Needs a device with ray
		// queries; where there is none -- OpenGL, always -- the maps are used
		// and the log says so once. Applies at once: flipping it recompiles
		// the lit shaders, no restart. One switch rather than one per effect,
		// because everything the rays will do (reflections next) sits on the
		// same structures and the same hardware question.
		bool RayTracing = false;

		// The two screen-space effects' traced twins (ENGINE-NOTES 7ao), each
		// only while RayTracing is on and each off by default. Reflections:
		// a mirror ray from every glossy pixel, shaded at the hit through the
		// material heap -- needs bindless as well as ray queries -- in place
		// of the SSR walk; the profile's ScreenSpaceReflections is then not
		// consulted. Ambient occlusion: SSAO's taps cast as short rays into
		// the scene in place of the depth-buffer probe; the profile's
		// AmbientOcclusion toggle is then not consulted, its radius and
		// intensity still are.
		// Off, Low, Medium or High.
		//
		// The level is a **gloss window**, not a resolution, and that is a
		// property of where the work sits rather than an oversight. The mirror
		// ray is mixed into the specular term inside the lit fragment, this
		// frame, weighted out as the surface roughens -- which is the whole of
		// its advantage over the screen-space walk. Moving it to a pass the
		// way the bounce moved (7bs) would need either a frame of lag or an
		// albedo attachment to rebuild a metal's F0 from, and both cost more
		// than the 0.30 ms the rays cost. So the level says how glossy a
		// surface has to be before it earns a ray.
		RayDetail RayTracedReflections = RayDetail::Off;
		// Off, Half or Full, the same three the profile's own AO offers and
		// for the same reason -- the level is the resolution the occlusion is
		// computed at. Where this is anything but Off it answers instead of
		// the profile's, which is what "the traced twin wins" has always
		// meant; the radius and the intensity still come from the profile.
		AoDetail RayTracedAmbientOcclusion = AoDetail::Off;

		// The third of the pattern (ENGINE-NOTES 7at): indirect diffuse cast
		// as rays from the shading point instead of gathered off the screen,
		// so light bounces from what is behind the camera and behind other
		// things, and lands on the surface's own albedo rather than on the
		// lit colour standing in for it. In the lit shader, not a post pass --
		// everything it needs is bound there. Needs bindless as well as ray
		// queries, like reflections do, because shading a hit reads the
		// material heap. While on, the profile's GlobalIllumination is not
		// consulted; its GiIntensity still is. Four rays a pixel: the most
		// expensive switch here, and off by default.
		//
		// Off, Low, Medium or High. Since 7bs the bounce is a pass rather than
		// part of the lit fragment, so the level can mean what it means
		// everywhere else: Low and Medium differ in rays per pixel, High also
		// traces at the frame's own resolution instead of half of it.
		RayDetail RayTracedGlobalIllumination = RayDetail::Off;

		// **Where the traced form's indirect light comes from.** The same two
		// options the rasterised form has and the same rule: Baked reads a
		// field off disk and needs one to exist, Realtime traces every frame.
		// Only means anything while the dial above is not Off.
		GiSource RayTracedGiSource = GiSource::Realtime;

		// GiBounces and the whole of the voxel form moved to the post profile
		// on 2026-08-20 (10.6, ENGINE-NOTES 7bg), where the rest of the GI
		// settings already were. What stays here is the *hardware* budget --
		// whether rays run at all -- which is the line 9.0 was actually
		// drawing; `GlobalIllumination` and `GiQuality` have always been in
		// the profile and have always cost passes and pixels.

		// More cascades means better texel density near the camera and more
		// scene renders. Four is the usual answer and the most this supports.
		int ShadowCascades = 4;

		// Per cascade, square. This is the single biggest lever on both quality
		// and cost: four 2048 maps is 64 MB of depth.
		int ShadowResolution = 2048;

		// How far from the camera shadows are drawn at all. Not the camera's
		// far plane, which is usually a kilometre: past this distance the
		// texels are so large the shadow is worse than none.
		float ShadowDistance = 40.0f;

		// Blend between a logarithmic split, which distributes texels correctly
		// and starves the far cascades, and a uniform one, which does the
		// reverse. 1 is fully logarithmic.
		float ShadowSplitLambda = 0.85f;

		// How far along the surface normal a sample is pushed, in shadow
		// texels. Raising it removes acne and starts detaching shadows from
		// their casters; there is no value that has neither, which is why the
		// shadow pass also writes back faces.
		float ShadowNormalOffset = 0.9f;
	};
}
