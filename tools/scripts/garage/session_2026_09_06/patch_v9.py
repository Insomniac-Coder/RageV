"""v9: both densities at the same re-aimed direction (this lobe's, and the
neighbour's as if it had drawn it), so equal lobes weigh exactly one."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_resolve.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
old = """		const float pdfHere = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)
							/ (4.0 * max(dot(V, H), 1.0e-4));
		const float w = min(pdfHere / max(h.z, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);
""".replace('\n', nl)
new = """		// Both densities at the SAME direction, the re-aimed one: this lobe's,
		// and the neighbour's as if it had drawn it (its own normal, roughness
		// and view). Equal lobes give exactly one whatever the parallax. The
		// draw's own pdf at its own direction (h.z, v8) put the parallax shift
		// into the ratio as noise and a dark bias; it is kept in the
		// attachment for the day the estimator needs it.
		const vec3 Vn = normalize(u_Scene.CameraPosition.xyz - Pn);
		const vec3 Hn = normalize(L + Vn);
		const float pdfHere = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)
							/ (4.0 * max(dot(V, H), 1.0e-4));
		const float pdfTheirs = DistributionGGX(Nn, Hn, rn) * max(dot(Nn, Hn), 1.0e-4)
							  / (4.0 * max(dot(Vn, Hn), 1.0e-4));
		const float w = min(pdfHere / max(pdfTheirs, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);
""".replace('\n', nl)
assert s.count(old) == 1; open(p, 'wb').write(s.replace(old, new).encode('utf-8')); print('v9 patched')
