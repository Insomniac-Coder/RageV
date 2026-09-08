# -*- coding: utf-8 -*-
"""Where does DirectWaterTrace's extra millisecond go?

At four samples it casts *fewer* rays than the two passes it replaces -- 1.97 M
against 2.38 M -- and still costs 2.25 ms against their 1.26. So it is not the
rays, and the answer is inside the pass.

Three arms, each removing one piece and leaving the rest. The picture is wrong
on purpose in the middle two and the time is honest, which is the contract
`--water-ablate` and `--shade-lights` already carry.

  full       as it stands
  cheapscore the reservoir scored by irradiance alone -- no lobe, no masking
             term, which is S1's cheap target
  noshade    the reservoir built and the survivors' rays traced, but the lobe
             never evaluated for the term
"""
import io, os, shutil, re, subprocess, sys

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'direct_trace.rvshader')
STAGED = os.path.join(RT, 'assets', 'shaders', 'direct_trace.rvshader')
CRLF, LF = chr(13) + chr(10), chr(10)

full = io.open(SRC, encoding='utf-8', newline='').read().replace(CRLF, LF)

# --- cheap score: the water branch returns before it builds the lobe -----
cheap = full.replace(
    '''	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - p.N * dot(windDir, p.N));
	const vec3 windB = cross(p.N, windT);
	vec3 viewT = p.V - p.N * dot(p.V, p.N);''',
    '''	return lum * NdotL * diffuseLum;   // ABLATION: irradiance alone
	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - p.N * dot(windDir, p.N));
	const vec3 windB = cross(p.N, windT);
	vec3 viewT = p.V - p.N * dot(p.V, p.N);''')
if cheap == full:
    sys.exit('could not ablate the score')

# --- no shading lobe: DirectTerm's water branch left flat ----------------
noshade = full.replace(
    '''	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - N * dot(windDir, N));
	const vec3 windB = cross(N, windT);
	float NDF;
	float G;''',
    '''	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - N * dot(windDir, N));
	const vec3 windB = cross(N, windT);
	float NDF = 1.0;   // ABLATION: the term's lobe never evaluated
	float G = 1.0;
	if (false)''')
if noshade == full:
    sys.exit('could not ablate the term')

ARMS = [('full', full), ('cheapscore', cheap), ('noshade', noshade)]


def bench(k=4):
    p = subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                        '--project=' + os.path.join(ROOT, 'SampleProject'),
                        '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                        '--render-defaults=off', '--vsync=off',
                        '--width=1600', '--height=900',
                        '--benchmark=150', '--frame-time=0.0166',
                        '--camera=70,4.5,705,0.01,-46.98,-2.86',
                        '--water-direct=on', '--rays-per-pixel=%d' % k],
                       cwd=RT, capture_output=True, text=True, timeout=1800,
                       errors='replace')
    out = (p.stdout or '') + (p.stderr or '')
    ms, rays = None, None
    for line in out.splitlines():
        if 'DirectWaterTrace' in line:
            m = re.findall(r'([0-9]+\.[0-9]+)', line)
            if len(m) >= 2:
                ms = float(m[-1])
        if 'rays per frame' in line:
            m = re.search(r'shadow ([0-9.]+) M', line)
            if m:
                rays = float(m.group(1))
        if 'did not compile' in line:
            print('   !!', line.strip()[:180])
    return ms, rays


try:
    print('%-11s %-10s %-10s' % ('arm', 'pass ms', 'shadow M'))
    for tag, text in ARMS:
        io.open(STAGED, 'w', encoding='utf-8', newline='').write(text)
        ms, rays = bench()
        print('%-11s %-10s %-10s' % (tag,
                                     '%.3f' % ms if ms else '--',
                                     '%.2f' % rays if rays else '--'))
finally:
    shutil.copyfile(SRC, STAGED)
    print('staged copy restored and identical:',
          io.open(SRC, 'rb').read() == io.open(STAGED, 'rb').read())
