"""Place audit 2's surviving findings in docs/RT-SERIES-2.md.

Every surviving verdict (confirmed / partly / new) goes to one owning item: a note in the item, and
a row in section 10's audit-2 table grouped by item. Two new items (RT2-44 the raster fallback,
RT2-45 the post chain) hold what had no owner, in a new milestone M7.
"""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io
import json
import os
import re

ROOT = (_RAGEV + '/')
P = ROOT + 'docs/RT-SERIES-2.md'
t = io.open(P, encoding='utf-8', newline='').read()
eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'
t = t.replace('\r\n', '\n')

phase2 = json.load(io.open(ROOT + 'build/rt24/audit2_phase2.json', encoding='utf-8'))
surv = [dict(v, job=r['key']) for r in phase2 for v in r['verdicts'] if v['verdict'] != 'refuted']
assert len(surv) == 225, len(surv)
refuted = sum(1 for r in phase2 for v in r['verdicts'] if v['verdict'] == 'refuted')

# ---- owner of each surviving finding (1-based, in audit2_surv.txt order) ----------------------
OWNER = {
    'RT2-1': [152], 'RT2-2': [38], 'RT2-4': [97], 'RT2-5': [3, 4],
    'RT2-6': [50, 184, 85, 160, 109, 72, 114, 166], 'RT2-7': [2, 87],
    'RT2-10': [67, 68, 81, 82, 83, 90, 99], 'RT2-11': [16, 17, 18, 96, 158],
    'RT2-12': [6, 40, 41, 95, 93, 84, 100, 101], 'RT2-16': [1], 'RT2-17': [91, 92, 171, 172],
    'RT2-20': [31, 116, 163, 164, 165, 167, 168], 'RT2-21': [46, 89, 161, 162, 52, 53, 54, 79, 88],
    'RT2-23': [169, 170], 'RT2-24': [32, 43, 44, 66],
    'RT2-25': [7, 26, 140, 141, 142, 143, 144, 145, 146, 155, 14, 47, 147, 156, 157],
    'RT2-26': [5, 39, 94, 24, 29, 30, 25, 27, 9, 28, 78, 150, 151, 154, 13, 121],
    'RT2-27': [45, 153], 'RT2-30': [34, 224, 178, 179, 180, 181],
    'RT2-31': [33, 56, 70, 73, 106, 71],
    'RT2-32': [12, 36, 37, 102, 103, 104, 51, 58, 59, 60, 61, 62, 63, 64, 65, 74, 98, 107, 108, 110,
               111, 112, 113],
    'RT2-33': [117, 129, 132, 133, 128, 135], 'RT2-38': [57, 86],
    'RT2-41': [8, 15, 48, 49, 69, 80, 105, 148, 149, 159, 55],
    'RT2-44': [10, 11, 19, 20, 21, 22, 23, 42, 75, 76, 77, 118, 119, 120, 123, 124, 173, 174, 175, 176,
               177, 182, 183] + list(range(185, 224)) + [225],
    'RT2-45': [35, 115, 122, 125, 126, 127, 130, 131, 134, 136, 137, 138, 139],
}
WAS_THERE = {2, 3, 4, 9, 28, 78, 17, 18, 158, 41, 95, 109, 114, 166, 144, 145, 147, 156, 157, 71}
PROBLEM_LISTED = {14, 140, 72}
DELETED_THERE = {34, 224, 178, 179, 180, 181}
seen = [n for ns in OWNER.values() for n in ns]
assert sorted(seen) == list(range(1, 226)), (len(seen), set(range(1, 226)) - set(seen))
owner_of = {n: item for item, ns in OWNER.items() for n in ns}

EFFECT = {'flicker': 'flicker', 'noise_or_speckle': 'noise', 'smear_or_ghost': 'smear',
          'wrong_brightness_or_colour': 'wrong brightness or colour', 'banding_or_pattern': 'banding',
          'aliasing_or_edge_jitter': 'jagged or jittery edges', 'frame_cost': 'cost',
          'none': 'no visible effect'}


