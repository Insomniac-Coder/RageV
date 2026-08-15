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
		// Animated, and seeded from the *frame number* rather than a clock, so
		// that rendering frame 30 twice produces the same bytes. That is the
		// rule TAA's jitter already follows and for the same reason -- see 7r.
		float FilmGrain = 0.0f;

		// How coarse the grain is, in pixels per speck. Larger reads as a
		// faster film stock; at 1 it is per-pixel noise, which reads as
		// digital sensor noise instead.
		float FilmGrainSize = 1.0f;
	};
}
