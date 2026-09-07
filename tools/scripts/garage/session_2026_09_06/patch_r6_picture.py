"""A debug view of the accumulated reflection picture itself
(`--debug-view=reflection-picture`): the accumulator's radiance over a
scale, written straight to the output, so a burst can measure the
reflection layer alone (WR-16 R4's change test; part of R6)."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:60]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

patch('RageV/src/RageV/Core/EngineConfig.h', [(
"""								   GiImportance, Reflection, ReflectionImage,
								   ReflectionChoice };""",
"""								   GiImportance, Reflection, ReflectionImage,
								   ReflectionChoice, ReflectionPicture };""")])

patch('RageV/src/RageV/Core/EngineConfig.cpp', [(
"""			else if (lowered == "reflection-choice" || lowered == "reflectionchoice")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionChoice;
""",
"""			else if (lowered == "reflection-choice" || lowered == "reflectionchoice")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionChoice;
			// The accumulated reflection picture itself, radiance over four,
			// straight to the output: the reflection layer alone, for tests
			// that need it apart from everything TAA and GI do to the frame.
			else if (lowered == "reflection-picture" || lowered == "reflectionpicture")
				config.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;
""")])

# The frame graph: the view's scale, and that it reads the reflection history.
s = open('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
m = re.search(r'const int mode = \(int\)view - 1;|mode = \(int\)view - 1|\(int\)config\.DebugView - 1', s)
assert m, 'debug mode numbering not found; check how `mode` is derived before trusting mode 8'
patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""							  : view == EngineConfig::DebugViewMode::ReflectionImage ? 20.0f
									: 1.0f;
			const bool reflectionView = view == EngineConfig::DebugViewMode::Reflection
									 || view == EngineConfig::DebugViewMode::ReflectionImage
									 || view == EngineConfig::DebugViewMode::ReflectionChoice;""",
"""							  : view == EngineConfig::DebugViewMode::ReflectionImage ? 20.0f
							  : view == EngineConfig::DebugViewMode::ReflectionPicture ? 4.0f
									: 1.0f;
			const bool reflectionView = view == EngineConfig::DebugViewMode::Reflection
									 || view == EngineConfig::DebugViewMode::ReflectionImage
									 || view == EngineConfig::DebugViewMode::ReflectionChoice
									 || view == EngineConfig::DebugViewMode::ReflectionPicture;""")])

patch('RageVEditor/assets/shaders/debug_view.rvshader', [(
"""	else if (mode >= 5)
	{
		known = u_Params.AuxValid > 0.5;
		value = texture(u_Aux, uv).a;
	}
""",
"""	else if (mode == 8)
	{
		// The accumulated reflection picture, radiance over the scale, as
		// it is: no ramp, no frame under it.
		known = u_Params.AuxValid > 0.5;
		o_Color = vec4(known ? texture(u_Aux, uv).rgb / max(u_Params.Scale, 1.0e-6) : vec3(0.0), 1.0);
		return;
	}
	else if (mode >= 5)
	{
		known = u_Params.AuxValid > 0.5;
		value = texture(u_Aux, uv).a;
	}
""")])
print('picture view patched; mode numbering line:', m.group(0))
