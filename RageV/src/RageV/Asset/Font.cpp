#include <rvpch.h>
#include "Font.h"

namespace RageV
{
	const Font::Glyph* Font::Find(uint32_t codepoint) const
	{
		const auto found = m_Glyphs.find(codepoint);
		return found != m_Glyphs.end() ? &found->second : nullptr;
	}

	float Font::Kerning(uint32_t left, uint32_t right) const
	{
		const auto found = m_Kerning.find(PairKey(left, right));
		return found != m_Kerning.end() ? found->second : 0.0f;
	}

	void Font::AddGlyph(const Glyph& glyph)
	{
		m_Glyphs[glyph.Codepoint] = glyph;
	}

	void Font::SetKerning(uint32_t left, uint32_t right, float advance)
	{
		// A zero adjustment is the default, so storing one would cost a lookup
		// slot to say what an absent entry already says.
		if (advance == 0.0f)
			return;

		m_Kerning[PairKey(left, right)] = advance;
	}

	void Font::Clear()
	{
		m_Glyphs.clear();
		m_Kerning.clear();
		AtlasFile.clear();
		AtlasWidth = AtlasHeight = 0;
		EmSize = PxRange = 0.0f;
		Ascent = Descent = LineHeight = 0.0f;
	}
}
