# -*- coding: utf-8 -*-
"""RT-8 job 2: the sea's mirror ray becomes a signal.

WR-16 S5 already traces the sea's reflection in a pass of its own, at a
fraction of the width and height, and the water draw reconstructs it with four
taps weighted by how far each ray went. What it has never had is a **temporal**
average: every frame's reflection is that frame's rays and nothing else, which
is why the sea's mirror is the noisiest thing in the bridge.

The contract is the machinery for exactly that, and after job 3 the sea can
reach it. So the traced picture goes through accumulate and blur like every
other signal -- at the resolution it was traced at, which is what the guidance
lanes are for -- and the water draw's four taps become the joint bilateral
upsample at the end of a real reconstruction rather than the whole of one.

The specular kind, not the diffuse: a reflection wants the direction test, the
hit-distance test and RT-15's mirror rule, and the trace already writes its hit
distance where the contract looks for it, with a negative alpha for no sea --
the same sentinel, by luck rather than design, and now by design.
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


# --- the guidance shader takes a whole texel, not a red channel ----------
patch(r'RageVEditor/assets/shaders/gbuffer_guide.rvshader', [
(
"""layout(location = 0) out float o_Depth;      // raw clip depth, as written""",
"""// **RT-8: a whole texel, not a red channel.** The lane is clip depth for
// every signal that sits on the G-buffer, and the sea's own position with a
// mask in w for the one that does not -- see SignalParams::PositionLane. An
// R32_SFLOAT target keeps the red and drops the rest, so the depth users are
// unchanged; the sea's target is four floats and keeps all four.
layout(location = 0) out vec4 o_Depth;       // clip depth in r, or a position and mask""",
    'guide depth output'),
(
"""	o_Depth = texelFetch(u_Depth, at, 0).r;""",
"""	o_Depth = texelFetch(u_Depth, at, 0);""",
    'guide depth write'),
])

# --- the contract may be told a layer has no per-object id ---------------
patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
(
"""			bool PositionLane = false;
		};""",
"""			bool PositionLane = false;
			// **RT-8: whether this layer has object ids at all.** The contract
			// weighs a history by whether it came from the same object, which
			// needs a lane saying which object -- the G-buffer has one and the
			// sea's layer does not, because the sea is one surface. Without
			// this the id test falls back to comparing a packed normal against
			// a packed normal, which on a turning wave differs every frame and
			// shortens the memory for a reason that is not there.
			bool NoObjectId = false;
		};""",
    'SignalParams::NoObjectId'),
(
"""		static SignalParams WaterLampSignal();""",
"""		static SignalParams WaterLampSignal();
		// **RT-8: the sea's mirror ray** -- a specular signal in slot 5, on the
		// sea's layer at the trace's own resolution. Specular because a
		// reflection wants the direction test, the hit-distance test and the
		// mirror rule, and the trace already writes the hit distance where the
		// contract reads it.
		static SignalParams WaterReflectionSignal();""",
    'WaterReflectionSignal declaration'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""		// RT-8: and which signal this is, so the counters can keep the sea's
		// numbers apart from the reflections'. The blur has no use for z.
		push.Probe.z = (float)index;""",
"""		// RT-8: and which signal this is, so the counters can keep the sea's
		// numbers apart from the reflections'. The blur has no use for z.
		push.Probe.z = (float)index;
		// RT-8: a layer with no object ids says so, rather than having the
		// test fall back to comparing packed normals.
		push.PreviousEye.w = signal.NoObjectId ? -1.0f : push.PreviousEye.w;""",
    'no-id flag'),
(
"""	void Renderer3D::AccumulateSignal(const SignalParams& signal,""",
"""	// **RT-8: the sea's mirror ray, on the contract.** The trace is WR-16 S5's,
	// at a fraction of the resolution; what is new is that its picture is now
	// averaged over the frames behind it and blurred while young, instead of
	// being this frame's rays and nothing else.
	//
	// Specular, because every one of the specular tests is the right question
	// for a sea: has the reflected direction swung (the wave turns), has what
	// the ray hits moved (the bridge above it has), and where does the image
	// sit now that the surface has risen (RT-15's mirror rule, which is what
	// a wave does to a reflection every frame).
	Renderer3D::SignalParams Renderer3D::WaterReflectionSignal()
	{
		SignalParams signal = ReflectionSignal();
		signal.Slot = 5;
		signal.PositionLane = true;
		// The sea is one surface, and its layer carries no id lane.
		signal.NoObjectId = true;
		return signal;
	}

	void Renderer3D::AccumulateSignal(const SignalParams& signal,""",
    'WaterReflectionSignal'),
])

# --- the shader honours it ----------------------------------------------
P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
old = """			const float idPenalty ="""
new = """			// **RT-8: a layer with no object ids does not get an id test.** The
			// engine says so with a negative PreviousEye.w, which is otherwise
			// a flag saying the eye is real; a layer that has no ids has no
			// second reading of it.
			const bool noIds = u_Reflection.PreviousEye.w < 0.0;
			const float idPenalty ="""
if s.count(old) != 1:
    sys.exit('idPenalty matched %d' % s.count(old))
s = s.replace(old, new, 1)

old = """			const bool sameMover = dot(objectShift, objectShift) > 0.0 && k == 0
				  && abs(wasId - g_ObjectId) < 0.5;"""
new = """			// RT-8: on a layer with no ids the id half of this is vacuously
			// true -- there is one object, so the history is always its own.
			const bool sameMover = dot(objectShift, objectShift) > 0.0 && k == 0
				  && (u_Reflection.PreviousEye.w < 0.0 || abs(wasId - g_ObjectId) < 0.5);"""
if s.count(old) != 1:
    sys.exit('sameMover matched %d' % s.count(old))
s = s.replace(old, new, 1)
io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print(P, 'honours the no-id flag')
