"""RT-7, first milestone: a light with a length is a capsule (WR-7).

`SourceRadius` gave every light a width; a tube has a length as well, and
without it a two-metre tube reflects as a dot. This adds `SourceLength` and the
Karis representative-point capsule to the analytic specular: the highlight is
built toward the point of the *segment* nearest the reflection ray rather than
the light's centre, so the tube reflects as a tube.

**Encoding, and why it costs no bytes.** For anything but a spot the two cone
lanes are written as exactly 1.0 and read by nothing -- the cone test is
`Params.z < Params.y`, which 1.0 < 1.0 fails. A cosine can never exceed 1, so
`Params.z = 1 + length` is unambiguous, keeps the cone disabled by construction
for any length, and needs no new lane in an 80-byte record that a bridge pixel
reads seventy-eight of. It is the idiom `Params.w` already uses for the hybrid
radius. The limitation it carries: a *spot* cannot also be a tube, because its
Params.z is a real cosine. Tubes are point lights, and WR-8's Area type is where
the general form belongs.

Length zero encodes as exactly 1.0, which is what the lane held before, so every
existing scene is bit-identical -- that null test is the acceptance.

Every anchor must match exactly once or nothing is written.
"""
import io, sys

T = '\t'


def patch(path, edits, marker):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker in s:
        print('%s already patched, skipped' % path)
        return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


# ------------------------------------------------------------------ Light.h
patch(r'RageV/src/RageV/Renderer/Light.h', [
 (T*2 + 'float SourceRadius = 0.0f;\n'
  '\n'
  + T*2 + '// Only one directional light\'s shadows are rendered per frame',
  T*2 + 'float SourceRadius = 0.0f;\n'
  '\n'
  + T*2 + '// **RT-7: and how long it is.** A radius makes a light a sphere; a\n'
  + T*2 + '// strip light, a tube or a lamp row is a *capsule*, and the\n'
  + T*2 + '// difference is the whole shape of its reflection -- a two-metre\n'
  + T*2 + '// tube seen in a polished floor is a two-metre streak, and with a\n'
  + T*2 + '// radius alone it is a dot. Metres, along the light\'s own forward\n'
  + T*2 + '// axis (`Direction`), centred on its position, so a tube is\n'
  + T*2 + '// authored by rotating the entity and needs no second vector.\n'
  + T*2 + '//\n'
  + T*2 + '// Specular only, as the radius is: at these lengths the diffuse\n'
  + T*2 + '// difference sits under the range window\'s own cut, and the term\n'
  + T*2 + '// the eye reads is the image. Zero is the light this always was.\n'
  + T*2 + 'float SourceLength = 0.0f;\n'
  '\n'
  + T*2 + '// Only one directional light\'s shadows are rendered per frame'),
 (T*2 + '// See Light::SourceRadius. Rides GpuLight.Direction.w to the shader.\n'
  + T*2 + 'float SourceRadius = 0.0f;',
  T*2 + '// See Light::SourceRadius. Rides GpuLight.Direction.w to the shader.\n'
  + T*2 + 'float SourceRadius = 0.0f;\n'
  + T*2 + '// See Light::SourceLength. Rides GpuLight.Params.z as 1 plus the\n'
  + T*2 + '// length -- a lane that holds cos(outer cone) for a spot and\n'
  + T*2 + '// exactly 1.0 for everything else, so a value above 1 can only be\n'
  + T*2 + '// this. A spot therefore cannot carry a length; tubes are point\n'
  + T*2 + '// lights, and WR-8\'s Area type is where the general form belongs.\n'
  + T*2 + 'float SourceLength = 0.0f;'),
], 'SourceLength')

# --------------------------------------------------------- the scene's copy
patch(r'RageV/src/RageV/Scene/Scene.cpp', [
 (T*3 + 'data.SourceRadius = light.Light.SourceRadius;',
  T*3 + 'data.SourceRadius = light.Light.SourceRadius;\n'
  + T*3 + 'data.SourceLength = light.Light.SourceLength;'),
 (T*3 + 'if (light.SourceRadius > 0.0f)\n'
  + T*4 + 'mixFloat(light.SourceRadius);',
  T*3 + 'if (light.SourceRadius > 0.0f)\n'
  + T*4 + 'mixFloat(light.SourceRadius);\n'
  + T*3 + '// The same reasoning: a length of zero is the world every stored\n'
  + T*3 + '// bake was solved in.\n'
  + T*3 + 'if (light.SourceLength > 0.0f)\n'
  + T*4 + 'mixFloat(light.SourceLength);'),
], 'SourceLength')

# ------------------------------------------------------------ the inspector
patch(r'RageV/src/RageV/Scene/ComponentRegistry.cpp', [
 (T*4 + 'Field<&LightComponent::Light, &Light::SourceRadius>("SourceRadius",\n'
  + T*5 + 'OnlyWhen(IsPositional, Drag(0.05f, 0.0f, 10.0f))),',
  T*4 + 'Field<&LightComponent::Light, &Light::SourceRadius>("SourceRadius",\n'
  + T*5 + 'OnlyWhen(IsPositional, Drag(0.05f, 0.0f, 10.0f))),\n'
  + T*4 + '// Metres of emitter length along the light\'s forward axis; 0 is a\n'
  + T*4 + '// sphere. A tube reflects as a streak of its own length rather\n'
  + T*4 + '// than a dot. See Light::SourceLength.\n'
  + T*4 + 'Field<&LightComponent::Light, &Light::SourceLength>("SourceLength",\n'
  + T*5 + 'OnlyWhen(IsPositional, Drag(0.05f, 0.0f, 20.0f))),'),
], 'SourceLength')

