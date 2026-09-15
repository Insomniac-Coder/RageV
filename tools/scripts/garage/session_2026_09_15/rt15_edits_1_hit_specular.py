# -*- coding: utf-8 -*-
"""Apply the hit-specular edits to the source shaders, each substitution matched exactly once,
line endings preserved per file. Run from anywhere."""
import io, os, sys

ROOT = r'C:\Users\ism19\Code\RageV\RageVEditor\assets\shaders'
CRLF, LF = '\r\n', '\n'
N = '\n'

EDITS = {}


def edit(path, old, new):
    EDITS.setdefault(path, []).append((old, new))


INC = 'include/pbr_fragment.glsl'

# 1. The struct's new fields.
edit(INC,
     "\t// reflection's history is where that point's image stood, not where this frame's is." + N
     + "\tvec3 Travel;" + N
     + "};" + N,
     "\t// reflection's history is where that point's image stood, not where this frame's is." + N
     + "\tvec3 Travel;" + N
     + "\t// **The specular half of the hit** (2026-09-15): the roughness and F0 the struck" + N
     + "\t// surface reflects with, the direction the ray came from, and the live lamps'" + N
     + "\t// highlight summed by the light loop -- so ShadeTraced can light the hit through" + N
     + "\t// the same lobe the lit shader lights the surface on screen with. A hit used to" + N
     + "\t// be shaded Lambert alone, and Lambert on a metal is nothing: the garage's box" + N
     + "\t// is Metallic 1, so a flat mirror facing the camera, which shows the wall behind" + N
     + "\t// it, showed black. Filled under RV_HIT_SPECULAR; zero and unread elsewhere." + N
     + "\tfloat Roughness;" + N
     + "\tvec3 F0;" + N
     + "\tvec3 View;" + N
     + "\tvec3 Specular;" + N
     + "};" + N)

