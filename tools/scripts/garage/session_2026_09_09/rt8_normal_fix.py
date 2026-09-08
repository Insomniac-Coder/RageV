# -*- coding: utf-8 -*-
"""RT-8: the sea's normal is not octahedral, and the contract was decoding it
as though it were.

`FetchWaterPoint` in include/water_lamps.glsl:

    p.N = vec3(surface.x, sqrt(max(1.0 - dot(surface.xy, surface.xy), 0.0)),
               surface.y);

Two horizontal components with the vertical recovered, because a sea's normal
always points up -- so nothing is lost and nothing is quantised, which is a
better encoding than octahedral *for a sea* and a different one from the rest
of the frame. Every place this session pointed the shared machinery at the
sea's surface lane has been running `OctDecode` over it since job 3.

It shows exactly as you would expect a wrong normal to show: the lobe points
somewhere it should not, so the sea comes out dimmer in the mid-tones with a
narrower, spikier highlight. Measured on job 1 before the fix: mean 16.76 ->
14.41 and the 99th percentile 112 -> 75 at the glitter camera.

`Probe.w` becomes a bitfield rather than a second flag: bit 0 the position
lane, bit 1 the up-normal lane. A layer describes itself in one number.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


DECODE = '''// **RT-8: how this layer encodes a normal.** Octahedral for the G-buffer, and
// two horizontal components with the vertical recovered for the sea -- whose
// normal always points up, so nothing is lost and nothing is quantised. That
// is the better encoding for a sea and a different one from the rest of the
// frame, and reading one as the other is silent: the lobe simply points
// somewhere it should not. Measured when it was: the sea's mid-tones a seventh
// dimmer and its highlight a third narrower.
vec3 DecodeSurfaceNormal(vec2 e)
{
	if (UpNormalLane())
		return vec3(e.x, sqrt(max(1.0 - dot(e, e), 0.0)), e.y);
	return OctDecode(e);
}

'''

for path in (r'RageVEditor/assets/shaders/reflection_accumulate.rvshader',
             r'RageVEditor/assets/shaders/reflection_blur.rvshader'):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    if 'UpNormalLane' in s:
        sys.exit(path + ': already patched')
    old = 'bool PositionLane() { return u_Reflection.Probe.w > 0.5; }'
    new = ('// A bitfield, not a flag: a layer describes itself in one number.\n'
           'bool PositionLane() { return (int(u_Reflection.Probe.w + 0.5) & 1) != 0; }\n'
           'bool UpNormalLane() { return (int(u_Reflection.Probe.w + 0.5) & 2) != 0; }\n'
           '\n' + DECODE.rstrip())
    if s.count(old) != 1:
        sys.exit('%s: PositionLane matched %d' % (path, s.count(old)))
    s = s.replace(old, new, 1)
    n = s.count('OctDecode(surface.rg)') + s.count('OctDecode(c.reflector.rg)') \
        + s.count('OctDecode(atSurface.reflector.rg)')
    s = s.replace('OctDecode(surface.rg)', 'DecodeSurfaceNormal(surface.rg)')
    s = s.replace('OctDecode(c.reflector.rg)', 'DecodeSurfaceNormal(c.reflector.rg)')
    s = s.replace('OctDecode(atSurface.reflector.rg)',
                  'DecodeSurfaceNormal(atSurface.reflector.rg)')
    io.open(path, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print('%s: %d normal decodes routed through the layer' % (path, n))

patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
('\t\t\tbool PositionLane = false;',
 '\t\t\tbool PositionLane = false;\n'
 '\t\t\t// **RT-8: and how it encodes a normal.** The G-buffer\'s is\n'
 '\t\t\t// octahedral; the sea\'s is two horizontal components with the\n'
 '\t\t\t// vertical recovered, because a sea\'s normal always points up.\n'
 '\t\t\t// Reading one as the other is silent and costs the sea a seventh of\n'
 '\t\t\t// its brightness -- it was measured doing exactly that.\n'
 '\t\t\tbool UpNormalLane = false;', 'SignalParams'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
('''		// RT-8: one where the depth slot carries the layer's own position.
		// The w lane, because the blur's z is its stride.
		push.Probe.w = signal.PositionLane ? 1.0f : 0.0f;''',
 '''		// **RT-8: what shape this layer is, as a bitfield.** Bit 0: the depth
		// slot carries the layer's own position rather than clip depth. Bit 1:
		// its normal lane is two horizontal components with the vertical
		// recovered rather than octahedral. The w lane, because the blur's z is
		// its stride.
		push.Probe.w = (signal.PositionLane ? 1.0f : 0.0f)
					 + (signal.UpNormalLane ? 2.0f : 0.0f);''', 'accumulate probe'),
('''		// RT-8: and the same flag the accumulate sets, from the same lane, so a
		// signal cannot have one pass reading a position and the other a depth.
		push.Probe.w = signal.PositionLane ? 1.0f : 0.0f;''',
 '''		// RT-8: and the same bitfield the accumulate sets, from the same lane,
		// so a signal cannot have one pass reading a layer one way and the
		// other reading it another.
		push.Probe.w = (signal.PositionLane ? 1.0f : 0.0f)
					 + (signal.UpNormalLane ? 2.0f : 0.0f);''', 'blur probe'),
('\t\tsignal.PositionLane = true;\n\t\t// The sea is one surface, and its layer carries no id lane.',
 '\t\tsignal.PositionLane = true;\n'
 '\t\tsignal.UpNormalLane = true;\n'
 '\t\t// The sea is one surface, and its layer carries no id lane.', 'mirror signal'),
('\t\t// The sea knows where it is; the depth buffer under it does not.\n\t\tsignal.PositionLane = true;',
 '\t\t// The sea knows where it is; the depth buffer under it does not.\n'
 '\t\tsignal.PositionLane = true;\n'
 '\t\t// And its normal always points up, so it is stored as two components.\n'
 '\t\tsignal.UpNormalLane = true;', 'lamp signal'),
])

patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
('\t\t\tbool       PositionLane = false;',
 '\t\t\tbool       PositionLane = false;\n'
 '\t\t\t// RT-8: and whether the normal lane is the sea\'s two-component form.\n'
 '\t\t\tbool       UpNormalLane = false;', 'guidance'),
('\t\t\tparams.PositionLane = params.PositionLane || guide.PositionLane;',
 '\t\t\tparams.PositionLane = params.PositionLane || guide.PositionLane;\n'
 '\t\t\tparams.UpNormalLane = params.UpNormalLane || guide.UpNormalLane;', 'resolve'),
('\t\t\t\t\t\t\tseaGuide.PositionLane = true;',
 '\t\t\t\t\t\t\tseaGuide.PositionLane = true;\n'
 '\t\t\t\t\t\t\tseaGuide.UpNormalLane = true;', 'lamp guide'),
('\t\t\t\t\t\tseaRayGuide.PositionLane = true;',
 '\t\t\t\t\t\tseaRayGuide.PositionLane = true;\n'
 '\t\t\t\t\t\tseaRayGuide.UpNormalLane = true;', 'ray guide'),
])

# --- and the direct pass's own copy -------------------------------------
patch(r'RageVEditor/assets/shaders/direct_trace.rvshader', [
('''	p.P = position.xyz;
	p.N = OctDecode(surface.rg);''',
 '''	p.P = position.xyz;
	// **Not octahedral.** The sea's surface pass writes the two horizontal
	// components and the vertical is recovered, because a sea's normal always
	// points up -- see FetchWaterPoint in include/water_lamps.glsl, which is
	// the copy this pass replaces. Decoding it as octahedral is silent and
	// costs the sea a seventh of its brightness; it was measured doing so.
	p.N = vec3(surface.x, sqrt(max(1.0 - dot(surface.xy, surface.xy), 0.0)), surface.y);''',
 'water normal'),
])
