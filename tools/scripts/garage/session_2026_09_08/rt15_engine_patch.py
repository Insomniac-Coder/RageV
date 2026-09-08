"""RT-15's root cause: the motion history is taken after the movement, not before.

Every anchor must match exactly once, or nothing is written.
"""
import io, sys

def patch(path, edits, marker):
    src = io.open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    if marker in s:
        print('%s already patched, skipped' % path); return
    for i, (old, new) in enumerate(edits, 1):
        n = s.count(old)
        if n != 1:
            sys.exit('%s anchor %d matched %d times' % (path, i, n))
        s = s.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print('patched %s (%d anchors)' % (path, len(edits)))


T = '\t'
H = r'RageV/src/RageV/Scene/Scene.h'
C = r'RageV/src/RageV/Scene/Scene.cpp'

patch(H, [(
 T*2 + 'void AdvanceMotionHistory();',
 T*2 + 'void AdvanceMotionHistory();\n'
 + T*2 + '// **Once a frame, and before anything has moved.** Whichever of the\n'
 + T*2 + '// two updates a frame reaches first takes the snapshot and sets\n'
 + T*2 + '// this; the frame update clears it on its way past. See the\n'
 + T*2 + '// comment on AdvanceMotionHistory for why the order is the whole\n'
 + T*2 + '// point.\n'
 + T*2 + 'bool m_MotionHistoryTaken = false;')], 'm_MotionHistoryTaken')

patch(C, [
 # The copy itself gains the explanation, and a note on what it must precede.
 (T + 'void Scene::AdvanceMotionHistory()\n'
  + T + '{',
  T + '// **Where every transform stood in the frame that was last drawn.**\n'
  + T + '//\n'
  + T + '// The renderer subtracts this from the current world matrix to get\n'
  + T + '// each pixel motion, which is what every temporal filter in the\n'
  + T + '// engine reprojects by -- the frame anti-aliasing, the reflection\n'
  + T + '// accumulator, and the reconstruction contract behind the traced\n'
  + T + '// signals. So the one thing it must be is *stale*: taken before this\n'
  + T + '// frame has moved anything.\n'
  + T + '//\n'
  + T + '// **It was not, and that is RT-15 (2026-09-08).** It ran only at the\n'
  + T + '// top of OnUpdateRuntime, and the loop steps the fixed scripts before\n'
  + T + '// that (Application.cpp) while OnFixedUpdateRuntime ends by deriving\n'
  + T + '// the world transforms -- so by the time this ran, anything an OnTick\n'
  + T + '// had moved was already at its new place and got copied to its own\n'
  + T + '// "previous". Where it was and where it is were the same matrix, and\n'
  + T + '// the object reported a motion of exactly zero while visibly crossing\n'
  + T + '// the screen. Measured on showroom_moving.rage: the panel travelled\n'
  + T + '// x = 389, 615, 839 over frames 20, 80 and 150 with the velocity lane\n'
  + T + '// reading 0.000 on it throughout, while a camera dolly moved the same\n'
  + T + '// pixels to 28.8 -- so the lane was live and the object was not in it.\n'
  + T + '//\n'
  + T + '// Physics and the per-frame scripts were never affected: both run\n'
  + T + '// after this point in OnUpdateRuntime. It was the tick rate, which is\n'
  + T + '// the engine main script rate, that was invisible to every filter.\n'
  + T + '//\n'
  + T + '// **Taken once a frame now, by whichever update reaches the frame\n'
  + T + '// first** -- the first fixed step where there is one, the frame update\n'
  + T + '// where there is not -- so it always holds the state that was drawn,\n'
  + T + '// however many steps a frame runs and whatever does the moving.\n'
  + T + 'void Scene::AdvanceMotionHistory()\n'
  + T + '{'),
 # The frame update: only if a step has not already done it.
 (T*2 + 'm_FrameDelta = ts.GetSeconds();\n'
  + T*2 + 'AdvanceMotionHistory();\n'
  + '\n'
  + T*2 + '// A paused frame derives and places, but advances nothing.',
  T*2 + 'm_FrameDelta = ts.GetSeconds();\n'
  + T*2 + '// The fixed steps of this frame have already run (Application steps\n'
  + T*2 + '// them before the layers update), so one of them has normally taken\n'
  + T*2 + '// the snapshot already. A frame that ran no step takes it here.\n'
  + T*2 + 'if (!m_MotionHistoryTaken)\n'
  + T*3 + 'AdvanceMotionHistory();\n'
  + T*2 + 'm_MotionHistoryTaken = false;\n'
  + '\n'
  + T*2 + '// A paused frame derives and places, but advances nothing.'),
 # The fixed step: before the pause guard and before anything moves.
 (T*2 + '// Before the pause guard, so a paused step still ages the edges by\n'
  + T*2 + '// exactly as much as an unpaused one -- otherwise a press made during\n'
  + T*2 + '// a pause would be waiting for the first step after it.\n'
  + T*2 + 'FrameClock::StepScope step;',
  T*2 + '// Before the pause guard, so a paused step still ages the edges by\n'
  + T*2 + '// exactly as much as an unpaused one -- otherwise a press made during\n'
  + T*2 + '// a pause would be waiting for the first step after it.\n'
  + T*2 + 'FrameClock::StepScope step;\n'
  + '\n'
  + T*2 + '// **RT-15: the snapshot belongs here, before an OnTick can move\n'
  + T*2 + '// anything.** This is the first thing a frame does to the scene, so\n'
  + T*2 + '// the transforms still hold what was drawn last frame. Only the\n'
  + T*2 + '// first step of a frame takes it -- a frame that runs three steps\n'
  + T*2 + '// must still report its motion against the frame that was drawn,\n'
  + T*2 + '// not against its own second step.\n'
  + T*2 + '//\n'
  + T*2 + '// Before the pause guard as well, and deliberately: a scene that\n'
  + T*2 + '// unpauses with a stale "previous" would give every object one\n'
  + T*2 + '// frame of invented motion.\n'
  + T*2 + 'if (!m_MotionHistoryTaken)\n'
  + T*2 + '{\n'
  + T*3 + 'AdvanceMotionHistory();\n'
  + T*3 + 'm_MotionHistoryTaken = true;\n'
  + T*2 + '}'),
], 'RT-15')
