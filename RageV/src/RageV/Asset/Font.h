#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace RageV
{
	// A baked font: what `tools/rvfont` produced, as the engine sees it.
	//
	// There is no font parsing here and no distance field generation. Both
	// happen once, offline, in a tool that links stb_truetype and msdfgen --
	// neither of which is linked by the engine or by a shipped game. What
	// arrives at runtime is a picture and a table of numbers.
	//
	// ---------------------------------------------------------------------
	// Everything is in em units
	// ---------------------------------------------------------------------
	//
	// Not pixels, not the face's internal grid. A layout multiplies by the font
	// size and is finished, and the same font draws at 12 px and 200 px from
	// one table. The only numbers here in pixels are the atlas rectangles,
	// which are pixels because the atlas is.
	class Font
	{
	public:
		struct Glyph
		{
			uint32_t Codepoint = 0;

			// Where the field lives in the atlas, in atlas pixels.
			float X = 0.0f, Y = 0.0f, W = 0.0f, H = 0.0f;

			// The quad to draw, in em units, relative to the pen position on
			// the baseline, with y up. A layout scales these by the font size
			// and offsets them by the pen.
			float Left = 0.0f, Bottom = 0.0f, Right = 0.0f, Top = 0.0f;

			// How far the pen moves after drawing it. A space has one of these
			// and no image at all, which is why the two are separate questions.
			float Advance = 0.0f;

			bool HasImage() const { return W > 0.0f && H > 0.0f; }
		};

		// --- what the shader needs -------------------------------------
		uint32_t AtlasWidth = 0;
		uint32_t AtlasHeight = 0;

		// Atlas pixels per em, and the distance range in atlas pixels. Together
		// they decide how sharp the field can be; see SmallestSharpSize.
		float EmSize = 0.0f;
		float PxRange = 0.0f;

		// --- what a layout needs ---------------------------------------
		float Ascent = 0.0f;       // above the baseline, positive
		float Descent = 0.0f;      // below it, negative, as the face reports
		float LineHeight = 0.0f;   // baseline to baseline

		// The atlas image, as named by the `.rvfont`. Relative to it, so a
		// font and its picture move together.
		std::string AtlasFile;

		// Null when the face had no glyph for it. The caller decides what to
		// substitute; the asset does not invent one, because a box drawn by
		// the font layer looks like a font bug rather than a missing character.
		const Glyph* Find(uint32_t codepoint) const;

		// Zero for the overwhelming majority of pairs, which is why only the
		// pairs a face actually adjusts are stored.
		float Kerning(uint32_t left, uint32_t right) const;

		size_t GetGlyphCount() const { return m_Glyphs.size(); }
		size_t GetKerningCount() const { return m_Kerning.size(); }
		bool IsEmpty() const { return m_Glyphs.empty(); }

		// The smallest on-screen size, in pixels per em, at which this atlas
		// can still antialias.
		//
		// `screenPxRange` is the distance range measured in screen pixels;
		// below 2 the field cannot resolve an edge and colour fringes spread
		// across the glyph. It scales with how large the text is drawn, so the
		// threshold is a property of the *atlas*:
		//
		//     screenPxRange = PxRange * size / EmSize  >=  2
		//
		// Derived rather than stored, so it cannot disagree with the two
		// numbers it comes from.
		float SmallestSharpSize() const
		{
			return PxRange > 0.0f ? 2.0f * EmSize / PxRange : 0.0f;
		}

		void AddGlyph(const Glyph& glyph);
		void SetKerning(uint32_t left, uint32_t right, float advance);
		void Clear();

	private:
		// Packed into one key rather than a map of maps: kerning is read once
		// per character pair during layout, and two lookups per character is
		// the kind of thing that only ever gets slower.
		static uint64_t PairKey(uint32_t left, uint32_t right)
		{
			return ((uint64_t)left << 32) | (uint64_t)right;
		}

		std::unordered_map<uint32_t, Glyph> m_Glyphs;
		std::unordered_map<uint64_t, float> m_Kerning;
	};
}
