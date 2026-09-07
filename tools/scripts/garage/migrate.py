"""One-time migration: the white studio becomes the underground garage.

Not a generator -- showroom.rage is hand-owned after this, the way the bridge
is. Re-running it against the migrated scene would find no room to delete.
"""
import sys, io, re, zlib
sys.path.insert(0, r'C:\Users\ism19\Code\RageV\tools\scripts\garage')
import scene_util as S

SCENE = r'C:\Users\ism19\Code\RageV\SampleProject\assets\scenes\showroom.rage'
SUB = sys.argv[1] if len(sys.argv) > 1 else \
    r'C:\Users\ism19\Code\RageV\tools\scripts\data\garage_baked.yaml'
GARAGE_Z = 14.0          # the graffiti wall lands ~13 m behind the car

# The hand-built studio, every piece of it.
ROOM = set([
 'Floor', 'Ceiling', 'Left Wall', 'Right Wall', 'Front Wall',
 'Portal Pier Left', 'Portal Pier Right', 'Portal Header',
 'Portal Reveal Left', 'Portal Reveal Right', 'Portal Reveal Head',
 'Skirting Left', 'Skirting Right', 'Skirting Back Left', 'Skirting Back Right',
 'Luminaire', 'Luminaire Lit Block', 'Luminaire Reveal Left',
 'Luminaire Reveal Right', 'Luminaire Reveal Front', 'Luminaire Reveal Back',
 'Panel Light 00', 'Panel Light 01', 'Panel Light 02', 'Panel Light 10',
 'Panel Light 11', 'Panel Light 12', 'Panel Light 20', 'Panel Light 21',
 'Panel Light 22', 'Key Light', 'Kicker Left', 'Kicker Right',
 'Bay Floor', 'Bay Ceiling', 'Bay Back Wall', 'Bay Wall Left', 'Bay Wall Right',
 'Bay Downlight 0', 'Bay Downlight 1', 'Bay Downlight 2', 'Bay Downlight 3',
 'Bay Downlight Light 0', 'Bay Downlight Light 1', 'Bay Downlight Light 2',
 'Bay Downlight Light 3',
 'Convex Mirror', 'Convex Mirror Rim', 'Charge Post', 'Charge Post Lens',
])

head, blocks, N = S.load(SCENE)
by_tag = dict((S.tag(b), b) for b in blocks)
used_ids = set(S.eid(b) for b in blocks)

kept = [b for b in blocks if S.tag(b) not in ROOM]
print('dropped %d studio entities, kept %d' % (len(blocks) - len(kept), len(kept)))

# --- the garage subtree ----------------------------------------------------
sd = io.open(SUB, 'r', encoding='utf-8', newline='').read()
SN = '\r\n' if sd.count('\r\n') else '\n'
gblocks, cur = [], None
for ln in sd.split(SN):
    if ln.startswith('  - EntityID:'):
        if cur is not None:
            gblocks.append(cur)
        cur = [ln]
    elif cur is not None:
        cur.append(ln)
if cur:
    gblocks.append(cur)
while gblocks and gblocks[-1] and gblocks[-1][-1] == '':
    gblocks[-1].pop()

# The exporter's own camera is a transform with nothing hanging off it.
gblocks = [b for b in gblocks if S.tag(b) != 'Camera']
groot = [b for b in gblocks if S.parent(b) is None][0]
for i, ln in enumerate(groot):
    if ln.strip().startswith('Position:'):
        groot[i] = '      Position: [0, 0, %g]' % GARAGE_Z
        break
for b in gblocks:
    assert S.eid(b) not in used_ids, 'id collision %s' % S.eid(b)
print('garage: %d entities, root at z=%+g' % (len(gblocks), GARAGE_Z))

# --- new entities ----------------------------------------------------------
def new_id(name):
    h = zlib.crc32(name.encode()) & 0xFFFFFFFF
    i = str(7810000000000000000 + h)
    assert i not in used_ids
    used_ids.add(i)
    return i

# Half baked, the owner's word (2026-09-05, said twice): the engine's own
# `Half bake` -- Unreal's Stationary -- the lamp's bounce in the field, its
# direct light and shadows live. Nothing of a Realtime lamp is ever in the
# field. The three product-shot lamps (key and kickers) stay Realtime: they
# are switched by the mode button, and a baked lamp cannot be switched.
def light(tag, pos, colour, intensity, rng, kind='Point', shadows=False,
          cone=(20, 30), mobility='Half bake'):
    return ['  - EntityID: ' + new_id(tag),
            '    TagComponent:',
            '      Tag: ' + tag,
            '    TransformComponent:',
            '      Position: [%g, %g, %g]' % pos,
            '      Rotation: [0, 0, 0]',
            '      Scale: [1, 1, 1]',
            '    LightComponent:',
            '      Type: ' + kind,
            '      Color: [%g, %g, %g]' % colour,
            '      Intensity: %g' % intensity,
            '      Range: %g' % rng,
            '      InnerCone: %g' % cone[0],
            '      OuterCone: %g' % cone[1],
            '      CastShadows: ' + ('true' if shadows else 'false'),
            '      Mobility: ' + mobility]

