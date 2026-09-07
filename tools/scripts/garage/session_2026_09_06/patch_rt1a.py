"""RT-1a, the lit shader (docs/RT-SERIES.md RT-1):
- the G-buffer's id lane packs the material's occlusion beside the shading
  roughness (the loss clamp in the DirectTrace pass needs it);
- under the direct-light signal the loop walks no light at all -- the pass
  carries the field's loss too now -- so T5's subtractive-only block goes;
- WR-16 S1's fixed budget (`RV_SHADOW_BUDGET`, --shadow-budget) and S4's
  sizing (--shade-lights, on screen and at hits) are removed: measurement
  instruments whose questions are answered, dead under the signal.
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'; s, nl = load(p)
lines = s.split(nl)

def find_line(pred, start=0):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    raise AssertionError('no line')

def remove_pp_span(start_pred):
    """Remove from the line matching start_pred (an #if...) to its matching #endif."""
    a = find_line(start_pred)
    depth = 0
    for i in range(a, len(lines)):
        t = lines[i].lstrip('\t')
        if t.startswith('#if'):
            depth += 1
        elif t.startswith('#endif'):
            depth -= 1
            if depth == 0:
                del lines[a:i + 1]
                return
    raise AssertionError('unterminated span')

def remove_brace_block(if_pred):
    """Remove an `if (...)` line and the { ... } block that follows it."""
    a = find_line(if_pred)
    assert lines[a + 1].strip() == '{', lines[a + 1]
    depth = 0
    for i in range(a + 1, len(lines)):
        depth += lines[i].count('{') - lines[i].count('}')
        if depth == 0:
            del lines[a:i + 1]
            return
    raise AssertionError('unterminated block')

# 1. the id lane: shading roughness in the integer part, occlusion in the fraction
i = find_line(lambda l: l == '\to_SurfaceId = vec2(v_Instance.y > 0.5 ? -v_ObjectId : v_ObjectId, shadingRoughness);')
lines[i] = '\to_SurfaceId = vec2(v_Instance.y > 0.5 ? -v_ObjectId : v_ObjectId,\n\t\t\t\t\t   floor(shadingRoughness * 1023.0) + clamp(occlusion, 0.0, 0.999));'.replace('\n', nl)
j = find_line(lambda l: l.startswith('\t// the object\'s id, negative for a Static surface (RT-first T5)'), i - 6)
lines[j] = '\t// the object\'s id, negative for a Static surface (RT-first T5); and beside'
lines.insert(j + 1, '\t// it the roughness analytic lights are shaded with (its integer part over')
lines.insert(j + 2, '\t// 1023) with the material\'s occlusion in the fraction (RT-1, for the loss')
lines.insert(j + 3, '\t// clamp) -- what the DirectTrace pass needs of the material and the split.')
k = find_line(lambda l: l.startswith('\t// DirectTrace pass needs of the material and the static/moving split.'), j)
del lines[k]

# 2. under the signal the loop has nothing to walk
i = find_line(lambda l: l == '\tint total = directionalCount + int(cellCount);')
lines[i + 1:i + 1] = [
    '\t// RT-1: under the direct-light signal every light -- its live share and',
    '\t// the field\'s loss alike -- is shaded, traced and clamped in the',
    '\t// DirectTrace pass, which also counts the lights; this loop walks none.',
    '\tif (directSignal)',
    '\t\ttotal = 0;',
]

# 3. T5's subtractive-only block goes with it
a = find_line(lambda l: l == '\t\t// RT-first T5: with the direct-light signal on, every light\'s live')
assert lines[a - 1] == '#ifdef RV_DIRECT_SIGNAL_INPUT', lines[a - 1]
b = find_line(lambda l: l == '#endif', a)
assert lines[b - 1] == '\t\t}' and lines[b - 2] == '\t\t\tliveShare = 0.0;', lines[b - 3:b]
del lines[a - 1:b + 1]

# 4. S1's fixed budget: the four spans, the reservoirs' flag, the sampler's test
while any(l.strip() in ('#ifdef RV_SHADOW_BUDGET', '#if defined(RV_RAY_SHADOWS) && defined(RV_SHADOW_BUDGET)') for l in lines):
    remove_pp_span(lambda l: l.strip() in ('#ifdef RV_SHADOW_BUDGET', '#if defined(RV_RAY_SHADOWS) && defined(RV_SHADOW_BUDGET)'))
i = find_line(lambda l: l == '\t\tbool budgeted = false;')
# and the comment lines that introduced it
j = i
while lines[j - 1].startswith('\t\t//'):
    j -= 1
del lines[j:i + 1]
i = find_line(lambda l: l.startswith('\tif (!directSignal && sampleCount > 0 && shadowBudget == 0 && shadeLimit < 0 && fieldWeight <= 0.0'))
lines[i] = lines[i].replace('shadowBudget == 0 && shadeLimit < 0 && ', '')
assert not any('shadowBudget' in l or 'budgetFullTarget' in l or 'budgetTotal' in l for l in lines), 'a budget reference survived'

# 5. S4's sizing flag: on screen and at hits
i = find_line(lambda l: l == '\tconst int shadeLimit = ((int(u_Scene.RayRates.w + 0.5) >> 8) & 255) - 1;')
assert lines[i + 1] == '\tint shadedLights = 0;'
j = i
while lines[j - 1].startswith('\t//'):
    j -= 1
del lines[j:i + 2]
remove_brace_block(lambda l: l == '\t\tif (shadeLimit >= 0 && i >= directionalCount)')
i = find_line(lambda l: l == '\tconst int hitShadeLimit = ((int(u_Scene.RayRates.w + 0.5) >> 8) & 255) - 1;')
assert lines[i + 1] == '\tint hitShaded = 0;'
j = i
while lines[j - 1].startswith('\t//'):
    j -= 1
del lines[j:i + 2]
remove_brace_block(lambda l: l == '\t\tif (hitShadeLimit >= 0 && i >= int(u_Scene.ClusterGrid.w))')
assert not any('shadeLimit' in l or 'shadedLights' in l or 'hitShadeLimit' in l or 'hitShaded' in l for l in lines), 'a sizing reference survived'

save(p, nl.join(lines))
print('RT-1a patched')
