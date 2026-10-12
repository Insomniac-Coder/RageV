"""Step 1 of the lamp-point pooling: move the emitter rows and the functions the resolve also needs
out of reflection_trace into include/reflection_emitters.glsl, verbatim, so the trace compiles to
the same thing. Asserts every cut is found exactly once."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

SH = (_RAGEV + '/RageVEditor/assets/shaders/')
T = SH + 'reflection_trace.rvshader'
t = io.open(T, encoding='utf-8', newline='').read()
assert t.count('\r\n') == t.count('\n')
t = t.replace('\r\n', '\n')


def cut(start, end_incl):
    """Remove and return the text from `start` up to and including `end_incl` (each unique)."""
    global t
    assert t.count(start) == 1, ('start', start[:60])
    i = t.index(start)
    j = t.index(end_incl, i)
    assert t.count(end_incl) >= 1
    j += len(end_incl)
    piece = t[i:j]
    t = t[:i] + t[j:]
    return piece


# A: the note on the re-light, the rows' description, the struct and both buffers.
relight_note = cut('// **And the rows reach the re-light as well (2026-09-22).**',
                   'which is RT-23. The re-light draws what the trace drew, rows included.\n')
block = cut('#if !defined(RV_REFLECTION_RECORD)\n\n// **RT-11: the scene\'s area emitters, and where on each one the light is.**',
            '\tfloat u_EmitterCdf[];\n};\n#endif\n')
rows = block[len('#if !defined(RV_REFLECTION_RECORD)\n\n'):-len('#endif\n')]
rows = rows.replace('layout(std430, set = 3, binding = 10) readonly buffer ReflectionEmitterBlock',
                    'layout(std430, set = 3, binding = RV_EMITTER_ROWS_BINDING) readonly buffer ReflectionEmitterBlock')
rows = rows.replace('layout(std430, set = 3, binding = 11) readonly buffer ReflectionEmitterCdfBlock',
                    'layout(std430, set = 3, binding = RV_EMITTER_CDF_BINDING) readonly buffer ReflectionEmitterCdfBlock')
assert 'RV_EMITTER_ROWS_BINDING' in rows and 'RV_EMITTER_CDF_BINDING' in rows

# B: LobePdf, EmitterDensityAt and EmitterScore, with their comments.
lobe = cut('// **How densely the lobe this pass samples points at `L`**',
           '\treturn D * NoH / (4.0 * VoH);\n}\n\n')
density = cut('// **How densely the aimed sampler would have drawn the point the ray struck.**',
              '\treturn density;\n}\n\n')
score = cut("// **What one emitter is worth to this texel, before any ray is cast.**",
            '\treturn power * best;\n}\n\n')

HEADER = """// **The area emitters a reflection aims at -- the rows, and what it takes to judge a point on one
// (RT-24, 2026-09-25).** Moved out of reflection_trace.rvshader verbatim, because the resolve now
// needs the same answers: it pools the lamp points its neighbours aimed at, and weighs each one
// from its own position with the very functions the trace drew it with (reflection_resolve.rvshader,
// "the lamp points, pooled"). One copy, so the two can never disagree about a lamp.
//
// The includer defines RV_EMITTER_ROWS_BINDING and RV_EMITTER_CDF_BINDING (set 3), and a
// `bool VisibleNormals()` before this file: LobePdf asks it which lobe the rays are drawn from.
#ifndef RV_REFLECTION_EMITTERS_GLSL
#define RV_REFLECTION_EMITTERS_GLSL

"""

HELPERS = """// **Where a stored draw puts its point, and what light and density the draw had there.**
//
// The trace keeps each texel's first aimed draw as the numbers it was drawn from (o_LampSample): the
// row, the side of the fitting the point was stepped out to, and the draw itself -- the aiming table's
// uv where the row has one, the rectangle's own (su, sv) where it radiates evenly -- plus the density
// the draw had there, which the table's cell fixed and a rounded uv could not find again at a cell's
// edge. These rebuild the point exactly as AimedAtEmitters built it.
vec3 EmitterNormal(GiEmitter light)
{
	return vec3(light.TangentU.w, light.TangentV.w, light.Radiance.w);
}

// Which face of the fitting a point at P sees: the side AimedAtEmitters steps its point out to.
float EmitterSide(GiEmitter light, vec3 P)
{
	const vec3 flat_ = light.CentreArea.xyz;
	return dot(P - flat_, EmitterNormal(light)) >= 0.0 ? 1.0 : -1.0;
}

vec3 EmitterPointFromDraw(GiEmitter light, vec2 drawn, float side)
{
	float su = drawn.x;
	float sv = drawn.y;
	if (int(light.Aim.y + 0.5) > 0)
	{
		su = light.UvToSurface0.x * drawn.x + light.UvToSurface0.y * drawn.y + light.UvToSurface0.z;
		sv = light.UvToSurface1.x * drawn.x + light.UvToSurface1.y * drawn.y + light.UvToSurface1.z;
	}
	const vec3 flat_ = light.CentreArea.xyz + light.TangentU.xyz * su + light.TangentV.xyz * sv;
	return flat_ + EmitterNormal(light) * (light.UvToSurface1.w * side);
}

vec3 EmitterRadianceFromDraw(GiEmitter light, vec2 drawn)
{
	vec3 radiance = light.Radiance.rgb;
	if (int(light.Aim.y + 0.5) > 0)
		radiance *= textureLod(u_Textures[nonuniformEXT(int(light.Aim.z + 0.5))], drawn, 0.0).rgb;
	return radiance;
}

#endif
"""

inc = HEADER + rows + '\n' + lobe + density + score + HELPERS
io.open(SH + 'include/reflection_emitters.glsl', 'w', encoding='utf-8', newline='').write(inc.replace('\n', '\r\n'))

# The include goes after VisibleNormals(), which LobePdf calls.
anchor = 'bool VisibleNormals() { return u_Lamps.Nee.y > 0.5; }\n'
assert t.count(anchor) == 1
t = t.replace(anchor, anchor + '\n#if !defined(RV_REFLECTION_RECORD)\n' + relight_note
              + '#define RV_EMITTER_ROWS_BINDING 10\n#define RV_EMITTER_CDF_BINDING 11\n'
              '#include "include/reflection_emitters.glsl"\n#endif\n')
io.open(T, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n'))
print('ok: include %d lines; trace %d lines' % (inc.count('\n'), t.count('\n')))
