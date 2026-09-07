# -*- coding: utf-8 -*-
"""RT-12, stage 2: the new views exist as names, and the log ramp.

Eleven additions. Every one reads a lane the reconstruction contract already
writes, so none of them costs a byte of bandwidth -- the contract gives all
four signals the same three attachments, and until now only the reflection's
were ever looked at:

    attachment 0   the picture, and `frames` in alpha  -> *-history
    attachment 1   reflector normal, plane, image distance
    attachment 2   roughness, the two luminance moments, refusal+choice
    attachment 3   the image's motion (RT-6.1; reflection only)

`--debug-view-log` puts any ramp on a logarithmic scale, which is RT-12's own
filed complaint: the direct-light view saturates at any linear scale, and RT-2
recorded the occlusion view "reads near-white on a linear ramp".
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

TAB = '\t' * 7 + '   '   # the enum body's own indent, taken from the file

H = 'RageV/src/RageV/Core/EngineConfig.h'
s = read(H)
if not has(s, 'TaaRefusal'):
    body = [
        "GiLight, GiRefusal,",
        "// **RT-12.** The temporal resolve's own refusal, which did not",
        "// exist before: a TAA ghost could be seen and never traced to the",
        "// clause that let it through.",
        "TaaRefusal,",
        "// The frames standing behind each texel, per signal -- the",
        "// reflection has had one since T4 and the other three never did,",
        "// though they write the very same lane.",
        "DirectHistory, GiHistory, AoHistory,",
        "// What each signal's estimate has been doing: sqrt(E[x^2] - E[x]^2)",
        "// from the two moments the contract keeps. The temporal variance of",
        "// the specification's §11, and the number every bound here is",
        "// floored at.",
        "ReflectionSigma, DirectSigma, GiSigma, AoSigma,",
        "// The occlusion's refusals, never exposed until now.",
        "AoRefusal,",
        "// The reflector's stored normal, and the virtual image's motion",
        "// (RT-6.1): what the direction test reads, and what a swinging",
        "// reflection does on screen.",
        "ReflectionNormal, ReflectionMotion };",
    ]
    s = rep(s, "GiLight, GiRefusal };\n",
            "\n".join([body[0]] + [TAB + line for line in body[1:]]) + "\n")
    s = rep(s,
        "\t\tfloat DebugViewMix = 0.2f;\n",
        "\t\tfloat DebugViewMix = 0.2f;\n"
        "\n"
        "\t\t// **--debug-view-log**: put the ramp on a logarithmic scale.\n"
        "\t\t//\n"
        "\t\t// RT-12 filed this as part of the item, and two records had already\n"
        "\t\t// tripped over it: the direct light saturates at any linear scale,\n"
        "\t\t// and RT-2 recorded the occlusion view reading \"near-white on a\n"
        "\t\t// linear ramp\". A frame count, a ray count and a radiance are all\n"
        "\t\t// quantities whose interesting range is the bottom decade.\n"
        "\t\tbool  DebugViewLog = false;\n")
    write(H, s)
    print('EngineConfig.h: eleven views and the log ramp')
else:
    print('EngineConfig.h already done')

C = 'RageV/src/RageV/Core/EngineConfig.cpp'
s = read(C)
if not has(s, '"taa-refusal"'):
    def view(names, enum, comment=None):
        out = ''
        if comment:
            out += ''.join("\t\t\t// %s\n" % line for line in comment)
        cond = ' || '.join('lowered == "%s"' % n for n in names)
        out += "\t\t\telse if (%s)\n\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::%s;\n" % (cond, enum)
        return out

    added = (
        view(["taa-refusal", "taarefusal"], "TaaRefusal", [
            "**RT-12.** The temporal resolve's refusal, in the reflection",
            "accumulator's encoding: the clause in the integer part (0 kept,",
            "1 off screen, 2 no history, 3 sky crossing, 4 object id, 5 depth,",
            "6 normal), and a half added where the nine-tap search found",
            "nothing either -- so a recovered pixel and a disoccluded one are",
            "half a band apart rather than indistinguishable."])
        + view(["direct-history", "directhistory"], "DirectHistory", [
            "The frames behind each texel, per signal. Black is a history just",
            "refused; white is the signal's full memory -- so this doubles as",
            "the confidence *as applied*, because a shortened memory is",
            "precisely what RT-6.3's direction test and RT-6.4's match",
            "confidence do to a pixel."])
        + view(["gi-history", "gihistory"], "GiHistory")
        + view(["ao-history", "aohistory"], "AoHistory")
        + view(["reflection-sigma", "reflectionsigma"], "ReflectionSigma", [
            "The pixel's own temporal spread, per signal."])
        + view(["direct-sigma", "directsigma"], "DirectSigma")
        + view(["gi-sigma", "gisigma"], "GiSigma")
        + view(["ao-sigma", "aosigma"], "AoSigma")
        + view(["ao-refusal", "aorefusal"], "AoRefusal")
        + view(["reflection-normal", "reflectionnormal"], "ReflectionNormal", [
            "The reflector's stored normal as RGB, and the virtual image's",
            "motion in texels: what the direction test reads, and what a",
            "swinging reflection does on screen."])
        + view(["reflection-motion", "reflectionmotion"], "ReflectionMotion"))

    s = rep(s,
        "\t\t\telse if (lowered == \"reflection-picture\" || lowered == \"reflectionpicture\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;\n",
        "\t\t\telse if (lowered == \"reflection-picture\" || lowered == \"reflectionpicture\")\n"
        "\t\t\t\tconfig.DebugView = EngineConfig::DebugViewMode::ReflectionPicture;\n"
        + added)
    write(C, s)
    print('EngineConfig.cpp: the names parse')
else:
    print('EngineConfig.cpp already done')

s = read(C)
if not has(s, '"debug-view-log"'):
    s = rep(s,
        "\t\tif (key == \"debug-view\" || key == \"debugview\")\n",
        "\t\tif (key == \"debug-view-log\" || key == \"debugviewlog\")\n"
        "\t\t\treturn ParseBool(value, config.DebugViewLog);\n"
        "\n"
        "\t\tif (key == \"debug-view\" || key == \"debugview\")\n")
    write(C, s)
    print('EngineConfig.cpp: --debug-view-log parses')
else:
    print('--debug-view-log already there')
