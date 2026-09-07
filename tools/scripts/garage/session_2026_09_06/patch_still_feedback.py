"""TemporalStillFeedback: a render setting for the TAA feedback of pixels
that did not move at all (the kStillFeedback constant that was switched
off for the bridge's water on 2026-09-02, now a per-project number). The
garage sets 0.98: parked bright edges went from 3.3 levels a frame to 0.9
with reflections off, the no-anti-aliasing floor being 1.0 (2026-09-06)."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:70]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

patch('RageV/src/RageV/Renderer/RenderSettings.h', [(
"""		float TemporalFeedback = 0.6f;
""",
"""		float TemporalFeedback = 0.6f;

		// The feedback for a pixel that did not move at all -- reprojected to
		// within a few thousandths of a texel of where it was. Such a pixel
		// cannot ghost: its history is this surface, and the only cost of
		// keeping more of it is a slower response to a real change of light.
		// What it buys is the jitter's flicker at edges: at 0.9 every frame
		// moves a bright edge a tenth of the way to whichever side the
		// jitter landed on (3.3 levels a frame on the garage's wall edges,
		// parked, against 1.0 with no anti-aliasing); at 0.98 it is 0.9.
		// Zero means "the same as TemporalFeedback". Per project because no
		// signal the resolve reads separates still steel from far water,
		// whose sparkle changes every frame at a few thousandths of a texel
		// of motion and smears into bands at 0.98 (the bridge, 2026-09-02).
		float TemporalStillFeedback = 0.0f;
""")])

patch('RageV/src/RageV/Scene/ComponentRegistry.cpp', [(
"""				Field<&RenderSettings::TemporalFeedback>("TemporalFeedback",
""",
"""				Field<&RenderSettings::TemporalStillFeedback>("TemporalStillFeedback",
					Named("Still feedback", OnlyWhen(UsesTaa,
						Drag(0.005f, 0.0f, 0.98f,
							"TAA feedback for pixels that did not move at all. Zero "
							"uses Feedback. Raise it (0.98) where the scene is still "
							"geometry: it removes the jitter's edge flicker. Leave it "
							"where far water sparkles: that smears into bands.")))),

				Field<&RenderSettings::TemporalFeedback>("TemporalFeedback",
""")])

patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""				const float feedback = desc.Render.TemporalFeedback;
""",
"""				const float feedback = desc.Render.TemporalFeedback;
				const float stillFeedback = desc.Render.TemporalStillFeedback;
"""),
("""					[source, previous, velocityIndex, feedback, hasHistory, jitter](RGPassContext& context)
""",
"""					[source, previous, velocityIndex, feedback, stillFeedback, hasHistory, jitter](RGPassContext& context)
"""),
("""							Format::R16G16B16A16_SFLOAT, jitter);
					});

				shaded = current;""",
"""							Format::R16G16B16A16_SFLOAT, jitter, stillFeedback);
					});

				shaded = current;"""),
("""								Format::R16G16B16A16_SFLOAT, jitter);""",
"""								Format::R16G16B16A16_SFLOAT, jitter, 0.0f);""")])

patch('RageV/src/RageV/Renderer/PostProcess.h', [(
"""									Math::Vec2 jitter);""",
"""									Math::Vec2 jitter, float stillFeedback);""")])

patch('RageV/src/RageV/Renderer/PostProcess.cpp', [(
"""									  Math::Vec2 jitter)
	{""",
"""									  Math::Vec2 jitter, float stillFeedback)
	{"""),
("""			PostParams Base;
			Vec2 Jitter{ 0.0f };
		};
		TemporalParams full;
		full.Jitter = jitter;
""",
"""			PostParams Base;
			Vec2 Jitter{ 0.0f };
			// The feedback for a pixel that did not move; zero for "the same".
			float StillFeedback = 0.0f;
			float Pad = 0.0f;
		};
		TemporalParams full;
		full.Jitter = jitter;
		full.StillFeedback = Math::Clamp(stillFeedback, 0.0f, 0.98f);
""")])

patch('RageVEditor/assets/shaders/taa_resolve.rvshader', [(
"""	vec2  Jitter;
} u_Params;""",
"""	vec2  Jitter;
	// The feedback for a pixel that did not move at all (RenderSettings::
	// TemporalStillFeedback); zero means Feedback. See kStillWithinTexels.
	float StillFeedback;
	float Pad;
} u_Params;"""),
("""const float kStillFeedback = 0.0;
""",
"""// **A render setting since 2026-09-06** (`u_Params.StillFeedback`,
// RenderSettings::TemporalStillFeedback): the garage has no water and its
// edges flickered at 0.9; the bridge keeps it off.
"""),
("""							   max(u_Params.Feedback, kStillFeedback), stillness);""",
"""							   max(u_Params.Feedback, u_Params.StillFeedback), stillness);""")])

patch('SampleProject/SampleProject.rvproject', [(
"""    TemporalFeedback: 0.9
""",
"""    TemporalFeedback: 0.9
    TemporalStillFeedback: 0.98
""")])
print('still feedback plumbed')