# ------------------------------------------------------------- the encoding
patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
 (T*3 + '// Cones are compared as cosines in the shader, so convert once here\n'
  + T*3 + '// rather than per fragment. Equal angles disable the cone test.\n'
  + T*3 + 'const float inner = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.InnerCone)) : 1.0f;\n'
  + T*3 + 'const float outer = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.OuterCone)) : 1.0f;',
  T*3 + '// Cones are compared as cosines in the shader, so convert once here\n'
  + T*3 + '// rather than per fragment. Equal angles disable the cone test.\n'
  + T*3 + 'const float inner = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.InnerCone)) : 1.0f;\n'
  + T*3 + '// **RT-7: and the outer lane carries the source length when there\n'
  + T*3 + '// is no cone to carry.** A cosine cannot exceed 1, so 1 plus the\n'
  + T*3 + '// length is unambiguous, and the shader\'s cone test\n'
  + T*3 + '// (`Params.z < Params.y`) stays false for any length, as it was\n'
  + T*3 + '// with the 1.0 this replaces. Zero length writes exactly 1.0, so\n'
  + T*3 + '// every light authored before this packs to the bit it always did.\n'
  + T*3 + 'const float outer = light.Type == Light::LightType::Spot\n'
  + T*4 + '\t\t\t  ? Math::Cos(Math::Radians(light.OuterCone))\n'
  + T*4 + '\t\t\t  : 1.0f + Math::Max(light.SourceLength, 0.0f);'),
], 'RT-7')

# -------------------------------------------------------------- the shading
patch(r'RageVEditor/assets/shaders/include/pbr_fragment.glsl', [
 (T*2 + 'float specRoughness = shadingRoughness;\n'
  + T*2 + 'float specScale = 1.0;\n'
  + T*2 + 'if (isPositional != 0.0 && light.Direction.w > 0.0)\n'
  + T*2 + '{\n'
  + T*3 + 'const vec3 R = reflect(-V, N);\n'
  + T*3 + 'const vec3 toCentre = light.Position.xyz - v_WorldPos;\n'
  + T*3 + 'const vec3 centreToRay = dot(toCentre, R) * R - toCentre;',
  T*2 + 'float specRoughness = shadingRoughness;\n'
  + T*2 + 'float specScale = 1.0;\n'
  + T*2 + '// **RT-7: and a light with a length is a capsule, not a sphere.**\n'
  + T*2 + '//\n'
  + T*2 + '// The sphere below answers "how wide is the source"; a tube, a\n'
  + T*2 + '// strip or a lamp row also answers "how long", and the length is\n'
  + T*2 + '// what makes its reflection a streak rather than a dot. The\n'
  + T*2 + '// representative point becomes the point of the *segment* nearest\n'
  + T*2 + '// the reflection ray, and the sphere step then widens around it --\n'
  + T*2 + '// so a capsule is the two in sequence, which is Karis\' own form.\n'
  + T*2 + '//\n'
  + T*2 + '// The length rides Params.z as 1 plus the metres (Renderer3D packs\n'
  + T*2 + '// it there; a cosine cannot exceed 1, and a spot\'s real cosine\n'
  + T*2 + '// reads as no length, which is why a spot cannot be a tube). Zero\n'
  + T*2 + '// length reads exactly zero here and every branch below is the one\n'
  + T*2 + '// it always took.\n'
  + T*2 + 'const float tubeLength = isPositional != 0.0\n'
  + T*3 + '\t\t\t\t\t   ? max(light.Params.z - 1.0, 0.0) : 0.0;\n'
  + T*2 + 'if (isPositional != 0.0 && (light.Direction.w > 0.0 || tubeLength > 0.0))\n'
  + T*2 + '{\n'
  + T*3 + 'const vec3 R = reflect(-V, N);\n'
  + T*3 + 'vec3 toCentre = light.Position.xyz - v_WorldPos;\n'
  + T*3 + 'if (tubeLength > 0.0)\n'
  + T*3 + '{\n'
  + T*4 + '// The segment, in the shading point\'s own frame: the light\'s\n'
  + T*4 + '// forward axis, centred on its position. Closest point on it\n'
  + T*4 + '// to the reflection ray, clamped to the ends -- the standard\n'
  + T*4 + '// segment/ray solve, with R unit so the determinant is\n'
  + T*4 + '// `d.d - (d.R)^2`. Parallel to the ray it degenerates and the\n'
  + T*4 + '// centre is as good an answer as any.\n'
  + T*4 + 'const vec3 half = light.Direction.xyz * (0.5 * tubeLength);\n'
  + T*4 + 'const vec3 p0 = toCentre - half;\n'
  + T*4 + 'const vec3 d = half + half;\n'
  + T*4 + 'const float dR = dot(d, R);\n'
  + T*4 + 'const float denom = dot(d, d) - dR * dR;\n'
  + T*4 + 'const float t = denom > 1.0e-6\n'
  + T*5 + '\t\t\t  ? clamp((dR * dot(p0, R) - dot(p0, d)) / denom, 0.0, 1.0)\n'
  + T*5 + '\t\t\t  : 0.5;\n'
  + T*4 + 'toCentre = p0 + d * t;\n'
  + T*3 + '}\n'
  + T*3 + 'const vec3 centreToRay = dot(toCentre, R) * R - toCentre;'),
], 'RT-7')

print('done')
