# -*- coding: utf-8 -*-
"""RT-15b (2026-09-15 evening): two engine edits on top of STATE A, each substitution matched
exactly once, line endings preserved per file.

  A. reflection_accumulate.rvshader -- the spoiler streaks. A silhouette texel where something
     moves on its own (RT-18's g_SilhouetteMoving) keeps a coverage-mixed history whose stored
     surface is whichever side the jitter sampled last; the pixels uncovered beside it borrowed
     it (k=1 neighbour, then k=0 centre) and faded it over the moving memory, one bar per frame
     of travel. Such a texel is marked in o_Extra.a's fraction and its history is refused by
     everyone but the silhouette texel itself.
  B. reflection_trace.rvshader + Renderer3D + FrameGraphBuilder + EngineConfig -- the moving
     0.12 cube's speckle and bands: a mirror sliding in its own plane has new reflected content
     at its leading edge every frame, which one ray a texel cannot converge. Where the surface
     moved on its own (the scene's velocity lane, camera motion taken out) a texel casts
     --reflection-moving-rays rays (four by default) and writes their mean.
"""
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


SH = 'RageVEditor/assets/shaders/'

# ---- A. the accumulator's silhouette-mover mark ------------------------------------------------
apply(SH + 'reflection_accumulate.rvshader', [
    ("const float kMovingCurvedMark = 8.0;" + N,
     "const float kMovingCurvedMark = 8.0;" + N
     + "// **RT-15b: the mark of a silhouette texel where something moves on its own**" + N
     + "// (g_SilhouetteMoving, RT-18's condition), an eighth in o_Extra.a's fraction, under" + N
     + "// the choice's quarters and exact in the half float. Such a texel's picture is the" + N
     + "// coverage mix of its two sides -- kept on purpose, it is what TAA's colour is at an" + N
     + "// edge -- but its stored surface is whichever side the jitter sampled last, so the" + N
     + "// texel beside it, or the texel itself a frame later once the edge has moved on," + N
     + "// passes every test and takes the other side's light as its own. Measured on the" + N
     + "// chrome cube crossing the car's wing (2026-09-15): each frame the two pixels" + N
     + "// uncovered nearest the wing's tip took the edge texel's mixed history -- age four," + N
     + "// five times the cube's own light -- and faded it over the eight-frame moving" + N
     + "// memory: one vertical bar per frame of travel. A marked history is nobody's but" + N
     + "// the silhouette texel's own, which RT-18 already tests." + N
     + "const float kSilhouetteMoverMark = 0.125;" + N
     + "bool MarkedSilhouetteMover(float packed) { return fract(packed * 4.0) >= 0.4; }" + N),
    ("\t\tc.extra = texelFetch(u_HistoryExtra, pastTexel, 0);" + N,
     "\t\tc.extra = texelFetch(u_HistoryExtra, pastTexel, 0);" + N
     + "\t\t// RT-15b: a silhouette texel's mixed history is nobody's but its own. Refused as" + N
     + "\t\t// the normal test refuses: it is another surface's light, in this one's clothes." + N
     + "\t\tif (MarkedSilhouetteMover(c.extra.a) && !(silhouette && k == 0))" + N
     + "\t\t{" + N
     + "\t\t\tif (k == 0) g_Refusal = 3;" + N
     + "\t\t\tcontinue;" + N
     + "\t\t}" + N),
    ("\t\t\t   0.5 * choice + float(g_Refusal) + (g_MovingCurved ? kMovingCurvedMark : 0.0));" + N,
     "\t\t\t   0.5 * choice + float(g_Refusal) + (g_MovingCurved ? kMovingCurvedMark : 0.0)" + N
     + "\t\t\t   + (g_SilhouetteMoving ? kSilhouetteMoverMark : 0.0));" + N),
])

