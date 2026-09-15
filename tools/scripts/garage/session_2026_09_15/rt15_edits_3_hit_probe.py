# -*- coding: utf-8 -*-
"""The probe at the hit: ProbeSlotAt in the include, used by the reflection and water passes."""
import io, os, sys

ROOT = r'C:\Users\ism19\Code\RageV\RageVEditor\assets\shaders'
CRLF, LF = '\r\n', '\n'
N = '\n'


def apply(rel, subs):
    path = os.path.join(ROOT, rel)
    raw = io.open(path, encoding='utf-8', newline='').read()
    crlf = CRLF in raw
    text = raw.replace(CRLF, LF)
    for old, new in subs:
        n = text.count(old)
        if n != 1:
            sys.exit('%s: a substitution matched %d times: %r' % (rel, n, old[:90]))
        text = text.replace(old, new)
    io.open(path, 'w', encoding='utf-8', newline='').write(text.replace(LF, CRLF) if crlf else text)
    print('%-28s done (%s)' % (rel, 'CRLF' if crlf else 'LF'))


apply('include/pbr_fragment.glsl', [(
    "// **A cube captured at a point is only right at that point.**" + N,
    "// **The one probe a traced hit is lit by** (2026-09-15): the strongest of the blend" + N
    + "// above at the hit's own position, the sky where none reaches. One and not the" + N
    + "// blend, as the bounce's GiProbeSlotAt chooses: a hit is not screen-continuous," + N
    + "// so the fade the blend buys is not seen there, and it would be two cube fetches" + N
    + "// per term where one does. The reflection and water passes used to light every" + N
    + "// hit with slot zero -- the sky, which inside a closed room is black." + N
    + "float ProbeSlotAt(vec3 position)" + N
    + "{" + N
    + "\tfloat slotA, slotB, weightA, weightB;" + N
    + "\tProbeBlendAt(position, slotA, slotB, weightA, weightB);" + N
    + "\treturn weightA > 0.0 ? slotA : 0.0;" + N
    + "}" + N
    + N
    + "// **A cube captured at a point is only right at that point.**" + N)])

apply('reflection_trace.rvshader', [(
    "\tconst TracedSurface hit = TraceSurface(P, N, direction, 1.0e4);" + N
    + "\tvec3 radiance = hit.Missed" + N
    + "\t\t\t\t  ? hit.Sky" + N
    + "\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x);" + N,
    "\tconst TracedSurface hit = TraceSurface(P, N, direction, 1.0e4);" + N
    + "\t// **The probe at the hit, not slot zero** (2026-09-15). Trace.x is documented as" + N
    + "\t// \"the probe a traced hit is lit by\" and has always been zero -- the sky, black" + N
    + "\t// inside a closed room. The lit shader's in-line ray lit its hit with the" + N
    + "\t// reflector's probe and the bounce lights its hits with the probe at the hit;" + N
    + "\t// this pass now does the latter, for the hit's diffuse and its specular alike." + N
    + "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N
    + "\tvec3 radiance = hit.Missed" + N
    + "\t\t\t\t  ? hit.Sky" + N
    + "\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, hitProbe), hitProbe);" + N)])

apply('water_trace.rvshader', [(
    "\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x)," + N,
    "\t// The probe at the hit, as the reflection pass chooses it (2026-09-15); Trace.x" + N
    + "\t// was always zero here too." + N
    + "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N
    + "\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, hitProbe), hitProbe)," + N)])