def cell(s):
    s = re.sub(r'\|([^|\n]{1,40})\|', r'abs(\1)', s)
    s = s.replace('|', '/').replace('\n', ' ')
    if s.count('`') % 2:
        s = s.replace('`', '')
    return s.strip()


def general(s):
    parts = re.split(r'(?<=[.;:])\s+', s.strip())
    out = parts[0]
    if len(out) < 70 and len(parts) > 1:
        out += ' ' + parts[1]
    if len(out) > 280:
        cut = out.rfind(' ', 0, 270)
        out = out[:cut].rstrip(',;:') + ' ...'
    return cell(out)


def status(n):
    if n in WAS_THERE:
        return 'was there'
    if n in PROBLEM_LISTED:
        return 'problem was listed; general form added'
    if n in DELETED_THERE:
        return 'goes with the deletion'
    return 'added'


# ---- item headings -----------------------------------------------------------------------------
HEAD = {int(m.group(1)): m.group(0) for m in re.finditer(r'^### RT2-(\d+) · .*$', t, re.M)}


def short_title(n):
    title = HEAD[n].split(' · ', 1)[1]
    return re.split(r'[;(]', title)[0].strip()


# ---- the table, grouped by item, duplicates (same file and line) folded -------------------------
rows = []
dups = 0
for item in sorted(OWNER, key=lambda s: int(s.split('-')[1])):
    n_item = int(item.split('-')[1])
    title = short_title(n_item) if n_item in HEAD else {
        44: 'The raster fallback on general forms', 45: 'The post chain on general forms'}[n_item]
    rows.append(('**%s** · %s' % (item, title), '', '', '', ''))
    done = set()
    for n in OWNER[item]:
        v = surv[n - 1]
        where = '%s:%d' % (os.path.basename(v['file']), v['line'])
        if where in done:
            dups += 1
            continue
        twins = [u for u in OWNER[item] if u != n and
                 '%s:%d' % (os.path.basename(surv[u - 1]['file']), surv[u - 1]['line']) == where]
        vis = v['visible_today'] or any(surv[u - 1]['visible_today'] for u in twins)
        prio = min([v['priority']] + [surv[u - 1]['priority'] for u in twins])
        done.add(where)
        found = ('Live. ' if vis else 'Latent. ') + EFFECT[v['effect']]
        if prio == 1:
            found += '; first to fix'
        if v['verdict'] == 'new':
            found += '; found by the skeptic'
        rows.append((cell(v['name'][:90]), where, found, general(v['general_form']), status(n)))


def row(cells):
    return '| ' + ' | '.join(cells) + ' |'


value_rows = sum(1 for r in rows if r[1])
live = sum(1 for r in rows if r[2].startswith('Live'))
first = sum(1 for r in rows if 'first to fix' in r[2])
table = [row(('Value', 'Where', 'Found', 'General form', 'On the roadmap')), row(('---',) * 5)]
table += [row(r) for r in rows]

AUDIT2 = """### Audit 2: the rest of the renderer (2026-09-24)

Twelve read-only agents listed every fixed value in the rest of the renderer: the lit shader, the direct light, the live bounce light and AO, the bake and probes, water and glass, post-processing and its settings, `Renderer3D` and the frame setup, and the raster fallbacks (SSR, SSGI, voxel GI, SSAO, shadow maps) -- 852 values. 270 broke the rule on paper. Twelve skeptics then tried to disprove each of those, plus 25 borderline ones the first pass had not flagged, and found 5 more. %d were refuted (fine, or inert where they sit). The other 225 survived; the same value found by two agents is listed once, so the table below has %d rows. %d affect today's picture (**Live**) and %d are marked first to fix. Live is the skeptics' judgement from reading the code: none of these has been measured on screen yet. Evidence: `build/rt24/audit2_phase1.json` and `audit2_phase2.json`.

Every surviving value is below, under the item that fixes it. The note at the top of each item says the same in plain words. Two new items hold what had no owner: RT2-44 (the raster fallback) and RT2-45 (the post chain), as milestone M7. Not audited: particles, UI, terrain generation, the editor grid and debug views, GPU culling and skinning.

""" % (refuted, value_rows, live, first) + '\n'.join(table) + '\n'

