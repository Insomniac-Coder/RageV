"""RT-1b, the C++ side (docs/RT-SERIES.md RT-1):
- `Lamps` becomes `RaysPerPixel` (the owner's name): the preset column, the
  engine-config fields, the flag `--rays-per-pixel=K[,target]` with
  `--light-sampling` kept as an alias;
- WR-16 S1's `--shadow-budget` and S4's `--shade-lights` go: the fields, the
  parsing, the help, the define, their bits in RayRates.w.
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

def cut_lines(s, nl, first_pred, last_pred):
    """Remove the lines from the first matching first_pred to the first matching last_pred after it."""
    lines = s.split(nl)
    a = next(i for i, l in enumerate(lines) if first_pred(l))
    b = next(i for i in range(a, len(lines)) if last_pred(lines[i]))
    del lines[a:b + 1]
    return nl.join(lines)

def rename(s):
    s = re.sub(r'\bHasLightSamplingOverride\b', 'HasRaysPerPixelOverride', s)
    s = re.sub(r'\bLightSamplingTarget\b', 'RaysPerPixelTarget', s)
    s = re.sub(r'\bLightSampling\b', 'RaysPerPixel', s)
    s = re.sub(r'\b(preset|rtPreset)\.Lamps\b', r'\1.RaysPerPixel', s)
    return s

# ---------------------------------------------------------------- RenderSettings.h
p = 'RageV/src/RageV/Renderer/RenderSettings.h'; s, nl = load(p)
lines = s.split(nl)
i = next(k for k, l in enumerate(lines) if l == '\t\tint   Lamps;')
j = i
while lines[j - 1].startswith('\t\t//'):
    j -= 1
lines[j:i + 1] = [
    '\t\t// **Rays per pixel** (the owner\'s name, RT-1): the lights a pixel chooses',
    '\t\t// by importance, shades and traces -- one shadow ray each -- on land',
    '\t\t// (the direct-light signal, RT-first T5) and on the water (WR-16 S4).',
    '\t\t// Zero shades every light: the reference arm.',
    '\t\tint   RaysPerPixel;',
]
s = nl.join(lines)
s = s.replace('--light-sampling', '--rays-per-pixel')
save(p, s)

# ---------------------------------------------------------------- EngineConfig.h
p = 'RageV/src/RageV/Core/EngineConfig.h'; s, nl = load(p)
# the help: the two instruments' entries go, the sampler's is renamed
def cut_help_entry(s, nl, option):
    """Remove one option's help entry: its first line and the continuation
    lines up to (not including) the next option or anything else."""
    lines = s.split(nl)
    a = next(i for i, l in enumerate(lines) if l.startswith('//   ' + option))
    b = a + 1
    while b < len(lines) and lines[b].startswith('//                           '):
        b += 1
    del lines[a:b]
    return nl.join(lines)
s = cut_help_entry(s, nl, '--shade-lights=N')
s = cut_help_entry(s, nl, '--shadow-budget=K[,full]')
s = rep(s, nl, """//   --light-sampling=K[,target]  WR-16 S4: a live surface with more lamps
//                           reaching it than K scores them cheaply, keeps K
//                           by weighted reservoir sampling, and shades and
//                           traces only those -- the unbiased estimate S1
//                           measured. target is 'term' (the default: the
//                           unshadowed term's luminance, the water's own lobe
//                           with its Fresnel and masking) or 'irradiance'
//                           (S1's cheap target, kept as the arm it lost as).""",
"""//   --rays-per-pixel=K[,target]  the lights a pixel chooses by importance,
//                           shades and traces (one shadow ray each), on land
//                           (the direct-light signal) and on the water (WR-16
//                           S4); zero shades every light -- the reference arm.
//                           target is 'term' (the default: the unshadowed
//                           term's luminance) or 'irradiance' (S1's cheap
//                           target). --light-sampling is the old name, still
//                           read.""")
# the fields
s = cut_lines(s, nl, lambda l: l.startswith('\t\t// --shadow-budget=K: a measurement'), lambda l: l == '\t\tbool  ShadowBudgetFullTarget = false;')
s = cut_lines(s, nl, lambda l: l.startswith('\t\t// --shade-lights=N: a measurement'), lambda l: l == '\t\tint   ShadeLights = -1;')
s = rep(s, nl, """		// --light-sampling=K[,target]: WR-16 S4's sampler. K reservoirs per
		// pixel over the cell's lamps, scored by the target, and only the K
		// survivors shaded and traced. Zero -- the default -- leaves every
		// lamp shaded as it is today. The target: 0 the cheap irradiance S1
		// measured unusable on the water, 1 the same with the specular lobe's
		// magnitude added, which is what the water's glitter needs. Carried
		// in RayRates.w's bits 16-19 and 20-21.
		// Set only when the flag was given, so `--light-sampling=0` can turn
		// the sampler OFF for one run against a project that carries it on --
		// which is the A/B the measurement scripts need.""",
"""		// --rays-per-pixel=K[,target] (RT-1; --light-sampling is the old name):
		// the lights a pixel chooses by importance, shades and traces -- on
		// land in the DirectTrace pass (RT-first T5), on the water in its lamp
		// passes (WR-16 S4). Zero shades every light. The target: 0 the cheap
		// irradiance S1 measured unusable on the water, 1 the same with the
		// specular lobe's magnitude added. Carried in RayRates.w's bits 16-19
		// and 20-21. Set only when the flag was given, so `--rays-per-pixel=0`
		// turns the sampling OFF for one run against a project that carries it
		// on -- the A/B the measurement scripts need.""")
