#pragma once

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
	};
}
