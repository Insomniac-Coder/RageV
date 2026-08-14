#pragma once

#include "RageV/Math/Math.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace RageV::Assets
{
	// A colour-grading lookup table, as read from an Adobe/IRIDAS `.cube`.
	//
	// The table is the whole of a "look": the frame's colour is a coordinate
	// into it and the entry there is what that colour becomes. Every grading
	// tool exports this format, which is the point -- the work here is
	// accepting a transform somebody else authored, unchanged, not inventing
	// one. ENGINE-NOTES 7t.
	struct ColorLut
	{
		// Entries per axis. A `.cube` calls it LUT_3D_SIZE; 17, 32 and 33 are
		// what tools actually emit.
		uint32_t Size = 0;

		// Size^3 entries, **red changing fastest**: index r + g*Size + b*Size^2.
		//
		// That order is the format's, and reversing it produces a table that
		// grades the picture plausibly and wrongly -- so the reader asserts it
		// rather than assuming it.
		//
		// RGBA rather than RGB because that is what the texture upload wants
		// and packing it here avoids a second pass over 36000 entries; alpha
		// is 1 and unused.
		std::vector<Vec4> Values;

		bool IsValid() const { return Size >= 2 && Values.size() == (size_t)Size * Size * Size; }
	};

	// Reads a `.cube`. False leaves `out` untouched.
	//
	// **A malformed file is refused, not salvaged.** A partially-read LUT is a
	// look nobody authored, and it would render -- so the failure has to be
	// the load rather than the picture.
	bool LoadCubeLut(ColorLut& out, const std::filesystem::path& path);

	// The table as half floats, RGBA, ready for a 3D texture upload.
	//
	// 16-bit rather than 8-bit unorm because a LUT is filtered and *then*
	// written to an 8-bit target: 8-bit entries quantise before the
	// interpolation and band in exactly the smooth gradients a grade is judged
	// on. 16-bit rather than 32 because linear filtering of a 32-bit float
	// image is optional in Vulkan and guaranteed for 16 -- a LUT that silently
	// fell back to point sampling would be a posterised grade on some hardware
	// and not others. ENGINE-NOTES 7t.
	std::vector<uint16_t> ToHalfRGBA(const ColorLut& lut);

	// The identity: every entry is its own coordinate, so grading with it is a
	// no-op. Used by the check that catches a wrong sampling scale, which is
	// the one mistake here that nothing about the image would reveal.
	ColorLut IdentityLut(uint32_t size);
}
