# -*- coding: utf-8 -*-
"""RT-8 job 3: the switch that lets the two averages be compared."""
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
(
"""		bool  WaterLampAccumulate = true;""",
"""		bool  WaterLampAccumulate = true;
		// **--water-contract=on|off (RT-8 job 3): whose averaging the sea uses.**
		//
		// On, the sea's lamp light goes through the signal contract like every
		// other signal -- one accumulate, one blur, one set of history tests,
		// reading the sea's own position lane instead of the depth buffer that
		// describes the seabed under it. Off, it keeps the private accumulate
		// it has had since WR-16 S4c.
		//
		// **This existed as an argument before it existed as a flag.** The
		// private pass was defended on the claim that the contract's geometric
		// gate cannot hold a sea -- a wave slides the point seen at a pixel by
		// tens of metres, so a test asking whether the surface is still the
		// distance it was would refuse the water constantly. Measured on the
		// bridge (2026-09-08) that is **false**: the contract's gate keeps 98.7%
		// of sea pixels at the pier, 98.6% at the glitter camera and 92.9% from
		// the deck, and the plane test alone refuses 1.1%. The argument was
		// true of the sea *before* the same session taught it to report its own
		// motion, and nobody re-took it afterwards.
		bool  WaterContract = true;""",
    'WaterContract'),
])

patch(r'RageV/src/RageV/Core/EngineConfig.cpp', [
(
"""		if (key == "water-lamp-accumulate" || key == "waterlampaccumulate")
			return ParseBool(value, config.WaterLampAccumulate);""",
"""		if (key == "water-lamp-accumulate" || key == "waterlampaccumulate")
			return ParseBool(value, config.WaterLampAccumulate);
		// RT-8 job 3: the sea's averaging on the contract, or its own.
		if (key == "water-contract" || key == "watercontract")
			return ParseBool(value, config.WaterContract);""",
    'water-contract parse'),
])
