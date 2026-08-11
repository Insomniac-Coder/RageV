#include <rvpch.h>
#include "TextLayout.h"

namespace RageV::UI
{
	namespace
	{
		constexpr uint32_t kReplacement = 0xFFFD;

		// How many continuation bytes a lead byte promises, or -1 if it is not
		// a lead byte at all.
		int SequenceLength(unsigned char lead)
		{
			if (lead < 0x80) return 1;
			if (lead < 0xC0) return -1;    // a continuation byte with no lead
			if (lead < 0xE0) return 2;
			if (lead < 0xF0) return 3;
			if (lead < 0xF8) return 4;
			return -1;
		}
	}

	std::vector<uint32_t> DecodeUtf8(const std::string& text)
	{
		std::vector<uint32_t> out;
		out.reserve(text.size());

		size_t i = 0;
		while (i < text.size())
		{
			const unsigned char lead = (unsigned char)text[i];
			const int length = SequenceLength(lead);

			if (length < 0 || i + (size_t)length > text.size())
			{
				// Malformed, or truncated at the end. One replacement character
				// and step one byte -- stepping the promised length would let a
				// corrupt lead byte eat valid characters after it.
				out.push_back(kReplacement);
				i++;
				continue;
			}

			static constexpr uint32_t kLeadMask[5] = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
			uint32_t code = lead & kLeadMask[length];

			bool valid = true;
			for (int k = 1; k < length; k++)
			{
				const unsigned char next = (unsigned char)text[i + k];
				if ((next & 0xC0) != 0x80)
				{
					valid = false;
					break;
				}
				code = (code << 6) | (next & 0x3F);
			}

			if (!valid)
			{
				out.push_back(kReplacement);
				i++;
				continue;
			}

			out.push_back(code);
			i += (size_t)length;
		}

		return out;
	}

	TextLayout BuildLine(const std::string& text, const Font& font, float size)
	{
		TextLayout layout;
		if (font.IsEmpty() || size <= 0.0f)
			return layout;

		const std::vector<uint32_t> codepoints = DecodeUtf8(text);
		layout.Glyphs.reserve(codepoints.size());

		const float atlasWidth = (float)font.AtlasWidth;
		const float atlasHeight = (float)font.AtlasHeight;

		float pen = 0.0f;
		uint32_t previous = 0;

		for (uint32_t codepoint : codepoints)
		{
			const Font::Glyph* glyph = font.Find(codepoint);
			if (!glyph)
			{
				// Nothing is drawn and the pen does not move. Substituting a
				// box here would be the font layer inventing content; the
				// decision belongs to whatever owns the string.
				previous = 0;
				continue;
			}

			if (previous != 0)
				pen += font.Kerning(previous, codepoint) * size;

			if (glyph->HasImage())
			{
				PlacedGlyph placed;

				// The font's plane bounds are in ems with y up from the
				// baseline; the output is y down from it. That is the whole of
				// the flip, and it happens here so nothing downstream repeats it.
				placed.X = pen + glyph->Left * size;
				placed.Y = -glyph->Top * size;
				placed.Width = (glyph->Right - glyph->Left) * size;
				placed.Height = (glyph->Top - glyph->Bottom) * size;

				// The atlas rectangle is where the glyph is in the image, so
				// this is a divide and nothing else. rvfont flips the field's
				// rows into image order when it bakes.
				placed.U0 = glyph->X / atlasWidth;
				placed.V0 = glyph->Y / atlasHeight;
				placed.U1 = (glyph->X + glyph->W) / atlasWidth;
				placed.V1 = (glyph->Y + glyph->H) / atlasHeight;

				layout.Glyphs.push_back(placed);
			}

			pen += glyph->Advance * size;
			previous = codepoint;
		}

		layout.Width = pen;
		// One line, measured by the font's own line box rather than by the ink
		// in it, so a label does not change height when its text does.
		layout.Height = font.LineHeight * size;
		layout.LineCount = codepoints.empty() ? 0 : 1;

		return layout;
	}

	float MeasureLine(const std::string& text, const Font& font, float size)
	{
		if (font.IsEmpty() || size <= 0.0f)
			return 0.0f;

		float pen = 0.0f;
		uint32_t previous = 0;

		for (uint32_t codepoint : DecodeUtf8(text))
		{
			const Font::Glyph* glyph = font.Find(codepoint);
			if (!glyph)
			{
				previous = 0;
				continue;
			}

			if (previous != 0)
				pen += font.Kerning(previous, codepoint) * size;

			pen += glyph->Advance * size;
			previous = codepoint;
		}

		return pen;
	}
}