s = rename(s)
assert 'ShadowBudget' not in s and 'ShadeLights' not in s, 'an instrument field survived'
save(p, s)

# ---------------------------------------------------------------- EngineConfig.cpp
p = 'RageV/src/RageV/Core/EngineConfig.cpp'; s, nl = load(p)
s = rep(s, nl, """		if (key == "light-sampling" || key == "lightsampling")""",
       """		if (key == "rays-per-pixel" || key == "raysperpixel"
			|| key == "light-sampling" || key == "lightsampling")   // the old name (RT-1)""")
lines = s.split(nl)
for start in ('\t\tif (key == "shade-lights" || key == "shadelights")', '\t\tif (key == "shadow-budget" || key == "shadowbudget")'):
    a = next(i for i, l in enumerate(lines) if l == start)
    b = next(i for i in range(a, len(lines)) if lines[i] == '')   # the blank line that ends the block
    del lines[a:b + 1]
s = nl.join(lines)
s = s.replace('light-sampling', 'rays-per-pixel')
s = rename(s)
assert 'ShadowBudget' not in s and 'ShadeLights' not in s and 'shade-lights' not in s and 'shadow-budget' not in s
save(p, s)

# ---------------------------------------------------------------- Renderer3D.cpp
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
s = cut_lines(s, nl, lambda l: l.startswith('\t\t// `--shadow-budget=K` (WR-16 S1), on the same rule as the counts'),
              lambda l: l == '\t\t\tdefines.push_back("RV_SHADOW_BUDGET");')
s = rep(s, nl, """			// .w: WR-16 S1's fixed shadow budget per pixel, a measurement;
			// zero leaves the per-light rays and the thinning alone.
			s_Data->Scene.RayRates = Vec4(""", """			// .w: the rays per pixel (bits 16-19) and the score's target (20-21),
			// the direct-light signal's switch (bit 22).
			s_Data->Scene.RayRates = Vec4(""")
s = rep(s, nl, """				// .w again: S1's budget in the low five bits, and above them
				// S4's sizing flag --shade-lights=N as N + 1 (zero is off).
				// Both are whole numbers well inside a float's exact range.
				(float)(config.ShadowBudget + (config.ShadowBudgetFullTarget ? 16 : 0)
						+ (config.ShadeLights >= 0 ? 256 * (config.ShadeLights + 1) : 0)
						+ 65536 * (config.HasLightSamplingOverride
									   ? config.LightSampling : preset.Lamps)
						+ 1048576 * config.LightSamplingTarget""",
"""				// Whole numbers well inside a float's exact range.
				(float)(65536 * (config.HasRaysPerPixelOverride
									 ? config.RaysPerPixel : preset.RaysPerPixel)
						+ 1048576 * config.RaysPerPixelTarget""")
s = rename(s)
assert 'ShadowBudget' not in s and 'ShadeLights' not in s and 'RV_SHADOW_BUDGET' not in s
save(p, s)

# ---------------------------------------------------------------- FrameGraphBuilder.cpp
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
s = rename(s)
s = s.replace('--light-sampling', '--rays-per-pixel')
save(p, s)
print('RT-1b patched')
