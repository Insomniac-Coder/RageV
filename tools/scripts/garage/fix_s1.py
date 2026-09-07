import io, sys
p = r'C:\Users\ism19\Code\RageV\RageVEditor\assets\shaders\include\pbr_fragment.glsl'
d = io.open(p, 'r', encoding='utf-8', newline='').read()
orig = d
N = '\r\n'

def sub(old, new, label):
    global d
    n = d.count(old)
    assert n == 1, 'MATCHED %d TIMES for %s' % (n, label)
    d = d.replace(old, new)
    print('ok:', label)

# A. Gate the reservoir declarations; keep budgetCode, keep a compile-time zero.
sub(
'\tconst int shadowBudget = clamp(budgetCode & 15, 0, 8);' + N,
'\t//' + N +
'\t// **The instrument is compiled in only when it is asked for**' + N +
'\t// (`--shadow-budget=K`), exactly as RV_DEBUG_VIEW is. The note above' + N +
'\t// already says eight reservoirs of a term each are registers this shader' + N +
'\t// does not have to spare -- and a *declaration* spends them whether or' + N +
'\t// not the branch that fills it ever runs. Left in unconditionally it' + N +
'\t// cost 0.84 ms of the showroom\'s 6.6 ms frame while switched off,' + N +
'\t// measured by bisect on 2026-09-05: c0d70c3 6.56 ms, 554589b 7.40 ms,' + N +
'\t// the opaque pass carrying all of it. Renderer3D pushes the define when' + N +
'\t// EngineConfig::ShadowBudget is non-zero.' + N +
'#ifdef RV_SHADOW_BUDGET' + N +
'\tconst int shadowBudget = clamp(budgetCode & 15, 0, 8);' + N,
'A: gate declarations')

sub(
'\t\t\tbudgetSeed[r] = BudgetHash(px.x ^ (px.y << 16u) ^ (uint(r) << 28u) ^ frameSalt);' + N +
'\t\t}' + N +
'\t}' + N +
'#endif' + N,
'\t\t\tbudgetSeed[r] = BudgetHash(px.x ^ (px.y << 16u) ^ (uint(r) << 28u) ^ frameSalt);' + N +
'\t\t}' + N +
'\t}' + N +
'#else' + N +
'\t// A compile-time zero, so the sampler\'s exclusivity test below reads as' + N +
'\t// it always did and folds away with it.' + N +
'\tconst int shadowBudget = 0;' + N +
'#endif' + N +
'#endif' + N,
'B: else-branch zero')

# C. The arm that elects a lamp into the reservoirs.
sub(
'\t\telse if (liveShare > 0.0 && kind != 0 && shadowBudget > 0)' + N +
'\t\t{' + N +
'\t\t\tbudgeted = true;' + N +
'\t\t}' + N,
'#ifdef RV_SHADOW_BUDGET' + N +
'\t\telse if (liveShare > 0.0 && kind != 0 && shadowBudget > 0)' + N +
'\t\t{' + N +
'\t\t\tbudgeted = true;' + N +
'\t\t}' + N +
'#endif' + N,
'C: election arm')

# D. The reservoir fill, whose #endif already carries the else.
sub(
'#ifdef RV_RAY_SHADOWS' + N + '\t\tif (budgeted)' + N,
'#if defined(RV_RAY_SHADOWS) && defined(RV_SHADOW_BUDGET)' + N + '\t\tif (budgeted)' + N,
'D: reservoir fill')

# E. The estimate that traces the K survivors.
sub(
'#ifdef RV_RAY_SHADOWS' + N + '\t// WR-16 S1: the K survivors are traced,',
'#if defined(RV_RAY_SHADOWS) && defined(RV_SHADOW_BUDGET)' + N + '\t// WR-16 S1: the K survivors are traced,',
'E: survivor estimate')

assert d != orig
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('written; CRLF preserved:', d.count('\r\n'), 'bare LF:', d.count('\n') - d.count('\r\n'))
