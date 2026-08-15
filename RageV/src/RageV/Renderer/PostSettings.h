#pragma once
#include "RageV/Core/UUID.h"

namespace RageV
{
	// How a rendered frame is *graded*: what it looks like, as opposed to what
	// it cost.
	//
	// **This is the whole of a `.rvpostprofile`**, and it is the home every
	// item in roadmap phase 9 lands in -- the colour-grading LUT, auto
	// exposure, vignette and grain, depth of field, motion blur, ambient
	// occlusion and screen-space reflections. They are added here and to
	// `PostSettingsRegistry`, and nothing else has to be touched to put them
	// on disk, in the inspector and in front of a C# script. ENGINE-NOTES 7s.
	//
	// A profile is attached to a `CameraComponent`, and it is optional: a
	// camera with no profile renders with exactly these defaults. That is why
	// there is no "is this field set?" bit beside each value -- there is
	// nothing underneath a profile to inherit from, so every field it holds
	// is a value it means.
	//
	// **Editing one edits the asset**, and an asset can be shared. Two cameras
	// pointed at the same profile are two cameras that will always agree,
	// which is usually what is wanted and is occasionally a surprise; the
	// inspector says which file it is writing to for that reason.
	struct PostSettings
	{
		// Applied before the tone curve, which is what makes it an exposure
		// control rather than a brightness one: it slides the scene along the
		// response curve instead of scaling the result of it.
		float Exposure = 1.0f;

		bool BloomEnabled = true;

		// Brightness at which a pixel starts to bleed. Above 1 only genuinely
		// over-bright things glow, which is usually what is wanted.
		float BloomThreshold = 1.0f;

		// Width of the ramp around the threshold. Zero is a hard cut, which
		// pops as something crosses it and reads as flickering.
		float BloomKnee = 0.5f;

		float BloomIntensity = 0.06f;

		// Ceiling on what a single pixel may contribute to bloom.
		//
		// Without one, anything very bright and very small -- the sun reflected
		// in curved metal is the usual culprit, a few hundred nits across less
		// than a texel of the mip being read -- survives the whole chain as an
		// isolated blob that floats in the air near the surface that produced
		// it. Every engine has this control for the same reason.
		//
		// It bounds the contribution, not the pixel: the scene keeps its real
		// values, and only what bleeds out of them is limited.
		float BloomClamp = 16.0f;

		// --- colour grading -----------------------------------------------------
		// A `.cube` lookup table, applied **after** the tone curve, on
		// display-referred values -- which is what a LUT exported from any
		// grading tool was authored against, so one does here what it did
		// there. The cost of that choice, stated rather than hidden: a LUT at
		// this point cannot recover highlight detail ACES has already
		// compressed. ENGINE-NOTES 7t.
		//
		// Spelled UUID rather than AssetHandle for the reason SkyTexture is:
		// they are the same type, and the asset layer already depends on this
		// one.
		UUID ColorLut = UUID::Invalid();

		// How much of the graded result is used, against the ungraded one.
		// A look is rarely wanted at full strength on the first try, and a
		// dial is the difference between grading and re-exporting.
		float ColorLutStrength = 1.0f;

		// --- lens and film ------------------------------------------------------
		// Three effects that model the camera rather than the scene, and each
		// runs at a different point in the chain because each is a different
		// physical thing. ENGINE-NOTES 7w.
		//
		// --- auto exposure (9.2). ENGINE-NOTES 7y ---------------------------
		//
		// **Off by default, and off is exact**: no compute is dispatched and
		// the tone mapping pass takes `Exposure` above unchanged. That is what
		// keeps every existing screenshot check valid without a line of change
		// in any of them, and it is the same guarantee the three lens effects
		// below rest on.
		//
		// On, `Exposure` stops being the exposure and becomes exposure
		// *compensation* -- it multiplies what the metering worked out. Taking
		// the slider away would mean the only fix for a scene the metering
		// reads wrong was switching the feature off.
		bool AutoExposure = false;

		// The stops the histogram spans. A pixel outside them lands in an end
		// bin rather than being dropped -- except below the bottom, which is
		// reserved and discarded, because a night scene is mostly pixels with
		// no light in them and letting them vote meters the darkness.
		float AutoExposureMinLog = -8.0f;
		float AutoExposureMaxLog = 4.0f;

		// The tails thrown away before averaging. This is the whole reason for
		// keeping a histogram instead of a log average: the top few percent are
		// the sun and the specular hits, and an average chases them every time
		// the camera turns past something bright.
		float AutoExposureLowPercent = 0.5f;
		float AutoExposureHighPercent = 0.95f;

		// What the metered average is exposed to. 0.18 is middle grey.
		float AutoExposureKey = 0.18f;

		// Bounds on the result, for a scene with nothing in it to meter.
		float AutoExposureMin = 0.03f;
		float AutoExposureMax = 32.0f;

		// How fast it moves, in stops per second. Converted against the frame
		// time with `1 - exp(-rate * dt)` rather than `rate * dt`, so ten steps
		// of 10 ms land where one step of 100 ms does.
		float AutoExposureSpeed = 3.0f;

