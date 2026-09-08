# -*- coding: utf-8 -*-
"""RT-8 job 1: the sea's direct light through the shared DirectTrace pass.

The sea has its own copy of choose-and-shade -- `water_choose` scores every
lamp that reaches a patch and keeps four by reservoir sampling, `water_shade`
shades them and traces their shadow rays. DirectTrace does exactly that for
every other surface in the frame, with the same reservoir, the same shadow ray
and the same field handling.

The only thing that is genuinely the sea's is the **lobe**: anisotropic
Beckmann about the wind rather than GGX, because Cox and Munk photographed the
sun's glitter and fitted a Gaussian slope distribution, twice, once along the
wind and once across it. That is what turns the highlight from a disc into the
streak a low light lays on wind-driven water, and it is one branch.

So DirectTrace gains a water variant: the same pass, reading the sea's layer
instead of the depth buffer, and taking the sea's lobe instead of GGX.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageVEditor/assets/shaders/direct_trace.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'RV_DIRECT_WATER' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# --- the lobe, and what the bindings mean under the water variant --------
once('''#define RV_TRACE_ONLY
#include "include/pbr_fragment.glsl"''',
'''#define RV_TRACE_ONLY
#ifdef RV_DIRECT_WATER
// **RT-8 job 1: the sea's lobe.** The header guards it behind RV_WATER, which
// is the water *draw*'s define; this pass is not the water draw and must not
// take the rest of what that define pulls in, so the file is included on its
// own below. See include/water_lobe.glsl for why Beckmann rather than GGX,
// why it is anisotropic about the wind, and why the roughness it takes is the
// RMS slope itself rather than a perceptual alpha.
#endif
#include "include/pbr_fragment.glsl"
#ifdef RV_DIRECT_WATER
#include "include/water_lobe.glsl"
#endif''',
     'lobe include')

once('''layout(set = 3, binding = 0) uniform sampler2D u_DepthIn;
layout(set = 3, binding = 1) uniform sampler2D u_SurfaceIn;
layout(set = 3, binding = 2) uniform sampler2D u_AlbedoIn;
layout(set = 3, binding = 3) uniform sampler2D u_SurfaceIdIn;''',
'''// **RT-8 job 1: and the sea's layer, in the same four slots.** The water
// surface pass writes a G-buffer of its own in all but name, so the variant
// binds it here rather than growing a second set: the position where the depth
// would be (the sea writes no depth, and the buffer under it holds the seabed),
// the same octahedral normal with the RMS slope where the roughness is and the
// wind angle where the metallic is, and the albedo with the specular dial.
// The id slot is unused -- the sea is one surface, never static, and its
// roughness needs no specular-antialiased second copy.
layout(set = 3, binding = 0) uniform sampler2D u_DepthIn;
layout(set = 3, binding = 1) uniform sampler2D u_SurfaceIn;
layout(set = 3, binding = 2) uniform sampler2D u_AlbedoIn;
layout(set = 3, binding = 3) uniform sampler2D u_SurfaceIdIn;''',
     'bindings note')

once('''	float NdotV;
	bool  Static;
	float FieldWeight;
};''',
'''	float NdotV;
	bool  Static;
	float FieldWeight;
#ifdef RV_DIRECT_WATER
	// RT-8: which way the wind blows, in radians, so the lobe can be built in
	// its frame. The sea's surface pass writes it where a metallic would be.
	float Wind;
#endif
};''',
     'DirectPoint::Wind')

# --- the lobe branch ------------------------------------------------------
once('''	const float NDF = DistributionGGX(N, H, specRoughness);
	const float G = GeometrySchlickGGX(p.NdotV, specRoughness)
				  * GeometrySchlickGGX(NdotL, specRoughness);
	const vec3 F = FresnelSchlick(max(dot(H, V), 0.0), p.F0);''',
'''#ifdef RV_DIRECT_WATER
	// **The sea's lobe, in the wind's frame.** Transcribed from ShadeLamp in
	// include/water_lamps.glsl, which is the copy this pass replaces -- the
	// same two branches, the same constants, so the two answer alike where
	// they overlap. A sized source stretches the lobe along the view rather
	// than the wind: a lamp's reflection on water is a column reaching towards
	// the eye, not a disc, and the wind frame cannot make one.
	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - N * dot(windDir, N));
	const vec3 windB = cross(N, windT);
	float NDF;
	float G;
	vec3 Tv = V - N * dot(V, N);
	const float tv2 = dot(Tv, Tv);
	if (positional && light.Direction.w > 0.0 && tv2 > 1.0e-6)
	{
		Tv *= inversesqrt(tv2);
		const vec3 Bv = cross(N, Tv);
		const float ax0 = max(specRoughness * 1.16, 0.02);
		const float ay0 = max(specRoughness * 0.86, 0.02);
		const float angular = light.Direction.w
							* inversesqrt(max(dot(light.Position.xyz - p.P,
												  light.Position.xyz - p.P), 1.0e-8));
		const float axS = min(ax0 * 3.0 + 4.0 * angular, 1.0);
		const float ayS = min(ay0 + 0.5 * angular, 1.0);
		specScale = sqrt((ax0 * ay0) / (axS * ayS));
		NDF = WaterBeckmannDX(H, N, Tv, Bv, axS, ayS);
		G   = WaterBeckmannG1X(V, N, Tv, Bv, ax0, ay0)
			* WaterBeckmannG1X(L, N, Tv, Bv, ax0, ay0);
	}
	else
	{
		NDF = WaterBeckmannD(H, N, windT, windB, specRoughness);
		G   = WaterBeckmannG1(V, N, windT, windB, specRoughness)
			* WaterBeckmannG1(L, N, windT, windB, specRoughness);
	}
#else
	const float NDF = DistributionGGX(N, H, specRoughness);
	const float G = GeometrySchlickGGX(p.NdotV, specRoughness)
				  * GeometrySchlickGGX(NdotL, specRoughness);
#endif
	const vec3 F = FresnelSchlick(max(dot(H, V), 0.0), p.F0);''',
     'water lobe branch')

# **The sized-source widening convention differs too**: water's roughness is
# the RMS slope itself, so an angular radius adds to it rather than to alpha.
once('''		const float angular = light.Direction.w *
							  inversesqrt(max(dot(toCentre, toCentre), 1.0e-8));
		const float alpha = specRoughness * specRoughness;
		const float widenedAlpha = min(alpha + 0.5 * angular, 1.0);
		specScale = (alpha / widenedAlpha) * (alpha / widenedAlpha);
		specRoughness = sqrt(widenedAlpha);''',
'''		const float angular = light.Direction.w *
							  inversesqrt(max(dot(toCentre, toCentre), 1.0e-8));
#ifdef RV_DIRECT_WATER
		// **Water's roughness is the RMS slope itself, not a perceptual
		// alpha**, so an angular radius adds to it directly. Squaring it here
		// would collapse the horizon variance to a hundredth of the measured
		// sea -- see include/water_lobe.glsl's note on the two conventions.
		const float widened = min(specRoughness + 0.5 * angular, 1.0);
		specScale = (specRoughness * specRoughness) / max(widened * widened, 1.0e-12);
		specRoughness = widened;
#else
		const float alpha = specRoughness * specRoughness;
		const float widenedAlpha = min(alpha + 0.5 * angular, 1.0);
		specScale = (alpha / widenedAlpha) * (alpha / widenedAlpha);
		specRoughness = sqrt(widenedAlpha);
#endif''',
     'widening convention')

# The diffuse: water carries its albedo and the 1/pi inside the term, as
# ShadeLamp does, because nothing downstream multiplies it back in for a layer
# the lit shader does not shade.
once('''	diffuse = kD * NdotL * radiance;
	specular = (numerator / denominator) * NdotL * radiance;
	return true;''',
'''#ifdef RV_DIRECT_WATER
	// The sea's albedo and the 1/pi ride inside the term, as ShadeLamp's do:
	// the water draw adds this pair straight in, where the lit shader
	// multiplies the opaque one by the albedo it already has.
	diffuse = kD * p.Albedo / PI * NdotL * radiance;
#else
	diffuse = kD * NdotL * radiance;
#endif
	specular = (numerator / denominator) * NdotL * radiance;
	return true;''',
     'diffuse convention')

# --- the surface, read from the sea's layer ------------------------------
once('''	const ivec2 texel = ivec2(gl_FragCoord.xy);
	const vec4 surface = texelFetch(u_SurfaceIn, texel, 0);
	const float depth = texelFetch(u_DepthIn, texel, 0).r;
	if (SurfaceIsEmpty(surface) || depth >= 1.0)
		return;''',
'''	const ivec2 texel = ivec2(gl_FragCoord.xy);
	const vec4 surface = texelFetch(u_SurfaceIn, texel, 0);
#ifdef RV_DIRECT_WATER
	// **RT-8 job 1: the sea knows where it is**, in full floats, with the mask
	// in w -- so there is no depth to test and nothing to reconstruct.
	const vec4 position = texelFetch(u_DepthIn, texel, 0);
	if (position.w <= 0.5)
		return;
#else
	const float depth = texelFetch(u_DepthIn, texel, 0).r;
	if (SurfaceIsEmpty(surface) || depth >= 1.0)
		return;
#endif''',
     'entry test')

once('''	// The surface, as rtgi_trace reconstructs it: linear depth through the
	// inverse projection at the pixel centre, less the jitter it was drawn
	// with, into the camera's frame.
	const bool flip = u_Direct.FlipY > 0.5;''',
'''	// The surface, as rtgi_trace reconstructs it: linear depth through the
	// inverse projection at the pixel centre, less the jitter it was drawn
	// with, into the camera's frame.
#ifndef RV_DIRECT_WATER
	const bool flip = u_Direct.FlipY > 0.5;''',
     'reconstruction open')

once('''	DirectPoint p;
	p.P = u_Direct.CameraPosition.xyz + vec3(dot(u_Direct.CameraRow0.xyz, view),
											  dot(u_Direct.CameraRow1.xyz, view),
											  dot(u_Direct.CameraRow2.xyz, view));
	p.N = OctDecode(surface.rg);
	p.RawRoughness = clamp(surface.b, 0.0, 1.0);
	p.Metallic = clamp(surface.a, 0.0, 1.0);
	// The id lane: the roughness the lit shader shades analytic lights with
	// (specular-antialiased -- without it the chrome poles came out three
	// levels dark), the material's occlusion, and Static in the sign.
	const vec2 surfaceId = texelFetch(u_SurfaceIdIn, texel, 0).rg;
	p.Roughness = clamp(floor(surfaceId.g) / 65535.0, 0.0, 1.0);
	p.Occlusion = clamp(fract(surfaceId.g), 0.0, 1.0);
	p.Static = surfaceId.r < 0.0;
	const vec4 albedo = texelFetch(u_AlbedoIn, texel, 0);
	p.Albedo = albedo.rgb;
	p.F0 = mix(vec3(0.08 * clamp(albedo.a, 0.0, 1.0)), albedo.rgb, p.Metallic);
	p.V = normalize(u_Scene.CameraPosition.xyz - p.P);
	p.NdotV = max(dot(p.N, p.V), 0.0);
	p.FieldWeight = p.Static && u_Scene.IrradianceExtents.w > 0.0
				  ? IrradianceFieldWeight(p.P, p.N) : 0.0;''',
'''#endif

	DirectPoint p;
#ifdef RV_DIRECT_WATER
	p.P = position.xyz;
	p.N = OctDecode(surface.rg);
	// The RMS slope, and the wind angle beside it -- where an opaque surface
	// keeps its roughness and its metallic.
	p.RawRoughness = clamp(surface.b, 0.0, 1.0);
	p.Roughness = p.RawRoughness;
	p.Wind = surface.a;
	// **A sea is a dielectric that never stands still and is never baked.**
	// No metallic, no material occlusion, never Static, no field weight -- the
	// four things the opaque path reads out of lanes the sea does not have.
	p.Metallic = 0.0;
	p.Occlusion = 1.0;
	p.Static = false;
	p.FieldWeight = 0.0;
	const vec4 albedo = texelFetch(u_AlbedoIn, texel, 0);
	p.Albedo = albedo.rgb;
	p.F0 = vec3(0.08 * clamp(albedo.a, 0.0, 1.0));
#else
	p.P = u_Direct.CameraPosition.xyz + vec3(dot(u_Direct.CameraRow0.xyz, view),
											  dot(u_Direct.CameraRow1.xyz, view),
											  dot(u_Direct.CameraRow2.xyz, view));
	p.N = OctDecode(surface.rg);
	p.RawRoughness = clamp(surface.b, 0.0, 1.0);
	p.Metallic = clamp(surface.a, 0.0, 1.0);
	// The id lane: the roughness the lit shader shades analytic lights with
	// (specular-antialiased -- without it the chrome poles came out three
	// levels dark), the material's occlusion, and Static in the sign.
	const vec2 surfaceId = texelFetch(u_SurfaceIdIn, texel, 0).rg;
	p.Roughness = clamp(floor(surfaceId.g) / 65535.0, 0.0, 1.0);
	p.Occlusion = clamp(fract(surfaceId.g), 0.0, 1.0);
	p.Static = surfaceId.r < 0.0;
	const vec4 albedo = texelFetch(u_AlbedoIn, texel, 0);
	p.Albedo = albedo.rgb;
	p.F0 = mix(vec3(0.08 * clamp(albedo.a, 0.0, 1.0)), albedo.rgb, p.Metallic);
#endif
	p.V = normalize(u_Scene.CameraPosition.xyz - p.P);
	p.NdotV = max(dot(p.N, p.V), 0.0);
#ifndef RV_DIRECT_WATER
	p.FieldWeight = p.Static && u_Scene.IrradianceExtents.w > 0.0
				  ? IrradianceFieldWeight(p.P, p.N) : 0.0;
#endif''',
     'surface setup')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('direct_trace has a water variant')
