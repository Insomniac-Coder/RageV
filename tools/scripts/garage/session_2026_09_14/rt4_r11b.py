# -*- coding: utf-8 -*-
"""R11, part 2: which weighting of the resolve's shared rays is unbiased?

The clean measurement (rt4_r11.py, harness fixed): the shipped resolve settles the
garage floor 2.50 levels under the truth (each texel's own rays, unbounded, memory
400); Gaussian-only weights +3.22; the ratio uncapped -5.93. Candidates, all at the
mean of frames 360-399 against r11c_truth:

  exact        the pdfs without DistributionGGX's 1e-4 denominator floor
  exactjac     and the neighbour's density moved to this texel's solid angle
               (its hit distance over this texel's, squared)
  ownjac       the neighbour's own draw pdf (the trace's, exact) with that change
  ownjacnocap  the same without the weight cap

Usage: rt4_r11b.py [arm ...] | rt4_r11b.py --analyse
"""
import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'session_2026_09_13'))
import stage_run  # noqa: E402
import rt20_measure as M  # noqa: E402

RES, TRACE = 'reflection_resolve.rvshader', 'reflection_trace.rvshader'
EXACT_FN = (RES, 'float Rotation(ivec2 texel)', '''// R11 test: GGX without DistributionGGX's 1e-4 denominator floor.
float GgxExact(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(3.14159265 * denom * denom, 1.0e-12);
}

float Rotation(ivec2 texel)''')
EXACT_HERE = (RES, 'DistributionGGX(N, H, roughness)', 'GgxExact(N, H, roughness)')
EXACT_THEIRS = (RES, 'DistributionGGX(Nn, Hn, rn)', 'GgxExact(Nn, Hn, rn)')
W_OLD = '\t\tconst float w = min(pdfHere / max(pdfTheirs, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);'
JAC_REAIMED = (RES, W_OLD, '\t\tconst float jacobian = (distanceHere * distanceHere) / max(f.a * f.a, 1.0e-6);\n'
                           '\t\tconst float w = min(pdfHere / max(pdfTheirs * jacobian, 1.0e-6), kMaxWeight) * exp(-2.0 * r * r);')
JAC_OWN = (RES, W_OLD, '\t\tconst float jacobian = (distanceHere * distanceHere) / max(f.a * f.a, 1.0e-6);\n'
                       '\t\tconst float w = min(pdfHere / max(h.z * jacobian, 1.0e-6), kMaxWeight) * exp(-2.0 * r * r);')
TRACE_EXACT = (TRACE, '''	const float pdf = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)
					/ (4.0 * max(dot(V, H), 1.0e-4));
	o_Hit = vec4(OctEncode(direction), min(pdf, 6.0e4), 0.0);''', '''	const float aT = roughness * roughness;
	const float a2T = aT * aT;
	const float nhT = max(dot(N, H), 0.0);
	const float dT = nhT * nhT * (a2T - 1.0) + 1.0;
	const float pdf = (a2T / max(3.14159265 * dT * dT, 1.0e-12)) * max(dot(N, H), 1.0e-4)
					/ (4.0 * max(dot(V, H), 1.0e-4));
	o_Hit = vec4(OctEncode(direction), min(pdf, 3.0e38), 0.0);''')
NOCAP = (RES, 'const float kMaxWeight = 4.0;', 'const float kMaxWeight = 1.0e6;')

stage_run.VARIANTS['r11exact'] = [EXACT_FN, EXACT_HERE, EXACT_THEIRS]
stage_run.VARIANTS['r11exactjac'] = [EXACT_FN, EXACT_HERE, EXACT_THEIRS, JAC_REAIMED]
stage_run.VARIANTS['r11ownjac'] = [EXACT_FN, EXACT_HERE, EXACT_THEIRS, JAC_OWN, TRACE_EXACT]
stage_run.VARIANTS['r11ownjacnocap'] = [EXACT_FN, EXACT_HERE, EXACT_THEIRS, JAC_OWN, TRACE_EXACT, NOCAP]

ARMS = [('r11d_exact', 'r11exact'), ('r11d_exactjac', 'r11exactjac'),
        ('r11d_ownjac', 'r11ownjac'), ('r11d_ownjacnocap', 'r11ownjacnocap')]
only = sys.argv[1:]
if not (only and only[0] == '--analyse'):
    stage_run.run_arms([(t, v, [], dict(first=360, frames=40, mean_from=360)) for t, v in ARMS if not only or t in only])


def mean_of(t):
    return np.load(os.path.join(stage_run.OUT, t + '.npy'))


def box(a, k=9):
    c = np.cumsum(np.cumsum(np.pad(a, ((k // 2 + 1, k // 2), (k // 2 + 1, k // 2)), mode='edge'), 0), 1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


truth = M.luma(mean_of('r11c_truth'))
floor = (slice(520, 880), slice(0, 1600))
bright = truth[floor] > 80
print('garage parked, mean of frames 360-399, 9x9 low-frequency luma against the truth')
for t in ['r11c_ship', 'r11c_gauss'] + [a for a, _ in ARMS]:
    if not os.path.exists(os.path.join(stage_run.OUT, t + '.npy')):
        continue
    d = box(M.luma(mean_of(t)) - truth)
    f = d[floor]
    print('  %-14s floor mean %+.2f, mean|.| %.2f, bright floor %+.2f, dark floor %+.2f, whole frame %+.2f'
          % (t[5:], f.mean(), np.abs(f).mean(), f[bright].mean(), f[~bright].mean(), d.mean()))
