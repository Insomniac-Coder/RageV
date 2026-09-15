# -*- coding: utf-8 -*-
"""The remaining four files of the hit-specular edit (pbr_fragment.glsl is already done)."""
import io, os, sys

ROOT = r'C:\Users\ism19\Code\RageV\RageVEditor\assets\shaders'
CRLF, LF = '\r\n', '\n'
N = '\n'


def apply(rel, subs, by_line=None):
    path = os.path.join(ROOT, rel)
    raw = io.open(path, encoding='utf-8', newline='').read()
    crlf = CRLF in raw
    text = raw.replace(CRLF, LF)
    if by_line:
        lines = text.split(LF)
        for finder in by_line:
            hits = [i for i, l in enumerate(lines) if finder[0] in l]
            if len(hits) != 1:
                sys.exit('%s: %r found %d times' % (rel, finder[0], len(hits)))
            i = hits[0]
            lines = finder[1](lines, i)
        text = LF.join(lines)
    for old, new in subs:
        n = text.count(old)
        if n != 1:
            sys.exit('%s: a substitution matched %d times: %r' % (rel, n, old[:90]))
        text = text.replace(old, new)
    io.open(path, 'w', encoding='utf-8', newline='').write(text.replace(LF, CRLF) if crlf else text)
    print('%-28s done (%s)' % (rel, 'CRLF' if crlf else 'LF'))


def rtgi_second(lines, i):
    nxt = lines[i + 1]
    tail = 'GiProbeSlotAt(second.Position)));'
    if not nxt.rstrip().endswith(tail):
        sys.exit('rtgi: the line after the second-bounce ShadeTraced is %r' % nxt)
    lines[i + 1] = nxt.replace(tail, 'GiProbeSlotAt(second.Position)),')
    indent = nxt[:len(nxt) - len(nxt.lstrip())]
    lines.insert(i + 2, indent + 'GiProbeSlotAt(second.Position));')
    return lines


apply('rtgi_trace.rvshader',
      [("\t\tvec3 shaded = max(ShadeTraced(first, arriving) - (first.IsEmitter ? first.Emissive : vec3(0.0))," + N,
        "\t\tvec3 shaded = max(ShadeTraced(first, arriving, GiProbeSlotAt(first.Position))" + N
        + "\t\t\t\t\t\t  - (first.IsEmitter ? first.Emissive : vec3(0.0))," + N)],
      by_line=[(': ShadeTraced(second, ProbeIrradiance(second.Normal,', rtgi_second)])

apply('irradiance_fill.rvshader',
      [("\t\tconst vec3 radiance = ShadeTraced(hit, ProbeIrradiance(hit.Normal, 0.0));" + N,
        "\t\tconst vec3 radiance = ShadeTraced(hit, ProbeIrradiance(hit.Normal, 0.0), 0.0);" + N)])

apply('water_trace.rvshader',
      [("#define RV_TRACE_ONLY" + N + "#include \"include/pbr_fragment.glsl\"" + N,
        "#define RV_TRACE_ONLY" + N
        + "// A hit is an image the eye sees: shaded with its specular half (pbr_fragment's" + N
        + "// RV_HIT_SPECULAR), so a metal in the sea's mirror is not black." + N
        + "#define RV_HIT_SPECULAR" + N
        + "#include \"include/pbr_fragment.glsl\"" + N),
       ("\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x))," + N,
        "\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x)," + N)])

apply('reflection_trace.rvshader',
      [("#define RV_TRACE_ONLY" + N
        + "// This pass's rays count in the reflection lane, not the bounce's." + N
        + "#define RV_REFLECTION_TRACE" + N,
        "#define RV_TRACE_ONLY" + N
        + "// This pass's rays count in the reflection lane, not the bounce's." + N
        + "#define RV_REFLECTION_TRACE" + N
        + "// **A hit is shaded with its specular half** (2026-09-15, pbr_fragment's" + N
        + "// RV_HIT_SPECULAR). Lambert alone left every metal black in every reflection --" + N
        + "// the garage's box is Metallic 1, so a flat mirror facing the camera showed nothing" + N
        + "// of the wall behind it. All three variants of this pass, so the measured change's" + N
        + "// record and re-light compare the same shading." + N
        + "#define RV_HIT_SPECULAR" + N),
       ("\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x));" + N,
        "\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x);" + N)])