		// --- depth of field (9.4). ENGINE-NOTES 7z --------------------------
		//
		// **Off by default, and off exactly**: no passes are added at all, so
		// the chain is the one that ran before the feature existed.
		//
		// Runs after the anti-aliasing resolve and before bloom. After the
		// resolve because reprojecting a temporal filter over an
		// already-defocused image asks it to reconcile a blur with a history
		// blurred differently; before bloom because a bright out-of-focus
		// highlight should glow as the disc it has become.
		bool DepthOfField = false;

		// The lens, in the units a photographer already has. The circle of
		// confusion comes from the thin-lens equation rather than from a blur
		// slider, so f/1.4 does what f/1.4 does and the relationship between
		// the three is not something to rediscover per scene.
		//
		// Where the plane of sharp focus is, in metres.
		float FocusDistance = 5.0f;

		// In millimetres, the way lenses are sold. 50 is normal on the 35 mm
		// sensor these are measured against; longer is both narrower and
		// shallower.
		float FocalLength = 50.0f;

		// The f-number. Small is a wide aperture and a shallow field: f/1.4
		// throws a background away, f/16 keeps most of a scene sharp.
		float Aperture = 2.8f;

		// Ceiling on the blur radius, in pixels of the output. The gather's
		// tap count is chosen against this: let the radius grow without bound
		// and the disc thins into a ring of separate dots, which reads as a
		// broken effect rather than as a shallow one.
		float MaxBokehRadius = 24.0f;

		// --- SSAO (9.6). ENGINE-NOTES 7ac -----------------------------------
		//
		// Occlusion from depth alone, applied as a multiply on the lit image
		// -- which darkens direct light too, the stated compromise of every
		// forward-plus-post AO. Treat it as contact shadowing and keep the
		// intensity restrained. Off adds no pass and is exact.
		bool AmbientOcclusion = false;

		// World metres the hemisphere reaches. Small is contact darkening in
		// creases; large is soft room-scale shading and costs cache misses.
		float AoRadius = 0.5f;

		// An exponent on the occlusion, so an open surface (occlusion 1) is
		// untouched at any setting and only the dark end deepens.
		float AoIntensity = 1.0f;

		// --- motion blur (9.5). ENGINE-NOTES 7ab ----------------------------
		//
		// A reconstruction gather along the motion vectors the scene already
		// writes for TAA -- the blur happens where the motion lands, so an
		// object smears over what it passes rather than stopping at its own
		// silhouette. Off adds no pass and is exact.
		bool MotionBlur = false;

		// Fraction of the frame the virtual shutter is open. 0.5 is the
		// 180-degree shutter every film camera defaults to. Scales the
		// per-frame velocity directly, so it stays honest at any frame rate.
		float MotionBlurShutter = 0.5f;

		// Ceiling on the smear in pixels, and also the tile size the dominant
		// motion is tracked at: a blur that can reach further than its tiles
		// can see tears at tile boundaries.
		float MotionBlurMaxRadius = 20.0f;

		// **All three default to off, and off is exact.** The shader branches
		// past each rather than computing a no-op, so a profile that has not
		// touched them renders the same bytes as a build without them -- which
		// is what keeps every existing screenshot check valid.

		// How dark the corners go. Runs in linear light, *before* the tone
		// curve: a vignette is less light reaching the corner, so it should
		// roll off through the same response curve the rest of the frame does.
		// Applied afterwards it multiplies display values and reads as a
		// shadow somebody painted on.
		float VignetteIntensity = 0.0f;

		// How gradually it arrives. Low is a hard circle, high is a slow
		// darkening that reaches most of the frame.
		float VignetteSmoothness = 0.5f;

		// Lateral dispersion, in fractions of the frame's width. Three taps of
		// the scene at three offsets, on the linear sample, because a lens
		// disperses before the sensor sees anything.
		//
		// The bloom is deliberately not dispersed: it is already blurred wider
		// than any sane offset, so two more taps would shift something nobody
		// could see had moved.
		float ChromaticAberration = 0.0f;

		// Film grain, applied last -- after the tone curve **and** after the
		// LUT. Grain is the texture of the recording medium, not a colour
		// anybody graded: run it before the LUT and the grade re-maps the
		// noise, so grain changes character with the look instead of sitting
		// on top of it.
		//
		// Built from two octaves of value noise rather than a hash per pixel,
		// so it is round clumps of varying size instead of a grid of squares,
		// and it is strongest in the *midtones* -- film has no variation left
		// to show once nothing is exposed or everything is. ENGINE-NOTES 7x.
		//
		// Animated, and seeded from the *frame number* rather than a clock, so
		// that rendering frame 30 twice produces the same bytes. That is the
		// rule TAA's jitter already follows and for the same reason -- see 7r.
		float FilmGrain = 0.0f;

		// How coarse the grain is, in pixels per speck -- the period of the
		// noise lattice. Larger reads as a faster stock.
		//
		// Two rather than one, because one puts the finest of the three
		// octaves past what the pixel grid can resolve: it sharpens into noise
		// instead of showing specks, which is the look this stopped being.
		float FilmGrainSize = 2.0f;
	};
}
