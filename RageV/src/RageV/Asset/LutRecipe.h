#pragma once

#include "RageV/Asset/CubeLut.h"
#include "RageV/Math/Math.h"

#include <filesystem>

namespace RageV::Assets
{
	// A colour grade as the knobs that make it, rather than the table they
	// bake into. The contents of a `.rvlut`.
	//
	// **This is the half a `.cube` cannot be.** A `.cube` is 33^3 baked
	// triples with no record of the transform that produced them, so there is
	// nothing in one to put in an inspector. A recipe is eleven numbers, every
	// one of which means something, and the table is derived. ENGINE-NOTES 7v.
	//
	// Applied **display-referred**, after the tone curve, because that is
	// where the LUT is sampled (7t). The order below is the order it runs in
	// and is not a matter of taste -- contrast after gain and before
	// saturation is what makes these behave the way a colourist expects.
	struct LutRecipe
	{
		// --- 1. white balance ---------------------------------------------------
		// Warm at positive, cool at negative. Zero is untouched, exactly.
		float Temperature = 0.0f;
		// Magenta at positive, green at negative.
		float Tint = 0.0f;

		// --- 2. lift, gamma, gain -----------------------------------------------
		// The three-band form: lift moves the shadows, gain scales the
		// highlights, gamma bends what is between them.
		Vec3 Lift{ 0.0f, 0.0f, 0.0f };
		Vec3 Gamma{ 1.0f, 1.0f, 1.0f };
		Vec3 Gain{ 1.0f, 1.0f, 1.0f };

		// --- 3. contrast --------------------------------------------------------
		// About a 0.5 pivot, which is mid-grey in the encoded values this
		// operates on.
		float Contrast = 1.0f;

		// --- 4. saturation ------------------------------------------------------
		// Toward Rec.709 luma. 0 is monochrome, 1 untouched, above 1 pushed.
		float Saturation = 1.0f;

		// --- the table it bakes into --------------------------------------------
		// Entries per axis. 33 is what grading tools emit most often and is
		// odd, so the midpoint of the range lands on an entry rather than
		// between two.
		int Size = 33;
	};

	// The table this recipe describes.
	//
	// **A recipe at its defaults bakes the identity table, exactly** -- not
	// nearly, byte-for-byte, so that a fresh `.rvlut` on a camera changes
	// nothing until a knob moves. Guaranteed by each stage skipping itself at
	// its default rather than computing a no-op that only looks like one.
	//
	// The size is what decides whether it *has* to. At the default 33 the step
	// is 1/32, every coordinate is a dyadic rational, and
	// `(x - 0.5f) * 1.0f + 0.5f` comes back bit-exact anyway -- measured. At
	// 20 the step is 1/19 and two of its twenty coordinates do not survive
	// that round trip; at 64, eleven do not. So the skip is not belt and
	// braces at the default and load-bearing everywhere else, and the check
	// exercises a size that can actually fail. ENGINE-NOTES 7v.
	ColorLut BakeRecipe(const LutRecipe& recipe);

	// Reads and writes `.rvlut`. Registry-driven, like the post profile, so a
	// knob added to the struct and the registry needs nothing here.
	class LutRecipeSerializer
	{
	public:
		static bool Save(const LutRecipe& recipe, const std::filesystem::path& path);
		static bool Load(LutRecipe& out, const std::filesystem::path& path);
	};

	// Whether this path is a recipe rather than a baked table. The one place
	// the two files are told apart, so the rule cannot drift between the
	// loader, the inspector and the picker.
	bool IsLutRecipePath(const std::filesystem::path& path);

	// Writes the baked table as a `.cube`, for taking a look somewhere else.
	//
	// The way out of the engine, and the reason a recipe is not a dead end:
	// what is authored here opens in Resolve or Photoshop like any other LUT.
	bool ExportCube(const ColorLut& lut, const std::filesystem::path& path);
}