# ---- B. the trace: more rays where the surface moves on its own --------------------------------
apply(SH + 'reflection_trace.rvshader', [
    ("#ifdef RV_REFLECTION_RECORD" + N + "#include \"include/change_record.glsl\"" + N + "#endif" + N,
     "// RT-15b: RecordMovedOnItsOwn, for the extra rays below as well as the record." + N
     + "#include \"include/change_record.glsl\"" + N),
    ("#ifndef RV_REFLECTION_RELIGHT" + N + "layout(set = 3, binding = 3) uniform sampler2D u_Albedo;" + N + "#endif" + N,
     "#ifndef RV_REFLECTION_RELIGHT" + N + "layout(set = 3, binding = 3) uniform sampler2D u_Albedo;" + N + "#endif" + N
     + "#if !defined(RV_REFLECTION_RECORD) && !defined(RV_REFLECTION_RELIGHT)" + N
     + "// **RT-15b: the scene's velocity**, for whether the surface under the texel moved on its" + N
     + "// own this frame -- the texels that cast more than one ray (see main)." + N
     + "layout(set = 3, binding = 4) uniform sampler2D u_Velocity;" + N
     + "#endif" + N),
    ("\t// x: the probe a traced hit is lit by. yz: the gloss window a surface must" + N
     + "\t// sit inside to earn a ray. w: one when u_Budget is the allocator's map." + N,
     "\t// x: RT-15b, how many rays a texel casts where its surface moves on its own" + N
     + "\t// (one elsewhere; the probe a hit is lit by is found at the hit, ProbeSlotAt)." + N
     + "\t// yz: the gloss window a surface must sit inside to earn a ray. w: one when" + N
     + "\t// u_Budget is the allocator's map." + N),
    ("vec3 GlossyReflectionLD(vec3 N, vec3 V, float roughness, ivec2 texel, uint frame)" + N + "{" + N,
     "// `ray`: RT-15b, which of a texel's rays this frame -- each takes its own turn of the" + N
     + "// lattice so a texel's rays cover different parts of the lobe; zero is bit-identical" + N
     + "// to the one-ray draw." + N
     + "vec3 GlossyReflectionLD(vec3 N, vec3 V, float roughness, ivec2 texel, uint frame, uint ray)" + N + "{" + N),
    ("\tconst vec2 rotation = fract(vec2(texel) * vec2(0.7548776662, 0.5698402910));" + N,
     "\tconst vec2 rotation = fract(vec2(texel) * vec2(0.7548776662, 0.5698402910)" + N
     + "\t\t\t\t\t\t\t  + vec2(float(ray)) * vec2(0.7548776662, 0.5698402910));" + N),
    ("\tconst vec3 direction = GlossyReflectionLD(N, V, roughness, texel, frame);" + N,
     "\tconst vec3 direction = GlossyReflectionLD(N, V, roughness, texel, frame, 0u);" + N),
    ("\tconst float travelled = hit.Missed ? 1.0e4 : length(hit.Position - P);" + N,
     "\tconst float travelled = hit.Missed ? 1.0e4 : length(hit.Position - P);" + N
     + "#if !defined(RV_REFLECTION_RECORD) && !defined(RV_REFLECTION_RELIGHT)" + N
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
     + "\t\tconst vec3 more = GlossyReflectionLD(N, V, roughness, texel, frame, uint(r));" + N
     + "\t\tconst TracedSurface hitMore = TraceSurface(P, N, more, 1.0e4);" + N
     + "\t\tconst float probeMore = ProbeSlotAt(hitMore.Position);" + N
     + "\t\tradianceSum += hitMore.Missed" + N
     + "\t\t\t\t\t ? hitMore.Sky" + N
     + "\t\t\t\t\t : ShadeTraced(hitMore, ProbeIrradiance(hitMore.Normal, probeMore), probeMore);" + N
     + "\t\ttravelledSum += hitMore.Missed ? 1.0e4 : length(hitMore.Position - P);" + N
     + "\t\tdirectionSum += more;" + N
     + "\t\tif (!hitMore.Missed && dot(hitMore.Travel, hitMore.Travel) > 0.0)" + N
     + "\t\t{" + N
     + "\t\t\ttravelSum += hitMore.Travel;" + N
     + "\t\t\tmoversStruck += 1.0;" + N
     + "\t\t}" + N
     + "\t}" + N
     + "\tradiance = radianceSum / float(rays);" + N
     + "#endif" + N),
    ("\to_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelled);" + N,
     "\to_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelledSum / float(rays));" + N),
    ("\to_Hit = vec4(OctEncode(direction), min(pdf, 6.0e4), 0.0);" + N,
     "\t// RT-15b: the rays' mean direction where there were several -- the resolve re-aims" + N
     + "\t// the texel's hit point from it; the pdf is the first ray's, as unread as it was." + N
     + "\to_Hit = vec4(OctEncode(rays > 1 ? normalize(directionSum) : direction), min(pdf, 6.0e4), 0.0);" + N),
    ("\to_HitTravel = vec4(hit.Missed ? vec3(0.0) : clamp(hit.Travel, vec3(-1.0e4), vec3(1.0e4)), 0.0);" + N,
     "\t// RT-15b: over several rays, the mean travel of those that struck a mover (as the" + N
     + "\t// accumulator's MoverTravel averages a 3x3); the texel then counts as a mover's." + N
     + "\to_HitTravel = rays > 1" + N
     + "\t\t\t\t? vec4(moversStruck > 0.0 ? clamp(travelSum / moversStruck, vec3(-1.0e4), vec3(1.0e4)) : vec3(0.0), 0.0)" + N
     + "\t\t\t\t: vec4(hit.Missed ? vec3(0.0) : clamp(hit.Travel, vec3(-1.0e4), vec3(1.0e4)), 0.0);" + N),
])

