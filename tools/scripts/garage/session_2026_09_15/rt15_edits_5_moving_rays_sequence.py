# -*- coding: utf-8 -*-
"""RT-15b, second cut of the moving rays (2026-09-15): a texel's rays take consecutive turns of
its own low-discrepancy sequence instead of lattice offsets. The offsets made a texel's extra
rays the first rays of its diagonal neighbours (the R2 step is the same per ray as per texel),
so the resolve averaged duplicates and the face came out with fine horizontal striping. One
ray a texel is bit for bit what it was: the draw index is then the frame's."""
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


apply('RageVEditor/assets/shaders/reflection_trace.rvshader', [
    # the sampler: back to (texel, frame), the frame being the draw index
    ("// `ray`: RT-15b, which of a texel's rays this frame -- each takes its own turn of the" + N
     + "// lattice so a texel's rays cover different parts of the lobe; zero is bit-identical" + N
     + "// to the one-ray draw." + N
     + "vec3 GlossyReflectionLD(vec3 N, vec3 V, float roughness, ivec2 texel, uint frame, uint ray)" + N + "{" + N,
     "// `frame` is the draw index: the frame's own, or (RT-15b) the frame's times the rays a" + N
     + "// texel casts plus the ray's number, so a texel's rays take consecutive turns of the" + N
     + "// same sequence and stay stratified across rays and frames alike." + N
     + "vec3 GlossyReflectionLD(vec3 N, vec3 V, float roughness, ivec2 texel, uint frame)" + N + "{" + N),
    ("\tconst vec2 rotation = fract(vec2(texel) * vec2(0.7548776662, 0.5698402910)" + N
     + "\t\t\t\t\t\t\t  + vec2(float(ray)) * vec2(0.7548776662, 0.5698402910));" + N,
     "\tconst vec2 rotation = fract(vec2(texel) * vec2(0.7548776662, 0.5698402910));" + N),
    # the ray count decided before the first ray, which then takes draw index frame * rays
    ("\tconst vec3 direction = GlossyReflectionLD(N, V, roughness, texel, frame, 0u);" + N,
     "#if !defined(RV_REFLECTION_RECORD) && !defined(RV_REFLECTION_RELIGHT)" + N
     + "\t// **RT-15b: more rays where the surface moves on its own.** A flat mirror sliding" + N
     + "\t// in its own plane shows a still picture, so a screen texel's history is only as" + N
     + "\t// old as the frames the mirror has covered it: the strip at its leading edge is" + N
     + "\t// new reflected content every frame, and one ray a texel at roughness 0.12 needs" + N
     + "\t// tens of frames to settle -- the chrome cube's face was speckle and 4-texel bands" + N
     + "\t// (2026-09-15). A curved mover's image history is young for the same reason. So a" + N
     + "\t// texel whose surface moved on its own this frame (the velocity lane with the" + N
     + "\t// camera's motion taken out, as the change record asks it) casts Trace.x rays and" + N
     + "\t// writes their mean; every other texel casts its one, bit for bit as before. The" + N
     + "\t// cost is the moving texels' alone: a few thousand extra rays a frame for the cube," + N
     + "\t// against the million and a half this pass casts." + N
     + "\tint rays = 1;" + N
     + "\t{" + N
     + "\t\tconst int movingRays = clamp(int(u_Lamps.Trace.x + 0.5), 1, 16);" + N
     + "\t\tif (movingRays > 1 && RecordMovedOnItsOwn(texelFetch(u_Velocity, texel, 0).xy, P, vec2(size)))" + N
     + "\t\t\trays = movingRays;" + N
     + "\t}" + N
     + "\t// The rays take consecutive turns of the texel's sequence, `rays` a frame; one ray," + N
     + "\t// and the draw is the frame's, bit for bit as before." + N
     + "\tconst uint draw = frame * uint(rays);" + N
     + "#else" + N
     + "\tconst uint draw = frame;" + N
     + "#endif" + N
     + "\tconst vec3 direction = GlossyReflectionLD(N, V, roughness, texel, draw);" + N),
    ("#if !defined(RV_REFLECTION_RECORD) && !defined(RV_REFLECTION_RELIGHT)" + N
     + "\t// **RT-15b: more rays where the surface moves on its own.** A flat mirror sliding" + N
     + "\t// in its own plane shows a still picture, so a screen texel's history is only as" + N
     + "\t// old as the frames the mirror has covered it: the strip at its leading edge is" + N
     + "\t// new reflected content every frame, and one ray a texel at roughness 0.12 needs" + N
     + "\t// tens of frames to settle -- the chrome cube's face was speckle and 4-texel bands" + N
     + "\t// (2026-09-15). A curved mover's image history is young for the same reason. So a" + N
     + "\t// texel whose surface moved on its own this frame (the velocity lane with the" + N
     + "\t// camera's motion taken out, as the change record asks it) casts Trace.x rays and" + N
     + "\t// writes their mean; every other texel casts its one, bit for bit as before. The" + N
     + "\t// cost is the moving texels' alone: a few thousand extra rays a frame for the cube," + N
     + "\t// against the million and a half this pass casts." + N
     + "\tint rays = 1;" + N
     + "\tvec3 radianceSum = radiance;" + N
     + "\tfloat travelledSum = travelled;" + N
     + "\tvec3 directionSum = direction;" + N
     + "\tvec3 travelSum = hit.Missed ? vec3(0.0) : hit.Travel;" + N
     + "\tfloat moversStruck = (!hit.Missed && dot(hit.Travel, hit.Travel) > 0.0) ? 1.0 : 0.0;" + N
     + "\t{" + N
     + "\t\tconst int movingRays = clamp(int(u_Lamps.Trace.x + 0.5), 1, 16);" + N
     + "\t\tif (movingRays > 1 && RecordMovedOnItsOwn(texelFetch(u_Velocity, texel, 0).xy, P, vec2(size)))" + N
     + "\t\t\trays = movingRays;" + N
     + "\t}" + N
     + "\tfor (int r = 1; r < rays; ++r)" + N
     + "\t{" + N
     + "\t\tconst vec3 more = GlossyReflectionLD(N, V, roughness, texel, frame, uint(r));" + N,
     "#if !defined(RV_REFLECTION_RECORD) && !defined(RV_REFLECTION_RELIGHT)" + N
     + "\t// The other rays, and their mean (see the count above)." + N
     + "\tvec3 radianceSum = radiance;" + N
     + "\tfloat travelledSum = travelled;" + N
     + "\tvec3 directionSum = direction;" + N
     + "\tvec3 travelSum = hit.Missed ? vec3(0.0) : hit.Travel;" + N
     + "\tfloat moversStruck = (!hit.Missed && dot(hit.Travel, hit.Travel) > 0.0) ? 1.0 : 0.0;" + N
     + "\tfor (int r = 1; r < rays; ++r)" + N
     + "\t{" + N
     + "\t\tconst vec3 more = GlossyReflectionLD(N, V, roughness, texel, draw + uint(r));" + N),
])
