# -*- coding: utf-8 -*-
"""RT-8 job 1, done after reading what it replaces.

`water_shade` does three things the shared shade pass did not, and missing them
is why a block-rate choice flattened the sea:

  1. It **re-scores its own picks at the shading pixel**. The choice was made
     for the block's centre; the weight it carries is only right there, so the
     target function is evaluated again here before anything is used.
  2. It **borrows three neighbours' picks** off a ring turned by a per-pixel
     angle, validated by depth and normal -- not by a distance in metres,
     because the sea is seen nearly edge on and twelve pixels toward the
     horizon is hundreds of metres of water.
  3. It **merges them as reservoirs**, re-scoring each candidate here, keeping
     one at random in proportion to its share, and carrying the sample counts
     so the denominator matches the numerator. The cap goes on each input,
     never on the sum -- capping the sum reads as a brightening.

So the choose pass must write what a merge needs: the lamp, **its sample
count**, and its weight -- the sea's own packing, index in the low sixteen bits
and the count in the high sixteen. It was writing one number per lamp, which is
enough to shade and not enough to merge.

**One difference from the sea's, and it is deliberate.** `water_shade` fetches
a neighbour's *reservoir* at a full-resolution offset while validating that
neighbour's *surface* at the same offset -- but its reservoir buffer is at the
block resolution, so with a block of two it validates one patch of water and
takes the reservoir of another, twice as far away. Here the ring is walked in
choice texels and the surface is validated at the point that choice was made
for, so the two describe the same water.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageVEditor/assets/shaders/direct_trace.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'DirectReservoir' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# --- the reservoir, packed as the sea packs it ---------------------------
once('''const uint kMaxRays = 8u;''',
'''const uint kMaxRays = 8u;

// **A choice, as it survives a pass boundary**: which lamp, how many samples
// stand behind it, and the weight its term is multiplied by. The sea's own
// packing -- index in the low sixteen bits, the count in the high sixteen, the
// weight a whole float beside it. A weight through a half would quantise the
// one number the estimate divides by.
struct DirectReservoir
{
	uint  Index;
	uint  M;
	float W;
};

DirectReservoir UnpackDirect(uvec4 indices, uvec4 weights, int slot)
{
	DirectReservoir r;
	r.Index = indices[slot] & 0xFFFFu;
	r.M = indices[slot] >> 16;
	r.W = uintBitsToFloat(weights[slot]);
	return r;
}

// A draw in [0, 1), independent per pixel, per reservoir and per neighbour --
// a weighted reservoir is unbiased only when each draw is independent of the
// last, which is the trap WR-16 S1 paid for with a sea that came out darker
// the fewer rays it had.
float DirectRandom(uint seed)
{
	return float(BudgetHash(seed) >> 8u) * (1.0 / 16777216.0);
}''',
     'reservoir helpers')

# --- the choose pass writes a count beside the index ---------------------
once('''		uvec4 outIndex = uvec4(0u);
		uvec4 outWorth = uvec4(0u);
		for (int r = 0; r < min(K, 4); ++r)
		{
			outIndex[r] = index[r];
			outWorth[r] = floatBitsToUint(weight[r] > 0.0 && total > 0.0
										  ? total / weight[r] : 0.0);
		}''',
'''		// **The count matters as much as the weight.** A merge divides by the
		// samples behind each input; without it a borrowed reservoir cannot be
		// combined with this one, which is the whole of what the shade pass
		// needs and the whole of what an earlier version of this left out.
		// One sweep stands behind a fresh choice, so the count is one.
		uvec4 outIndex = uvec4(0u);
		uvec4 outWorth = uvec4(0u);
		for (int r = 0; r < min(K, 4); ++r)
		{
			const bool live = weight[r] > 0.0 && total > 0.0;
			outIndex[r] = (index[r] & 0xFFFFu) | (live ? (1u << 16) : 0u);
			outWorth[r] = floatBitsToUint(live ? total / weight[r] : 0.0);
		}''',
     'choose output')

# --- the shade pass: re-score, borrow, merge, then shade -----------------
once('''		const uvec4 chosen = texelFetch(u_ChoiceIn, chosenAt, 0);
		const uvec4 worthBits = texelFetch(u_WorthIn, chosenAt, 0);
		float visible[4];
		uint  chosenIndex[4];
		float worth[4];
		for (int r = 0; r < 4; ++r)
		{
			chosenIndex[r] = chosen[r];
			worth[r] = uintBitsToFloat(worthBits[r]);
			visible[r] = 0.0;
		}
		const int chosenCount = min(K, 4);''',
'''		const uvec4 chosen = texelFetch(u_ChoiceIn, chosenAt, 0);
		const uvec4 worthBits = texelFetch(u_WorthIn, chosenAt, 0);
		float visible[4];
		uint  chosenIndex[4];
		uint  chosenM[4];
		float worth[4];
		float chosenTarget[4];
		const int chosenCount = min(K, 4);
		// **Re-scored here, because the choice was not made here.** With the
		// picking on a block, this pixel is one of four the choice serves and
		// the weight it carries is only right at the one it was made for. A
		// lamp that reaches the block's centre and not this pixel is dropped
		// rather than shaded at the wrong strength.
		for (int r = 0; r < 4; ++r)
		{
			const DirectReservoir mine = UnpackDirect(chosen, worthBits, r);
			chosenIndex[r] = mine.Index;
			chosenM[r] = mine.M;
			worth[r] = mine.W;
			chosenTarget[r] = 0.0;
			visible[r] = 0.0;
			if (mine.M > 0u && mine.W > 0.0)
			{
				chosenTarget[r] = ScoreDirect(p, mine.Index);
				if (chosenTarget[r] <= 0.0)
				{
					chosenM[r] = 0u;
					worth[r] = 0.0;
				}
			}
		}

		// **The neighbours' picks, re-scored here and merged in.** This is
		// what a block-rate choice needs to keep its detail: four pixels
		// sharing one pick see the same lamps, and borrowing from a ring gives
		// each of them a different set to weigh. Walked in *choice* texels and
		// validated at the point each choice was made for, so the surface
		// tested and the reservoir taken describe the same water.
		const int seaNeighbours = int(u_Direct.CameraRow1.w + 0.5);
		if (seaNeighbours > 0)
		{
			const int choiceBlock = max(int(u_Direct.CameraRow0.w + 0.5), 1);
			const uvec2 px = uvec2(gl_FragCoord.xy);
			const uint salt = u_Direct.Animated > 0.5
							? uint(mod(u_Direct.Frame, 1024.0)) * 0xC2B2AE35u : 0u;
			const uint mCap = 8u;
			const float turn = DirectRandom(px.x ^ (px.y << 16u) ^ salt ^ 0x9E3779B9u)
							 * 6.28318530718;
			const vec3 forward = normalize(vec3(-u_Direct.CameraRow0.z,
												-u_Direct.CameraRow1.z,
												-u_Direct.CameraRow2.z));
			const float myDepth = dot(p.P - u_Direct.CameraPosition.xyz, forward);
			for (int n = 0; n < seaNeighbours; ++n)
			{
				const float angle = turn + float(n) * 2.09439510239;
				const ivec2 theirChoice = chosenAt
					+ ivec2(round(vec2(cos(angle), sin(angle)) * 6.0));
				if (any(lessThan(theirChoice, ivec2(0)))
					|| any(greaterThanEqual(theirChoice, chooseSize)))
				{
					continue;
				}
				// The layer point that choice was made for -- the choose pass's
				// own rule, so the two agree about which water this is.
				const ivec2 theirLayer = clamp(theirChoice * choiceBlock + choiceBlock / 2,
											   ivec2(0), layerSize - 1);
				const vec4 theirPos = texelFetch(u_DepthIn, theirLayer, 0);
				if (theirPos.w <= 0.5)
					continue;
				// **Depth and normal, never a distance in metres.** The sea is
				// seen nearly edge on, so a world-space slack rejects every
				// neighbour a grazing view has. Within a tenth of the depth,
				// and normals within a wide angle -- wide because a wave's
				// normal turns fast and two neighbours on one swell are still
				// the same swell.
				const float theirDepth = dot(theirPos.xyz - u_Direct.CameraPosition.xyz,
											 forward);
				if (abs(theirDepth - myDepth) > 0.1 * max(myDepth, 1.0))
					continue;
				const vec2 theirXZ = texelFetch(u_SurfaceIn, theirLayer, 0).xy;
				const vec3 theirN = vec3(theirXZ.x,
										 sqrt(max(1.0 - dot(theirXZ, theirXZ), 0.0)),
										 theirXZ.y);
				if (dot(theirN, p.N) < 0.85)
					continue;

				const uvec4 theirIndex = texelFetch(u_ChoiceIn, theirChoice, 0);
				const uvec4 theirWorth = texelFetch(u_WorthIn, theirChoice, 0);
				for (int r = 0; r < chosenCount; ++r)
				{
					const DirectReservoir theirs = UnpackDirect(theirIndex, theirWorth, r);
					if (theirs.M == 0u || theirs.W <= 0.0)
						continue;
					const float here = ScoreDirect(p, theirs.Index);
					if (here <= 0.0)
						continue;
					const float mineSum = chosenTarget[r] > 0.0
										? chosenTarget[r] * worth[r] * float(chosenM[r]) : 0.0;
					const float theirSum = here * theirs.W * float(min(theirs.M, mCap));
					const float sum = mineSum + theirSum;
					if (sum <= 0.0)
						continue;
					uint keptIndex = chosenIndex[r];
					float keptTarget = chosenTarget[r];
					if (DirectRandom(px.x ^ (px.y << 16u) ^ (uint(r) << 24u)
									 ^ (uint(n) << 8u) ^ salt ^ 0x85EBCA6Bu)
						< theirSum / sum)
					{
						keptIndex = theirs.Index;
						keptTarget = here;
					}
					// **The cap goes on each input, never on the sum.** Each
					// neighbour brings min(M, cap) samples into the numerator,
					// so the denominator has to carry the same ones or the
					// estimate is inflated by whatever was clamped away.
					const uint merged = chosenM[r] + min(theirs.M, mCap);
					chosenIndex[r] = keptIndex;
					chosenM[r] = merged;
					chosenTarget[r] = keptTarget;
					worth[r] = keptTarget > 0.0
							 ? sum / (float(merged) * keptTarget) : 0.0;
				}
			}
		}''',
     'shade re-score and merge')

once('''			if (worth[r] <= 0.0)
				continue;''',
'''			if (worth[r] <= 0.0 || chosenM[r] == 0u)
				continue;''',
     'shade guard')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('the shade pass re-scores and borrows, as the sea does')