# ---- C. the engine: the velocity bound to the trace, the count pushed, the flag ----------------
apply('RageV/src/RageV/Core/EngineConfig.h', [
    ("\t\tfloat ReflectionMovingBlurRadius = -1.0f;" + N,
     "\t\tfloat ReflectionMovingBlurRadius = -1.0f;" + N
     + "\t\t// **--reflection-moving-rays=<count> (RT-15b):** the rays a texel casts where its" + N
     + "\t\t// surface moves on its own (reflection_trace.rvshader); negative, the tuning's four." + N
     + "\t\tint ReflectionMovingRays = -1;" + N),
])
apply('RageV/src/RageV/Core/EngineConfig.cpp', [
    ("\t\t// And the occlusion's and the indirect light's (RT-5 part 5)." + N + "\t\tif (key == \"ao-blur\" || key == \"aoblur\")" + N,
     "\t\t// RT-15b: the rays a texel casts where its surface moves on its own." + N
     + "\t\tif (key == \"reflection-moving-rays\" || key == \"reflectionmovingrays\")" + N
     + "\t\t{" + N
     + "\t\t\ttry { config.ReflectionMovingRays = std::stoi(value); }" + N
     + "\t\t\tcatch (...) { RV_CORE_WARN(\"reflection-moving-rays expects a count; got '{0}'\", value); return false; }" + N
     + "\t\t\treturn true;" + N
     + "\t\t}" + N
     + "\t\t// And the occlusion's and the indirect light's (RT-5 part 5)." + N + "\t\tif (key == \"ao-blur\" || key == \"aoblur\")" + N),
])
apply('RageV/src/RageV/Renderer/Renderer3D.h', [
    ("\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& albedo," + N
     + "\t\t\t\t\t\t\t\t\t float giAverage," + N
     + "\t\t\t\t\t\t\t\t\t // RT-13 stage 3: tracing the glass layer rather than" + N,
     "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& albedo," + N
     + "\t\t\t\t\t\t\t\t\t // RT-15b: the scene's velocity lane, for the texels whose" + N
     + "\t\t\t\t\t\t\t\t\t // surface moves on its own and so cast more rays; null" + N
     + "\t\t\t\t\t\t\t\t\t // (the glass layer) casts one everywhere." + N
     + "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& velocity," + N
     + "\t\t\t\t\t\t\t\t\t float giAverage," + N
     + "\t\t\t\t\t\t\t\t\t // RT-13 stage 3: tracing the glass layer rather than" + N),
])
apply('RageV/src/RageV/Renderer/Renderer3D.cpp', [
    ("\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& albedo," + N
     + "\t\t\t\t\t\t\t\t\t  float giAverage, bool glassLayer)" + N,
     "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& albedo," + N
     + "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& velocity," + N
     + "\t\t\t\t\t\t\t\t\t  float giAverage, bool glassLayer)" + N),
    ("\t\tinputs->SetTexture(3, albedo ? albedo : surface, s_Data->PointSampler);" + N
     + "\t\tinputs->Commit();" + N
     + N
     + "\t\tLampPushConstants push;" + N
     + "\t\tpush.PreviousViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);" + N
     + "\t\tpush.History.y = Math::Max(giAverage, 1.0f);" + N
     + "\t\tFillLampFlip(push, *s_Data);" + N
     + "\t\tconst Vec2 gloss = Renderer::GetReflectionGloss();" + N
     + "\t\tpush.Trace = Vec4(0.0f, gloss.x, gloss.y, budget ? 1.0f : 0.0f);" + N,
     "\t\tinputs->SetTexture(3, albedo ? albedo : surface, s_Data->PointSampler);" + N
     + "\t\t// RT-15b: the velocity lane, for the texels that cast more rays. Asked of the" + N
     + "\t\t// set, for RT-20's reason: a staged older copy has no binding 4. Black where the" + N
     + "\t\t// caller has none (the glass layer): nothing moved, one ray everywhere." + N
     + "\t\tif (inputs->HasBinding(4))" + N
     + "\t\t\tinputs->SetTexture(4, velocity ? velocity : TextureLoader::TransparentBlack(*s_Data->Device)," + N
     + "\t\t\t\t\t\t\t   s_Data->PointSampler);" + N
     + "\t\tinputs->Commit();" + N
     + N
     + "\t\tLampPushConstants push;" + N
     + "\t\tpush.PreviousViewProjection = Math::Inverse(s_Data->Scene.ViewProjection);" + N
     + "\t\tpush.History.y = Math::Max(giAverage, 1.0f);" + N
     + "\t\tFillLampFlip(push, *s_Data);" + N
     + "\t\tconst Vec2 gloss = Renderer::GetReflectionGloss();" + N
     + "\t\t// RT-15b: Trace.x carries the rays a texel casts where its surface moves on its" + N
     + "\t\t// own (four unless --reflection-moving-rays says otherwise). It used to carry the" + N
     + "\t\t// probe a hit is lit by, always zero; the shader finds that at the hit now." + N
     + "\t\tconst int movingRays = EngineConfig::Get().ReflectionMovingRays;" + N
     + "\t\tpush.Trace = Vec4((float)(movingRays >= 0 ? Math::Clamp(movingRays, 1, 16) : 4)," + N
     + "\t\t\t\t\t\t  gloss.x, gloss.y, budget ? 1.0f : 0.0f);" + N),
])
apply('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
    ("\t\t\t\t\tRenderer3D::TraceReflections(context.Color(sceneHDR, normalIndex)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Depth(sceneHDR)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t budgetBound ? context.Color(budgetMap) : nullptr," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Color(sceneHDR, albedoIndex)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t giAverage);" + N,
     "\t\t\t\t\tRenderer3D::TraceReflections(context.Color(sceneHDR, normalIndex)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Depth(sceneHDR)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t budgetBound ? context.Color(budgetMap) : nullptr," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Color(sceneHDR, albedoIndex)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t // RT-15b: the velocity lane, for the moving texels' rays." + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Color(sceneHDR, velocityIndex)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t giAverage);" + N),
    ("\t\t\t\t\tRenderer3D::TraceReflections(context.Color(glassLayer, 1)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Depth(glassLayer)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t glassBudgetBound ? context.Color(budgetMap) : nullptr," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Color(glassLayer, 2)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t giAverage, true);" + N,
     "\t\t\t\t\tRenderer3D::TraceReflections(context.Color(glassLayer, 1)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Depth(glassLayer)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t glassBudgetBound ? context.Color(budgetMap) : nullptr," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t context.Color(glassLayer, 2)," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t // RT-15b: no velocity for the glass layer: one ray a texel." + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t nullptr," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t giAverage, true);" + N),
])
