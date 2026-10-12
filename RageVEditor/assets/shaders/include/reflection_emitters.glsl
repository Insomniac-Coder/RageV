// **The area emitters a reflection aims at -- the rows, and what it takes to judge a point on one
// (RT-24, 2026-09-25).** Moved out of reflection_trace.rvshader verbatim, because the resolve now
// needs the same answers: it pools the lamp points its neighbours aimed at, and weighs each one
// from its own position with the very functions the trace drew it with (reflection_resolve.rvshader,
// "the lamp points, pooled"). One copy, so the two can never disagree about a lamp.
//
// The includer defines RV_EMITTER_ROWS_BINDING and RV_EMITTER_CDF_BINDING (set 3), and a
// `bool VisibleNormals()` before this file: LobePdf asks it which lobe the rays are drawn from.
#ifndef RV_REFLECTION_EMITTERS_GLSL
#define RV_REFLECTION_EMITTERS_GLSL

// **RT-11: the scene's area emitters, and where on each one the light is.**
//
// The bounce pass has aimed at these since it was built (rtgi_trace): for every
// ray it casts it *also* picks a point on a ceiling tube and fires one ray
// straight at it, so a tube's light arrives every frame at full strength
// whether or not a hemisphere ray happened to point at one. This pass never
// learned to. A reflection ray found a tube only by luck, and when it did it
// brought back the tube's whole radiance at the weight of one lobe sample --
// two or three hundred display levels against a floor's two. Hit or miss by
// chance, one ray a texel: that is the sparkle measured on the driving car
// (2026-09-21), and it is the estimator, not the filter that comes after it.
//
// Same rows and same aiming tables as the bounce reads, to the byte -- one
// upload, two readers -- so what the two passes call a lamp is one answer.
struct GiEmitter
{
	vec4 CentreArea;
	// The two half-extents; their w lanes, with Radiance.w, are the unit plane
	// normal, built once on the CPU rather than per shadow ray.
	vec4 TangentU;
	vec4 TangentV;
	vec4 Radiance;
	// x where this emitter's table starts in u_EmitterCdf, y cells a side (zero:
	// radiate evenly and the two rows below are unread), z the emissive map's
	// heap slot, w log2(y).
	vec4 Aim;
	// Texture uv to the rectangle's own (su, sv) in [-1, 1].
	vec4 UvToSurface0;
	vec4 UvToSurface1;
};

layout(std430, set = 3, binding = RV_EMITTER_ROWS_BINDING) readonly buffer ReflectionEmitterBlock
{
	GiEmitter u_Emitters[];
};

layout(std430, set = 3, binding = RV_EMITTER_CDF_BINDING) readonly buffer ReflectionEmitterCdfBlock
{
	float u_EmitterCdf[];
};

// **How densely the lobe this pass samples points at `L`** -- the same number
// o_Hit's third lane already carries for the first ray, as a function so the
// aimed samples can ask it about a direction nothing was drawn along.
//
// This is what makes the two estimators below comparable. One draws a direction
// from the lobe and looks at what is there; the other picks a point on a lamp
// and asks how likely the lobe was to have looked at it. Weighing them against
// each other needs both answers in the same units, which is a density per unit
// solid angle.
float LobePdf(vec3 N, vec3 V, vec3 L, float roughness)
{
	const float alpha = roughness * roughness;
	// A mirror has no lobe to speak of: all of its density sits on one
	// direction, and no aimed sample will ever land on it. Reported as
	// enormous, which drives the weighting below to "the mirror ray carries
	// this" -- which is the right answer and the picture it already produces.
	if (alpha < 1.0e-4)
		return 1.0e8;
	const vec3 H = normalize(V + L);
	const float NoH = max(dot(N, H), 0.0);
	const float VoH = max(dot(V, H), 1.0e-4);
	const float D = DistributionGGX(N, H, roughness);
	if (VisibleNormals())
	{
	// Heitz 2018: drawing from the visible normals makes the density
	// G1(V) D(H) / (4 NoV), the masking term and NoH having cancelled.
	// Smith's exact G1 for GGX, not the lit shader's GeometrySchlickGGX --
	// that one carries the direct-lighting k and is a different function.
	const float NoV = max(dot(N, V), 1.0e-4);
	const float a2 = alpha * alpha;
	const float G1 = 2.0 * NoV / (NoV + sqrt(a2 + (1.0 - a2) * NoV * NoV));
	return G1 * D / (4.0 * NoV);
	}
	return D * NoH / (4.0 * VoH);
}

