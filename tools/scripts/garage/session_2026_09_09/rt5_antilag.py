# -*- coding: utf-8 -*-
"""RT-5: the evidence-driven anti-lag, and with it RT-16's fix.

**Why the two earlier attempts failed.** R4 tried to read a change out of a
noisy fresh sample against its own history, which is guessing: on a still scene
the sampling noise alone looks like a change, so it fired where nothing had
happened. The series' own note calls that "the R4 spotting".

**What the G-buffer changed.** Reaching the line below means every surface test
has already passed -- same object, same plane, same facing, same roughness, same
material. So the accumulator no longer has to ask *did the surface change*; it
knows it did not. The only question left is whether the **light on it** changed,
which is a far smaller one, and the pixel's own moments already say how much
this pixel's estimate wobbles when nothing is happening.

So: **evidence against the pixel's own noise, gated on nothing having moved.**
The history is compared with what the fresh neighbourhood is reporting now; if
they differ by more than N times the pixel's own standard deviation -- with a
floor, so a fully converged pixel does not trip on rounding -- the light has
changed and the memory is cut to its shortest. The fresh rays take over within
a few frames and the average rebuilds from there.

**Measured on four metrics at once**, because the note is explicit that the
bound cannot be relaxed without reopening lag on a light switch: the Switcher's
settle (RT-16's number), parked drift, the dolly, and the edge shake.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


patch(r'RageV/src/RageV/Core/EngineConfig.h', [
('\t\tfloat TaaStillFeedbackOverride = -1.0f;',
 '\t\t// **--anti-lag=N (RT-5): how many of a pixel\'s own standard deviations\n'
 '\t\t// the history may sit from what its neighbours report now before the\n'
 '\t\t// memory is cut.** Zero is off, which is the behaviour before RT-5.\n'
 '\t\t//\n'
 '\t\t// The test only runs where every surface test has already passed and\n'
 '\t\t// the pixel did not move, so it is not asking whether the surface\n'
 '\t\t// changed -- it knows it did not. It asks whether the *light* did, and\n'
 '\t\t// the moments the accumulator already keeps say how much this pixel\n'
 '\t\t// wobbles when nothing is happening. That is what the two earlier\n'
 '\t\t// attempts (R4) lacked: they read a change out of a noisy sample with\n'
 '\t\t// no idea how noisy it was, and fired on stills.\n'
 '\t\tfloat SignalAntiLag = 0.0f;\n'
 '\t\t// The floor under that noise estimate, in levels, so a converged pixel\n'
 '\t\t// whose sigma has gone to nothing does not trip on rounding.\n'
 '\t\tfloat SignalAntiLagFloor = 0.02f;\n'
 '\t\tfloat TaaStillFeedbackOverride = -1.0f;', 'flags'),
])

patch(r'RageV/src/RageV/Core/EngineConfig.cpp', [
('\t\tif (key == "taa-still-feedback" || key == "taastillfeedback")',
 '\t\tif (key == "anti-lag" || key == "antilag")\n'
 '\t\t{\n'
 '\t\t\ttry\n'
 '\t\t\t{\n'
 '\t\t\t\tconfig.SignalAntiLag = Math::Clamp(std::stof(value), 0.0f, 64.0f);\n'
 '\t\t\t}\n'
 '\t\t\tcatch (const std::exception&)\n'
 '\t\t\t{\n'
 '\t\t\t\tRV_CORE_WARN("anti-lag expects a number from 0 to 64; got \'{0}\'", value);\n'
 '\t\t\t\treturn false;\n'
 '\t\t\t}\n'
 '\t\t\treturn true;\n'
 '\t\t}\n'
 '\t\tif (key == "anti-lag-floor" || key == "antilagfloor")\n'
 '\t\t{\n'
 '\t\t\ttry\n'
 '\t\t\t{\n'
 '\t\t\t\tconfig.SignalAntiLagFloor = Math::Clamp(std::stof(value), 0.0f, 16.0f);\n'
 '\t\t\t}\n'
 '\t\t\tcatch (const std::exception&)\n'
 '\t\t\t{\n'
 '\t\t\t\tRV_CORE_WARN("anti-lag-floor expects a number from 0 to 16; got \'{0}\'", value);\n'
 '\t\t\t\treturn false;\n'
 '\t\t\t}\n'
 '\t\t\treturn true;\n'
 '\t\t}\n'
 '\t\tif (key == "taa-still-feedback" || key == "taastillfeedback")', 'parse'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
('\t\t\tVec4 PreviousEye{ 0.0f, 0.0f, 0.0f, 0.0f };\n\t\t};',
 '\t\t\tVec4 PreviousEye{ 0.0f, 0.0f, 0.0f, 0.0f };\n'
 '\t\t\t// **RT-5: the anti-lag.** x how many of the pixel\'s own standard\n'
 '\t\t\t// deviations the history may sit from what its neighbours report\n'
 '\t\t\t// now, zero being off; y the floor under that noise estimate.\n'
 '\t\t\t// A lane of its own rather than a bit packed into another: the\n'
 '\t\t\t// packing is where a push constant stops being readable.\n'
 '\t\t\tVec4 AntiLag{ 0.0f, 0.02f, 0.0f, 0.0f };\n\t\t};', 'push struct'),
('\t\tpush.Blur = { signal.YoungRadius, signal.BlurFrames, signal.YoungOverreach, signal.MaxRadius };\n'
 '\t\tmotion.ViewProjection = s_Data->Scene.ViewProjection;',
 '\t\tpush.Blur = { signal.YoungRadius, signal.BlurFrames, signal.YoungOverreach, signal.MaxRadius };\n'
 '\t\t// RT-5: the anti-lag, from the engine rather than the signal -- it is a\n'
 '\t\t// property of how noisy a pixel is, which is the same question for all\n'
 '\t\t// of them.\n'
 '\t\tpush.AntiLag = { EngineConfig::Get().SignalAntiLag,\n'
 '\t\t\t\t\t\t EngineConfig::Get().SignalAntiLagFloor, 0.0f, 0.0f };\n'
 '\t\tmotion.ViewProjection = s_Data->Scene.ViewProjection;', 'accumulate push'),
])

patch(r'RageVEditor/assets/shaders/reflection_accumulate.rvshader', [
('\tvec4 Tuning;\n\tvec4 Blur;',
 '\tvec4 Tuning;\n\tvec4 Blur;', 'push block (unchanged)'),
])
