# -*- coding: utf-8 -*-
"""Coloured metals reflect in colour again.

**The defect, and the code admitted it.** The traced reflection is added to the
frame by a single number carried in the scene's alpha, and the lit shader made
that number by collapsing a colour to its brightness:

    // One channel, so a coloured metal's tint is not carried.
    reflectionWeight = reflectionShare
                     * dot((F0 * envBRDF.x + envBRDF.y) * occlusion, luma);

`F0` is what a surface reflects at normal incidence, and **for a metal it is the
metal's own colour** -- gold's F0 is gold, copper's is copper. Collapsing it to a
luminance throws the colour away, so every coloured metal in the engine has been
reflecting the room in **grey**. The probe's half of the reflection keeps its
tint (that multiply is a colour, one line above); only the *traced* half lost it.

**Where the fix goes, and why not in the composite.** The composite has one
scalar to multiply by and no way to know the surface, so tinting there would
mean binding the albedo, the surface and enough of the camera to rebuild the
view vector -- into a pass whose push constants are already full. The trace has
all of it already: it reads the surface lane for the normal, roughness and
metallic, reconstructs the world point, and includes `pbr_fragment.glsl`, so
`EnvBRDF` is in scope. It needs one more binding -- the albedo lane -- and then
the picture it stores is already tinted, which is what it should always have
been: the reflection off gold *is* gold-coloured.

The lit shader then owes only the scalar part, `reflectionShare * occlusion`.
Everything else is unchanged, so a **grey** metal must come out identical and a
coloured one must change -- which is the check.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# ------------------------------------------------------------------ shader
T = 'RageVEditor/assets/shaders/reflection_trace.rvshader'
s = read(T)
if not has(s, 'u_Albedo'):
    s = rep(s,
        "layout(set = 3, binding = 2) uniform sampler2D u_Budget;\n",
        "layout(set = 3, binding = 2) uniform sampler2D u_Budget;\n"
        "// **The albedo lane, for the metal's own colour.** rgb is the albedo and\n"
        "// a is the dielectric specular scalar -- exactly what the lit shader\n"
        "// builds F0 from, and F0 for a metal *is* the metal's colour. Without it\n"
        "// this pass hands the compositor an untinted picture and a coloured metal\n"
        "// reflects the room in grey.\n"
        "layout(set = 3, binding = 3) uniform sampler2D u_Albedo;\n")

    s = rep(s,
        "\tconst float travelled = hit.Missed ? 1.0e4 : length(hit.Position - P);\n"
        "\to_Reflection = vec4(min(radiance, vec3(64.0)), travelled);\n",
        "\tconst float travelled = hit.Missed ? 1.0e4 : length(hit.Position - P);\n"
        "\n"
        "\t// **The surface's own reflectance, in colour.** The same split-sum term\n"
        "\t// the lit shader applies to the probe -- F0 * envBRDF.x + envBRDF.y --\n"
        "\t// built from the same F0: 0.08 * specular for a dielectric, the albedo\n"
        "\t// itself for a metal. Applied here rather than in the composite because\n"
        "\t// the composite has one scalar and no way to know the surface, and this\n"
        "\t// pass already has the normal, the view vector and the roughness.\n"
        "\t//\n"
        "\t// The lit shader used to do this and collapse it to a luminance, because\n"
        "\t// the weight it passes on has one channel. That is what made gold reflect\n"
        "\t// grey. Stored tinted, the picture is what it always should have been.\n"
        "\tconst vec4 albedo = texelFetch(u_Albedo, texel, 0);\n"
        "\tconst vec3 F0 = mix(vec3(0.08 * clamp(albedo.a, 0.0, 1.0)), albedo.rgb,\n"
        "\t\t\t\t\t\tclamp(surface.a, 0.0, 1.0));\n"
        "\tconst vec2 envBRDF = EnvBRDF(max(dot(N, V), 0.0), roughness);\n"
        "\tconst vec3 reflectance = F0 * envBRDF.x + envBRDF.y;\n"
        "\to_Reflection = vec4(min(radiance * reflectance, vec3(64.0)), travelled);\n")
    write(T, s)
    print('reflection_trace.rvshader: the picture is tinted at the source')
else:
    print('trace already done')

# --------------------------------------------------------------- lit shader
P = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'
s = read(P)
if not has(s, 'the tint is the trace'):
    s = rep(s,
        "\t// Exactly what the probe's radiance was multiplied by, as a luminance:\n"
        "\t// the pass's picture, added after the temporal filter, is added by\n"
        "\t// this. One channel, so a coloured metal's tint is not carried.\n"
        "\treflectionWeight = reflectionShare\n"
        "\t\t\t\t\t * dot((F0 * envBRDF.x + envBRDF.y) * occlusion, vec3(0.2126, 0.7152, 0.0722));\n",
        "\t// **Only the scalar part now -- the tint is the trace's.** This used to\n"
        "\t// be the whole split-sum term collapsed to a luminance, and the comment\n"
        "\t// here said so: \"one channel, so a coloured metal's tint is not\n"
        "\t// carried\". It was not carried, and every coloured metal reflected the\n"
        "\t// room in grey for it. `reflection_trace` now multiplies its picture by\n"
        "\t// `F0 * envBRDF.x + envBRDF.y` in colour, where F0 for a metal is the\n"
        "\t// metal's own colour, so what remains for the weight is how much of the\n"
        "\t// probe gave way and how occluded the pixel is -- both scalars.\n"
        "\treflectionWeight = reflectionShare * occlusion;\n")
    write(P, s)
    print("pbr_fragment.glsl: the weight is the scalar it should be")
else:
    print('lit shader already done')

# ---------------------------------------------------------------- the plumbing
R = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
s = read(R)
if not has(s, 'const RHI::Ref<RHITexture>& albedo'):
    s = rep(s,
        "\tvoid Renderer3D::TraceReflections(const RHI::Ref<RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& budget,\n"
        "\t\t\t\t\t\t\t\t\t  float giAverage)\n",
        "\tvoid Renderer3D::TraceReflections(const RHI::Ref<RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& budget,\n"
        "\t\t\t\t\t\t\t\t\t  const RHI::Ref<RHITexture>& albedo,\n"
        "\t\t\t\t\t\t\t\t\t  float giAverage)\n")
    s = rep(s,
        "\t\tslot.ReflectionTraceInputs->SetTexture(2, budget ? budget : surface, s_Data->PointSampler);\n"
        "\t\tslot.ReflectionTraceInputs->Commit();\n",
        "\t\tslot.ReflectionTraceInputs->SetTexture(2, budget ? budget : surface, s_Data->PointSampler);\n"
        "\t\t// The albedo lane, for the metal's own colour. A declared binding must\n"
        "\t\t// be filled, and the surface stands in where the G-buffer did not run --\n"
        "\t\t// its rgb is then a normal, which makes a nonsense tint rather than a\n"
        "\t\t// crash, and that path does not trace.\n"
        "\t\tslot.ReflectionTraceInputs->SetTexture(3, albedo ? albedo : surface, s_Data->PointSampler);\n"
        "\t\tslot.ReflectionTraceInputs->Commit();\n")
    write(R, s)
    print('Renderer3D.cpp: the albedo lane is bound')

H = 'RageV/src/RageV/Renderer/Renderer3D.h'
s = read(H)
if not has(s, 'const RHI::Ref<RHI::RHITexture>& albedo,'):
    s = rep(s,
        "\t\tstatic void TraceReflections(const RHI::Ref<RHI::RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& budget,\n"
        "\t\t\t\t\t\t\t\t\t float giAverage);\n",
        "\t\tstatic void TraceReflections(const RHI::Ref<RHI::RHITexture>& surface,\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& budget,\n"
        "\t\t\t\t\t\t\t\t\t // The albedo lane: F0's colour, so a metal's\n"
        "\t\t\t\t\t\t\t\t\t // reflection carries the metal's own tint.\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& albedo,\n"
        "\t\t\t\t\t\t\t\t\t float giAverage);\n")
    write(H, s)
    print('Renderer3D.h: the signature')