// **How densely the aimed sampler would have drawn the point the ray struck.**
// One for a row that radiates evenly; for one with an aiming table, the cell's
// share of the whole times the number of cells -- the same number the sampler
// used going the other way, so the two weights below still sum to one.
//
// The struck point is projected onto the rectangle's plane first, because it is
// on the fitting's shell rather than on the rectangle: the rectangle is what
// the estimator models the fitting as, and the projection is where on that
// model the light came from.
float EmitterDensityAt(GiEmitter light, vec3 point)
{
	float density = 1.0;
	{
		const vec3 d = point - light.CentreArea.xyz;
		const float uLen2 = max(dot(light.TangentU.xyz, light.TangentU.xyz), 1.0e-9);
		const float vLen2 = max(dot(light.TangentV.xyz, light.TangentV.xyz), 1.0e-9);
		const float su = clamp(dot(d, light.TangentU.xyz) / uLen2, -1.0, 1.0);
		const float sv = clamp(dot(d, light.TangentV.xyz) / vLen2, -1.0, 1.0);

		const int grid = int(light.Aim.y + 0.5);
		if (grid > 0)
		{
			// Back through the uv map to the cell, so the density here is the
			// same number the aimed sampler used going the other way. The map
			// is affine, so its inverse is a two by two.
			const float a = light.UvToSurface0.x, b = light.UvToSurface0.y;
			const float c = light.UvToSurface0.z;
			const float e = light.UvToSurface1.x, f = light.UvToSurface1.y;
			const float g = light.UvToSurface1.z;
			const float det = a * f - b * e;
			if (abs(det) > 1.0e-12)
			{
				const float u = (f * (su - c) - b * (sv - g)) / det;
				const float v = (-e * (su - c) + a * (sv - g)) / det;
				const int shift = int(light.Aim.w + 0.5);
				const int cx = clamp(int(u * float(grid)), 0, grid - 1);
				const int cy = clamp(int(v * float(grid)), 0, grid - 1);
				const int cell = (cy << shift) + cx;
				const int base = int(light.Aim.x + 0.5);
				const float upper = u_EmitterCdf[base + cell];
				const float lower = cell > 0 ? u_EmitterCdf[base + cell - 1] : 0.0;
				density = max(upper - lower, 1e-9) * float(grid * grid);
			}
		}
	}
	return density;
}

// **What one emitter is worth to this texel, before any ray is cast.**
//
// The first build of RT-11 picked one of the sixteen rows uniformly, and that
// is what made it fail: a garage texel sees one tube and fifteen it cannot,
// so fifteen draws in sixteen came back zero and the sixteenth came back at
// sixteen times weight. The aimed sample was a lottery of its own -- and the
// caps downstream, which exist precisely to stop lotteries, ate it. Measured
// on the wet floor: the weighting took 0.72 display levels off and the aim
// gave 0.10 back.
//
// So the pick is by what each row is worth here: its brightness, its size,
// how squarely it faces this point, how far it is, and -- the part that makes
// it a *reflection's* score rather than a surface's -- how much of this
// texel's lobe actually points at it. Three probes along the rectangle, the
// centre and the two ends of its longer side, because a ceiling tube is long
// and thin and its centre alone says nothing about whether the lobe clips an
// end of it.
//
// A bad score costs variance and nothing else: the weights below divide by the
// probability the score produced, so the picture is the same whatever it says.
float EmitterScore(vec3 P, vec3 N, vec3 V, float roughness, GiEmitter light)
{
	const vec3 lightNormal = vec3(light.TangentU.w, light.TangentV.w, light.Radiance.w);
	const float power = dot(light.Radiance.rgb, vec3(0.2126, 0.7152, 0.0722))
					  * max(light.CentreArea.w, 0.0);
	if (power <= 0.0)
		return 0.0;
	const bool longU = dot(light.TangentU.xyz, light.TangentU.xyz)
					 >= dot(light.TangentV.xyz, light.TangentV.xyz);
	const vec3 along = longU ? light.TangentU.xyz : light.TangentV.xyz;
	float best = 0.0;
	for (int p = 0; p < 3; ++p)
	{
		const vec3 point = light.CentreArea.xyz + along * (float(p) - 1.0);
		vec3 toLight = point - P;
		const float distance2 = max(dot(toLight, toLight), 1.0e-4);
		toLight *= inversesqrt(distance2);
		if (dot(N, toLight) <= 0.0)
			continue;
		const float cosLight = abs(dot(lightNormal, toLight));
		best = max(best, cosLight * LobePdf(N, V, toLight, roughness) / distance2);
	}
	return power * best;
}

// **Where a stored draw puts its point, and what light and density the draw had there.**
//
// The trace keeps each texel's first aimed draw as the numbers it was drawn from (o_LampSample): the
// row, the side of the fitting the point was stepped out to, and the draw itself -- the aiming table's
// uv where the row has one, the rectangle's own (su, sv) where it radiates evenly -- plus the density
// the draw had there, which the table's cell fixed and a rounded uv could not find again at a cell's
// edge. These rebuild the point exactly as AimedAtEmitters built it.
vec3 EmitterNormal(GiEmitter light)
{
	return vec3(light.TangentU.w, light.TangentV.w, light.Radiance.w);
}

// Which face of the fitting a point at P sees: the side AimedAtEmitters steps its point out to.
float EmitterSide(GiEmitter light, vec3 P)
{
	const vec3 flat_ = light.CentreArea.xyz;
	return dot(P - flat_, EmitterNormal(light)) >= 0.0 ? 1.0 : -1.0;
}

vec3 EmitterPointFromDraw(GiEmitter light, vec2 drawn, float side)
{
	float su = drawn.x;
	float sv = drawn.y;
	if (int(light.Aim.y + 0.5) > 0)
	{
		su = light.UvToSurface0.x * drawn.x + light.UvToSurface0.y * drawn.y + light.UvToSurface0.z;
		sv = light.UvToSurface1.x * drawn.x + light.UvToSurface1.y * drawn.y + light.UvToSurface1.z;
	}
	const vec3 flat_ = light.CentreArea.xyz + light.TangentU.xyz * su + light.TangentV.xyz * sv;
	return flat_ + EmitterNormal(light) * (light.UvToSurface1.w * side);
}

vec3 EmitterRadianceFromDraw(GiEmitter light, vec2 drawn)
{
	vec3 radiance = light.Radiance.rgb;
	if (int(light.Aim.y + 0.5) > 0)
		radiance *= textureLod(u_Textures[nonuniformEXT(int(light.Aim.z + 0.5))], drawn, 0.0).rgb;
	return radiance;
}

#endif
