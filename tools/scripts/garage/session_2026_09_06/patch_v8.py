"""v8: the reuse disc is a fifth of the lobe's footprint (parallax blur is
the disc's size over the lobe's, squared: 4%), sized by the 3x3 minimum hit
distance (ReBLUR) so a far-hit texel cannot pull the band beside it; the
weight is the true pdf ratio (this lobe at the re-aimed direction over the
neighbour's draw), capped at 4, times a Gaussian over the disc."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_resolve.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
def rep(old, new):
    global s; o = old.replace('\n', nl); assert s.count(o) == 1, old[:60]; s = s.replace(o, new.replace('\n', nl))
rep("""const float kLobeWidth = 2.8;
const int kMaxReach = 24;
// A neighbour's tail draw (tiny pdf) landing in this texel's core would
// carry an unbounded weight -- the ratio estimator's firefly. Capped
// relative to the centre's own weight, which is 4 VoH / NoH ~ 4.
const float kMaxWeight = 32.0;
""",
"""const float kLobeWidth = 2.8;
// The disc is a fifth of that. A neighbour's sample is the scene seen from
// the neighbour's point; averaging over a disc blurs the picture by the
// disc's size over the lobe's, squared -- a disc the size of the lobe
// (2026-09-06, first two attempts) doubled the bands' edges and rounded
// them into blobs; a fifth costs four percent and still gathers 24
// independent rays. The lobe-ratio weight below cannot help here: on a
// flat floor every neighbour's lobe is this one's, the ratio is one, and
// the estimator is a plain average of whatever the disc holds.
const float kReuseFraction = 0.2;
const int kMaxReach = 24;
// The weight is this lobe's density at the re-aimed direction over the
// neighbour's draw: one between equals, more where the neighbour's lobe is
// the wider (its draw was the less likely here), capped so a tail draw
// cannot become a firefly.
const float kMaxWeight = 4.0;
"""),
rep("""	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	const float footprint = max(fresh.a, 0.0) * kLobeWidth * alpha;
""",
"""	const float eyeDistance = length(P - u_Scene.CameraPosition.xyz);
	// The hit distance that sizes the disc is the 3x3 minimum (ReBLUR): a
	// texel's own ray landed on the tube or the wall behind it by chance,
	// and the wall's wide disc would drag the tube's band across it.
	float hitDistance = max(fresh.a, 0.0);
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			const float d = texelFetch(u_Fresh, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).a;
			if (d >= 0.0)
				hitDistance = min(hitDistance, d);
		}
	const float footprint = hitDistance * kLobeWidth * alpha * kReuseFraction;
"""),
rep("""	for (int i = -1; i < kTaps; ++i)
	{
		ivec2 at = texel;
		if (i >= 0)
		{
			const float r = sqrt((float(i) + 0.5) / float(kTaps));
""",
"""	for (int i = -1; i < kTaps; ++i)
	{
		ivec2 at = texel;
		float r = 0.0;
		if (i >= 0)
		{
			r = sqrt((float(i) + 0.5) / float(kTaps));
"""),
rep("""		const float w = min(DistributionGGX(N, H, roughness) / max(h.z, 1.0e-4), kMaxWeight);
""",
"""		const float pdfHere = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)
							/ (4.0 * max(dot(V, H), 1.0e-4));
		const float w = min(pdfHere / max(h.z, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);
""")
open(p, 'wb').write(s.encode('utf-8')); print('v8 patched')
