# -*- coding: utf-8 -*-
"""RT-8 job 3, the shader half: the contract learns to read a position lane.

The contract rebuilds the surface under a texel from the depth buffer, which
is why the sea could never join it -- water writes no depth, and the buffer
under the sea holds the seabed. But the sea already knows where it is, in full
floats, which is *better* than a reconstruction. So the depth binding gains a
second meaning: a position-and-mask lane, told apart by a push-constant flag.

Nothing about the depth path changes. Both shaders take the same flag from the
same slot, so a signal cannot have one reading a position and the other a depth.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)

NOTE = '''// **RT-8: and the same surface from a layer that knows where it is.** The
// contract rebuilds P from the depth buffer, which assumes the signal it is
// filtering sits on a surface the depth buffer describes. The sea does not:
// water writes no depth, and the buffer under it holds the seabed and the
// pier, so every test here would have been asking about the wrong surface.
// That, and not the plane test, was what kept the sea out of the contract.
//
// The sea does know its own position -- it writes it in full floats, because
// a bay is a kilometre across -- so the depth binding takes a second meaning
// rather than a second binding: **Probe.z is one when what is bound at the
// depth slot is a position-and-mask lane, xyz the world point and w the mask.**
// That is not a workaround for the sea; it is the more exact of the two, and
// any future layer with a position of its own can take the same route.
bool PositionLane() { return u_Reflection.Probe.z > 0.5; }

'''


def patch(path, extra_out):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    if 'PositionLane' in s:
        sys.exit(path + ': already patched')

    old = '''	const vec4 surface = texelFetch(u_Surface, texel, 0);
	const float depth = texelFetch(u_Depth, texel, 0).r;
	if (surface.b <= 0.0 || depth >= 1.0)
		return false;'''
    new = '''	const vec4 surface = texelFetch(u_Surface, texel, 0);
	const vec4 depthLane = texelFetch(u_Depth, texel, 0);
	if (PositionLane())
	{
		// The layer's own point, and its mask where the depth test would be.
		if (surface.b <= 0.0 || depthLane.w <= 0.5)
			return false;
		P = depthLane.xyz;
		N = OctDecode(surface.rg);
''' + extra_out + '''		return true;
	}
	const float depth = depthLane.r;
	if (surface.b <= 0.0 || depth >= 1.0)
		return false;'''
    if s.count(old) != 1:
        sys.exit('%s: SurfaceAt matched %d' % (path, s.count(old)))
    s = s.replace(old, new, 1)

    # The flag itself goes in front of the first function that asks for it.
    anchor = 'bool SurfaceAt('
    if s.count(anchor) < 1:
        sys.exit(path + ': no SurfaceAt')
    i = s.index(anchor)
    # Back up over the comment block immediately above it.
    j = s.rindex(LF + LF, 0, i) + 2
    s = s[:j] + NOTE + s[j:]
    io.open(path, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(path, '-- the depth slot may be a position lane')


patch(r'RageVEditor/assets/shaders/reflection_accumulate.rvshader', '')
patch(r'RageVEditor/assets/shaders/reflection_blur.rvshader',
      '		roughness = clamp(surface.b, 0.0, 1.0);\n')
