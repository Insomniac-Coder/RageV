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
	// That split is why world-space text costs a pipeline rather than a second
	// text engine: a label on a sign in the world needs exactly this, with a
	// different matrix afterwards.
	//
	// It is also pure arithmetic. Every awkward part of text -- where a line
	// breaks, what kerning does to a width, what centring means when a line has
	// a trailing space -- is decided here, with no GPU and no window, which is
	// why it can be tested rather than looked at.

	enum class TextAlign
	{
		Left,
		Center,
		Right,
	};

	struct TextStyle
	{
		// Em size, in whatever units the caller works in: pixels for a HUD,
		// world units for a sign.
		float Size = 16.0f;

		// Break lines to fit this width. Zero means never -- only an explicit
		// newline starts a line.
		float WrapWidth = 0.0f;

		TextAlign Align = TextAlign::Left;

		// Multiplier on the font's own line height. The font decides what a
		// line *is*; this is the caller loosening or tightening it.
		float LineSpacing = 1.0f;
	};

	// One glyph, placed, in the caller's units.
	//
	// The origin is the **top left of the text block**, y down -- not the
	// baseline. A UI positions text in a box, so a box-relative origin is what
	// every caller would otherwise compute for itself; `FirstBaseline` is there
	// for the ones that genuinely want the baseline.
	struct PlacedGlyph
	{
		float X = 0.0f, Y = 0.0f, Width = 0.0f, Height = 0.0f;

		// Normalised into the atlas.
		float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
	};

	struct TextLayout
	{
		std::vector<PlacedGlyph> Glyphs;

		// The widest line, and the height of the line boxes -- **not** of the
		// ink. A row of full stops is one line tall, so a label does not change
		// height when its text does.
		float Width = 0.0f;
		float Height = 0.0f;

		uint32_t LineCount = 0;

		// Distance from the top of the block to the first baseline, for callers
		// aligning text against something else that sits on one.
		float FirstBaseline = 0.0f;
	};

	// Decodes UTF-8 into codepoints, replacing anything malformed with U+FFFD
	// rather than dropping it. A dropped byte silently shortens the string; a
	// replacement character is visible, which is what makes somebody fix the
	// encoding instead of the layout.
	std::vector<uint32_t> DecodeUtf8(const std::string& text);

	// The whole job: newlines, wrapping, kerning and alignment.
	TextLayout Build(const std::string& text, const Font& font, const TextStyle& style);

	// One line, unwrapped and left aligned. The common case, and what a
	// measurement usually means.
	TextLayout BuildLine(const std::string& text, const Font& font, float size);

	// How wide one line would be, without placing anything.
	float MeasureLine(const std::string& text, const Font& font, float size);
}