# ---- item notes --------------------------------------------------------------------------------
A2 = "> **Added from audit 2 (2026-09-24; section 10 lists each value with its general form).** "
NOTES = {
    1: """The bake file
> loader rejects a file above a fixed "sane" size. General form: check the payload against the size
> its own header implies, and against the file's length.""",
    2: """The capture
> harness takes its picture at a fixed frame number. General form: capture once the histories report
> settled, or after the longest memory in force.""",
    4: """Found by the
> skeptics: a shadow ray fetches a cutout texture even when the cutoff cannot fail. Guard it (cost
> only, no change to the picture).""",
    5: """Confirmed
> independently: CPU-path ids follow submission order, so when the camp's fox or a terrain chunk
> crosses the frame edge every later id shifts, and both the reflection memory and TAA refuse good
> history (live on the camp and the bridge, first to fix); and the struck identity folds to 1..1021.""",
    6: """Two more arms,
> both defects rather than tuned values: local probe cubes are sampled in the sky's rotated frame
> (wrong local reflections whenever the sky is rotated), and the reflection ray budget reads its
> history through a flipped coordinate, so each tile's rays follow a mirrored tile (first to fix).
> Arms 8, 11 and 13 were confirmed; arm 13's general form: one weight definition for every see-through
> writer, derived from the projection. The direct light's and GI's re-light tolerances join the
> storage-format rule in the note above.""",
    7: """Confirmed: the
> direct light's random draws move only while TAA jitters (this item). Added: on the ray side, the
> thin-member fade's dither is keyed to a world lattice instead of the ray's own pixel and frame.""",
    10: """The sized-lamp
> highlight. On the sea the direct trace stretches a lamp's streak by fixed factors and renormalises it
> by a square root (live on the bridge's water, first to fix); the water's own lamp shading and the lit
> water each add a stretch factor of their own (the first is live); the direct light
> scores a lamp's highlight with a different lobe than it shades with (live, first to fix); and a hard
> shadow is recognised by a ray length of 10 km instead of the light's kind. General form: the lobe
> evaluated against the lamp's real extent with an exact energy normalisation, the score from the
> shading's own term, the light's kind passed explicitly.""",
    11: """Confirmed: the
> first 16 emitters in scene order, each as its bounding box, and the same cap in GI (this item).
> Added: a surface counts as an emitter only above an emission of 1.0 (general form: it emits at
> all), and the ray-structure code repeats that threshold as a second literal.""",
    12: """The Hybrid
> lamps' cull distance is packed in eighths of a metre in 13 bits and capped at about 1 km by rounding
> down, so a lamp reaching further loses its light beyond that, a dark ring (the bridge's lamps are far
> from it; general form: all ones means "never reject", or a rounded-up half float); the world
> grid's fixed 8x4x32 cells (this item); the orthographic cluster range; and the neighbour-sharing
> rings of the direct light and the water (a fixed spacing and a water-only normal tolerance).""",
    16: """The frame counter
> every traced pass seeds from advances once per scene draw, not per view. The camp's realtime
> reflection probe draws the scene again several times a second, so the main view skips values (live
> on the camp); a second viewport, or a probe updating every frame, leaves each view only every other
> value, so the water's quad lanes visit 2 of 4 and the reflection sequence keeps one parity. General
> form: one frame index per view, kept with the view's own history state.""",
    17: """The measured-change
> filter's plane test is in metres and its floor is an amount per frame (so it follows the frame
> rate); the joint upsample's depth tolerance; the guide's texel choice through a coordinate that
> lands between texels. General form: plane tests in pixel units from the view depth and pixel angle,
> the floor judged on the change the history carries, one integer rule for a texel's representative.""",
    20: """The tile budgets:
> importance smoothed over a fixed number of frames, motion saturating at 4 pixels a frame, fixed
> material weights, dwell frames, and the AO budget read without reprojection (a moving object gets
> another tile's rays). General form: averages over exactly one jitter period (the owner's
> TemporalJitterPhase), the young share from the accumulators' own counts, reprojected reads, weights
> from each estimator's contribution.""",
    21: """Ray origins are
> lifted by fixed metres (traced hits, shadow rays, AO rays; general form: by the origin's own float
> precision); parallax step counts fixed at 28/10 and 24/8 (general form: from the texels the ray
> crosses); the terrain's wall-projection blend tuned on one cliff; the terrain weight map without
> mips; ray hits reading cutout textures at a fixed mip (general form: ray cones).""",
    23: """The reflection ray
> budget's young-share thresholds and its row-only pair counting. General form: linear in the share,
> pairs counted in both directions.""",
    24: """Extra lamps for
> young pixels use a fixed 6-frame threshold and tile; the shadow-ray fade uses fixed distances
> (300-600 m at Quality) and a visibility borrow that depends on the order lamps are visited. General
> form: extra lamps from the history's shortfall against its own memory; skipped rays by Russian
> roulette (unbiased, order-free); the fade by a lamp's share, not metres.""",
    25: """The runtime
> irradiance field: a fixed blend per sweep and hysteresis, a fixed ray budget, 1 m cells in a 24x12x24
> box (this item), a lead distance and a move threshold in metres, a direction smoothing per frame,
> and a box that snaps by a step other than its cell pitch (found by the skeptics). Also GI's bound
> width, a miss read as sky even where the ray could not have left the scene, and the
> GI clamps 4 and 32 and the unoccluded sky at bake hits (this item). General form: history kept per
> world position with change measured, budgets and cell size as owner settings or from the scene's own
> volumes, one definition of the cell, a miss as sky only when the ray left the scene.""",
    26: """The Hybrid lamps'
> baked-to-live transition bands (a fixed band in metres, in three places; general form: the field's
> own cell spacing, the narrowest transition it can hold); the bake settling, writing and giving up
> after fixed frame counts (general form: on events -- the solve for this lighting hash finished
> unchanged); the probe "moved" tolerance; volume padding; BC6H encoding by a fixed box fit (banding);
> the stamp's epsilon; the CPU probe irradiance's sample count; the 8-volume cap (this item); and the AO
> signal: a fixed blur reach and no measured-change record, so an occluder's change is caught only by
> motion.""",
    27: """The probe parallax
> proxy takes its radius from the blend radius instead of the capture; 16 probe slots. General form:
> the proxy from the capture, slots from the scene's probe count within a memory budget.""",
    30: """Audit 2 found fixed
> values in paths this item deletes (the old post-chain AO accumulate; gi_denoise's clamps, curve and
> frame cap). They go with the deletion and are not fixed separately.""",
    31: """Glass hides what is
> behind it by how bright its reflection is, not by its reflectance: the garage car's windows hide the
> interior behind bright reflections and show it behind dim ones, and the split moves with the light
> level (live, first to fix; general form: coverage from the glass's own reflectance). Also
> the sea's quad-shared refraction walks its lanes in a fixed order (live), the S5 tap tolerance in
> metres, the backdrop's sky test, and the glass panes' ray plan with fixed counts. (The mirror-ray cap
> was added this morning.)""",
    32: """Twenty-three values
> in the sea: the lamp glint memory of 16 frames and scatter memory of 4 (live, first to fix) and their
> bound; the detail normals' tile, scroll, fade distance and strength (the fade is live); the pixel footprint on the
> water, the Cox-Munk slope ceiling and the footprint saturation (live, first to fix); caustic cells in
> metres read at an undefined mip (live); the contact-foam band from screen depth instead of vertical
> depth (live); foam drift, clamp and resolution; the grid's division cap and band; the water's
> roughness; the detail tile's fixed wave fan (live); the refraction reach. General form: everything
> from the body's own dials and the wave spectrum (dispersion, Cox-Munk from the implied wind),
> footprints from the pixel, memories from the estimate's own statistics.""",
    33: """Light glows: a fixed
> side-glow share and cone edges, a back cutoff and a 1 m depth pull (live: glows seen from the side of
> spot lamps). Fog: the sky treated as FarClip away (live) and a fixed horizontal-ray threshold. General
> form: glows follow the light's own emission toward the camera; fog to the sky integrates to infinity.""",
    38: """Thin members (the
> bridge's cables) fade out at fixed distances in metres, on screen and in rays (live: flicker). General
> form: fade by projected width in pixels, the member's thickness over the pixel footprint.""",
    41: """Eleven whole-scene
> reaches written in metres (10 km in most rays, the GI reach, the bake solve's reach), and the
> terrain's wall texture scale re-derived from screen derivatives, whose rounding grows with distance
> from the world origin (live on the bridge's Cliff and Headland cameras: the wall texture jumps).
> General form: one reach from the ray structure's bounds; the known texture mapping instead of a
> derivative ratio.""",
}

