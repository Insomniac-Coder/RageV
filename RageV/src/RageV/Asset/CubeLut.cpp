#include <rvpch.h>
#include "CubeLut.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace RageV::Assets
{
	namespace
	{
		// Bigger than any tool emits, and small enough that a corrupt header
		// cannot ask for a terabyte: 64^3 is 262144 entries.
		constexpr uint32_t kMaxSize = 64;

		// A `.cube` line, with comments and whitespace gone. Empty when there
		// is nothing on it.
		std::string Clean(const std::string& line)
		{
			const size_t hash = line.find('#');
			std::string text = hash == std::string::npos ? line : line.substr(0, hash);

			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};

			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}
	}

	bool LoadCubeLut(ColorLut& out, const std::filesystem::path& path)
	{
		std::string text;
		if (!IO::VFS::ReadText(path, text))
			return false;

		ColorLut loaded;

		// The domain a `.cube` declares. Almost always 0..1; a file that says
		// otherwise is rescaled on the way in rather than rejected, because
		// that is cheap and the alternative is refusing a valid file.
		Vec3 domainMin(0.0f, 0.0f, 0.0f);
		Vec3 domainMax(1.0f, 1.0f, 1.0f);

		std::istringstream stream(text);
		std::string line;

		while (std::getline(stream, line))
		{
			const std::string cleaned = Clean(line);
			if (cleaned.empty())
				continue;

			std::istringstream fields(cleaned);
			std::string keyword;
			fields >> keyword;

			if (keyword == "LUT_3D_SIZE")
			{
				int size = 0;
				if (!(fields >> size) || size < 2 || size > (int)kMaxSize)
				{
					RV_CORE_ERROR("{0}: LUT_3D_SIZE {1} is not between 2 and {2}",
								  path.string(), size, kMaxSize);
					return false;
				}

				loaded.Size = (uint32_t)size;
				loaded.Values.reserve((size_t)size * size * size);
				continue;
			}

			// A 1D LUT is a different thing that shares the extension. Refused
			// by name so the message says what the file is rather than
			// complaining about the numbers in it.
			if (keyword == "LUT_1D_SIZE")
			{
				RV_CORE_ERROR("{0} is a 1D LUT; colour grading takes a 3D one", path.string());
				return false;
			}

			if (keyword == "DOMAIN_MIN") { fields >> domainMin.x >> domainMin.y >> domainMin.z; continue; }
			if (keyword == "DOMAIN_MAX") { fields >> domainMax.x >> domainMax.y >> domainMax.z; continue; }
			if (keyword == "TITLE")      continue;

			// Anything else has to be a data row, and a data row cannot come
			// before the size -- there would be nowhere to put it.
			if (loaded.Size == 0)
			{
				RV_CORE_ERROR("{0}: data before LUT_3D_SIZE", path.string());
				return false;
			}

			float r = 0.0f, g = 0.0f, b = 0.0f;
			std::istringstream values(cleaned);
			if (!(values >> r >> g >> b))
			{
				RV_CORE_ERROR("{0}: '{1}' is neither a keyword nor a colour",
							  path.string(), cleaned);
				return false;
			}

			loaded.Values.emplace_back(r, g, b, 1.0f);
		}

		if (loaded.Size == 0)
		{
			RV_CORE_ERROR("{0}: no LUT_3D_SIZE, so this is not a 3D cube LUT", path.string());
			return false;
		}

		const size_t expected = (size_t)loaded.Size * loaded.Size * loaded.Size;
		if (loaded.Values.size() != expected)
		{
			// Counted rather than trusted. A truncated file is the common
			// corruption, and the entries it does have are all in the right
			// place -- so it would load, grade, and be wrong only in the
			// shadows, which is the hardest kind of wrong to notice.
			RV_CORE_ERROR("{0}: LUT_3D_SIZE {1} needs {2} entries, found {3}",
						  path.string(), loaded.Size, expected, loaded.Values.size());
			return false;
		}

		// Rescale out of a declared domain, if it was not the usual one.
		const Vec3 span = domainMax - domainMin;
		if (span.x != 1.0f || span.y != 1.0f || domainMin.x != 0.0f || domainMin.y != 0.0f)
		{
			for (Vec4& value : loaded.Values)
			{
				value.x = span.x != 0.0f ? (value.x - domainMin.x) / span.x : 0.0f;
				value.y = span.y != 0.0f ? (value.y - domainMin.y) / span.y : 0.0f;
				value.z = span.z != 0.0f ? (value.z - domainMin.z) / span.z : 0.0f;
			}
		}

		out = std::move(loaded);
		return true;
	}

	namespace
	{
		// IEEE 754 binary32 to binary16, by bit surgery.
		//
		// Written out rather than pulled in: the only values that reach it are
		// LUT entries, which are small, finite and non-negative in every file
		// anyone will load -- so the interesting half of a general converter
		// (denormals, infinities, NaN) is unreachable here and is handled by
		// clamping rather than by branching, which is both shorter and
		// impossible to get subtly wrong.
		uint16_t ToHalf(float value)
		{
			// Clamped to what binary16 can say. 65504 is its largest finite
			// value; a LUT entry outside 0..1 is already unusual and one
			// outside this is not a colour.
			value = std::clamp(value, -65504.0f, 65504.0f);

			uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));

			const uint32_t sign = (bits >> 16) & 0x8000u;
			int exponent = (int)((bits >> 23) & 0xFFu) - 127 + 15;
			uint32_t mantissa = bits & 0x7FFFFFu;

			if (exponent <= 0)
			{
				// Subnormal or zero in binary16. A LUT entry this small is
				// black to within a part in 16384, so flushing to zero costs
				// nothing measurable and removes the fiddliest arm.
				return (uint16_t)sign;
			}

			if (exponent >= 31)
				return (uint16_t)(sign | 0x7BFFu);   // the clamp above, in bits

			// Round to nearest even on the bit being dropped.
			const uint32_t rounded = mantissa + 0x00000FFFu + ((mantissa >> 13) & 1u);
			if (rounded & 0x00800000u)
			{
				exponent++;
				mantissa = 0;
				if (exponent >= 31)
					return (uint16_t)(sign | 0x7BFFu);
			}
			else
			{
				mantissa = rounded;
			}

			return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
		}
	}

	std::vector<uint16_t> ToHalfRGBA(const ColorLut& lut)
	{
		std::vector<uint16_t> halves(lut.Values.size() * 4);

		for (size_t i = 0; i < lut.Values.size(); i++)
		{
			for (int channel = 0; channel < 4; channel++)
				halves[i * 4 + channel] = ToHalf(lut.Values[i][channel]);
		}

		return halves;
	}

	ColorLut IdentityLut(uint32_t size)
	{
		ColorLut lut;
		if (size < 2 || size > kMaxSize)
			return lut;

		lut.Size = size;
		lut.Values.reserve((size_t)size * size * size);

		const float step = 1.0f / (float)(size - 1);

		// Red fastest, matching the file format's own order -- so this
		// function is also a statement of what that order is, and the check
		// that grades with it would fail if the reader disagreed.
		for (uint32_t b = 0; b < size; b++)
		{
			for (uint32_t g = 0; g < size; g++)
			{
				for (uint32_t r = 0; r < size; r++)
					lut.Values.emplace_back(r * step, g * step, b * step, 1.0f);
			}
		}

		return lut;
	}
}
