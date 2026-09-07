import io
p = r'C:\Users\ism19\Code\RageV\RageV\src\RageV\Renderer\Renderer3D.cpp'
d = io.open(p, 'r', encoding='utf-8', newline='').read()
crlf = d.count('\r\n'); print('CRLF:', crlf, 'bare LF:', d.count('\n') - crlf)
N = '\r\n' if crlf else '\n'

old = ('\t\t\t\tif (!s_Data->DebugCounts)' + N +
       '\t\t\t\t\tEnsureDebugCounts(1, 1);' + N +
       '\t\t\t}' + N +
       '\t\t}' + N +
       '\t\tif (s_Data->RayReflectionsOn)' + N +
       '\t\t\tdefines.push_back("RV_RAY_REFLECTIONS");' + N)
assert d.count(old) == 1, 'anchor matched %d times' % d.count(old)

new = ('\t\t\t\tif (!s_Data->DebugCounts)' + N +
       '\t\t\t\t\tEnsureDebugCounts(1, 1);' + N +
       '\t\t\t}' + N +
       '\t\t}' + N +
       '\t\t// `--shadow-budget=K` (WR-16 S1), on the same rule as the counts' + N +
       '\t\t// above: a run without the flag pays nothing. The instrument holds' + N +
       '\t\t// eight reservoirs of a lamp\'s whole term in the lit shader, and a' + N +
       '\t\t// declaration spends the registers whether or not the branch that' + N +
       '\t\t// fills it runs -- so compiled in unconditionally it cost 0.84 ms of' + N +
       '\t\t// the showroom\'s frame with K at zero (bisect, 2026-09-05). A' + N +
       '\t\t// command-line measurement flag, read once, so deciding it here at' + N +
       '\t\t// compile time is the whole of it.' + N +
       '\t\tif (EngineConfig::Get().ShadowBudget > 0)' + N +
       '\t\t\tdefines.push_back("RV_SHADOW_BUDGET");' + N +
       '\t\tif (s_Data->RayReflectionsOn)' + N +
       '\t\t\tdefines.push_back("RV_RAY_REFLECTIONS");' + N)
d = d.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok: RV_SHADOW_BUDGET define added')
