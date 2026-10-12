"""Place every value RT-24's hard-coded-values audit found in docs/RT-SERIES-2.md.

Fixes the two notes that claimed change 3 landed (it was reverted), extends the audit notes,
adds notes to the items that own the rest, and adds section 10: every value and where it goes.
"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

P = (_RAGEV + '/docs/RT-SERIES-2.md')
t = io.open(P, encoding='utf-8', newline='').read()
eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'
t = t.replace('\r\n', '\n')


def replace(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:90])
    t = t.replace(a, b)


def after_heading(heading, note):
    """Insert a note under an item's heading, after any notes already there."""
    global t
    assert t.count(heading) == 1, heading
    i = t.index(heading)
    j = t.index('\n', i) + 1
    while t[j:j + 1] == '\n' or t[j:j + 2] == '> ':
        j = t.index('\n', j) + 1
    t = t[:j] + note + '\n\n' + t[j:]


AUDIT = "> **Added from RT-24's hard-coded-values audit (2026-09-24; section 10 lists every value).** "

# ---- the intro -------------------------------------------------------------------------------
replace("(section 5). Nothing in it is built yet. Sources:",
        "(section 5). Parts of five items were built early during RT-24 and are not committed yet; "
        "each item's note says what. Sources:")
replace("RT-24's work is uncommitted: `include/pbr_fragment.glsl`, `reflection_accumulate.rvshader`, "
        "`tools/scripts/garage/watch_arm.py`, `docs/HANDOFF.md` and `docs/RT-SERIES.md` are modified, and "
        "`SampleProject/Source/Orbiter.cpp`, `spin_measure.py` and `ghost_map.py` are untracked.*",
        "RT-24's work is uncommitted (RT2-0's progress note lists it). Section 10 places every fixed value "
        "RT-24's hard-coded-values audit found.*")

# ---- change 3 was reverted: the two notes that said it landed -----------------------------------
replace("""> rebuilt at the pixel centre; the tube lamps' length energy; the reflection memory set by its
> lookup's own uncertainty; the resolve no longer spreads aimed lamp light. Dropped: H (parked
> flicker doubled), J (its motion gate breaks the owner's no-guessed-values rule; ungated it raised
> parked flicker).""", """> rebuilt at the pixel centre; the tube lamps' length energy; the resolve no longer spreads aimed
> lamp light. Dropped: H (parked flicker doubled), J (its motion gate breaks the owner's
> no-guessed-values rule; ungated it raised parked flicker), and the reflection memory set by its
> lookup's own uncertainty (built as change 3; it made the orbits worse and was reverted).""")
replace("""> to RT-21's highlight fix; engine fixes only, no scene changes). A hard-coded-values audit of the
> chain (262 values, 53 flagged, 23 surviving an adversarial check) is in `build/rt24/audit_phase*.json`.""",
        """> to RT-21's highlight fix; engine fixes only, no scene changes). A hard-coded-values audit of the
> reflection chain and the frame filter read 262 fixed values; section 10 places every one it found.""")
replace("""> slid across the surface; (24b) the memory from the lookup's own uncertainty (the image depth's
> spread projected through last frame's camera, against the lobe's blur) in place of the travel-based
> smear cap, slack and shrinking floor; (a consistent resolve) the aimed samples' lamp light kept out
> of the neighbour sharing and added back per texel. The rewrite itself is still this item's.""",
        """> slid across the surface; (a consistent resolve) the aimed samples' lamp light kept out of the
> neighbour sharing and added back per texel. Tried and reverted: (24b) the memory from the lookup's
> own uncertainty in place of the travel-based smear cap, slack and shrinking floor (change 3; it made
> the orbits worse), so those rules are still in the source and still this item's. The rewrite itself
> is still this item's.""")

# ---- the six notes added earlier today: their lead now points at section 10 --------------------
t = t.replace("> **Added from RT-24's hard-coded-values audit (2026-09-24; evidence `build/rt24/audit_phase2.json`).** ",
              AUDIT)

