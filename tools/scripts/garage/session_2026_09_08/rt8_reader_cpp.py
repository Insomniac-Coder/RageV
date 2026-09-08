"""RT-8: the C++ half of the reader.

Anchors carry no indentation of their own -- the indent of each insertion is
read back off the line the anchor lands on, which is what stopped three
attempts at guessing it.
"""
import io, sys

P = r'RageV/src/RageV/Renderer/PostProcess.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
if 'waterMotion' in s:
    sys.exit('already patched')


def indent_of(anchor):
    i = s.index(anchor)
    j = s.rindex('\n', 0, i) + 1
    line = s[j:i]
    return line if line.strip() == '' else ''


def insert_after(anchor, lines):
    """Put `lines` on their own lines after the anchor, at the anchor's indent."""
    global s
    if s.count(anchor) != 1:
        sys.exit('anchor %r matched %d times' % (anchor[:48], s.count(anchor)))
    pad = indent_of(anchor)
    s = s.replace(anchor, anchor + ''.join('\n' + pad + l for l in lines), 1)


def replace_once(old, new):
    global s
    if s.count(old) != 1:
        sys.exit('anchor %r matched %d times' % (old[:48], s.count(old)))
    s = s.replace(old, new, 1)


# 1. Dispatch takes an eighth texture. The anchor is the last parameter; the
#    close paren moves onto the new one.
a = 'const Ref<RHITexture>& seventh, Sampling seventhSampling)\n\t{'
pad = indent_of(a)
replace_once(a, 'const Ref<RHITexture>& seventh, Sampling seventhSampling,\n'
                + pad + '// RT-8: binding 9, the water layer.\n'
                + pad + 'const Ref<RHITexture>& eighth, Sampling eighthSampling)\n\t{')

# 2. ...and binds it at 9.
insert_after('set->SetTexture(8, seventh, samplerFor(seventhSampling));', [
    '// RT-8: the water layer\'s motion, at 9. The resolve alone again, and',
    '// always bound where it is declared -- a mask of zero is what says',
    '// "no wave here", so black is a meaningful value rather than the',
    '// undefined read an unbound binding would be.',
    'if (eighth)',
    '\tset->SetTexture(9, eighth, samplerFor(eighthSampling));',
])

# 3. TemporalResolve takes the water layer.
a = 'const Ref<RHITexture>& material, bool boxGeometry)\n\t{'
pad = indent_of(a)
replace_once(a, 'const Ref<RHITexture>& material, bool boxGeometry,\n'
                + pad + '// **RT-8: the water layer\'s motion and mask.** Null leaves\n'
                + pad + '// every pixel on the geometry\'s velocity, which is what the\n'
                + pad + '// sea had -- and what made a moving wave look stationary.\n'
                + pad + 'const Ref<RHITexture>& waterMotion)\n\t{')

# 4. ...and passes it down to the dispatch.
a = 'material, Sampling::Point);\n\t}'
pad = indent_of(a)
replace_once(a, 'material, Sampling::Point,\n'
                + pad + '// RT-8: the water layer. Point for the same reason the\n'
                + pad + '// scene\'s velocity is -- and because its z is a mask,\n'
                + pad + '// which averaged across a shoreline would be half a wave.\n'
                + pad + 'waterMotion ? waterMotion : s_Data->Black, Sampling::Point);\n\t}')

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('patched PostProcess.cpp')
