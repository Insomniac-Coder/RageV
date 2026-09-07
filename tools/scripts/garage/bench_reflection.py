"""The reflection pass's frame cost in the garage, as a palindrome.

    bench_reflection.py [--frames=300] [--size=1600x900]

Two arms at the owner's camera, four pairs in the order A B B A A B B A so
the GPU's drift over a session shows as spread rather than as a difference:
  scaled    the reflection passes as shipped (one ray a texel since
            2026-09-06; the name is kept for the old comparisons)
  off       --reflection-pass=off, the old in-line rays in the lit shader
The `unscaled` arm (the preset's full ray count on every glossy pixel) went
with the one-ray trace (WR-16 R2, 2026-09-06): the line it edited is dead.
Every number is the engine's own --benchmark report; the per-pass lines are
`scene/<Pass>  <cpu ms>  <gpu ms>` under "render graph, by pass". Keep the
editor closed.
"""
import os, re, subprocess, sys, shutil, statistics

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'reflection_trace.rvshader')
DST = os.path.join(RT, 'assets', 'shaders', 'reflection_trace.rvshader')
CAM = '-2.3,0.72,-2,11,0,4'
SCALE_LINE = '\tcount = clamp(int(floor(float(count) * mirror + 0.5)), 1, 8);\n'


def stage(scaled):
    text = open(SRC, encoding='utf-8').read()
    if not scaled:
        assert text.count(SCALE_LINE) == 1
        text = text.replace(SCALE_LINE, '')
    open(DST, 'w', encoding='utf-8', newline='\n').write(text)


def run(frames, size, extra):
    cmd = [os.path.join(RT, 'RageVRuntime.exe'),
           '--project=' + os.path.join(ROOT, 'SampleProject'),
           '--scene=scenes/showroom.rage', '--rhi=vulkan',
           '--render-defaults=off', '--vsync=off',
           '--width=%d' % size[0], '--height=%d' % size[1],
           '--benchmark=%d' % frames, '--import-cache=off',
           '--camera=' + CAM] + list(extra)
    p = subprocess.run(cmd, cwd=RT, capture_output=True, text=True, timeout=900, errors='replace')
    text = p.stdout + p.stderr
    out = {}
    m = re.search(r'frame\s+mean\s+([0-9.]+) ms\s+median\s+([0-9.]+)\s+p95\s+([0-9.]+)', text)
    if m:
        out['mean'] = float(m[1]); out['median'] = float(m[2])
    m = re.search(r'whole frame \(GPU\)\s+([0-9.]+) ms', text)
    if m:
        out['gpu'] = float(m[1])
    m = re.search(r'reflection ([0-9.]+) M, GI', text)
    if m:
        out['rays'] = float(m[1])
    for name in ('ReflectionTrace', 'ReflectionResolve', 'ReflectionAccumulate', 'ReflectionBlur',
                 'ReflectionComposite', 'Scene', 'TAA resolve'):
        # The graph prefixes its passes with the graph's name: `scene/ReflectionTrace`.
        m = re.search(r'\[benchmark\]\s+(?:[A-Za-z]+/)?%s\s+([0-9.]+)\s+([0-9.]+)' % re.escape(name), text)
        if m:
            out[name] = float(m[2])
    if 'mean' not in out:
        print(text[-3000:])
        raise SystemExit('no report')
    return out


def main(argv):
    opts = dict(a.lstrip('-').split('=', 1) for a in argv if '=' in a)
    frames = int(opts.get('frames', 300))
    size = tuple(int(v) for v in opts.get('size', '1600x900').split('x'))
    arms = {'scaled': (True, []), 'off': (True, ['--reflection-pass=off'])}
    order = ['scaled', 'off', 'off', 'scaled', 'scaled', 'off', 'off', 'scaled']
    results = {k: [] for k in arms}
    try:
        for arm in order:
            scaled, extra = arms[arm]
            stage(scaled)
            r = run(frames, size, extra)
            results[arm].append(r)
            print('%-9s frame %6.2f ms  gpu %6.2f  trace %5.2f  resolve %5.2f  accumulate %5.2f  blur %5.2f  composite %5.2f  scene %6.2f  rays %5.2f M' % (
                arm, r.get('mean', 0), r.get('gpu', 0), r.get('ReflectionTrace', 0), r.get('ReflectionResolve', 0),
                r.get('ReflectionAccumulate', 0), r.get('ReflectionBlur', 0), r.get('ReflectionComposite', 0), r.get('Scene', 0), r.get('rays', 0)))
    finally:
        shutil.copyfile(SRC, DST)
    print()
    print('%-9s %14s %14s %14s %14s %14s %14s %14s %12s' % ('arm', 'frame ms', 'gpu ms', 'trace ms', 'resolve ms', 'accum ms', 'blur ms', 'scene ms', 'rays M'))
    for arm in arms:
        rs = results[arm]
        def col(key):
            vals = [r.get(key, 0.0) for r in rs]
            return '%6.2f +-%4.2f' % (statistics.mean(vals), (max(vals) - min(vals)) / 2.0)
        print('%-9s %14s %14s %14s %14s %14s %14s %14s %12s' % (arm, col('mean'), col('gpu'), col('ReflectionTrace'),
                                                              col('ReflectionResolve'), col('ReflectionAccumulate'),
                                                              col('ReflectionBlur'), col('Scene'), col('rays')))


if __name__ == '__main__':
    main(sys.argv[1:])