# ---- extend the existing notes ----------------------------------------------------------------
replace("""> prediction is rounded to the lane's precision before the difference -- so rounding can never fire it.""",
        """> prediction is rounded to the lane's precision before the difference -- so rounding can never fire it.
> The same rule for two more: the velocity lane's format itself (two half floats; general form: a
> format whose rounding stays far under a texel at any on-screen motion, such as two 32-bit floats),
> and the re-light's change test (0.004 of the value plus 1e-4; general form: half a step of the
> stored half float at that value, plus the float path's own step).""")

replace("""> reflection kind's direction test, floor one), the diffuse half keeping its surface-bound floor.""",
        """> reflection kind's direction test, floor one), the diffuse half keeping its surface-bound floor.
> Also: that direction test runs on this signal's view-independent half too, cutting its memory to
> four on smooth surfaces while the camera turns (it belongs to view-dependent payloads only); and
> every diffuse-kind signal's blur reaches a fixed 8 texels (`MaxRadius`; first pass only). General
> form: the reach from this item's penumbra width, in texels.""")

replace("""> will use. General form: weight on the exposed value, so the knee sits at display white.""",
        """> will use. General form: weight on the exposed value, so the knee sits at display white.
> (4) The material widening listed below is a factor of 8 picked from one measurement
> (`kStableWiden`). General form: extra room only for the part of the pixel's light that does not
> change with the view, which the composite already separates. (5) The box's own flicker memory is 16
> frames (`kMomentFrames`). General form: two jitter cycles, from the project's jitter setting.
> Checked and fine: the 26-degree normal test (world-space normals do not change with speed or scale).""")

# ---- new notes in the items that own the rest ---------------------------------------------------
after_heading('### RT2-23 · Reflection denoiser v2', AUDIT + """The rest of the
> reflection chain's fixed values. v2 carries none of them over as a tuned number; each is replaced
> by the general form given.
> - *Memory (live).* RT-24's floor that shrinks as the picture travels sets the memory to one frame
>   at speed, so the one-ray grain reaches the screen unaveraged. Behind it: the travel hyperbola, the
>   smear cap (`SmearTexels` 6, which alone sets the memory at the slow start and end of a drag),
>   `slack` with its 0.25 floor and its rule that a mirror forgets four times faster (backwards: a
>   near-mirror's lookup is exact, so it should keep memory longest), the full memory (`Memory` 64),
>   `SilhouetteMemory` 4, the 0-0.3 gloss ramp, and the moving layer's own 12, slack and floor.
>   General form: one rule -- keep N frames while N times how far the old picture can be misplaced
>   stays within the picture's own blur (the lobe's footprint, never under one texel), and stop
>   adding frames once the texel's standard error is under one display step. A first build of it
>   (change 3) made the orbits worse and was reverted: the misplacement estimate needs the work, not
>   a number. `MovingMemory` (now 1 for reflections, which does nothing) is deleted, and its comment
>   in `Renderer3D.h` corrected.
> - *Trust tests (latent).* The direction test (`kMirrorSensitivity`, `kRoughSensitivity`, its
>   halfway ramp), the hit-distance test (`kMirrorHitTolerance`, `kRoughHitTolerance`, `kHitAgree`,
>   the ramp to three times, a 0.25 m floor), the struck-object test (`kHitIdentityAgree`,
>   `kHitNormalAgree`, the 0.05-0.3 ramp), the match confidence (`kMaterialAgree`, `kIdAgree`, the id
>   and plane ramps, the 0.8-0.94 facing ramp), the facing refusal, the 0.5 roughness refusal, the
>   silhouette tests, the curvature allowance (0.15 per metre), the plane tolerances in metres (0.05 m
>   plus 1% of the distance in the accumulator, 0.02 m plus 1% in the resolve and the blur),
>   `kSurfaceNearer` and `kCurvedMover` (per metre). General form: each tolerance from the spread of
>   the quantity it tests -- the lobe's angular width for directions, the lobe's hit-distance spread
>   for distances, the two lobes' overlap for roughness, one texel's depth span for planes, the
>   normal encoding's own step for curvature. Nothing in metres, no hand-set ramp.
> - *Bounds (latent).* The glossy end's 12 spreads and its 0.08-0.3 ramp, `BoundWidth` 3,
>   `kSettledBound` 2 and `kSettledFrames` 32, the moments' 16 frames. General form: the bound is a
>   stated number of the texel's own measured spreads (a convention, as the firefly clamp's 3 is), and
>   the moments' memory follows the jitter cycle.
> - *Image distance (latent).* Its memory (a cap of 8 and a 1:1 blend with the stored distance) and
>   the 1 km miss clamp; in the trace, a miss written as 10 km and, with more than one ray, a mean of
>   different surfaces' distances. General form: 24a's one real hit per texel with misses flagged,
>   blended only where the stored and fresh distances describe the same surface.
> - *Resolve (latent).* `kCredibleDensity`, `kMaxReach` (24 texels), and the texel-size fallback
>   (0.0015 of the distance) and floor (0.0002 of it). General form: no separate density bound (the
>   ratio weight already carries it), the reach from the lobe's footprint, a texel's size from the
>   projection itself.
> - *Blur (two live).* The uncertainty gate's edges (0.02 and 0.20, fitted to one measurement): at
>   speed they give each texel of the rough floor anything from no blur to full blur, independently
>   of its neighbours, which reads as speckle. `MovingRadius` (4 texels) caps how much one-ray grain
>   the blur can remove at speed. Also `BlurFrames`, `YoungOverreach`, `kSigmaLFloor` and the mean
>   floor (absolute 1e-3), and the texel-size fallback and floor. General form: the radius is the
>   lobe's footprint in texels, and only the edge stop that compares a neighbour's difference with the
>   centre's own noise decides which taps count, so a settled texel keeps itself with no threshold;
>   floors relative to the values compared; a texel's size from the projection.
> - *Rays (latent, one defect).* Four rays where a surface moves on its own; the ray budget's 6-frame
>   young threshold, 1-ray minimum, 16-pixel tiles, 0.75-ray dead band and 8-frame dwell. And a
>   defect: the budget reads last frame's counts at the same screen tile, not where the texels were,
>   so a moving object gets another tile's rays. General form (24d): rays follow each texel's own
>   measured uncertainty, looked up where the texel was last frame; 24d sets the replacements for
>   the tile size, dead band and dwell by the same rule instead of carrying them over.""")

