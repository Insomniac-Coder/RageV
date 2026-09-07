"""WR-16 R4, second attempt: confidence-driven memory that cannot fire on a
spike. The change is judged on the 3x3 neighbourhood's mean of the fresh
picture (a spike is one texel, a change is all nine), against the larger
of the pixel's own temporal spread, the neighbourhood's spatial spread and
a noise floor; it must persist three frames (a counter packed above the
`choice` channel); and it cuts the memory to four frames, not one."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
def rep(old, new):
    global s; o = old.replace('\n', nl); assert s.count(o) == 1, old[:60]; s = s.replace(o, new.replace('\n', nl))

rep("""const float kTemporalSigma = 2.0;
""",
"""const float kTemporalSigma = 2.0;
// **Confidence (WR-16 R4, second attempt, 2026-09-06).** The history is
// doubted when the 3x3 neighbourhood's mean of the fresh picture sits more
// than kChangeSigma spreads from the history's mean for kChangePersist
// frames running; then the memory is cut to kResetMemory frames until the
// fresh agrees again. The spread is the largest of the pixel's own
// temporal sigma, the neighbourhood's spatial sigma (a band's edge is not
// a change) and kNoiseFloor of the mean (a converged texel does not fire
// on a level). The first attempt judged the single texel and dropped the
// history outright: one bright tap among 24 reset it, 1.3% of the floor's
// texels jumped 25 levels a frame, and the owner saw "random spotting".
const float kChangeSigma = 3.0;
const float kChangePersist = 3.0;
const float kResetMemory = 4.0;
const float kNoiseFloor = 0.05;
const float kConfidenceAfterFrames = 8.0;
""")

rep("""	float choice = 0.0;
""",
"""	float choice = 0.0;
	// Frames running that the fresh neighbourhood has disagreed with the
	// history (0-3), carried in o_Extra.a above `choice`.
	float persisted = 0.0;
""")

rep("""			const float memory = max(max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack),
									 fewest);
			frames = min(c.past.a + 1.0, memory);
""",
"""			float memory = max(max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack),
							   fewest);
			// The change test (see kChangeSigma). Motion and change are
			// different evidence and multiply.
			if (c.past.a >= kConfidenceAfterFrames)
			{
				const float spread = max(max(sigma, Luma(sd)), kNoiseFloor * max(c.extra.g, 0.01));
				const float deviation = abs(Luma(mean) - c.extra.g) / spread;
				const float before = floor(c.extra.a * 0.5 + 1.0e-3);
				persisted = deviation > kChangeSigma ? min(before + 1.0, kChangePersist) : 0.0;
				if (persisted >= kChangePersist)
					memory = min(memory, kResetMemory);
			}
			frames = min(c.past.a + 1.0, memory);
""")

rep("""	o_Extra = vec4(roughness, momMean, momMeanSq, choice);""",
"""	o_Extra = vec4(roughness, momMean, momMeanSq, choice + 2.0 * persisted);""")
open(p, 'wb').write(s.encode('utf-8')); print('R4b patched')
