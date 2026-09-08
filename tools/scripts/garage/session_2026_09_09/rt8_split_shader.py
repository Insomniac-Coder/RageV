# -*- coding: utf-8 -*-
"""RT-8 job 1: the shared direct pass splits into choosing and shading.

The sea's two private passes are cheap because of a split -- the picking runs
once per 2x2 block and the shading runs per pixel -- and one fused pass cannot
have both. So the shared pass learns the same shape.

Three modes out of one file, so there is one copy of the score, the term, the
visibility and the field handling:

  RV_DIRECT_CHOOSE   walk the cluster list, score, keep K by reservoir
                     sampling, write which lamps and what each is worth.
  RV_DIRECT_SHADE    read that, shade those lamps, trace their rays.
  neither            the fused pass, exactly as before and untouched.

**What the choose pass writes, and why it is one number per lamp.** The fused
path finishes with `total / (K * weight[r])` -- one over the probability that
lamp had of being chosen, over K. Written as that ratio rather than as the
weight and the total separately, so the shade pass needs one value per lamp and
no second attachment for a per-pixel scalar. Whole floats through a UINT
attachment, never a half: a weight is the one number the estimate divides by,
and the water's own choose pass has that lesson written on it already.

**Four lamps, not eight.** The reservoir is written as two uvec4s, and the
sea's own path is capped at four (`RV_LAMP_RESERVOIRS`), so a split capped at
four is exactly like-for-like with what it replaces. Above four the fused pass
still runs, as it always has.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageVEditor/assets/shaders/direct_trace.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'RV_DIRECT_CHOOSE' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# --- outputs and the choice's own bindings -------------------------------
once('''layout(location = 0) out vec4 o_Diffuse;
layout(location = 1) out vec4 o_Specular;''',
'''#if defined(RV_DIRECT_CHOOSE)
// **RT-8: which lamps, and what each is worth.** Whole integers in both: an
// index is not a thing to interpolate, and a weight through a half float would
// quantise the one number the estimate divides by.
layout(location = 0) out uvec4 o_Choice;
layout(location = 1) out uvec4 o_Worth;
#else
layout(location = 0) out vec4 o_Diffuse;
layout(location = 1) out vec4 o_Specular;
#endif

#if defined(RV_DIRECT_SHADE)
// What the choose pass decided, on its own grid. Fetched by texel, never
// sampled: a lamp index is not a thing to average, and neither is the
// reciprocal probability beside it.
layout(set = 3, binding = 4) uniform usampler2D u_ChoiceIn;
layout(set = 3, binding = 5) uniform usampler2D u_WorthIn;
#endif''',
     'outputs')

# --- the two exits -------------------------------------------------------
once('''	o_Diffuse = vec4(0.0, 0.0, 0.0, -1.0);
	o_Specular = vec4(0.0, 0.0, 0.0, -1.0);
''',
'''#if defined(RV_DIRECT_CHOOSE)
	o_Choice = uvec4(0u);
	o_Worth = uvec4(0u);
#else
	o_Diffuse = vec4(0.0, 0.0, 0.0, -1.0);
	o_Specular = vec4(0.0, 0.0, 0.0, -1.0);
#endif
''',
     'exit values')

# --- the directional lights belong to whoever shades ---------------------
once('''	for (int i = 0; i < directionalCount; ++i)
	{
		const GpuLight light = u_Lights.Lights[i];
		const float liveShare = 1.0 - FieldShare(p, light);
		if (liveShare <= 0.0 || !DirectTerm(p, light, d, s, L, toward))
			continue;
		const float visible = DirectVisibility(p, light, uint(i), L, toward, RV_RAY_MASK_SCENE);
		diffuse += d * (liveShare * visible);
		specular += s * (liveShare * visible);
	}
''',
'''#ifndef RV_DIRECT_CHOOSE
	// The directional lights are not chosen -- there are one or two of them and
	// they are the sun -- so they belong to whoever shades, and the choose pass
	// leaves them alone.
	for (int i = 0; i < directionalCount; ++i)
	{
		const GpuLight light = u_Lights.Lights[i];
		const float liveShare = 1.0 - FieldShare(p, light);
		if (liveShare <= 0.0 || !DirectTerm(p, light, d, s, L, toward))
			continue;
		const float visible = DirectVisibility(p, light, uint(i), L, toward, RV_RAY_MASK_SCENE);
		diffuse += d * (liveShare * visible);
		specular += s * (liveShare * visible);
	}
#endif
''',
     'directional loop')

# --- the fused reservoir is skipped whole in shade mode ------------------
once("""	const int K = clamp(int(u_Direct.Rays + 0.5), 0, int(kMaxRays));
	if (K == 0 || int(cellCount) <= K)""",
"""	const int K = clamp(int(u_Direct.Rays + 0.5), 0, int(kMaxRays));
#if defined(RV_DIRECT_SHADE)
	// **The chosen lamps, shaded and traced.** Everything above this is the
	// same point the choose pass built; this is what the fused path does with
	// its reservoir, reading it instead of building it.
	{
		const ivec2 chooseSize = textureSize(u_ChoiceIn, 0);
		const ivec2 chosenAt = clamp(ivec2(gl_FragCoord.xy)
									 / max(int(u_Direct.CameraPosition.w + 0.5), 1),
									 ivec2(0), chooseSize - 1);
		const uvec4 chosen = texelFetch(u_ChoiceIn, chosenAt, 0);
		const uvec4 worthBits = texelFetch(u_WorthIn, chosenAt, 0);
		float visible[4];
		uint  chosenIndex[4];
		float worth[4];
		for (int r = 0; r < 4; ++r)
		{
			chosenIndex[r] = chosen[r];
			worth[r] = uintBitsToFloat(worthBits[r]);
			visible[r] = 0.0;
		}
		const int chosenCount = min(K, 4);
		for (int r = 0; r < chosenCount; ++r)
		{
			if (worth[r] <= 0.0)
				continue;
			const GpuLight light = u_Lights.Lights[chosenIndex[r]];
			if (!DirectTerm(p, light, d, s, L, toward))
				continue;
			// One ray per *distinct* lamp: two reservoirs that kept the same
			// lamp are two samples of one visibility, not two rays.
			bool traced = false;
			for (int q = 0; q < r; ++q)
			{
				if (worth[q] > 0.0 && chosenIndex[q] == chosenIndex[r])
				{
					visible[r] = visible[q];
					traced = true;
					break;
				}
			}
			if (!traced)
				visible[r] = DirectVisibility(p, light, chosenIndex[r], L, toward,
											  RV_RAY_MASK_SCENE);
			// `worth` is already total/weight -- one over the probability this
			// lamp had of being chosen -- so the estimate is that over K.
			const float liveShare = 1.0 - FieldShare(p, light);
			const float scale = visible[r] * liveShare * worth[r] / float(chosenCount);
			diffuse += d * scale;
			specular += s * scale;
		}
	}
