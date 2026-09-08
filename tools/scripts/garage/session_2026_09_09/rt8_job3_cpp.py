# -*- coding: utf-8 -*-
"""RT-8 job 3, the engine half: a signal may bring its own layer.

Three small things, and then the sea is just another signal.

  * `SignalParams::PositionLane` -- the flag the two shaders read from Probe.z.
  * `SignalGuidance` gains lane indices, so a signal can point at three lanes
    of one target instead of needing a target laid out for it.
  * A sixth signal slot, because the sea's lamp light is one.
"""
import io, sys

CRLF, LF = CRLF_LF = (chr(13) + chr(10), chr(10))


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


patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
(
"""			float PairMemory = 0.0f;       // Pair: the twin's own memory in frames; zero shares the first payload's
		};""",
"""			float PairMemory = 0.0f;       // Pair: the twin's own memory in frames; zero shares the first payload's
			// **RT-8: what is bound at the depth slot.** False, and it is clip
			// depth and the contract rebuilds the world point from it -- every
			// signal before the sea. True, and it is the layer's own position
			// in xyz with a mask in w, which is what the water surface writes:
			// exact rather than reconstructed, and the only shape that works
			// for a layer the depth buffer does not describe.
			bool PositionLane = false;
		};""",
    'SignalParams::PositionLane'),
(
"""		// RT-3: the traced bounce's tuning -- a diffuse-kind RGB signal in slot 3.
		static SignalParams GiSignal();""",
"""		// RT-3: the traced bounce's tuning -- a diffuse-kind RGB signal in slot 3.
		static SignalParams GiSignal();
		// **RT-8: the sea's lamp light** -- the same shape as the direct light
		// (a diffuse-kind pair, the scattered half and the glinting half) on
		// the sea's own layer, in slot 4. The two memories the water's own
		// accumulate kept apart are the pair's two, and the reason is the same:
		// what enters the water is broad and slow, what glints off it is
		// exactly what a turning wave changes.
		static SignalParams WaterLampSignal();""",
    'WaterLampSignal declaration'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""				Ref<RHIResourceSet> SignalAccumulateInputs[4];""",
"""				// RT-8: six, not four -- the sea's lamp light is slot 4 and
				// slot 5 is free. The clamp in AccumulateSignal must agree.
				Ref<RHIResourceSet> SignalAccumulateInputs[6];""",
    'accumulate slots'),
(
"""				Ref<RHIResourceSet> SignalBlurInputs[4];""",
"""				Ref<RHIResourceSet> SignalBlurInputs[6];""",
    'blur slots'),
(
"""		const int index = Math::Clamp(signal.Slot, 0, 3);
		Ref<RHIResourceSet>& inputs = slot.SignalAccumulateInputs[index];""",
"""		const int index = Math::Clamp(signal.Slot, 0, 5);
		Ref<RHIResourceSet>& inputs = slot.SignalAccumulateInputs[index];""",
    'accumulate clamp'),
(
"""		push.Probe.x = signal.BoundWidth;
		push.Probe.y = signal.Slack;
		push.Tuning = { signal.SmearTexels, signal.MovingMemory, signal.SilhouetteMemory, signal.PairMemory };""",
"""		push.Probe.x = signal.BoundWidth;
		push.Probe.y = signal.Slack;
		// RT-8: one where the depth slot carries the layer's own position.
		push.Probe.z = signal.PositionLane ? 1.0f : 0.0f;
		push.Tuning = { signal.SmearTexels, signal.MovingMemory, signal.SilhouetteMemory, signal.PairMemory };""",
    'accumulate probe'),
])

# The blur's clamp and its copy of the flag.
src = io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
head, _, tail = s.partition('void Renderer3D::BlurSignal(')
if not tail:
    sys.exit('BlurSignal not found')
body, sep2, rest = tail.partition('\n\tRenderer3D::SignalParams Renderer3D::ReflectionSignal()')
for old, new, what in (
    ('const int index = Math::Clamp(signal.Slot, 0, 3);',
     'const int index = Math::Clamp(signal.Slot, 0, 5);', 'blur clamp'),
    ('push.Probe.x = signal.BoundWidth;\n\t\tpush.Probe.y = signal.Slack;',
     'push.Probe.x = signal.BoundWidth;\n\t\tpush.Probe.y = signal.Slack;\n'
     '\t\t// RT-8: and the blur reads the same flag from the same slot, so a\n'
     '\t\t// signal cannot have one pass reading a position and the other a depth.\n'
     '\t\tpush.Probe.z = signal.PositionLane ? 1.0f : 0.0f;', 'blur probe'),
):
    if body.count(old) != 1:
        sys.exit('%s matched %d in BlurSignal' % (what, body.count(old)))
    body = body.replace(old, new, 1)
s = head + 'void Renderer3D::BlurSignal(' + body + sep2 + rest
io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', 'w', encoding='utf-8',
        newline=CRLF if crlf else LF).write(s)
print('BlurSignal patched')
