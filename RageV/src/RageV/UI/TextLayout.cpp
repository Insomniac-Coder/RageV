#include <rvpch.h>
#include "TextLayout.h"

namespace RageV::UI
{
	namespace
	{
		constexpr uint32_t kReplacement = 0xFFFD;
		constexpr uint32_t kNewline = '\n';
		constexpr uint32_t kSpace = ' ';

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

		bool IsBreakable(uint32_t codepoint)
		{
			// Only the space, deliberately. Breaking on a hyphen needs to know
			// whether it is a hyphen or a minus sign, and breaking between CJK
			// characters needs to know it is looking at CJK -- both are real,
			// both are line-breaking rules rather than layout, and both belong
			// with the rest of the deferred text work in ENGINE-NOTES 7d.
			return codepoint == kSpace;
		}

		// The pen advance from `previous` to `codepoint`, kerning included.
		// `previous` of zero means the start of a line, where there is nothing
		// to kern against.
		float Advance(const Font& font, uint32_t previous, uint32_t codepoint, float size)
		{
			const Font::Glyph* glyph = font.Find(codepoint);
			if (!glyph)
				return 0.0f;

			float advance = glyph->Advance * size;
			if (previous != 0)
				advance += font.Kerning(previous, codepoint) * size;

			return advance;
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
				// and step a single byte -- stepping the promised length would
				// let a corrupt lead byte swallow the valid characters after it.
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

	TextLayout Build(const std::string& text, const Font& font, const TextStyle& style)
	{
		TextLayout layout;
		if (font.IsEmpty() || style.Size <= 0.0f)
			return layout;

		const std::vector<uint32_t> codepoints = DecodeUtf8(text);
		if (codepoints.empty())
			return layout;

		const float size = style.Size;
		const float lineHeight = font.LineHeight * size * Math::Max(style.LineSpacing, 0.0f);

		// --- 1. break into lines ------------------------------------------
		//
		// Greedy: take words until one does not fit, then break at the last
		// space. That is what every word processor does and what a reader
		// expects; the alternatives (Knuth-Plass and friends) optimise a
		// paragraph as a whole and are for typesetting, not a HUD.
		struct Line { size_t Begin = 0; size_t End = 0; };
		std::vector<Line> lines;

		const bool wrapping = style.WrapWidth > 0.0f;

		size_t lineBegin = 0;
		size_t lastBreak = std::string::npos;   // index of the space to break at
		float pen = 0.0f;
		uint32_t previous = 0;

		for (size_t i = 0; i < codepoints.size(); i++)
		{
			const uint32_t codepoint = codepoints[i];

			if (codepoint == kNewline)
			{
				lines.push_back({ lineBegin, i });
				lineBegin = i + 1;
				lastBreak = std::string::npos;
				pen = 0.0f;
				previous = 0;
				continue;
			}

			const float advance = Advance(font, previous, codepoint, size);

			// A line always keeps at least one glyph, or a box narrower than a
			// single character would loop forever finding nothing that fits.
			const bool overflows = wrapping && i > lineBegin && (pen + advance) > style.WrapWidth;

			if (overflows)
			{
				if (lastBreak != std::string::npos)
				{
					// Break at the space. The space itself ends the line and is
					// not carried onto the next one, so a wrapped paragraph does
					// not start every line with a gap.
					lines.push_back({ lineBegin, lastBreak });
					lineBegin = lastBreak + 1;
				}
				else
				{
					// No space to break at: a single word wider than the box.
					// Broken mid-word rather than allowed to overflow, because
					// a long number or path silently running out of its panel is
					// worse than an ugly break.
					lines.push_back({ lineBegin, i });
					lineBegin = i;
				}

				// Re-measure the new line from its start.
				lastBreak = std::string::npos;
				pen = 0.0f;
				previous = 0;

				for (size_t k = lineBegin; k <= i; k++)
				{
					pen += Advance(font, previous, codepoints[k], size);
					if (IsBreakable(codepoints[k]) && k != lineBegin)
						lastBreak = k;
					previous = codepoints[k];
				}
				continue;
			}

			pen += advance;
			if (IsBreakable(codepoint) && i != lineBegin)
				lastBreak = i;
			previous = codepoint;
		}

		lines.push_back({ lineBegin, codepoints.size() });

		// --- 2. place the glyphs -------------------------------------------
		const float atlasWidth = (float)font.AtlasWidth;
		const float atlasHeight = (float)font.AtlasHeight;

		// The first baseline sits an ascent below the top of the block, which is
		// what makes two labels of different sizes line up along their tops.
		layout.FirstBaseline = font.Ascent * size;
		layout.LineCount = (uint32_t)lines.size();
		layout.Height = lineHeight * (float)lines.size();

		// Measured first, because centring needs to know how wide the widest
		// line is before any of them can be placed.
		std::vector<float> widths(lines.size(), 0.0f);
		for (size_t index = 0; index < lines.size(); index++)
		{
			// Trailing spaces do not count. A line that wrapped ends with the
			// space that broke it; measuring it would push centred text
			// leftwards by half a space for reasons nobody could see.
			size_t end = lines[index].End;
			while (end > lines[index].Begin && codepoints[end - 1] == kSpace)
				end--;

			float width = 0.0f;
			uint32_t last = 0;
			for (size_t k = lines[index].Begin; k < end; k++)
			{
				width += Advance(font, last, codepoints[k], size);
				last = codepoints[k];
			}

			widths[index] = width;
			layout.Width = Math::Max(layout.Width, width);
		}

		layout.Glyphs.reserve(codepoints.size());

		for (size_t index = 0; index < lines.size(); index++)
		{
			const float baseline = layout.FirstBaseline + lineHeight * (float)index;

			float x = 0.0f;
			switch (style.Align)
			{
			case TextAlign::Center: x = (layout.Width - widths[index]) * 0.5f; break;
			case TextAlign::Right:  x = layout.Width - widths[index]; break;
			case TextAlign::Left:
			default:                x = 0.0f; break;
			}

			uint32_t last = 0;
			for (size_t k = lines[index].Begin; k < lines[index].End; k++)
			{
				const uint32_t codepoint = codepoints[k];
				const Font::Glyph* glyph = font.Find(codepoint);
				if (!glyph)
				{
					// Nothing drawn and the pen does not move. Substituting a
					// box would be the text layer inventing content; that
					// decision belongs to whoever owns the string.
					last = 0;
					continue;
				}

				if (last != 0)
					x += font.Kerning(last, codepoint) * size;

				if (glyph->HasImage())
				{
					PlacedGlyph placed;

					// Font metrics are y up from the baseline; the output is y
					// down from the top of the block. This is the whole of that
					// flip, and it happens once, here.
					placed.X = x + glyph->Left * size;
					placed.Y = baseline - glyph->Top * size;
					placed.Width = (glyph->Right - glyph->Left) * size;
					placed.Height = (glyph->Top - glyph->Bottom) * size;

					// The atlas rectangle is simply where the glyph is in the
					// image, so this is a divide. rvfont flips the field's rows
					// into image order when it bakes.
					placed.U0 = glyph->X / atlasWidth;
					placed.V0 = glyph->Y / atlasHeight;
					placed.U1 = (glyph->X + glyph->W) / atlasWidth;
					placed.V1 = (glyph->Y + glyph->H) / atlasHeight;

					layout.Glyphs.push_back(placed);
				}

				x += glyph->Advance * size;
				last = codepoint;
			}
		}

		return layout;
	}

	TextLayout BuildLine(const std::string& text, const Font& font, float size)
	{
		TextStyle style;
		style.Size = size;
		return Build(text, font, style);
	}

	float MeasureLine(const std::string& text, const Font& font, float size)
	{
		if (font.IsEmpty() || size <= 0.0f)
			return 0.0f;

		float pen = 0.0f;
		uint32_t previous = 0;

		for (uint32_t codepoint : DecodeUtf8(text))
		{
			if (!font.Find(codepoint))
			{
				previous = 0;
				continue;
			}

			pen += Advance(font, previous, codepoint, size);
			previous = codepoint;
		}

		return pen;
	}
}
