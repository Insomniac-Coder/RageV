// rvfont -- bakes a .ttf into a distance-field atlas the engine can draw.
//
// ---------------------------------------------------------------------------
// Why this is a tool and not part of the engine
// ---------------------------------------------------------------------------
//
// Generating a distance field is slow, deterministic, and needs a font parser
// and a geometry library. Doing it at load time would put both into every
// shipped game and spend the cost on every launch, to produce a file that is
// identical every time. So it happens once, here, and the runtime loads a PNG
// and a table of numbers.
//
// That is also why `stb_truetype` and `msdfgen` are linked by this executable
// and by nothing else in the repository.
//
// ---------------------------------------------------------------------------
// MTSDF rather than MSDF
// ---------------------------------------------------------------------------
//
// The three colour channels carry the multi-channel field, whose median
// reconstructs sharp corners. The fourth carries the plain signed distance.
// The atlas is written as RGBA either way -- an RGB PNG would be padded to
// four channels on upload -- so the true distance is free, and it is what an
// outline, a drop shadow or a glow needs later. Nothing reads it yet.
//
// ---------------------------------------------------------------------------
// The number that decides whether text looks right
// ---------------------------------------------------------------------------
//
// `screenPxRange` is the distance range measured in *screen* pixels. Below 1
// the field cannot resolve an edge; below 2 the antialiasing visibly fails and
// colour fringes spread across the glyph. It is
//
//     screenPxRange = pxRange * (screen pixels per em) / (atlas pixels per em)
//
// so it depends on the atlas *and* on how large the text is drawn -- which is
// why the shader computes it from screen-space derivatives rather than trusting
// anyone, and why this tool prints the smallest on-screen size its output can
// support. A soft-looking build with no explanation is the failure this avoids.

#include "RageV/Core/Log.h"

#include "stb_truetype.h"
#include "msdfgen.h"