# 2. The gate and the prototypes, before TraceSurface.
edit(INC,
     "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N,
     "// **Which passes shade a hit with its specular half.** The lit shader's own in-line" + N
     + "// mirror and refraction rays always; a trace-only pass asks for it itself before the" + N
     + "// include (reflection_trace, water_trace). The bounce (rtgi_trace) and the bake's" + N
     + "// solve (irradiance_fill) do not, so the field and the realtime bounce are what they" + N
     + "// were: giving them the term is one define each, and a re-bake to see it." + N
     + "#if !defined(RV_TRACE_ONLY) && !defined(RV_HIT_SPECULAR)" + N
     + "#define RV_HIT_SPECULAR" + N
     + "#endif" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "// Defined further down with the rest of the lobe; a hit needs them here." + N
     + "float DistributionGGX(vec3 N, vec3 H, float roughness);" + N
     + "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);" + N
     + "vec3 FresnelSchlick(float cosTheta, vec3 F0);" + N
     + "#endif" + N
     + N
     + "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N)

# 3. The struct's initialisation.
edit(INC,
     "\tsurface.Travel = vec3(0.0);" + N + N
     + "\t// Off the surface along its geometric normal, the shadow ray's offset," + N,
     "\tsurface.Travel = vec3(0.0);" + N
     + "\tsurface.Roughness = 1.0;" + N
     + "\tsurface.F0 = vec3(0.0);" + N
     + "\tsurface.View = -direction;" + N
     + "\tsurface.Specular = vec3(0.0);" + N + N
     + "\t// Off the surface along its geometric normal, the shadow ray's offset," + N)

# 4. The roughness, read and put through the macro with the albedo.
edit(INC,
     "\t{" + N
     + "\t\tfloat ignored = 1.0;" + N
     + "\t\tApplyMacro(albedo, ignored, hitPosition, material.Macro.x, material.Macro.y);" + N
     + "\t}" + N,
     "\t// **And the roughness through the same macro** (2026-09-15): the field moves" + N
     + "\t// roughness against albedo, and the hit's specular half reflects with it. The" + N
     + "\t// albedo comes out bit-identical to the `ignored` roughness this used to pass." + N
     + "\tfloat roughness = hit.Surface.y;" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\tif ((material.MapFlags & MAP_ROUGHNESS) != 0)" + N
     + "\t\troughness *= textureLod(u_Textures[nonuniformEXT(material.Maps1.x)], uv, 0.0).r;" + N
     + "#endif" + N
     + "\tApplyMacro(albedo, roughness, hitPosition, material.Macro.x, material.Macro.y);" + N)

# 5. F0 and the view vector, after the metallic read.
edit(INC,
     "\tfloat metallic = hit.Surface.x;" + N
     + "\tif ((material.MapFlags & MAP_METALLIC) != 0)" + N
     + "\t\tmetallic *= textureLod(u_Textures[nonuniformEXT(material.Maps1.y)], uv, 0.0).r;" + N,
     "\tfloat metallic = hit.Surface.x;" + N
     + "\tif ((material.MapFlags & MAP_METALLIC) != 0)" + N
     + "\t\tmetallic *= textureLod(u_Textures[nonuniformEXT(material.Maps1.y)], uv, 0.0).r;" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\t// What the struck surface reflects with: the lit shader's F0 (0.08 * specular for" + N
     + "\t// a dielectric, the albedo itself for a metal), its clamped roughness, and the" + N
     + "\t// view vector, which for a hit is back along the ray." + N
     + "\tfloat hitSpecularScalar = material.Specular;" + N
     + "\tif ((material.MapFlags & MAP_SPECULAR) != 0)" + N
     + "\t\thitSpecularScalar *= textureLod(u_Textures[nonuniformEXT(material.Maps1.z)], uv, 0.0).r;" + N
     + "\tconst vec3 hitF0 = mix(vec3(0.08 * clamp(hitSpecularScalar, 0.0, 1.0)), albedo," + N
     + "\t\t\t\t\t\t   clamp(metallic, 0.0, 1.0));" + N
     + "\troughness = clamp(roughness, 0.045, 1.0);   // fully smooth aliases badly, as on screen" + N
     + "\tconst vec3 hitV = normalize(-direction);" + N
     + "\tconst float hitNdotV = max(dot(hitNormal, hitV), 0.0);" + N
     + "\tvec3 hitSpecular = vec3(0.0);" + N
     + "#endif" + N)

# 6. The loop's highlight.
edit(INC,
     "\t\tlit += diffuse / PI * lightColor * attenuation * max(dot(hitNormal, L), 0.0) * shadow" + N
     + "\t\t\t * hitLiveShare;" + N
     + "\t}" + N,
     "\t\tconst float hitNdotL = max(dot(hitNormal, L), 0.0);" + N
     + "\t\tlit += diffuse / PI * lightColor * attenuation * hitNdotL * shadow * hitLiveShare;" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\t\t// **The lamp's highlight on the struck surface**: Cook-Torrance through the" + N
     + "\t\t// terms the lit shader uses, toward where the ray came from, and shared with" + N
     + "\t\t// the field by the same hitLiveShare the diffuse is." + N
     + "\t\t{" + N
     + "\t\t\tconst vec3 H = normalize(hitV + L);" + N
     + "\t\t\tconst float D = DistributionGGX(hitNormal, H, roughness);" + N
     + "\t\t\tconst float G = GeometrySmith(hitNormal, hitV, L, roughness);" + N
     + "\t\t\tconst vec3 F = FresnelSchlick(max(dot(H, hitV), 0.0), hitF0);" + N
     + "\t\t\thitSpecular += (D * G * F / (4.0 * hitNdotV * hitNdotL + 0.0001))" + N
     + "\t\t\t\t\t\t  * lightColor * attenuation * hitNdotL * shadow * hitLiveShare;" + N
     + "\t\t}" + N
     + "#endif" + N
     + "\t}" + N)

# 7. Carried out.
edit(INC,
     "\tsurface.Backface = hitBackface;" + N
     + "\treturn surface;" + N,
     "\tsurface.Backface = hitBackface;" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\tsurface.Roughness = roughness;" + N
     + "\tsurface.F0 = hitF0;" + N
     + "\tsurface.View = hitV;" + N
     + "\tsurface.Specular = hitSpecular;" + N
     + "#endif" + N
     + "\treturn surface;" + N)

# 8. ShadeTraced: the signature, and the specular half.
edit(INC,
     "// A described hit plus what arrives at it. Callers check `Missed` first: this" + N
     + "// deliberately does not, so a miss does not pay for an irradiance fetch it" + N
     + "// throws away." + N
     + "vec3 ShadeTraced(TracedSurface surface, vec3 arriving)" + N
     + "{" + N,
     "// A described hit plus what arrives at it. Callers check `Missed` first: this" + N
     + "// deliberately does not, so a miss does not pay for an irradiance fetch it" + N
     + "// throws away. `probe` is the cube a hit reflects (RV_HIT_SPECULAR): the one the" + N
     + "// caller lit the hit's diffuse with, unread where the specular half is off." + N
     + "vec3 ShadeTraced(TracedSurface surface, vec3 arriving, float probe)" + N
     + "{" + N)

edit(INC,
     "\tvec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;" + N
     + "#ifdef RV_IRRADIANCE_FILL" + N,
     "\tvec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;" + N
     + "\t// The hit's specular half (2026-09-15), under RV_HIT_SPECULAR; nothing otherwise." + N
     + "\tvec3 specular = vec3(0.0);" + N
     + "#ifdef RV_IRRADIANCE_FILL" + N)

edit(INC,
     "\t\tif (VolumeIrradiance(surface.Position, surface.Normal, true, hitBounce, hitSky, hitDirect," + N
     + "\t\t\t\t\t\t\t hitDirection, hitCoherent))" + N
     + "\t\t\tambientLight += hitDirect;" + N
     + "\t}" + N
     + "#endif" + N
     + "\treturn surface.Direct + surface.Diffuse * (ambientLight + arriving) + surface.Emissive;" + N,
     "\t\tif (VolumeIrradiance(surface.Position, surface.Normal, true, hitBounce, hitSky, hitDirect," + N
     + "\t\t\t\t\t\t\t hitDirection, hitCoherent))" + N
     + "\t\t{" + N
     + "\t\t\tambientLight += hitDirect;" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\t\t\t// **And their highlight**, from the dominant lamp the field recovers" + N
     + "\t\t\t// (DerivedLamp) -- the virtual lamp the lit shader gives a static surface" + N
     + "\t\t\t// on screen (its bakedHighlight). Their diffuse arrived through hitDirect." + N
     + "\t\t\tif (dot(hitCoherent, hitCoherent) > 0.0)" + N
     + "\t\t\t{" + N
     + "\t\t\t\tconst float NdotLb = max(dot(surface.Normal, hitDirection), 0.0);" + N
     + "\t\t\t\tif (NdotLb > 0.0)" + N
     + "\t\t\t\t{" + N
     + "\t\t\t\t\tconst vec3 Hb = normalize(surface.View + hitDirection);" + N
     + "\t\t\t\t\tconst float Db = DistributionGGX(surface.Normal, Hb, surface.Roughness);" + N
     + "\t\t\t\t\tconst float Gb = GeometrySmith(surface.Normal, surface.View, hitDirection, surface.Roughness);" + N
     + "\t\t\t\t\tconst vec3 Fb = FresnelSchlick(max(dot(Hb, surface.View), 0.0), surface.F0);" + N
     + "\t\t\t\t\tspecular += (Db * Gb * Fb" + N
     + "\t\t\t\t\t\t\t\t / (4.0 * max(dot(surface.Normal, surface.View), 0.0) * NdotLb + 0.0001))" + N
     + "\t\t\t\t\t\t\t  * hitCoherent * NdotLb;" + N
     + "\t\t\t\t}" + N
     + "\t\t\t}" + N
     + "#endif" + N
     + "\t\t}" + N
     + "\t}" + N
     + "#ifdef RV_HIT_SPECULAR" + N
     + "\t// **The room the struck surface reflects: the probe through the split-sum term**," + N
     + "\t// as the lit shader's environment specular -- the roughness picks the convolution" + N
     + "\t// level, the parallax carries the reflected ray out to the probe's sphere. One" + N
     + "\t// probe, the one the caller lit the diffuse with; the ray itself goes no deeper" + N
     + "\t// (TraceReflection: one bounce, by construction), so what a mirror shows of a" + N
     + "\t// mirror is the probe's view of the room. And the live lamps' highlights, which" + N
     + "\t// the light loop summed. On a metal these three are the whole of what it shows." + N
     + "\t{" + N
     + "\t\tconst float NoV = max(dot(surface.Normal, surface.View), 0.0);" + N
     + "\t\tconst vec3 R = RotateIntoSky(reflect(-surface.View, surface.Normal));" + N
     + "\t\tconst vec3 prefiltered = textureLod(u_Environment," + N
     + "\t\t\t\t\t\t\t\t\t\t\tvec4(ProbeParallax(surface.Position, R, probe), probe)," + N
     + "\t\t\t\t\t\t\t\t\t\t\tsurface.Roughness * u_Scene.Environment.y).rgb" + N
     + "\t\t\t\t\t\t   * u_Scene.Environment.x;" + N
     + "\t\tconst vec2 envBRDF = textureLod(u_BRDF, vec2(NoV, surface.Roughness), 0.0).rg;" + N
     + "\t\tspecular += surface.Specular + prefiltered * (surface.F0 * envBRDF.x + envBRDF.y);" + N
     + "\t}" + N
     + "#endif" + N
     + "#endif" + N
     + "\treturn surface.Direct + specular + surface.Diffuse * (ambientLight + arriving) + surface.Emissive;" + N)

# 9. The callers.
edit(INC,
     "\treturn ShadeTraced(surface, ProbeIrradiance(surface.Normal, probe));" + N,
     "\treturn ShadeTraced(surface, ProbeIrradiance(surface.Normal, probe), probe);" + N)
edit(INC,
     "\t\t\t\tvec3 behind = ShadeTraced(behindHit," + N
     + "\t\t\t\t\t\t\t\t\t\t  ProbeIrradiance(behindHit.Normal, v_Instance.x));" + N,
     "\t\t\t\tvec3 behind = ShadeTraced(behindHit," + N
     + "\t\t\t\t\t\t\t\t\t\t  ProbeIrradiance(behindHit.Normal, v_Instance.x), v_Instance.x);" + N)
edit('rtgi_trace.rvshader',
     "\t\t\t\t\t : ShadeTraced(second, ProbeIrradiance(second.Normal," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t\t   GiProbeSlotAt(second.Position)));" + N,
     "\t\t\t\t\t : ShadeTraced(second, ProbeIrradiance(second.Normal," + N
     + "\t\t\t\t\t\t\t\t\t\t\t\t\t   GiProbeSlotAt(second.Position))," + N
     + "\t\t\t\t\t\t\t\t   GiProbeSlotAt(second.Position));" + N)