after_heading('### RT2-7 · Random numbers that move in every anti-aliasing mode', AUDIT + """The
> reflection trace indexes each texel's random rotation as x + 1601 y, so on any target wider than
> 1601 texels (the reflection grid is 3200 wide at 2x supersampling, the native target 2560) the texel
> 1601 columns to the right shares its random numbers with the texel one row down. The two are far
> apart, so no filter mixes them today, but the number is tied to a resolution (first pass only).
> General form: index by the target's own width, so no two texels share a rotation.""")

after_heading('### RT2-11 · Emissive geometry as sampleable lights; every fixture counted once', AUDIT + """Already
> here: the 16-slot cap (the audit found 7 of the garage's 23 glowing fittings never aimed at, whose
> lucky hits then skip the resolve's clamp) and the 12-step search. Added: each glowing surface is
> scored for aimed rays at three fixed points (the centre and both ends of its long axis). A poor score
> costs only noise and no visible effect was found, but a count of points is a guess at the shape.
> General form: score at the point on the emitter's axis nearest the lobe's main direction (a
> closed-form closest approach), plus the centre.""")

after_heading('### RT2-31 · Glass and water in the ray-traced world; one transmissive layer', AUDIT + """The
> water's and glass's own mirror ray is capped at 8 times the probe plus a floor (`ReflectionFloor`
> 0.05, made a setting when it crushed the bridge's lamp reflections at night). Both are fixed
> brightness levels (first pass only). General form: once this item gives these rays their own
> accumulators, the cap becomes the accumulators' relative clamp, from the texel's own statistics,
> with no absolute floor.""")

after_heading('### RT2-41 · Large-world precision', AUDIT + """Reflection rays stop at
> 10 km (`tMax` 1e4 m) and the accumulator clamps a miss to 1 km: world-size constants (first pass
> only). General form: the ray length from the scene's own extent (the ray structure's bounds), and a
> miss written as a flag (RT2-23).""")