#include <yaml-cpp/yaml.h>
#include <stb_write_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	// ------------------------------------------------------------------
	// Defaults, and the reasoning behind the two that matter
	// ------------------------------------------------------------------

	// Atlas pixels per em. Below about 32 the field cannot separate two edges
	// of the same channel that come within two field pixels -- which is exactly
	// what a thin stroke is -- and the glyph develops notches. 48 leaves room
	// for a light weight without quadrupling the atlas.
	constexpr int kDefaultEm = 48;

	// The distance range, in atlas pixels. Larger is not safer: too wide a
	// range and the field runs out of room between nearby edges, which shows up
	// as its own artifacts. Six is the accepted middle.
	constexpr int kDefaultRange = 6;

	// Transparent margin around each glyph in the atlas, so that bilinear
	// filtering at the edge of one glyph cannot reach into its neighbour.
	constexpr int kPadding = 2;

	struct Options
	{
		std::filesystem::path Font;
		std::filesystem::path Out;
		std::string Charset = "ascii";
		int Em = kDefaultEm;
		int Range = kDefaultRange;
	};

	struct Glyph
	{
		uint32_t Codepoint = 0;

		// Where it landed in the atlas, in pixels. Zero-sized for a glyph with
		// no outline -- a space -- which still needs its advance.
		int X = 0, Y = 0, W = 0, H = 0;

		// The quad to draw, in em units, relative to the pen position on the
		// baseline. Y is up. These are what layout multiplies by the font size.
		double Left = 0.0, Bottom = 0.0, Right = 0.0, Top = 0.0;

		double Advance = 0.0;

		// The generated field, kept until it is blitted into the atlas.
		std::vector<float> Pixels;   // RGBA, W*H*4
	};

	// --- the command line ---------------------------------------------

	bool StartsWith(const std::string& text, const char* prefix)
	{
		const size_t length = std::strlen(prefix);
		return text.size() >= length && text.compare(0, length, prefix) == 0;
	}

	void PrintUsage()
	{
		std::printf(
			"rvfont -- bake a .ttf into a distance-field atlas\n\n"
			"  rvfont --font=<path.ttf> --out=<path without extension>\n"
			"         [--em=%d] [--range=%d] [--charset=ascii|latin1|@<file>]\n\n"
			"Writes <out>.rvfont (metrics) and <out>.png (the atlas).\n\n"
			"  --em      atlas pixels per em. Below 32 thin strokes develop notches.\n"
			"  --range   distance range in atlas pixels. Wider is not better.\n"
			"  --charset ascii   U+0020..U+007E\n"
			"            latin1  the above plus U+00A0..U+00FF\n"
			"            @<file> every distinct character in a UTF-8 file, which\n"
			"                    is how a CJK subset is specified -- the whole of\n"
			"                    CJK at once is an unusably large texture.\n",
			kDefaultEm, kDefaultRange);
	}

	// --- UTF-8, only as much as a charset file needs -------------------

	std::vector<uint32_t> DecodeUtf8(const std::string& text)
	{
		std::vector<uint32_t> out;
		size_t i = 0;

		while (i < text.size())
		{
			const unsigned char lead = (unsigned char)text[i];
			uint32_t code = 0;
			size_t extra = 0;

			if (lead < 0x80)        { code = lead;        extra = 0; }
			else if (lead < 0xE0)   { code = lead & 0x1F; extra = 1; }
			else if (lead < 0xF0)   { code = lead & 0x0F; extra = 2; }
			else                    { code = lead & 0x07; extra = 3; }

			if (i + extra >= text.size())
				break;   // truncated at the end of the file; nothing to add

			for (size_t k = 1; k <= extra; k++)
				code = (code << 6) | ((unsigned char)text[i + k] & 0x3F);

			out.push_back(code);
			i += extra + 1;
		}

		return out;
	}

	bool BuildCharset(const std::string& spec, std::vector<uint32_t>& out)
	{
		auto addRange = [&out](uint32_t first, uint32_t last)
		{
			for (uint32_t c = first; c <= last; c++)
				out.push_back(c);
		};

		if (spec == "ascii")
		{
			addRange(0x20, 0x7E);
			return true;
		}

		if (spec == "latin1")
		{
			addRange(0x20, 0x7E);
			addRange(0xA0, 0xFF);
			return true;
		}

		if (StartsWith(spec, "@"))
		{
			const std::filesystem::path path = spec.substr(1);
			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				RV_CORE_ERROR("rvfont: cannot open the charset file '{0}'", path.string());
				return false;
			}

			const std::string text((std::istreambuf_iterator<char>(file)),
								   std::istreambuf_iterator<char>());

			out = DecodeUtf8(text);

			// A charset file is written by a person listing what their game
			// says, so it will have duplicates and newlines in it.
			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
			out.erase(std::remove_if(out.begin(), out.end(),
									 [](uint32_t c) { return c == '\r' || c == '\n' || c == '\t'; }),
					  out.end());

			// Space carries an advance and no outline, and text without it is
			// one long word. Nobody remembers to put it in the file.
			if (std::find(out.begin(), out.end(), 0x20u) == out.end())
				out.insert(out.begin(), 0x20u);

			return !out.empty();
		}

		RV_CORE_ERROR("rvfont: unknown charset '{0}'; expected ascii, latin1 or @<file>", spec);
		return false;
	}

	// --- stb_truetype outlines into an msdfgen shape -------------------

	// stb_truetype reports contours as a flat vertex list in font units, with a
	// move starting each contour. msdfgen wants contours of edges. The two
	// disagree about nothing except shape, so this is a transcription.
	//
	// `scale` converts font units to em units, so every number downstream --
	// bounds, advances, the quad -- is in ems and independent of the face's
	// internal grid.
	bool BuildShape(const stbtt_fontinfo& font, int glyphIndex, double scale,
					msdfgen::Shape& shape)
	{
		stbtt_vertex* vertices = nullptr;
		const int count = stbtt_GetGlyphShape(&font, glyphIndex, &vertices);
		if (count <= 0 || !vertices)
			return false;   // no outline: a space, and not an error

		msdfgen::Contour* contour = nullptr;
		msdfgen::Point2 pen(0.0, 0.0);

		auto point = [scale](short x, short y)
		{
			return msdfgen::Point2((double)x * scale, (double)y * scale);
		};

		for (int i = 0; i < count; i++)
		{
			const stbtt_vertex& v = vertices[i];

			switch (v.type)
			{
			case STBTT_vmove:
				contour = &shape.addContour();
				pen = point(v.x, v.y);
				break;

			case STBTT_vline:
			{
				if (!contour)
					break;
				const msdfgen::Point2 end = point(v.x, v.y);
				contour->addEdge(msdfgen::EdgeHolder(pen, end));
				pen = end;
				break;
			}

			case STBTT_vcurve:
			{
				if (!contour)
					break;
				const msdfgen::Point2 end = point(v.x, v.y);
				contour->addEdge(msdfgen::EdgeHolder(pen, point(v.cx, v.cy), end));
				pen = end;
				break;
			}

			case STBTT_vcubic:
			{
				if (!contour)
					break;
				const msdfgen::Point2 end = point(v.x, v.y);
				contour->addEdge(msdfgen::EdgeHolder(pen, point(v.cx, v.cy),
													 point(v.cx1, v.cy1), end));
				pen = end;
				break;
			}

			default:
				break;
			}
		}

		stbtt_FreeShape(&font, vertices);

		// TrueType winds an outer contour clockwise in a y-up space, and holes
		// the other way. msdfgen reads the opposite sense as "inside", so
		// handing it these directly produces a field that is inside-out: the
		// distances are correct and every sign is wrong, which renders as the
		// page with the letter cut out of it.
		//
		// Reversing every contour flips the sense while preserving the
		// *relative* winding, which is what keeps the counters in 'B' and '8'
		// as holes rather than turning them solid.
		//
		// This is the one place stb_truetype's convention and msdfgen's meet,
		// and there is no flag on either side that says so.
		for (msdfgen::Contour& contour : shape.contours)
			contour.reverse();

		return !shape.contours.empty();
	}
}

