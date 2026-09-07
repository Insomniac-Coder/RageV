"""Second half of patch_taa_filtered.py (the header half is applied): the
.cpp, the frame graph and the shader, with the .cpp strings taken from the
file's real text (comments included)."""
import os, sys
os.chdir(r'C:\Users\ism19\Code\RageV')
sign = sys.argv[1] if len(sys.argv) > 1 else '-1.0'

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:70]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

patch('RageV/src/RageV/Renderer/PostProcess.cpp', [(
"""									  const Ref<RHITexture>& moments, Format momentsFormat)
	{
		PostParams params;
""",
"""									  const Ref<RHITexture>& moments, Format momentsFormat,
									  Math::Vec2 jitter)
	{
		// The base block, then this frame's jitter (clip units, as the scene
		// block carries it): the resolve filters the current frame around
		// the unjittered pixel centre before blending it.
		struct TemporalParams
		{
			PostParams Base;
			Vec2 Jitter{ 0.0f };
		};
		TemporalParams full;
		full.Jitter = jitter;
		PostParams& params = full.Base;
"""),
("""		Dispatch(cmd, Shader::TaaResolve, outputFormat, current, history,
				 &params, sizeof(params), Sampling::Point, Sampling::Linear,""",
"""		Dispatch(cmd, Shader::TaaResolve, outputFormat, current, history,
				 &full, sizeof(full), Sampling::Point, Sampling::Linear,""")])

patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""					[source, previous, velocityIndex, feedback, hasHistory](RGPassContext& context)
					{
						PostProcess::TemporalResolve(""",
"""					[source, previous, velocityIndex, feedback, hasHistory, jitter](RGPassContext& context)
					{
						PostProcess::TemporalResolve("""),
("""							hasHistory ? context.Color(previous, 1) : nullptr,
							Format::R16G16B16A16_SFLOAT);
					});

				shaded = current;""",
"""							hasHistory ? context.Color(previous, 1) : nullptr,
							Format::R16G16B16A16_SFLOAT, jitter);
					});

				shaded = current;""")])

patch('RageVEditor/assets/shaders/taa_resolve.rvshader', [(
"""	float HasMoments;
	float FlipY;
} u_Params;""",
"""	float HasMoments;
	float FlipY;
	// This frame's jitter in clip units (the scene block's Jitter.xy).
	vec2  Jitter;
} u_Params;

// **The current sample is filtered around the unjittered pixel centre
// before it is blended** (Unreal's filtered current). The raw sample at a
// bright edge is one side or the other depending on where the jitter put
// it, and at a feedback of 0.9 every frame moved the pixel a tenth of the
// way there: parked, bright edges changed 3.3 levels a frame with no
// reflections in the picture and 1.0 with no anti-aliasing at all
// (2026-09-06). A Gaussian over the 3x3, centred where the pixel's own
// centre actually is this frame, gives the coverage instead of the coin.
// The box and the moments keep the raw taps: they describe what the pixel
// can be, and the filter must not narrow that.
const float kFilterSigma = 0.5;
const float kJitterSign = %SIGN%;
""".replace('%SIGN%', sign)),
("""	vec3 centreYCoCg = ToYCoCg(current.rgb);
	vec3 lowest = centreYCoCg;
	vec3 highest = centreYCoCg;
	float alphaLow = current.a;
	float alphaHigh = current.a;
""",
"""	vec3 centreYCoCg = ToYCoCg(current.rgb);
	vec3 lowest = centreYCoCg;
	vec3 highest = centreYCoCg;
	float alphaLow = current.a;
	float alphaHigh = current.a;
	// The jitter in texels; a rendered sample at texel offset o sits at
	// o - jitter on the unjittered grid.
	vec2 jitterTexels = u_Params.Jitter * 0.5 / u_Params.TexelSize;
	jitterTexels.y = u_Params.FlipY > 0.5 ? -jitterTexels.y : jitterTexels.y;
	jitterTexels *= kJitterSign;
	const float twoSigmaSq = 2.0 * kFilterSigma * kFilterSigma;
	float filterWeight = exp(-dot(jitterTexels, jitterTexels) / twoSigmaSq);
	vec3 filtered = current.rgb * filterWeight;
"""),
("""			const vec4 tap = texture(u_Current, uv + u_Params.TexelSize * vec2(x, y));
			vec3 neighbour = ToYCoCg(tap.rgb);
			lowest = min(lowest, neighbour);
			highest = max(highest, neighbour);
			alphaLow = min(alphaLow, tap.a);
			alphaHigh = max(alphaHigh, tap.a);
		}
	}
""",
"""			const vec4 tap = texture(u_Current, uv + u_Params.TexelSize * vec2(x, y));
			vec3 neighbour = ToYCoCg(tap.rgb);
			lowest = min(lowest, neighbour);
			highest = max(highest, neighbour);
			alphaLow = min(alphaLow, tap.a);
			alphaHigh = max(alphaHigh, tap.a);
			const vec2 offset = vec2(x, y) - jitterTexels;
			const float w = exp(-dot(offset, offset) / twoSigmaSq);
			filtered += tap.rgb * w;
			filterWeight += w;
		}
	}
	centreYCoCg = ToYCoCg(filtered / max(filterWeight, 1.0e-6));
""")])
print('taa patched, sign', sign)
