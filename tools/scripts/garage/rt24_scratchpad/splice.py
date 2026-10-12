import io, sys
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
new = io.open(sys.argv[1], encoding='utf-8').read().replace('\r\n', '\n')
raw = io.open(p, encoding='utf-8', newline='').read()
crlf = raw.count('\r\n') > raw.count('\n') // 2
t = raw.replace('\r\n', '\n')
head = '\t\tRGResource glassReflected = kRGInvalid;\n'
tail = '\t\telse if (desc.GlassReflections)\n\t\t{\n\t\t\tdesc.GlassReflections->Invalidate();\n\t\t}\n'
assert t.count(head) == 1 and t.count(tail) == 1
start = t.index(head)
end = t.index(tail, start) + len(tail)
t = t[:start] + new + t[end:]
if crlf:
    t = t.replace('\n', '\r\n')
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('spliced, crlf =', crlf)
