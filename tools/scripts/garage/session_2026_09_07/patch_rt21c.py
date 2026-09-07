"""RT-2.1 (c): the parallax march reads the height at the pixel's own mip.

Both marches (Parallax for a material, ParallaxLayer for a terrain layer)
read level zero of the height map whatever the pixel's footprint; at a
kilometre every fetch of a layer's dozen was a cache miss, and Headland's
four terrain layers cost 5.4 ms of a 20 ms frame that way (G-buffer 3.6 ms
of which 3.1, lit 4.4 of which 2.3), measured by disabling the march.
Now the mip is the one the hardware would filter the map at, from the
coordinate's explicit derivatives (FootprintLod), so a far pixel marches the
relief its footprint sees -- filtered as its colour is -- and fetches
neighbours. Level zero stays level zero up close, where the footprint is
under a texel."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if s.count('\r\n') > s.count('\n') / 2 else '\n'


def rep(s, old, new, count=1):
    for ending in ('\n', '\r\n'):
        o = old.replace('\n', ending)
        if s.count(o) == count:
            return s.replace(o, new.replace('\n', ending))
    raise AssertionError('anchor: %r' % old[:80])


assert 'FootprintLod' not in s

# The helper, before both material paths.
s = rep(s, """#ifndef RV_TRACE_ONLY

#ifndef RV_LAYERED
vec3 PerturbNormal(mat3 TBN, vec2 uv)
""", """#ifndef RV_TRACE_ONLY

// The mip a map is filtered at for this pixel, from the coordinate's explicit
// derivatives -- the isotropic rule the hardware's own level of detail uses,
// rho = the longer of the two texel-space derivatives, lambda = log2(rho).
//
// **The parallax marches read the height at this level, not at level zero.**
// A march at level zero samples one texel's relief wherever the pixel
// happens to land, which at a distance is noise the colour beside it has
// already filtered away; and it costs more than the rest of the material
// put together, because at a kilometre every fetch of a layer's dozen lands
// on a different cache line. Headland's four terrain layers spent 5.4 ms of a
// 20 ms frame that way -- 3.1 in the G-buffer pass, 2.3 in the lit pass --
// measured by switching the march off (RT-2.1). At the footprint's level the
// march finds the relief the pixel actually sees, filtered as its colour is,
// and its fetches are neighbours. Up close the footprint is under a texel
// and this is level zero, as before.
float FootprintLod(sampler2D map, vec2 ddx, vec2 ddy)
{
	const vec2 size = vec2(textureSize(map, 0));
	const float rho = max(length(ddx * size), length(ddy * size));
	return max(log2(max(rho, 1e-8)), 0.0);
}

#ifndef RV_LAYERED
vec3 PerturbNormal(mat3 TBN, vec2 uv)
""")

# The material's march.
s = rep(s, """vec2 Parallax(vec2 uv, vec3 viewTS)
{
	float scale = u_Material.HeightScale;
""", """vec2 Parallax(vec2 uv, vec3 viewTS, float lod)
{
	float scale = u_Material.HeightScale;
""")
s = rep(s, "textureLod(u_HeightMap, cur, 0.0)", "textureLod(u_HeightMap, cur, lod)", 2)
s = rep(s, """	if (HasMap(MAP_HEIGHT))
		uv = Parallax(uv, transpose(TBN) * V);
""", """	// The height map's mip for this pixel, before the branch: derivatives
	// belong in uniform control flow.
	const float heightLod = FootprintLod(u_HeightMap, dFdx(uv), dFdy(uv));
	if (HasMap(MAP_HEIGHT))
		uv = Parallax(uv, transpose(TBN) * V, heightLod);
""")

# The layer's march.
s = rep(s, "vec2 ParallaxLayer(sampler2D surface, vec2 uv, vec3 viewTS, float scale)",
        "vec2 ParallaxLayer(sampler2D surface, vec2 uv, vec3 viewTS, float scale, float lod)")
s = rep(s, "textureLod(surface, cur, 0.0)", "textureLod(surface, cur, lod)", 2)

# The two call sites inside the SHADE_LAYER macro: each line ends in a
# continuation backslash after padding; the new argument goes on a line of
# its own, padded to the same width.
def macro_call(s, tail, extra):
    pattern = re.compile(r'^(?P<lead>\t+)normalize\(transpose\((?P<tbn>TBNW?)\) \* V\), ' + re.escape(tail)
                         + r'\);(?P<pad> *)\\(?P<nl>\r?\n)', re.M)
    m = pattern.search(s)
    assert m, tail
    lead, tbn, pad, eol = m.group('lead'), m.group('tbn'), m.group('pad'), m.group('nl')
    width = len(m.group(0)) - len(eol) - 1          # columns before the backslash (tabs count one)
    first = f"{lead}normalize(transpose({tbn}) * V), {tail},"
    second = f"{lead}{extra});"
    line1 = first + ' ' * max(width - len(first), 1) + '\\' + eol
    line2 = second + ' ' * max(width - len(second), 1) + '\\' + eol
    assert pattern.subn('', s)[1] == 1
    return s[:m.start()] + line1 + line2 + s[m.end():]


s = macro_call(s, "pom * (1.0 - wall)", "FootprintLod(LAYER_ROUGHNESS(i), ddxL, ddyL)")
s = macro_call(s, "pom * wall", "FootprintLod(LAYER_ROUGHNESS(i), ddxW, ddyW)")

open(p, 'wb').write(s.encode('utf-8'))
print('ok', p)
# Show the macro lines as patched.
i = s.index('uvL = ParallaxLayer(')
print(s[i - 40:i + 420].replace('\r', ''))
