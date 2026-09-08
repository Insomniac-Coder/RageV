"""The stride in the two shaders that declare the block for themselves."""
import io, sys

STRIDE = ('// **How many lanes one slot holds.** Named because it was spelled 32 in four\n'
          '// files, and growing the block meant finding all four: this and\n'
          '// pbr_fragment.glsl\'s copy are where a slot\'s width is written now\n'
          '// (RayCounters::Count on the CPU side; the three must agree).\n'
          'const uint RAY_COUNTER_STRIDE = 64u;\n')

for p, anchor in (
    ('RageVEditor/assets/shaders/taa_resolve.rvshader', 'const uint RAY_LANE_TAA_PIXELS = 10u;'),
    ('RageVEditor/assets/shaders/rtao_compute.rvshader', 'const uint RAY_LANE_AO = 4u;'),
):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if 'RAY_COUNTER_STRIDE' in s:
        print(p, 'already'); continue
    for old, new, what in (
        ('uint Counts[64 * 32];', 'uint Counts[64 * 64];', 'array'),
        (anchor, STRIDE + anchor, 'stride const'),
    ):
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    n = s.count('RayCounterSlot() * 32u')
    if n == 0:
        sys.exit(p + ': no stride use')
    s = s.replace('RayCounterSlot() * 32u', 'RayCounterSlot() * RAY_COUNTER_STRIDE')
    io.open(p, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('%s: %d stride use(s) named' % (p, n))

# reflection_accumulate has no declaration of its own -- it includes
# pbr_fragment.glsl, whose constant it picks up.
p = 'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')
n = s.count('RayCounterSlot() * 32u')
if n:
    s = s.replace('RayCounterSlot() * 32u', 'RayCounterSlot() * RAY_COUNTER_STRIDE')
    io.open(p, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('%s: %d stride use(s) named' % (p, n))
