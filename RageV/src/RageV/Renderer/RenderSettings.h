#pragma once
#include <cstdint>

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

	// How the directional light's shadow is decided (ENGINE-NOTES 7am).
	//
	//   Maps: cascaded shadow maps -- the scene rendered from the light into
	//   depth maps, sampled with two biases against acne and detachment, out
	//   to ShadowDistance and no further.
	//
	//   RayTraced: one ray per pixel toward the light, into an acceleration
	//   structure of the scene. No acne, no detachment, no distance limit,
	//   and hard-edged. Needs a device with ray queries; where there is none
	//   -- OpenGL, always -- it falls back to Maps and the log says so once.
	//   Spot and point lights keep their maps either way, for now.
	enum class ShadowMode : uint32_t
	{
		Maps      = 0,
		RayTraced = 1,
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
		// otherwise, and clamped to what the hardware actually offers.
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

		// Maps or rays for the directional light. Maps by default, so no
		// existing project changes appearance; RayTraced falls back to Maps
		// on a device without ray queries.
		ShadowMode ShadowMethod = ShadowMode::Maps;

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
