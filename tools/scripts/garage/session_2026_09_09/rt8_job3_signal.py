# -*- coding: utf-8 -*-
"""RT-8 job 3: the sea's lamp light as a signal, with the tuning it earned."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageV/src/RageV/Renderer/Renderer3D.cpp'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'WaterLampSignal' in s:
    sys.exit('already patched')

old = '''	void Renderer3D::AccumulateSignal(const SignalParams& signal,'''
new = '''	// **RT-8: the sea's lamp light, on the contract instead of its own copy.**
	//
	// The same shape as the direct light -- a diffuse-kind pair, the scattered
	// half and the glinting half -- because it *is* the direct light, of the
	// lamps the sea's own choose pass drew. What is new is the layer: the
	// contract reads the sea's position rather than a depth buffer that
	// describes the seabed under it (PositionLane).
	//
	// **The two memories are the pair's two, and the numbers are the ones the
	// sea's own accumulate was measured to want** (2026-09-08): sixteen frames
	// for the glint, against the sixty-four the scattered half keeps. A wave
	// carries glitter that is not attached to the water, so a long memory there
	// smears the sparkle into a haze; the sweep at the pier read 1.432 at two
	// frames, 1.019 at eight, 0.912 at sixteen and 0.829 at sixty-four, with
	// contrast rising the whole way, and sixteen is where the curve flattens.
	Renderer3D::SignalParams Renderer3D::WaterLampSignal()
	{
		SignalParams signal = DirectSignal();
		signal.Slot = 4;
		// The sea knows where it is; the depth buffer under it does not.
		signal.PositionLane = true;
		// The glint's memory, in the pair's own slot. EngineConfig keeps the
		// dial the sweep moved, so a run can still ask for another number.
		signal.PairMemory = (float)Math::Max(EngineConfig::Get().WaterLampMemoryGlint, 1);
		signal.Memory = (float)Math::Max(EngineConfig::Get().WaterLampMemoryScatter, 1);
		// **No young-history blur, for the sea's own reason and not the direct
		// light's.** The glitter track *is* high-frequency detail: a spatial
		// blur across it is the one thing the anisotropic lobe exists to avoid.
		signal.YoungRadius = 0.0f;
		return signal;
	}

	void Renderer3D::AccumulateSignal(const SignalParams& signal,'''
if s.count(old) != 1:
    sys.exit('anchor matched %d' % s.count(old))
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(
    s.replace(old, new, 1))
print('WaterLampSignal added')