edit('rtgi_trace.rvshader',
     "\t\tvec3 shaded = max(ShadeTraced(first, arriving) - (first.IsEmitter ? first.Emissive : vec3(0.0))," + N,
     "\t\tvec3 shaded = max(ShadeTraced(first, arriving, GiProbeSlotAt(first.Position))" + N
     + "\t\t\t\t\t\t  - (first.IsEmitter ? first.Emissive : vec3(0.0))," + N)
edit('irradiance_fill.rvshader',
     "\t\tconst vec3 radiance = ShadeTraced(hit, ProbeIrradiance(hit.Normal, 0.0));" + N,
     "\t\tconst vec3 radiance = ShadeTraced(hit, ProbeIrradiance(hit.Normal, 0.0), 0.0);" + N)
edit('water_trace.rvshader',
     "#define RV_TRACE_ONLY" + N
     + "#include \"include/pbr_fragment.glsl\"" + N
     + "#include \"include/water_lobe.glsl\"" + N,
     "#define RV_TRACE_ONLY" + N
     + "// A hit is an image the eye sees: shaded with its specular half (pbr_fragment's" + N
     + "// RV_HIT_SPECULAR), so a metal in the sea's mirror is not black." + N
     + "#define RV_HIT_SPECULAR" + N
     + "#include \"include/pbr_fragment.glsl\"" + N
     + "#include \"include/water_lobe.glsl\"" + N)
edit('water_trace.rvshader',
     "\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x))," + N,
     "\to_Reflection = vec4(ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x)," + N)
edit('reflection_trace.rvshader',
     "#define RV_TRACE_ONLY" + N
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
     + "#define RV_HIT_SPECULAR" + N)
edit('reflection_trace.rvshader',
     "\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x));" + N,
     "\t\t\t\t  : ShadeTraced(hit, ProbeIrradiance(hit.Normal, u_Lamps.Trace.x), u_Lamps.Trace.x);" + N)


def main():
    for rel, subs in EDITS.items():
        path = os.path.join(ROOT, rel)
        raw = io.open(path, encoding='utf-8', newline='').read()
        crlf = CRLF in raw
        text = raw.replace(CRLF, LF)
        for old, new in subs:
            n = text.count(old)
            if n != 1:
                sys.exit('%s: a substitution matched %d times: %r' % (rel, n, old[:90]))
            text = text.replace(old, new)
        out = text.replace(LF, CRLF) if crlf else text
        io.open(path, 'w', encoding='utf-8', newline='').write(out)
        print('%-32s %d substitutions applied (%s)' % (rel, len(subs), 'CRLF' if crlf else 'LF'))


if __name__ == '__main__':
    main()
