# -*- coding: utf-8 -*-
"""Why the chrome cube reflects black, and the before/after of the fix (2026-09-15, evening).

The garage box -- floor, ceiling and all four walls are one mesh, `Cube`, material
underground_garage_pbr_45_pbr_cube_0.rmat -- is *Metallic: 1*. A reflection ray's hit is
shaded Lambert only (TraceSurface: `diffuse = albedo * (1 - metallic)`, ShadeTraced adds the
field, ambient and probe *to that diffuse*), so a hit on any metal is exactly black: the wall
behind the camera, which is what a flat mirror facing the camera shows, comes back as nothing.
The raster lights the same wall through its specular lobe (the lamps' highlights, the field's
dominant lamp, the probe through the split-sum term) and that is the grey-blue the reverse
view shows.

  diag      stage the trace to write what each cube ray struck: the hit's metallic, its albedo
            luma, the struck instance's identity; the stopped roughness-0 cube at the owner's shot
  before    the pictures with the shaders as they stand: cube at roughness 0 and 0.12, stopped
            (frames 150-169) and moving (116-124); the chrome sphere the same; the parked garage
            (frames 150-189); and the view straight back from the camera (the reference)
  after     the same set, tagged `after`, once the fix is in the source shaders
  sheets    the comparison sheets: BEFORE | AFTER | REFERENCE, brightened, circled, captioned

Usage: rt15_cube_metal.py diag|before|after|sheets
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402
import rt15_items as it  # noqa: E402
import rt15_cube_sweep as sw  # noqa: E402
import rt15_sphere as sp  # noqa: E402

stage_run, s2 = it.stage_run, it.s2
N = '\n'
INC = 'include/pbr_fragment.glsl'
TRACE = 'reflection_trace.rvshader'
OUT = os.path.join(stage_run.ROOT, 'build', 'rt15', 'metal')
FT = 0.0166
K = 120
REVERSE_CAM = '-2.3,1.49,20,11,180,0'
s2.CAMS['reverse'] = REVERSE_CAM
FACE = sw.FACE   # y0, y1, x0, x1 of the cube's face at the owner's shot

stage_run.VARIANTS['hitmat'] = [
    (INC, "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N,
          "float g_DiagMetal = -1.0; float g_DiagAlbedo = -1.0; float g_DiagIdentity = -1.0;" + N
          + "// `reach` is how far the ray may travel, in world metres. A reflection wants" + N),
    (INC, "\tvec3 diffuse = albedo * (1.0 - metallic);" + N,
          "\tvec3 diffuse = albedo * (1.0 - metallic);" + N
          + "\tg_DiagMetal = metallic; g_DiagAlbedo = dot(albedo, vec3(0.2126, 0.7152, 0.0722));"
          + " g_DiagIdentity = float(hit.Identity);" + N),
    (TRACE, "\to_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelled);" + N,
            "\to_Reflection = vec4(hit.Missed ? -1.0 : g_DiagMetal, g_DiagAlbedo, g_DiagIdentity, travelled);" + N),
]
CROP = (640, 300, 220, 120)   # x, y, w, h: the stopped cube's face

# **The shading as it was, staged from the fixed source**: the reflection pass without its
# hit-specular define, and its hits lit by push slot Trace.x (always zero, the sky) again.
# Bit-identical to this morning's build on the cube's settled frames (checked by `identity`),
# which is what lets BEFORE and AFTER come from one run of one script.
stage_run.VARIANTS['nospec'] = [
    (TRACE, "#define RV_HIT_SPECULAR" + N, ""),
    (TRACE, "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N, "\tconst float hitProbe = u_Lamps.Trace.x;" + N),
    # And the lit shader's own in-line rays (the car's glass reflects through them).
    (INC, "#if !defined(RV_TRACE_ONLY) && !defined(RV_HIT_SPECULAR)" + N + "#define RV_HIT_SPECULAR" + N + "#endif" + N, ""),
]


def diag():
    os.makedirs(OUT, exist_ok=True)
    try:
        sw.with_roughness(0.0)
        s2.arms_at('owner', [('r15m_diag', 'hitmat', ['--capture-signals=reflections,crop=%d:%d:%d:%d' % CROP],
                              dict(frames=2, first=150, cube=(-9.0, 3.0, K * FT)))])
    finally:
        stage_run.CUBE_ENTITY = sw.BASE
    base = os.path.join(stage_run.SHOTS, 'rt5b_r15m_diag_reflections%d_crop.npy')
    pic, ident = np.load(base % 0)[-1], np.load(base % 4)[-1]
    ids = ident[..., 0]
    cube = np.abs(ids - 278.0) < 0.5
    if not cube.any():
        cube = ids > 200
    metal, albedo, struck = pic[..., 0][cube], pic[..., 1][cube], pic[..., 2][cube]
    print('cube texels: %d' % cube.sum())
    print('rays that missed: %.1f%%' % ((metal < -0.5).mean() * 100))
    hit = metal >= -0.5
    print('hit metallic: median %.3f, share at 1.0: %.1f%%, share below 0.5: %.1f%%'
          % (np.median(metal[hit]), (metal[hit] > 0.99).mean() * 100, (metal[hit] < 0.5).mean() * 100))
    print('hit albedo luma: median %.3f  p10 %.3f  p90 %.3f' % (np.median(albedo[hit]), np.percentile(albedo[hit], 10), np.percentile(albedo[hit], 90)))
    print('hit diffuse (albedo x (1 - metallic)) median %.4f' % np.median(albedo[hit] * (1.0 - metal[hit])))
    ids_hit, counts = np.unique(np.round(struck[hit]), return_counts=True)
    order = np.argsort(-counts)
    print('struck identities (folded entity ids), most common first:', ', '.join('%d x%d' % (ids_hit[i], counts[i]) for i in order[:8]))


# **STATE 1, staged rather than edited** (owner's hold on source changes, 2026-09-15 evening):
# the hit-specular fix with hits still lit by push slot Trace.x (always zero, the sky). The
# only line between STATE A and STATE 1 that reaches a pixel is the probe choice in the two
# passes; the include's ProbeSlotAt helper existed in neither run's picture (unused there).
stage_run.VARIANTS['state1'] = [
    (TRACE, "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N, "\tconst float hitProbe = u_Lamps.Trace.x;" + N),
    ('water_trace.rvshader', "\tconst float hitProbe = ProbeSlotAt(hit.Position);" + N, "\tconst float hitProbe = u_Lamps.Trace.x;" + N),
]
ARMS = {'before': ('r15m_b', 'nospec'), 'after': ('r15m_a', 'ship'), 'state1': ('r15s1', 'state1')}


def render(arm):
    tag, variant = ARMS[arm]
    try:
        for r in (0.0, 0.12):
            sw.with_roughness(r)
            rt = '%s_c%02d' % (tag, int(round(r * 100)))
            s2.arms_at('owner', [
                (rt + '_ref', variant, [], dict(frames=20, first=150, mean_from=150, cube=(-9.0, 3.0, K * FT))),
                (rt + '_mov', variant, [], dict(frames=9, first=K - 4, cube=(-9.0, 3.0, 6.0)))])
    finally:
        stage_run.CUBE_ENTITY = sw.BASE
    # The chrome sphere (roughness 0.05), stopped where frame 120 has it, and crossing.
    sp.burst(tag + '_sph_ref', K * FT, 150, 20, variant)
    sp.burst(tag + '_sph_mov', 6.0, K - 4, 9, variant)
    s2.arms_at('owner', [(tag + '_parked', variant, [], dict(frames=40, first=150, mean_from=150))])
    s2.arms_at('reverse', [(tag + '_reverse', variant, [], dict(frames=2, first=150))])


def identity():
    """The staged `nospec` arm against this morning's build (rt5b_r15m_b_* rendered before any
    shader was touched, now under rt5b_r15m_old_*): the same frames, bit for bit, or not."""
    import shutil
    for f in os.listdir(stage_run.SHOTS):
        if f.startswith('rt5b_r15m_b_c00_ref_'):
            shutil.copyfile(os.path.join(stage_run.SHOTS, f), os.path.join(stage_run.SHOTS, f.replace('r15m_b_', 'r15m_old_')))
    try:
        sw.with_roughness(0.0)
        s2.arms_at('owner', [('r15m_id_ref', 'nospec', [], dict(frames=20, first=150, cube=(-9.0, 3.0, K * FT)))])
    finally:
        stage_run.CUBE_ENTITY = sw.BASE
    worst = 0
    for k in range(150, 170):
        a = np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_r15m_old_c00_ref_%d.png' % k)).convert('RGB'), dtype=int)
        b = np.asarray(Image.open(os.path.join(stage_run.SHOTS, 'rt5b_r15m_id_ref_%d.png' % k)).convert('RGB'), dtype=int)
        d = np.abs(a - b)
        worst = max(worst, int(d.max()))
        print('frame %d: largest pixel difference %d, pixels differing %.4f%%' % (k, d.max(), (d > 0).any(axis=2).mean() * 100))
    print('nospec against this morning\'s build: worst %d' % worst)


# --- the sheets ------------------------------------------------------------------------------

def _font(size):
    for name in ('arial.ttf', 'C:/Windows/Fonts/arial.ttf', 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def brighten(a, gain):
    """The dark garage lifted so it can be seen: a gain on the linear-ish 8-bit values, clipped."""
    return np.clip(a * gain, 0, 255)


def sheet(path, header, panels, captions, circle=None, gain=3.0, scale=2):
    """Panels side by side, each with a caption under it and a yellow circle drawn at `circle`
    (cx, cy, r in panel pixels, before scaling). One header line above."""
    font, small = _font(22), _font(17)
    tiles = []
    for p in panels:
        im = Image.fromarray(brighten(np.asarray(p, dtype=float), gain).astype(np.uint8))
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        if circle:
            cx, cy, r = circle
            d = ImageDraw.Draw(im)
            d.ellipse([(cx - r) * scale, (cy - r) * scale, (cx + r) * scale, (cy + r) * scale], outline=(255, 230, 0), width=3)
        tiles.append(im)
    gap, top, bottom = 12, 44, 80
    w = sum(t.width for t in tiles) + gap * (len(tiles) - 1)
    h = max(t.height for t in tiles)
    out = Image.new('RGB', (w, h + top + bottom), (20, 20, 20))
    draw = ImageDraw.Draw(out)
    draw.text((8, 10), header, fill=(255, 255, 255), font=font)
    x = 0
    for t, cap in zip(tiles, captions):
        out.paste(t, (x, top))
        y = top + h + 6
        for line in _wrap(cap, t.width, small, draw):
            draw.text((x + 4, y), line, fill=(230, 230, 230), font=small)
            y += 20
        x += t.width + gap
    out.save(path)
    return path


def _wrap(text, width, font, draw):
    words, lines, cur = text.split(), [], ''
    for wd in words:
        trial = (cur + ' ' + wd).strip()
        if draw.textlength(trial, font=font) > width - 8 and cur:
            lines.append(cur)
            cur = wd
        else:
            cur = trial
    if cur:
        lines.append(cur)
    return lines


img = lambda tag, k: np.asarray(Image.open(os.path.join(stage_run.SHOTS, '%s_%d.png' % (tag, k))).convert('RGB'), dtype=float)
mean_npy = lambda tag: np.load(os.path.join(stage_run.OUT, tag + '.npy'))


def face_crop(a, pad=40):
    y0, y1, x0, x1 = FACE
    return a[y0 - pad:y1 + pad, x0 - pad:x1 + pad]


def reverse_patch(a):
    """The wall straight behind the camera, mirrored left to right -- what a flat mirror facing
    the camera shows. A 2.4 m cube 14 m from the eye mirrors about a 4 m patch of the wall 27 m
    behind the eye, which in the reverse view (target 18 m off) is roughly 300 by 160 pixels
    about the centre; the crop is a little wider than that."""
    h, w = a.shape[:2]
    patch = a[h // 2 - 100:h // 2 + 100, w // 2 - 150:w // 2 + 150]
    return patch[:, ::-1]


def sheets(arm='r15m_a', out=None, label='AFTER (STATE A)', after_note=None):
    """BEFORE (nospec) | <arm> | REFERENCE for the cube; BEFORE | <arm> for the sphere and the
    parked garage. `arm` is the tag prefix of the run to show against the nospec BEFORE."""
    out = out or OUT
    os.makedirs(out, exist_ok=True)
    after_note = after_note or ('%s: the face shows the lit wall behind the camera, mirrored, the way the sphere and the raster already show it.' % label)
    made = []
    y0, y1, x0, x1 = FACE
    pad = 40
    circ = ((x1 - x0) // 2 + pad, (y1 - y0) // 2 + pad, 75)
    # How bright the face is against the wall it mirrors, in the un-brightened frames: the
    # face's inner 60% against the reverse view's centre patch.
    L = it.L
    ref_patch = L(reverse_patch(img('rt5b_r15m_b_reverse', 151)))
    for r in (0.0, 0.12):
        rr = '%02d' % int(round(r * 100))
        for a in ('r15m_b', arm):
            face = L(mean_npy('%s_c%s_ref' % (a, rr)))[y0 + 24:y1 - 24, x0 + 44:x1 - 44]
            print('roughness %-4g %-16s face luma mean %6.1f (p10 %5.1f, p90 %5.1f)   wall behind the camera, mirrored patch: mean %6.1f (p10 %5.1f, p90 %5.1f)'
                  % (r, 'before' if a == 'r15m_b' else label, face.mean(), np.percentile(face, 10), np.percentile(face, 90),
                     ref_patch.mean(), np.percentile(ref_patch, 10), np.percentile(ref_patch, 90)))
    for r in (0.0, 0.12):
        rr = '%02d' % int(round(r * 100))
        for state, get in (('stopped and settled', lambda t: mean_npy('%s_c%s_ref' % (t, rr))),
                           ('moving (frame 121, 3 m/s)', lambda t: img('rt5b_%s_c%s_mov' % (t, rr), 121))):
            before, after = face_crop(get('r15m_b')), face_crop(get(arm))
            ref = reverse_patch(img('rt5b_r15m_b_reverse', 151))
            ref = ref[:before.shape[0], :before.shape[1]] if ref.shape[0] >= before.shape[0] else ref
            name = os.path.join(out, 'cube_r%s_%s.png' % (rr, 'stopped' if 'stopped' in state else 'moving'))
            made.append(sheet(name, 'Chrome cube, roughness %g, %s -- look inside the circle at the cube face (all three brightened x3)' % (r, state),
                              [before, after, ref],
                              ['BEFORE: the face is black (r=0) or speckles over black (r=0.12): every ray that reached the metallic wall behind the camera came back as nothing.',
                               after_note,
                               'REFERENCE: the wall straight behind the camera (build/rt15/reverse_view.png camera), flipped left-right as a mirror shows it.'],
                              circle=circ))
    # The sphere, stopped and moving.
    sy0, sy1, sx0, sx1 = 250, 470, 560, 900
    scirc = ((sx1 - sx0) // 2, (sy1 - sy0) // 2, 100)
    for state, get in (('stopped and settled', lambda t: np.mean([img('%s_sph_ref' % t, k) for k in range(150, 170)], axis=0)),
                       ('moving (frame 121)', lambda t: img('%s_sph_mov' % t, 121))):
        before, after = get('r15m_b')[sy0:sy1, sx0:sx1], get(arm)[sy0:sy1, sx0:sx1]
        name = os.path.join(out, 'sphere_%s.png' % ('stopped' if 'stopped' in state else 'moving'))
        made.append(sheet(name, 'Chrome sphere, %s -- look inside the circle (brightened x3)' % state, [before, after],
                          ['BEFORE: the sphere as it was.', '%s: the same sphere with this state\'s hit shading.' % label],
                          circle=scirc, scale=2))
    # The parked garage: whole frame, and the difference.
    b, a = mean_npy('r15m_b_parked'), mean_npy('%s_parked' % arm)
    diff = np.abs(a - b).max(axis=2)
    print('parked garage, mean of frames 150-189: |%s - before| mean %.2f levels, max %.0f, pixels over 8: %.2f%%'
          % (label, diff.mean(), diff.max(), (diff > 8).mean() * 100))
    amp = np.repeat(np.clip(diff * 4.0, 0, 255)[..., None], 3, axis=2)
    name = os.path.join(out, 'parked_garage.png')
    made.append(sheet(name, 'The parked garage (owner\'s shot, mean of 40 frames) -- before, %s, and where they differ (|difference| x4, white = changed)' % label,
                      [b[:860], a[:860], amp[:860]],
                      ['BEFORE: as shipped this morning.', '%s: what changes is what the floor and the car reflect of the metallic room.' % label,
                       'DIFFERENCE x4: brighter means a bigger change.'], circle=None, gain=2.0, scale=1))
    for m in made:
        print(m)


# --- the true mirror reference ------------------------------------------------------------------
#
# What a perfect flat mirror in the cube's front face shows is the scene seen from the eye
# mirrored in that face's plane, flipped left to right. The owner's shot has the eye at
# (-2.3, 1.488, 8.974) looking 4 degrees down along -z (focus (-2.3, 0.72, -2), distance 11,
# yaw 0, pitch 4: forward = rotY(-yaw) rotX(-pitch) (0, 0, -1), eye = focus - forward * 11).
# The cube is the unit primitive at scale 1.2 centred at z = -6, so its front face is the
# plane z = -5.4 and the mirrored eye sits at z = -10.8 - 8.974 = -19.774, looking 4 degrees
# down along +z: focus = eye + forward' * 11 = (-2.3, 0.72, -8.8), yaw 180, pitch 4. The
# cube itself is left out of the reference (a mirror does not show itself), and the same
# pixel rectangle the face covers in the shot covers the reference's mirror image of it.
MIRROR_CAM = '-2.3,0.72,-8.8,11,180,4'
s2.CAMS['mirror'] = MIRROR_CAM


def mirror():
    s2.arms_at('mirror', [('r15m_mirror', 'ship', [], dict(frames=2, first=150))])


def face_rect(with_cube, without_cube, margin=2):
    """The cube's footprint inside FACE: where the frame with the cube differs from the one
    without it by more than 24 levels, as a tight rectangle."""
    y0, y1, x0, x1 = FACE
    d = np.abs(with_cube - without_cube).max(axis=2)[y0:y1, x0:x1] > 24
    ys, xs = np.nonzero(d)
    return (y0 + ys.min() + margin, y0 + ys.max() - margin, x0 + xs.min() + margin, x0 + xs.max() - margin)


def mirror_sheet(arm='r15m_a', label='run 8 (STATE A)'):
    os.makedirs(OUT, exist_ok=True)
    L = it.L
    ref = img('rt5b_r15m_mirror', 151)[:, ::-1]           # the mirror image
    nocube = mean_npy('r15m_b_parked')                    # the shot without the cube, for the footprint
    made = []
    for rr, r in (('00', 0.0), ('12', 0.12)):
        stopped = mean_npy('%s_c%s_ref' % (arm, rr))
        moving = img('rt5b_%s_c%s_mov' % (arm, rr), 121)
        fy0, fy1, fx0, fx1 = face_rect(stopped, nocube)
        pad = 24
        crop = lambda a: a[fy0 - pad:fy1 + pad, fx0 - pad:fx1 + pad]
        inner = lambda a: L(a)[fy0 + 8:fy1 - 8, fx0 + 8:fx1 - 8]
        e_stop, e_mov = np.abs(inner(stopped) - inner(ref)), np.abs(inner(moving) - inner(ref))
        print('roughness %-4g face rect y %d..%d x %d..%d: luma stopped %.1f, moving %.1f, true mirror %.1f; '
              '|face - mirror| stopped mean %.1f (>16: %.0f%%), moving mean %.1f (>16: %.0f%%)'
              % (r, fy0, fy1, fx0, fx1, inner(stopped).mean(), inner(moving).mean(), inner(ref).mean(),
                 e_stop.mean(), (e_stop > 16).mean() * 100, e_mov.mean(), (e_mov > 16).mean() * 100))
        circ = ((fx1 - fx0) // 2 + pad, (fy1 - fy0) // 2 + pad, max(fx1 - fx0, fy1 - fy0) // 2 + 6)
        name = os.path.join(OUT, 'mirror_r%s.png' % rr)
        made.append(sheet(name, 'Chrome cube, roughness %g, %s against a true mirror -- look inside the circle (all brightened x3)' % (r, label),
                          [crop(stopped), crop(moving), crop(ref)],
                          ['CUBE, stopped and settled (mean of 20 frames): what the engine\'s reflection ray shows on the face.',
                           'CUBE, moving at 3 m/s (frame 121).',
                           'TRUE MIRROR: the scene rendered from the eye mirrored in the cube\'s face plane, cube removed, flipped left-right -- what a perfect mirror there would show.'],
                          circle=circ, scale=4))
    for m in made:
        print(m)


if __name__ == '__main__':
    {'diag': diag, 'before': lambda: render('before'), 'after': lambda: render('after'), 'sheets': sheets,
     'identity': identity, 'mirror': mirror, 'mirror_sheet': mirror_sheet,
     'state1': lambda: render('state1'),
     'sheets_state1': lambda: sheets('r15s1', os.path.join(OUT, 'state1'), 'STATE 1 (run 4 again)',
                                     'STATE 1 (run 4 re-rendered): hit specular on, but hits lit by probe slot 0 (the sky): the face is faintly lit dark blue-grey.')
     }[sys.argv[1]]()
