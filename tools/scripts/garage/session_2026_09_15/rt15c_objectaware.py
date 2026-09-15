# -*- coding: utf-8 -*-
"""RT-15c (2026-09-15 night): reflections back before TAA, and the object-aware history made hard.

Owner: "I WANT THE REFLECTIONS TO GO BACK BEFORE TAA AND I WANT THE OBJECT AWARE TEMPORAL
HISTORY TO DO IT'S JOB" -- after B1's no-borrowing rule (edit 4) was judged not a fix.

The build under test:
  - --reflection-moving-layer defaults off: the whole reflection is composited before TAA and the
    accumulator keeps one layer (FrameGraphBuilder, EngineConfig).
  - reflection_accumulate: another object's history is refused outright (HistoryAt), the texel's
    own history is filtered only within its object (OwnHistoryPicture), the bound reads only its
    object's neighbours (Neighbourhood); edit 4's silhouette-mover mark is gone.

Arms (all TAA, the scene's cube roughness 0.12):
  cube_new / cube_nocheck      owner's shot, the cube crossing the car, frames 100-140
  car_before / car_after       the driving car close up, frames 60-99: reflections before TAA
                               (the new default) and after it (--reflection-moving-layer=on)
  parked_new / parked_nocheck  the parked garage, frames 150-189 (edge flicker)
  dolly_new / dolly_nocheck    the camera dolly, frames 60-119
`nocheck` stages SameObject() to always agree: the object check off, nothing else changed.

Usage: rt15c_objectaware.py render [cube|car|parked|dolly ...]
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rt15_items as it  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
ACC = 'reflection_accumulate.rvshader'
stage_run.VARIANTS['nocheck'] = [
    (ACC, 'bool SameObject(float id) { return !HardObjectIds() || abs(id - g_ObjectId) < 0.5; }',
          'bool SameObject(float id) { return true; }'),
]
CAR = '=porsche_992_gt3_r|1.0|2.0'
GROUPS = {
    'cube': ('owner', [('r15c_cube_new', 'ship', [], dict(frames=41, first=100, cube=(-9.0, 3.0, 6.0))),
                       ('r15c_cube_nocheck', 'nocheck', [], dict(frames=41, first=100, cube=(-9.0, 3.0, 6.0)))]),
    'car': ('close', [('r15c_car_before', 'ship', [], dict(frames=40, first=60, BURST_SLIDE=CAR)),
                      ('r15c_car_after', 'ship', ['--reflection-moving-layer=on'], dict(frames=40, first=60, BURST_SLIDE=CAR))]),
    'parked': ('owner', [('r15c_parked_new', 'ship', [], dict(frames=40, first=150)),
                         ('r15c_parked_nocheck', 'nocheck', [], dict(frames=40, first=150))]),
    'dolly': ('owner', [('r15c_dolly_new', 'ship', [], dict(frames=60, first=60, speed=0.6, stop=1.5)),
                        ('r15c_dolly_nocheck', 'nocheck', [], dict(frames=60, first=60, speed=0.6, stop=1.5))]),
}

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'render':
        groups = sys.argv[2:] or list(GROUPS)
        try:
            for g in groups:
                cam, arms = GROUPS[g]
                s2.arms_at(cam, arms)
        finally:
            print('staged copies restored and identical to source:', stage_run.restore())
