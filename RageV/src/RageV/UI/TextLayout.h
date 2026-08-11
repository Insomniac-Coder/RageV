#pragma once

#include "RageV/Asset/Font.h"
#include "RageV/Math/Math.h"

#include <string>
#include <vector>

namespace RageV::UI
{
	// Turns a string into positioned glyph quads.
	//
	// **Separate from anything that draws, and separate from canvas layout.**
	// That split is the reason world-space text costs a pipeline rather than a
	// second text engine: a label on a sign in the world needs exactly this,
	// with a different matrix afterwards.
	//
	// It is also pure arithmetic, so the awkward parts -- advances, kerning,
	// where a line breaks -- are testable with no GPU and no window.

	// One glyph, placed. Coordinates are in the same units as the size passed
	// to Build: pixels for a HUD, world units for a sign.
	//
	// The rectangle is in a y-*down* space with the origin at the start of the
	// first line's baseline, because that is what both a screen and a texture
	// use. Font metrics are y-up, and Build is where the two meet -- once,
	// rather than at every call site.
	struct PlacedGlyph
	{
		float X = 0.0f, Y = 0.0f, Width = 0.0f, Height = 0.0f;

		// Texture coordinates into the atlas, already normalised.
		float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
	};

	struct TextLayout
	{
		std::vector<PlacedGlyph> Glyphs;

		// The bounding box of what was placed, in the same space. Height counts
		// *lines*, not ink, so a string of full stops is still one line tall --
		// centring on ink would make every label jump as its text changed.
		float Width = 0.0f;
		float Height = 0.0f;
		uint32_t LineCount = 0;
	};

	// Decodes UTF-8 into codepoints, replacing anything malformed with U+FFFD
	// rather than dropping it. A dropped byte silently shortens the string; a
	// replacement character is visible, which is what makes somebody fix the
	// encoding rather than the layout.
	std::vector<uint32_t> DecodeUtf8(const std::string& text);

	// Lays out a single line, with kerning, starting at the origin.
	//
	// `size` is the em size in the caller's units, which is what every metric
	// in the font is multiplied by.
	TextLayout BuildLine(const std::string& text, const Font& font, float size);

	// How wide a line would be, without placing anything. For measuring before
	// deciding where to put it.
	float MeasureLine(const std::string& text, const Font& font, float size);
}