# ---- section 10 --------------------------------------------------------------------------------
ROWS = [
    ('**Frame filter** (`taa_resolve`)', '', '', '', ''),
    ('`kDepthTolerance` (2%)', 'taa_resolve', '"Same surface": this frame\'s depth against last frame\'s',
     'Live. At speed most of the floor and both poles fail it and show the raw frame: edge jitter', 'RT2-28 (added)'),
    ('No-history exits', 'taa_resolve', 'A pixel with no usable history showed the raw jittered sample',
     'Fixed in RT-24 (change 1)', 'RT2-28 (done early)'),
    ('`kStillWithinTexels` (0.003 texel)', 'taa_resolve', '"Is this pixel still"',
     'Latent. A precision guard written as a number', 'RT2-28 and RT2-6 (added)'),
    ('`kStableWiden` (8)', 'taa_resolve', 'Widens the colour box on rough non-metals',
     'Latent. Small smear effect at fast speed', 'RT2-28 (the problem was listed; general form added)'),
    ('`kMomentFrames` (16)', 'taa_resolve', "Memory of the box's own flicker estimate", 'Latent', 'RT2-28 (added)'),
    ('`Compress`/`Expand` (1/(1+Y))', 'taa_resolve', 'Brightness curve used when averaging',
     'Latent. Tied to absolute brightness, not the exposure', 'RT2-28 (added)'),
    ('`kNormalTolerance` (26 degrees)', 'taa_resolve', 'Normal test for "same surface"',
     'Fine. World-space normals do not change with speed or scale', 'none needed'),
    ('**Reflection memory** (`reflection_accumulate`, `Renderer3D`)', '', '', '', ''),
    ('Floor that shrinks with travel (RT-24 fix 2)', 'reflection_accumulate', 'The fewest frames a reflection keeps',
     'Live. At speed it sets the memory to one frame, so the one-ray grain reaches the screen',
     'RT2-23 (added; change 3 tried and reverted)'),
    ('Travel hyperbola', 'reflection_accumulate', 'Shortens memory with travel', 'Latent', 'RT2-23 (added); RT2-30 deletes'),
    ('`SmearTexels` (6)', 'Renderer3D.h', 'Memory limited to 6 texels of travel',
     'Live at the slow start and end of a drag', 'RT2-23 (added)'),
    ('`slack` (0.25 floor, 0.25 mirror factor)', 'reflection_accumulate', 'Travel that halves the memory; mirrors forget 4x faster',
     'Latent, and the mirror rule is backwards', 'RT2-23 (added)'),
    ('`MovingMemory` (8)', 'Renderer3D.h', 'Ceiling on memory while moving',
     'Fixed for reflections (now 1, which does nothing); its comment is wrong', 'RT2-23: delete it (added)'),
    ('`Memory` (64)', 'Renderer3D.h', 'Full memory', 'Latent', 'RT2-23 (added)'),
    ('`SilhouetteMemory` (4)', 'Renderer3D.h', 'Memory floor at silhouettes', 'Latent (first pass only)', 'RT2-23 (added)'),
    ('Gloss ramp (roughness 0 to 0.3)', 'reflection_accumulate', 'Splits mirror from rough for the memory rules', 'Latent', 'RT2-23 (added)'),
    ('Moving layer: 12 spreads, slack, floor', 'reflection_accumulate', 'Bound and memory of the moving layer (RT-15)',
     'Latent (first pass only)', 'RT2-23 (added; the layer\'s fate is 24e\'s)'),
    ('**Reflection trust tests** (`reflection_accumulate`)', '', '', '', ''),
    ('`kMirrorSensitivity`, `kRoughSensitivity`, halfway ramp', 'reflection_accumulate', 'How far the reflected direction may turn',
     'Latent (the last two first pass only). Also runs on view-independent signals', 'RT2-23 and RT2-24 (added)'),
    ('`kMirrorHitTolerance`, `kRoughHitTolerance`, `kHitAgree`, ramp to 3x, 0.25 m floor', 'reflection_accumulate',
     'Hit-distance agreement', 'Latent (the last three first pass only)', 'RT2-23 (added)'),
    ('`kHitIdentityAgree` (0.2), `kHitNormalAgree` (0.9), 0.05-0.3 ramp', 'reflection_accumulate',
     'Struck-object test (RT-17)', 'Latent (first pass only)', 'RT2-23 (added)'),
    ('`kMaterialAgree` (0.15), `kIdAgree` (0.25), id ramp, plane edge (0.35), facing ramp (0.8-0.94)',
     'reflection_accumulate', 'Match confidence', 'Latent (first pass only)', 'RT2-23 (added)'),
    ('Facing refusal', 'reflection_accumulate', 'Refuses a history facing elsewhere', 'Latent', 'RT2-23 (added)'),
    ('Roughness refusal (0.5)', 'reflection_accumulate', 'Refuses a history of another roughness', 'Latent', 'RT2-23 (added)'),
    ('Staged roughness thresholds (0.15/0.5, 0.5/1.5 texels)', 'watch_arm.py arm', 'Trust by lobe difference',
     'Fixed in RT-24: change 2 does it with no numbers', 'RT2-23 (done early)'),
    ('Silhouette tests (cosine 0.8; 0.05 m + 1%)', 'reflection_accumulate', 'Where a silhouette skips the tests',
     'Latent. In metres', 'RT2-23 (added)'),
    ('History plane tolerance (0.05 m + 1%)', 'reflection_accumulate', 'Refuses a history off the plane',
     'Latent (first pass only). In metres', 'RT2-23 (added)'),
    ('Curvature allowance (0.15 per metre)', 'reflection_accumulate', 'Absorbs normal rounding', 'Latent',
     'RT2-23 (added), with RT2-21\'s signed curvature'),
    ('`kCurvedMover`, `kSurfaceNearer`', 'reflection_accumulate', 'Choice between two histories on movers',
     'Latent. Per metre', 'RT2-23 (added); RT2-30 deletes the choice'),
    ('`kMotionFloor` (1/8 texel)', 'reflection_accumulate', '"Moved on its own"', 'Latent. Rounding trips it at speed', 'RT2-6 (added)'),
    ('**Reflection bounds** (`reflection_accumulate`, `Renderer3D`)', '', '', '', ''),
    ('Glossy end: 12 spreads, 0.08-0.3 ramp', 'reflection_accumulate', 'Colour box width on glossy surfaces', 'Latent', 'RT2-23 (added)'),
    ('`BoundWidth` (3)', 'Renderer3D.h', 'Colour box width', 'Latent', 'RT2-23 (added)'),
    ('`kSettledBound` (2), `kSettledFrames` (32)', 'reflection_accumulate', 'Box widening once settled',
     'Latent (first pass only)', 'RT2-23 (added); RT2-6 arm 12 fixes its counter; RT2-30 deletes it'),
    ('`kMomentFrames` (16)', 'reflection_accumulate', "Moments' memory", 'Latent (first pass only)', 'RT2-23 (added)'),
    ('Firefly clamp width (3 spreads)', 'reflection_accumulate', 'Caps a sample at 3 spreads of its neighbours',
     'Fine. Relative to the texel\'s own neighbours; 3 is a stated convention', 'RT2-6 arm 7 already reworks the clamp'),
    ('**Reflection image distance** (`reflection_accumulate`, `reflection_trace`)', '', '', '', ''),
    ('Image-distance memory (cap 8, 1:1 blend)', 'reflection_accumulate', "Smooths the reflection's depth", 'Latent',
     'RT2-0 arm F and RT2-23 24a (were there); general form added'),
    ('Miss as 10 km, clamped to 1 km; multi-ray mean distance', 'reflection_trace, reflection_accumulate',
     'A miss written as a distance', 'Latent', 'RT2-23 24a (was there); general form added'),
    ('**Direct light** (`Renderer3D`, `reflection_accumulate`)', '', '', '', ''),
    ('`PairMemory` (4)', 'Renderer3D.cpp', "The lamps' own highlights keep 4 frames",
     'Live. A trail of stale streaks on the chrome poles at speed', 'RT2-24 (added)'),
    ('`Fewest` (1 to 4) and the diffuse floor of 4', 'Renderer3D.h', "Floor under the direct light's memory",
     'Live, through `PairMemory`', 'RT2-24 (added)'),
    ('`MaxRadius` (8 texels)', 'Renderer3D.h', "Blur reach of every diffuse-kind signal", 'Latent (first pass only)', 'RT2-24 (added)'),
    ('**Reflection sharing** (`reflection_resolve`)', '', '', '', ''),
    ('`kEmitterShare` (0.5)', 'reflection_resolve', 'Lamp-lit taps skip the outlier clamp',
     'Live. Lucky lamp hits pass whole: speckle', 'RT2-23 (added)'),
    ('`kReuseFraction` (1/5)', 'reflection_resolve', 'Size of the sharing disc',
     'Live. Sets how much one-ray grain survives', 'RT2-23 (added)'),
    ('`kMaxWeight` (4)', 'reflection_resolve', "Cap on a shared tap's weight",
     'Live at puddle edges (plausible, not measured)', 'RT2-23 (added)'),
    ("Lamp light re-aimed by a neighbour's ray", 'reflection_resolve', "Spread each lamp's light in a halo",
     'Fixed in RT-24 (fix 1). It costs grain in motion; keep or revert is open', 'RT2-23 (done early); RT2-12 the proper version'),
    ('`kCredibleDensity`', 'reflection_resolve', 'Lifts the clamp for high-density taps', 'Latent', 'RT2-23 (added)'),
    ('`kMaxReach` (24 texels)', 'reflection_resolve', 'Largest disc', 'Latent (first pass only)', 'RT2-23 (added)'),
    ('Plane tolerance (0.02 m + 1%)', 'reflection_resolve, reflection_blur', 'Is a neighbour on the same plane',
     'Latent. In metres (the blur\'s first pass only)', 'RT2-23 (added)'),
    ('Texel size: fallback 0.0015 of the distance, floor 0.0002', 'reflection_resolve, reflection_blur',
     'Metres per texel when not measured', 'Latent (partly first pass only)', 'RT2-23 (added)'),
    ('**Reflection blur** (`reflection_blur`, `Renderer3D`)', '', '', '', ''),
    ('Uncertainty gate edges (0.02, 0.20)', 'reflection_blur', 'How much blur a texel gets from its measured noise',
     'Live. At speed each rough-floor texel gets anything from none to full blur: speckle', 'RT2-23 (added)'),
    ('`MovingRadius` (4 texels)', 'Renderer3D.cpp', "The blur's radius",
     'Live. Caps how much one-ray grain the blur removes at speed', 'RT2-23 (added)'),
    ('`kSigmaLFloor` and the mean floor (1e-3)', 'reflection_blur', 'Absolute brightness floors', 'Latent (the mean floor first pass only)', 'RT2-23 (added)'),
    ('`BlurFrames`, `YoungOverreach`', 'Renderer3D.h', "The blur's frame ramp and overreach", 'Latent', 'RT2-23 (added)'),
    ('`YoungRadius`', 'Renderer3D.cpp', 'Young-pixel blur', 'Fine. 0 on the floor chain, an off switch', 'none needed'),
    ('**Reflection rays** (`reflection_trace`, the ray budget)', '', '', '', ''),
    ('16 aimed glowing surfaces (`kMaxAreaEmitters`)', 'Renderer3D, reflection_trace', 'Which lamps get aimed rays',
     'Live. 7 of the garage\'s 23 fittings never get one: speckle', 'RT2-11 (was there)'),
    ("12-step search of an emitter's table", 'reflection_trace', 'Picks a point on an emitter', 'Latent (first pass only)',
     'RT2-11 (was there: picks without a search)'),
    ('3 score points per emitter', 'reflection_trace', 'Where an emitter is scored for aimed rays', 'Latent', 'RT2-11 (added)'),
    ('Cap of 64 per channel', 'reflection_trace', 'Guards the stored moments',
     'Live. Tube reflections come out less blue than the tubes', 'RT2-6 arm 5 (was there)'),
    ('"Moved on its own" tolerance (1/8 texel)', 'change_record.glsl', 'One ray or four per texel',
     'Live (plausible). At speed rounding flips still texels between 1 and 4 rays', 'RT2-6 (added)'),
    ('4 moving rays; budget 6 frames, 1 ray, 16-pixel tiles, 0.75 dead band, 8-frame dwell',
     'Renderer3D, FrameGraphBuilder, RenderSettings', 'Ray counts', 'Latent (first pass only)', 'RT2-23 24d (added)'),
    ('Budget read at the same screen tile', 'reflection_trace, reflection_budget', "Last frame's ray counts, not reprojected",
     'Latent defect: a moving object gets another tile\'s rays', 'RT2-23 24d (added)'),
    ('Re-light tolerance (0.004 of the value + 1e-4)', 'reflection_trace', 'Did a replayed sample change', 'Latent', 'RT2-6 (added)'),
    ('Random index stride (1601)', 'reflection_trace', "Each texel's random rotation", 'Latent (first pass only). Tied to resolution', 'RT2-7 (added)'),
    ('Ray length (10 km)', 'reflection_trace', 'Farthest hit', 'Latent (first pass only). World size', 'RT2-41 (added)'),
    ('**Formats** (`FrameGraphBuilder`)', '', '', '', ''),
    ('Object ids in half-float history lanes', 'FrameGraphBuilder', 'Is this the same object', 'Latent. Exact only below 2,048 objects', 'RT2-5 (was there)'),
    ('Velocity in two half floats', 'FrameGraphBuilder', 'Motion vectors', 'Latent. Precision falls with speed', 'RT2-6 (added)'),
    ('**Glass and water** (`pbr_fragment`, `PostSettings`)', '', '', '', ''),
    ('`ReflectionFloor` (0.05) and the 8x probe cap', 'pbr_fragment, PostSettings', "Cap on water's and glass's own mirror ray",
     'Latent (first pass only). Absolute brightness', 'RT2-31 (added)'),
    ("**Found while chasing the lights' sharpness** (not part of the audit)", '', '', '', ''),
    ('Minimum roughness (0.045)', 'pbr_fragment', 'No surface can be smoother', 'Latent. No puddle or mirror sharper than it',
     'RT2-21 (added)'),
    ('No ground truth for the lights on the floor', 'the reference renderer', "How sharp the tubes' reflections should be",
     'Open. They fattened when RT-21 removed the highlight cap', 'RT2-2 (added)'),
]


