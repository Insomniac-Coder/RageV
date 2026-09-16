# -*- coding: utf-8 -*-
"""RT-15c (2026-09-15 night): a hit's specular lobe widened by the lobe the ray was drawn from
(path-space regularisation, Kaplanyan & Dachsbacher 2013). Measured with the whole-scene arms
(rt15_scene_arms.py): STATE A's sharp, bright metal hits are what put speckle on the walls and
floor around the crossing cube and shimmer on the chrome pipes (frame-to-frame change 1.27 /
1.29 / 0.90 against HEAD's 0.98 / 1.07 / 0.85; with the hit shading off, HEAD's numbers back).
A rough receiver's one ray is one sample of a wide lobe; where it lands on the chrome cube's
mirror of a tube, the whole tube comes back in one sample. Adding the receiver's GGX alpha to
the hit's in quadrature makes the hit's lobe the convolution of the two: a mirror's ray (alpha
zero) sees the hit exactly as before, a rough wall's ray sees a rough hit."""
import io, os, sys

ROOT = r'C:\Users\ism19\Code\RageV'
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
            sys.exit('%s: a substitution matched %d times: %r' % (rel, n, old[:100]))
        text = text.replace(old, new)
    io.open(path, 'w', encoding='utf-8', newline='').write(text.replace(LF, CRLF) if crlf else text)
    print('%-58s %d substitutions (%s)' % (rel, len(subs), 'CRLF' if crlf else 'LF'))


apply('RageVEditor/assets/shaders/include/pbr_fragment.glsl', [
    ("float DistributionGGX(vec3 N, vec3 H, float roughness);" + N
     + "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);" + N
     + "vec3 FresnelSchlick(float cosTheta, vec3 F0);" + N
     + "#endif" + N,
     "float DistributionGGX(vec3 N, vec3 H, float roughness);" + N
     + "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);" + N
     + "vec3 FresnelSchlick(float cosTheta, vec3 F0);" + N
     + "// **The GGX alpha of the lobe the ray was drawn from** (RT-15c), set by the pass before" + N
     + "// it traces: the receiver's roughness squared. Zero -- a mirror ray, or a pass that" + N
     + "// does not say -- and a hit is shaded exactly as it was. See TraceSurface." + N
     + "float g_RayLobeAlpha = 0.0;" + N
     + "#endif" + N),
    ("\troughness = clamp(roughness, 0.045, 1.0);   // fully smooth aliases badly, as on screen" + N,
     "\troughness = clamp(roughness, 0.045, 1.0);   // fully smooth aliases badly, as on screen" + N
     + "\t// **Widened by the lobe the ray was drawn from** (path-space regularisation," + N
     + "\t// Kaplanyan and Dachsbacher 2013). A rough receiver's one ray is one sample of a" + N
     + "\t// wide lobe, and a sharp highlight where it lands -- the chrome cube's mirror of a" + N
     + "\t// tube -- comes back whole in that one sample: a firefly on the receiver that no" + N
     + "\t// history settles (the garage's walls and floor speckled around the crossing cube" + N
     + "\t// and the chrome pipes shimmered, 2026-09-15, measured against HEAD). The" + N
     + "\t// receiver's lobe integrates over that highlight anyway, so the two alphas are" + N
     + "\t// added in quadrature and the hit's lobe becomes the convolution of the two: a" + N
     + "\t// mirror's ray (alpha zero) sees the hit exactly as before, a rough wall's ray" + N
     + "\t// sees a rough hit. The probe's mip, the field's lamp and the live lamps all" + N
     + "\t// shade with this roughness below." + N
     + "\tif (g_RayLobeAlpha > 0.0)" + N
     + "\t{" + N
     + "\t\tconst float alphaHit = roughness * roughness;" + N
     + "\t\troughness = clamp(sqrt(sqrt(alphaHit * alphaHit + g_RayLobeAlpha * g_RayLobeAlpha)), 0.045, 1.0);" + N
     + "\t}" + N),
])
apply('RageVEditor/assets/shaders/reflection_trace.rvshader', [
    ("\tconst float roughness = clamp(surface.b, 0.0, 1.0);" + N
     + "\tconst vec2 gloss = u_Lamps.Trace.yz;" + N,
     "\tconst float roughness = clamp(surface.b, 0.0, 1.0);" + N
     + "\t// RT-15c: what this texel's rays are drawn from, for the hits' lobes (TraceSurface)." + N
     + "\tg_RayLobeAlpha = roughness * roughness;" + N
     + "\tconst vec2 gloss = u_Lamps.Trace.yz;" + N),
])