int main(int argc, char** argv)
{
	RageV::Log::Init();

	Options options;

	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];

		if (arg == "--help" || arg == "-h")             { PrintUsage(); return 0; }
		else if (StartsWith(arg, "--font="))            options.Font = arg.substr(7);
		else if (StartsWith(arg, "--out="))             options.Out = arg.substr(6);
		else if (StartsWith(arg, "--charset="))         options.Charset = arg.substr(10);
		else if (StartsWith(arg, "--em="))              options.Em = std::atoi(arg.c_str() + 5);
		else if (StartsWith(arg, "--range="))           options.Range = std::atoi(arg.c_str() + 8);
		else
		{
			RV_CORE_ERROR("rvfont: unknown argument '{0}'", arg);
			PrintUsage();
			return 2;
		}
	}

	if (options.Font.empty() || options.Out.empty())
	{
		PrintUsage();
		return 2;
	}

	// Clamped rather than accepted: an em of 4 or a range of 200 produces a
	// file that loads, draws, and looks broken for reasons nobody can see.
	if (options.Em < 16 || options.Em > 256)
	{
		RV_CORE_WARN("rvfont: --em={0} is outside 16..256; clamping", options.Em);
		options.Em = std::clamp(options.Em, 16, 256);
	}
	if (options.Range < 2 || options.Range > 32)
	{
		RV_CORE_WARN("rvfont: --range={0} is outside 2..32; clamping", options.Range);
		options.Range = std::clamp(options.Range, 2, 32);
	}

	// --- the font file ------------------------------------------------
	std::ifstream file(options.Font, std::ios::binary);
	if (!file)
	{
		RV_CORE_ERROR("rvfont: cannot open '{0}'", options.Font.string());
		return 1;
	}

	const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(file)),
										 std::istreambuf_iterator<char>());
	file.close();

	stbtt_fontinfo font{};
	if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
	{
		RV_CORE_ERROR("rvfont: '{0}' is not a font stb_truetype can read",
					  options.Font.string());
		return 1;
	}

	std::vector<uint32_t> charset;
	if (!BuildCharset(options.Charset, charset))
		return 1;

	// --- metrics, in em units -----------------------------------------
	//
	// Everything below is in ems rather than pixels or font units, so a layout
	// multiplies by the font size and is done. The face's internal grid stops
	// mattering the moment this scale is applied.
	int ascentUnits = 0, descentUnits = 0, lineGapUnits = 0;
	stbtt_GetFontVMetrics(&font, &ascentUnits, &descentUnits, &lineGapUnits);

	// stb exposes the em square only through the scale it would use for it.
	const double unitsPerEm = 1.0 / (double)stbtt_ScaleForMappingEmToPixels(&font, 1.0f);
	const double toEm = 1.0 / unitsPerEm;

	const double ascent = ascentUnits * toEm;
	const double descent = descentUnits * toEm;      // negative, as the font says
	const double lineHeight = (ascentUnits - descentUnits + lineGapUnits) * toEm;

	// The distance range in em units. msdfgen works in shape coordinates, and
	// the projection below converts to pixels -- so a range expressed in atlas
	// pixels has to be divided by the same scale.
	const double emSize = (double)options.Em;
	const double rangeEm = (double)options.Range / emSize;

	// --- generate ------------------------------------------------------
	std::vector<Glyph> glyphs;
	glyphs.reserve(charset.size());

	int missing = 0;

	for (uint32_t codepoint : charset)
	{
		const int index = stbtt_FindGlyphIndex(&font, (int)codepoint);
		if (index == 0 && codepoint != 0)
		{
			missing++;
			continue;   // the face has no glyph for it; leave it out rather than bake a box
		}

		Glyph glyph;
		glyph.Codepoint = codepoint;

		int advanceUnits = 0, bearingUnits = 0;
		stbtt_GetGlyphHMetrics(&font, index, &advanceUnits, &bearingUnits);
		glyph.Advance = advanceUnits * toEm;

		msdfgen::Shape shape;
		if (!BuildShape(font, index, toEm, shape))
		{
			// A space. It carries an advance and occupies no atlas.
			glyphs.push_back(glyph);
			continue;
		}

		shape.normalize();

		// Assigns each edge one of three channels so that edges meeting at a
		// sharp corner differ. This is the whole of what makes MSDF sharper
		// than SDF, and getting it wrong is invisible until a corner rounds off.
		msdfgen::edgeColoringSimple(shape, 3.0);

		const msdfgen::Shape::Bounds bounds = shape.getBounds();

		// The quad, in em units, grown by the padding *and* by the distance
		// range: the field has to extend beyond the outline or the outermost
		// texels have nothing to interpolate towards.
		const double marginEm = rangeEm * 0.5 + (double)kPadding / emSize;

		glyph.Left   = bounds.l - marginEm;
		glyph.Bottom = bounds.b - marginEm;
		glyph.Right  = bounds.r + marginEm;
		glyph.Top    = bounds.t + marginEm;

		glyph.W = (int)std::ceil((glyph.Right - glyph.Left) * emSize);
		glyph.H = (int)std::ceil((glyph.Top - glyph.Bottom) * emSize);

		if (glyph.W <= 0 || glyph.H <= 0)
		{
			glyphs.push_back(glyph);
			continue;
		}

		msdfgen::Bitmap<float, 4> bitmap(glyph.W, glyph.H);

		const msdfgen::Projection projection(msdfgen::Vector2(emSize, emSize),
											 msdfgen::Vector2(-glyph.Left, -glyph.Bottom));
		const msdfgen::SDFTransformation transformation(projection, msdfgen::Range(rangeEm));

		msdfgen::generateMTSDF(bitmap, shape, transformation);

		glyph.Pixels.resize((size_t)glyph.W * glyph.H * 4);
		for (int y = 0; y < glyph.H; y++)
		{
			for (int x = 0; x < glyph.W; x++)
			{
				const float* texel = bitmap(x, y);
				float* out = glyph.Pixels.data() + ((size_t)y * glyph.W + x) * 4;
				out[0] = texel[0];
				out[1] = texel[1];
				out[2] = texel[2];
				out[3] = texel[3];
			}
		}

		glyphs.push_back(std::move(glyph));
	}

	if (glyphs.empty())
	{
		RV_CORE_ERROR("rvfont: the face has no glyph for anything in the charset");
		return 1;
	}

	// --- pack ----------------------------------------------------------
	//
	// Shelf packing, tallest first. Not the tightest algorithm available, and
	// it does not need to be: an atlas is built once and the waste is a few
	// percent of a texture measured in hundreds of kilobytes. A better packer
	// is a day's work to save nothing anybody would notice.
	std::vector<Glyph*> order;
	order.reserve(glyphs.size());
	for (Glyph& glyph : glyphs)
	{
		if (glyph.W > 0 && glyph.H > 0)
			order.push_back(&glyph);
	}

	std::sort(order.begin(), order.end(),
			  [](const Glyph* a, const Glyph* b) { return a->H > b->H; });

	// Start from something square-ish for the area involved and grow if the
	// shelves overrun. Powers of two because some hardware still cares.
	int atlasWidth = 128;
	{
		size_t area = 0;
		for (const Glyph* glyph : order)
			area += (size_t)(glyph->W + kPadding) * (glyph->H + kPadding);

		while ((size_t)atlasWidth * atlasWidth < area && atlasWidth < 8192)
			atlasWidth *= 2;
	}

	int atlasHeight = 0;
	{
		int penX = kPadding, penY = kPadding, shelf = 0;

		for (Glyph* glyph : order)
		{
			if (penX + glyph->W + kPadding > atlasWidth)
			{
				penX = kPadding;
				penY += shelf + kPadding;
				shelf = 0;
			}

			glyph->X = penX;
			glyph->Y = penY;

			penX += glyph->W + kPadding;
			shelf = std::max(shelf, glyph->H);
		}

		atlasHeight = penY + shelf + kPadding;
	}

	// Height is rounded to a multiple of four, not to a power of two. Both
	// backends require OpenGL 4.5 or Vulkan, where non-power-of-two textures
	// are universal; rounding it up like the width doubled a 512x272 atlas to
	// 512x512 and half of it was empty.
	atlasHeight = (atlasHeight + 3) & ~3;

	// --- blit ----------------------------------------------------------
	std::vector<unsigned char> atlas((size_t)atlasWidth * atlasHeight * 4, 0);

	for (const Glyph* glyph : order)
	{
		for (int y = 0; y < glyph->H; y++)
		{
			for (int x = 0; x < glyph->W; x++)
			{
				const float* source = glyph->Pixels.data() + ((size_t)y * glyph->W + x) * 4;

				// The atlas image has y down; the field was generated y up,
				// which is the convention every glyph outline uses. The flip
				// happens here rather than in the shader, so the runtime never
				// has to know -- and a shader that flips is a shader that also
				// flips for world-space text, where it would be wrong.
				//
				// Note it flips *within the glyph's own rows*, not the whole
				// image. Flipping the image would make the recorded rectangle
				// mean something other than where the glyph is, and every
				// reader would have to undo it to build a texture coordinate.
				// This way the rectangle is simply where the glyph is.
				const int destY = glyph->Y + (glyph->H - 1 - y);
				unsigned char* dest = atlas.data()
									+ ((size_t)destY * atlasWidth + (glyph->X + x)) * 4;

				for (int c = 0; c < 4; c++)
					dest[c] = (unsigned char)std::clamp((int)(source[c] * 255.0f + 0.5f), 0, 255);
			}
		}
	}

	std::filesystem::path pngPath = options.Out;
	pngPath += ".png";
	std::filesystem::path metricsPath = options.Out;
	metricsPath += ".rvfont";

	if (const std::filesystem::path parent = pngPath.parent_path(); !parent.empty())
	{
		std::error_code error;
		std::filesystem::create_directories(parent, error);
	}

	if (stbi_write_png(pngPath.string().c_str(), atlasWidth, atlasHeight, 4,
					   atlas.data(), atlasWidth * 4) == 0)
	{
		RV_CORE_ERROR("rvfont: could not write '{0}'", pngPath.string());
		return 1;
	}

	// --- metrics -------------------------------------------------------
	YAML::Emitter out;
	out << YAML::BeginMap;
	out << YAML::Key << "Font" << YAML::Value << options.Font.filename().string();
	out << YAML::Key << "Atlas" << YAML::Value << pngPath.filename().string();
	out << YAML::Key << "AtlasWidth" << YAML::Value << atlasWidth;
	out << YAML::Key << "AtlasHeight" << YAML::Value << atlasHeight;
	// Atlas pixels per em, and the distance range in atlas pixels. The shader
	// needs both to work out how sharp it is allowed to be.
	out << YAML::Key << "EmSize" << YAML::Value << options.Em;
	out << YAML::Key << "PxRange" << YAML::Value << options.Range;
	out << YAML::Key << "Ascent" << YAML::Value << ascent;
	out << YAML::Key << "Descent" << YAML::Value << descent;
	out << YAML::Key << "LineHeight" << YAML::Value << lineHeight;

	out << YAML::Key << "Glyphs" << YAML::Value << YAML::BeginSeq;
	for (const Glyph& glyph : glyphs)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "C" << YAML::Value << glyph.Codepoint;
		out << YAML::Key << "Advance" << YAML::Value << glyph.Advance;
		if (glyph.W > 0 && glyph.H > 0)
		{
			out << YAML::Key << "Rect" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq << glyph.X << glyph.Y << glyph.W << glyph.H << YAML::EndSeq;
			out << YAML::Key << "Plane" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq << glyph.Left << glyph.Bottom << glyph.Right << glyph.Top
				<< YAML::EndSeq;
		}
		out << YAML::EndMap;
	}
	out << YAML::EndSeq;

	// --- kerning -------------------------------------------------------
	//
	// Only pairs the face actually adjusts, which for a Latin charset is a few
	// hundred out of the several thousand possible. Writing the zeroes would
	// multiply the file size for no information.
	out << YAML::Key << "Kerning" << YAML::Value << YAML::BeginSeq;
	int kerningPairs = 0;
	for (const Glyph& left : glyphs)
	{
		for (const Glyph& right : glyphs)
		{
			const int units = stbtt_GetCodepointKernAdvance(&font, (int)left.Codepoint,
														   (int)right.Codepoint);
			if (units == 0)
				continue;

			out << YAML::Flow << YAML::BeginSeq
				<< left.Codepoint << right.Codepoint << (units * toEm) << YAML::EndSeq;
			kerningPairs++;
		}
	}
	out << YAML::EndSeq;
	out << YAML::EndMap;

	std::ofstream metrics(metricsPath);
	if (!metrics)
	{
		RV_CORE_ERROR("rvfont: could not write '{0}'", metricsPath.string());
		return 1;
	}
	metrics << out.c_str() << "\n";
	metrics.close();

	// --- what the result can actually do -------------------------------
	//
	// Printed rather than left to be discovered. A font baked at a small em and
	// drawn large is fine; baked large and drawn small is fine; baked with too
	// little range and drawn small is a blurry mess with no visible cause.
	const double smallestSharp = 2.0 * emSize / (double)options.Range;

	RV_CORE_INFO("rvfont: {0} glyphs, {1} kerning pairs, atlas {2}x{3}",
				 glyphs.size(), kerningPairs, atlasWidth, atlasHeight);
	if (missing > 0)
		RV_CORE_WARN("rvfont: {0} requested characters are not in the face", missing);
	RV_CORE_INFO("rvfont: wrote {0} and {1}", metricsPath.string(), pngPath.string());
	RV_CORE_INFO("rvfont: sharp from about {0:.0f} px upwards "
				 "(below that screenPxRange drops under 2 and the antialiasing fails)",
				 smallestSharp);

	return 0;
}