def row(cells):
    return '| ' + ' | '.join(cells) + ' |'


table = [row(('Value', 'Where', 'What it does', 'Found', 'On the roadmap')), row(('---',) * 5)]
table += [row(r) for r in ROWS]

SECTION10 = """## 10. The hard-coded-values audit: every value, and where it goes

RT-24's audit (2026-09-24) read every fixed value in the reflection chain and the frame filter (`reflection_trace`, `reflection_resolve`, `reflection_accumulate`, `reflection_blur`, `reflection_composite`, `taa_resolve` and their C++ settings) against the owner's rule: general solutions only, nothing that depends on speed, frame rate, scale, the scene or a surface. Of 262 values, 170 are fine: maths, pixel units or numerical guards. The other 92 break the rule on paper. The 53 of those with a path to a symptom were checked a second time by a skeptic, who also found 5 the first pass missed; the other 39 were flagged by the first pass only. Evidence: `build/rt24/audit_phase1.json` and `audit_phase2.json`.

Every value found is below, with where the roadmap handles it. **Live** means it affects today's picture. **Latent** means it breaks the rule without a visible effect today. **First pass only** means it was not checked a second time. The general form for each is in the note at the top of the item named.

""" + '\n'.join(table) + """

**Not yet audited this way:** the direct light, bounce light, AO, the lit shader (`pbr_fragment.glsl`, apart from the two rows above), water, glass, post-processing and the bake. Each item that rewrites one of those passes audits its fixed values the same way before it starts, and adds what it finds to this table.
"""

t = t.rstrip('\n') + '\n\n' + SECTION10

io.open(P, 'w', encoding='utf-8', newline='').write(t.replace('\n', eol))
print('ok', len(ROWS), 'rows;', sum(1 for r in ROWS if '(added' in r[4] or 'added)' in r[4]), 'added')