NEW_ITEMS = """### RT2-44 · The raster fallback on general forms

- **What is wrong now.** Audit 2 (section 10) found about 60 fixed values in the raster recipe, the fallback when rays are off:
  - shadow maps: at most 4 local maps and one set of cascades, not ranked by what they contribute to the view; a fixed depth bias and slope; a 10% spot frustum widening; near planes, a cascade start floor, a radius quantum and a caster pull-back in metres;
  - SSR: a fixed stride, stride growth and iteration cap, a blur disc capped in pixels, a thickness and a maximum distance in metres, a fixed Hi-Z level count, and the hit snapped to a half-resolution texel centre;
  - SSGI and SSAO: biases and a distance falloff in metres, hard normal-agreement switches, fixed blur radii and depth-ratio edge stops, an SSAO rotation that never changes from frame to frame, and taps that fall off screen counted as unoccluded (a crease brightens near the frame edge);
  - voxel GI: normals transformed without the inverse-transpose, textures read at their average, the last voxel write winning, only the sun shadowed in the grid, a cone aperture and step that double-count opacity, cascades snapped finer than the level they are read at, no conservative rasterisation, a voxel size clamp in metres, and the second bounce read from last frame's grid with this frame's origins;
  - the raster water refraction: a fixed push and a foreground test in metres.
- **What changes.** Each value is replaced by the general form in section 10: pixel and texel units from the projection, the light's own size, statistics instead of thresholds, the published algorithm where one exists. One arm at a time, as in RT2-6. Per D1 the OpenGL shader set stays frozen, so these land in the Vulkan raster recipe only.
- **Owner sees.** With rays off: shadows, reflections and ambient light that behave the same at any resolution, world size and frame rate.
- **Touches.** The `ssr_*`, `ssgi_*`, `ssao_*`, `voxel*` and `shadow_depth*` shaders, `cull_casters.rvshader`, `include/voxel_cone.glsl`, `VoxelGI.cpp`, `ShadowMap.cpp/.h`, the raster shadow setup in `Scene.cpp`, the shadow pipelines in `Renderer3D.cpp`, the SSR, SSGI and voxel defaults in `PostSettings.h`, the raster refraction in `pbr_fragment.glsl`.
- **Scope.** Patch. **Depends on.** RT2-2, RT2-6. **Risk.** Low to medium: raster mode is a fallback, and each arm is judged by diff images.
- **Exit gate.**
  - Each arm: a diff image against before, in raster mode, on the garage, the bridge's three cameras and the camp, judged by the owner.
  - `check_ssao.py` and `check_ssr.py` pass.
  - The same frames at 1600x900 and 2560x1600 show the same effect widths, and a scene scaled by ten shows the same shadows.
  - A,B,B,A.
- **Size.** 4-6 sessions.
- **Closes.** Audit 2's RT2-44 rows (section 10).

### RT2-45 · The post chain on general forms

- **What is wrong now.** Audit 2 (section 10) found fixed values in post-processing:
  - auto exposure: the histogram's range is not derived from the owner's exposure limits, and light below 0.0001 is dropped from the metering (both first to fix; the second is live);
  - bloom: its threshold, knee and ceiling are in absolute brightness, not the brightness after exposure (live), and so is its outlier weight;
  - motion blur: the shutter is a fraction of a frame, so the blur length changes with the frame rate (live); "in front" is decided by 0.5 m of depth (live); a fixed tap count;
  - depth of field: a fixed 48 taps, and the bokeh cap in pixels, so it changes with resolution;
  - SMAA: a diagonal margin measured as doing nothing;
  - the sky: a fixed gradient cube size and a dither compensation constant; the clear colour for a colour sky differs from what rays see.
- **What changes.** The exposure range from the owner's own limits; bloom and its weights in exposed units; the shutter as a time in seconds; depth tests without metres; tap counts from the reach in pixels; the SMAA diagonal pass made the reference one or removed; one background for rays and raster. One arm at a time, judged by the owner.
- **Owner sees.** Bloom and exposure that respond the same in dim and bright scenes; motion blur that looks the same at 30 and 120 FPS; depth of field the same at any resolution.
- **Touches.** The `tonemap`, `bloom_*`, `luminance_*`, `motionblur_*`, `dof_*`, `smaa_*` and `sky` shaders; `PostProcess.cpp`, `PostSettings.h`, `AutoExposure.cpp`, `Skybox.cpp`, `FrameGraphBuilder.h` (the clear colour).
- **Scope.** Patch. **Depends on.** RT2-2. **Risk.** Low: each arm is a look change the owner judges.
- **Exit gate.**
  - `check_auto_exposure.py`, `check_motion_blur.py` (at two frame rates), `check_depth_of_field.py` and `check_smaa.py` pass.
  - Diff images on the three scenes, judged by the owner.
  - Bloom at two light scales, with exposure matched, gives the same picture.
- **Size.** 3-4 sessions.
- **Closes.** Audit 2's RT2-45 rows (section 10).

"""


