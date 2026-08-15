// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// The layout of the hi-Z atlas: a min-depth pyramid over the SSR trace's
// pixels, packed into one 2D target because the RHI has no per-mip render
// targets and the trace has to reach every level from one binding.
//
//   level 0 : the trace resolution, at (0, 0), size (W, H)
//   level k : (W >> k, H >> k), stacked down the right-hand column at
//             x = W, y = H - (H >> (k - 1))  -- so level 1 sits at the top
//             of the column, level 2 below it, and so on.
//
// The atlas is (W + W/2) wide and H tall. Two shaders read this file -- the
// pass that builds the atlas and the trace that walks it -- and the whole
// point of it being one file is that they cannot disagree about where a
// level lives. ENGINE-NOTES 7ag.

ivec2 HiZLevelSize(ivec2 base, int level)
{
	return max(base >> level, ivec2(1));
}

ivec2 HiZLevelOrigin(ivec2 base, int level)
{
	if (level == 0)
		return ivec2(0);
	return ivec2(base.x, base.y - (base.y >> (level - 1)));
}

ivec2 HiZAtlasSize(ivec2 base)
{
	return ivec2(base.x + (base.x >> 1), base.y);
}