#else
#if defined(RV_DIRECT_CHOOSE)
	// A split pass always samples: the fused path's "few enough, shade them
	// all" shortcut cannot be written into four slots. Where the cell is small
	// the reservoir picks those same lamps, with weights that still sum right;
	// it is a little noisier than shading every one and it is not biased.
	if (false)
#else
	if (K == 0 || int(cellCount) <= K)
#endif""",
     'shade block and shortcut guard')

# ...and the fused block is closed off before the field's loss.
once("""	// **The field's loss (7cx, RT-1).** A static surface the field answers""",
"""#endif   // RV_DIRECT_SHADE: the fused reservoir above is not built here

	// **The field's loss (7cx, RT-1).** A static surface the field answers""",
     'fused block end')

# --- the reservoir's result: written, or used in place -------------------
once('''		if (total > 0.0)
		{''',
'''#if defined(RV_DIRECT_CHOOSE)
		// Written as total/weight rather than as the two apart, so the shade
		// pass needs one number per lamp and no attachment for a per-pixel
		// scalar. Zero where the reservoir never saw a candidate.
		uvec4 outIndex = uvec4(0u);
		uvec4 outWorth = uvec4(0u);
		for (int r = 0; r < min(K, 4); ++r)
		{
			outIndex[r] = index[r];
			outWorth[r] = floatBitsToUint(weight[r] > 0.0 && total > 0.0
										  ? total / weight[r] : 0.0);
		}
		o_Choice = outIndex;
		o_Worth = outWorth;
		return;
#endif
		if (total > 0.0)
		{''',
     'reservoir output')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('direct_trace splits into choosing and shading')