def replace(a, b):
    global t
    assert t.count(a) == 1, (t.count(a), a[:90])
    t = t.replace(a, b)


def after_heading(heading, note):
    global t
    assert t.count(heading) == 1, heading
    i = t.index(heading)
    j = t.index('\n', i) + 1
    while t[j:j + 1] == '\n' or t[j:j + 2] == '> ':
        j = t.index('\n', j) + 1
    t = t[:j] + note + '\n\n' + t[j:]


for n, note in NOTES.items():
    after_heading(HEAD[n], A2 + note)

# the new items go after RT2-43, before section 5
replace('\n## 5. Milestones and decisions', '\n' + NEW_ITEMS.rstrip('\n') + '\n\n## 5. Milestones and decisions')

# milestones, totals, the short version
replace("- **Then scale** (RT2-34 to RT2-43): a GPU-resident scene, a ray-tracing world module, a job system, "
        "GPU-driven draws, levels of detail, scattered instances such as foliage, streaming, and large-world precision.",
        "- **Then scale** (RT2-34 to RT2-43): a GPU-resident scene, a ray-tracing world module, a job system, "
        "GPU-driven draws, levels of detail, scattered instances such as foliage, streaming, and large-world precision.\n"
        "- **Last, the fallback and the post chain** (RT2-44 and RT2-45): the raster mode's and "
        "post-processing's fixed values replaced by general forms (audit 2, section 10).")