TUBE = (0.342, 0.631, 1.0)     # the tubes' own emission colour (light / light2)
WARM = (1.0, 0.60, 0.26)

added = []
# **A spot light under every tube** (owner, 2026-09-05 night: "just add spot
# lights"; line lights are a separate conversation). The bars are read from
# the export itself so a re-export moves the lights with them: every node
# named "Bottom light bars.*" whose mesh spans more than two metres is a
# tube; the rest are its end caps. A light's forward axis is -Z, so a
# rotation of -90 degrees about X aims it straight down; the cone is wide
# because a bare tube under a shallow reflector lights most of a hemisphere.
import json, os
import numpy as np
GLTF = os.path.join(os.path.dirname(os.path.dirname(SUB)), '..', '..', 'SampleProject', 'assets',
                    'models', os.path.basename(SUB)[:-5], 'underground_garage_pbr.gltf')
GLTF = os.path.normpath(GLTF)
g = json.load(io.open(GLTF, 'r', encoding='utf-8'))
def _trs(n):
    T = np.array(n.get('translation', [0, 0, 0]), float); S = np.array(n.get('scale', [1, 1, 1]), float)
    x, y, z, w = n.get('rotation', [0, 0, 0, 1])
    R = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                  [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    return T, R, S
tubes = []
for n in g['nodes']:
    if not n.get('name', '').startswith('Bottom light bars'):
        continue
    lo = np.array([1e9] * 3); hi = -lo
    for prim in g['meshes'][n['mesh']]['primitives']:
        a = g['accessors'][prim['attributes']['POSITION']]
        lo = np.minimum(lo, a['min']); hi = np.maximum(hi, a['max'])
    T, R, S = _trs(n)
    corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
    w = (R @ (corners * S).T).T + T
    if (w.max(0) - w.min(0))[0] > 2.0:
        c = (w.max(0) + w.min(0)) / 2
        tubes.append((n['name'], (round(float(c[0]), 2), round(float(c[1]) - 0.1, 2),
                                  round(float(c[2]) + GARAGE_Z, 2))))
assert len(tubes) == 20, 'expected 20 tubes, found %d' % len(tubes)
for name, pos in tubes:
    # 20 judged from the first render: at 10 the far walls read 25.9 against
    # the still's 52.6 (engine_pbr7); the cone spills to the pillars.
    b = light('Tube ' + name.split('.')[-1], pos, TUBE, 20.0, 16.0, kind='Spot',
              shadows=True, cone=(60, 88))
    b[5] = '      Rotation: [-1.5708, 0, 0]'
    added.append(b)

# **The .blend's own two lights**, which no earlier import carried (lights_blend.py):
# an area light 1.9 x 0.32 m at 785 W just under the ceiling above the near
# floor, and a 1000 W point by the far wall. There is no area light type; a
# wide spot in its place, pointing down, stands in for it.
b = light('Blend Area Light', (-6.12, 6.4, 18.65 + GARAGE_Z), (0.589, 0.803, 1.0), 40.0, 26.0,
          kind='Spot', shadows=True, cone=(60, 85))
b[5] = '      Rotation: [-1.5708, 0, 0]'
added.append(b)
added.append(light('Blend Point Light', (4.65, 2.84, -12.87 + GARAGE_Z), (1.0, 1.0, 1.0), 40.0, 22.0,
                   shadows=True))
# The one warm fixture on the back wall, and the only warm thing in frame.
added.append(light('Wall Fixture', (3.6, 2.45, -12.2), WARM, 5.0, 7.0))

# Mode 2, the product shot: a key over the car and two kickers. Off in mode 1,
# which is the room as the reference renders it.
added.append(light('Key Light', (0.0, 5.2, 1.2), (0.95, 0.97, 1.0), 0.0, 14.0,
                   kind='Spot', shadows=True, cone=(26, 46), mobility='Realtime'))
added.append(light('Kicker Left', (-3.4, 1.5, -2.6), TUBE, 0.0, 7.0, mobility='Realtime'))
added.append(light('Kicker Right', (3.4, 1.5, -2.6), TUBE, 0.0, 7.0, mobility='Realtime'))
print('added %d lights' % len(added))

# --- the probe and the volume, resized for a room 25 m across --------------
def field(b, comp, key, value):
    inside = False
    for i, ln in enumerate(b):
        if re.match(r'    [A-Za-z]+Component:', ln):
            inside = ln.strip() == comp + ':'
        elif inside and re.match(r'\s+%s:' % re.escape(key), ln):
            b[i] = re.sub(r'(\s+%s:).*' % re.escape(key), r'\1 ' + value, ln)
            return True
    raise AssertionError('%s.%s not found on %s' % (comp, key, S.tag(b)))

# The room is 47 x 6.8 x 54 m with its centre at z = GARAGE_Z. The camera
# that matters now stands at the near end (solve_camera.py), so the probe
# and the field cover the whole room rather than the lit far end: a
# reflection ray that hits a Static surface reads the field for its light,
# and outside the box that is nothing -- the floor's mirror came back black
# for exactly that reason. One metre spacing keeps the field at ~18k cells.
probe = by_tag['Showroom Probe']
field(probe, 'TransformComponent', 'Position', '[0, 1.6, %g]' % (GARAGE_Z + 4.0))
field(probe, 'ReflectionProbeComponent', 'FarClip', '80')
field(probe, 'ReflectionProbeComponent', 'Influence', '42')

vol = by_tag['Irradiance Volume']
field(vol, 'TransformComponent', 'Position', '[0, 3.4, %g]' % GARAGE_Z)
field(vol, 'IrradianceVolumeComponent', 'Extents', '[24, 3.5, 27.5]')
field(vol, 'IrradianceVolumeComponent', 'Spacing', '1')

# --- the car, on the near side of the room -----------------------------------
# The page's wide still looks down the room from z = +29.9 (solve_camera.py).
# Its four lamps are separate roots placed relative to it, so they move by
# the same offset; the orbit script's target follows, at its longest reach,
# so the in-game view frames the car from the still's direction.
# Under the tubes (owner, 2026-09-05 night: "it should be under the tube light
# section"): the tube field spans scene z -9.8..5.7 and x -8.6..8.0, the car
# sits at its middle, on the still's camera axis, nose to the camera.
CAR = (-2.3, 0.0, -2.0)
for name in ('porsche_992_gt3_r', 'Headlamp Beam Left', 'Headlamp Beam Right',
             'Tail Glow Left', 'Tail Glow Right'):
    b = by_tag[name]
    for i, ln in enumerate(b):
        m = re.match(r'(\s+Position: )\[([^\]]*)\]', ln)
        if m:
            x, y, z = (float(v) for v in m.group(2).split(','))
            b[i] = '%s[%g, %g, %g]' % (m.group(1), x + CAR[0], y + CAR[1], z + CAR[2])
            break
    else:
        raise AssertionError('no Position on ' + name)
cam = by_tag['Showroom Camera']
for key, value in (('TargetX', CAR[0]), ('TargetY', 0.72 + CAR[1]), ('TargetZ', CAR[2]),
                   ('Distance', 11.0), ('MaxDistance', 12.0), ('Pitch', 4.0)):
    for i, ln in enumerate(cam):
        if ln.strip().startswith(key + ':'):
            cam[i] = '        %s: %g' % (key, value)
            break
    else:
        raise AssertionError('no %s on the camera script' % key)
print('car at %s, camera target follows' % (CAR,))

# --- the two modes, pointed at what exists now -----------------------------
mode = by_tag['Mode Button']
for i, ln in enumerate(mode):
    if ln.strip().startswith('StudioFills:'):
        mode[i] = "        StudioFills: 'Key Light,Kicker Left,Kicker Right'"
    elif ln.strip().startswith('ShowroomFills:'):
        mode[i] = "        ShowroomFills: ''"

# --- environment: an underground room has no sky ---------------------------
head = head.replace('AmbientIntensity: 0.04', 'AmbientIntensity: 0.012')
for k, v in (('SkyHorizon', '[0.012, 0.014, 0.019]'),
             ('SkyZenith', '[0.01, 0.012, 0.017]'),
             ('SkyGround', '[0.008, 0.009, 0.012]'),
             ('AmbientColor', '[0.42, 0.52, 0.68]')):
    head = re.sub(r'(  %s:).*' % k, r'\1 ' + v, head)

body = []
for b in kept + gblocks + added:
    body.extend(b)
out = head + N.join(body) + N
io.open(SCENE, 'w', encoding='utf-8', newline='').write(out)
print('showroom.rage: %d entities written' % (len(kept) + len(gblocks) + len(added)))
