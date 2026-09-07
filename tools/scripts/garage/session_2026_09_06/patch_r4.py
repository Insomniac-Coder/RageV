"""WR-16 R4: confidence-driven memory (anti-lag). A texel whose fresh sample
lands more than a few of its own standard deviations from its history's
mean has a changed signal: its memory is cut, and past four sigmas the
history is dropped (a hard reset). Within two sigmas nothing changes:
Monte Carlo variation is not change."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
def rep(old, new):
    global s; o = old.replace('\n', nl); assert s.count(o) == 1, old[:60]; s = s.replace(o, new.replace('\n', nl))

rep("""const float kTemporalSigma = 2.0;
""",
"""const float kTemporalSigma = 2.0;
// **Confidence (WR-16 R4, 2026-09-06).** How far, in the pixel's own
// standard deviations, the fresh sample may sit from the history's mean
// before the history is doubted: full trust inside kChangeSigmaLow, none
// past kChangeSigmaHigh (the history is dropped and the count restarts).
// The deviation is measured against a floor of kNoiseFloor times the mean,
// so a converged, near-noiseless texel does not fire on a one-level
// flicker, and only once the moments have eight frames behind them.
const float kChangeSigmaLow = 2.0;
const float kChangeSigmaHigh = 4.0;
const float kNoiseFloor = 0.05;
const float kConfidenceAfterFrames = 8.0;
""")

rep("""			const float memory = max(max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack),
									 fewest);
			frames = min(c.past.a + 1.0, memory);
""",
"""			float memory = max(max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack),
							   fewest);
			// The change test. `sigma` above is the pixel's own luma spread
			// over the last sixteen frames; a fresh sample outside it by a
			// margin is not noise, it is the signal having changed (a tube
			// switched off: the floor's bands held for a 64-frame time
			// constant before this -- 44 levels off after sixty frames).
			// Motion and change are different evidence and multiply.
			if (c.past.a >= kConfidenceAfterFrames)
			{
				const float spread = max(sigma, kNoiseFloor * max(c.extra.g, 0.01));
				const float deviation = abs(luma - c.extra.g) / spread;
				const float confidence = 1.0 - smoothstep(kChangeSigmaLow, kChangeSigmaHigh, deviation);
				memory = max(memory * confidence, 1.0);
			}
			frames = min(c.past.a + 1.0, memory);
""")
open(p, 'wb').write(s.encode('utf-8')); print('R4 patched')
