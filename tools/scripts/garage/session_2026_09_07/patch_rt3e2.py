"""RT-3, part E2: the --gi-signal flag alone.

Split out of patch_rt3e.py, which applied its first five sections and stopped
on this one -- the anchor there had a single space where EngineConfig.h has
two. Never re-run a half-applied script whole.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(H)
if not has(s, 'GiSignal'):
    s = rep(s,
        "\t\tbool  AoSignal = true;\n",
        "\t\tbool  AoSignal = true;\n"
        "\t\t// RT-3: --gi-signal=off puts the traced bounce back on the\n"
        "\t\t// one-frame-late buffer and gi_denoise, which is the reference arm.\n"
        "\t\tbool  GiSignal = true;\n")
    write(H, s)
    print('EngineConfig.h patched')
else:
    print('EngineConfig.h already has GiSignal')

C = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(C)
if not has(s, '"gi-signal"'):
    s = rep(s,
        '\t\tif (key == "ao-signal" || key == "aosignal")\n'
        '\t\t\treturn ParseBool(value, config.AoSignal);\n',
        '\t\tif (key == "ao-signal" || key == "aosignal")\n'
        '\t\t\treturn ParseBool(value, config.AoSignal);\n'
        '\n'
        '\t\t// RT-3: the reference arm for the traced bounce as a signal.\n'
        '\t\tif (key == "gi-signal" || key == "gisignal")\n'
        '\t\t\treturn ParseBool(value, config.GiSignal);\n')
    write(C, s)
    print('EngineConfig.cpp patched')
else:
    print('EngineConfig.cpp already has gi-signal')