replace("- **Size.** 44 items, roughly 228 to 293 working sessions", "- **Size.** 46 items, roughly 235 to 303 working sessions")
i = t.index('| M6 Scale | RT2-34 to RT2-43 |')
j = t.index('\n', i)
t = t[:j] + ("\n| M7 The fallback and the post chain | RT2-44 to RT2-45 | 7-10 | Raster mode and post-processing "
             "on general forms, from audit 2 | none | nothing |") + t[j:]
replace("Total: about 228 to 293 working sessions.", "Total: about 235 to 303 working sessions.")

# section 10: audit 1's closing paragraph now points at audit 2, then audit 2 itself
replace("""**Not yet audited this way:** the direct light, bounce light, AO, the lit shader (`pbr_fragment.glsl`, apart from the two rows above), water, glass, post-processing and the bake. Each item that rewrites one of those passes audits its fixed values the same way before it starts, and adds what it finds to this table.
""", """**The rest of the renderer** was audited the same way on the same day: audit 2, below.

""" + AUDIT2)

io.open(P, 'w', encoding='utf-8', newline='').write(t.replace('\n', eol))
print('ok: %d rows (%d duplicates folded), %d live, %d first to fix, %d refuted, %d notes' %
      (value_rows, dups, live, first, refuted, len(NOTES)))
