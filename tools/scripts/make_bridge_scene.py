#!/usr/bin/env python3
"""Build the Golden Gate demo scene.

    python tools/scripts/make_bridge_models.py
    (run the runtime or editor once, so the registry mints the .meta files)
    python tools/scripts/make_bridge_seabed.py
    python tools/scripts/make_bridge_scene.py

**Stage 1 was the silhouette, and it is still the geometry here.** What has
changed since is what the sea is looked at against. The water component grew
refraction, absorption, a measured depth ramp, caustics and contact foam --
and every one of those measures a distance to a bottom, over a bay that had
none, under a flat grey background that gave the surface nothing to reflect.
So the two things this file now writes, and the reason it takes flags:

- **A sky.** `Sky: Color` draws nothing and reflects nothing: the environment
  cube a `Color` background resolves to is literally black (`Skybox.cpp`),
  so every facet of the sea was mirroring one flat value and no shader could
  make that read as water. `Gradient` costs no asset and gives back a real
  environment, a real irradiance, and a horizon for the specular track to run
  along.
- **A seabed**, from `make_bridge_seabed.py`: the strait's real bathymetry
  and the headlands either side of it, to the surveyed numbers.

Both are switchable, because the point of building them is to look at the
difference: `--sky=flat|dusk|night`, `--seabed=bay|none`.

**Six cameras, and the hero is a flag.** They are entities now rather than a
promote-and-`git checkout` dance, and `--hero=<name>` decides which one gets
ViewRank 0, which is the one the runtime opens. Grazing shots from deck
height always read flat -- 0.85 m waves are sub-pixel in silhouette past
about 200 m -- so a downward pitch is what lets the sea show its shape at
all.

**Cameras that stand on land have no Y written here.** They are placed on
the terrain at generate time, so reshaping the ground moves them with it
instead of leaving them buried in a cliff or hanging over one.
"""

import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from make_demo_scene import Scene, vec  # noqa: E402
import postprofile  # noqa: E402
import make_lut  # noqa: E402
import make_bridge_models as bridge  # noqa: E402
import make_bridge_seabed as seabed  # noqa: E402
import rockscatter  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "SampleProject" / "assets"
MODELS = ASSETS / "models" / "bridge"
SCENE = ASSETS / "scenes" / "GoldenGateDemo.rage"


# --- photometry (WR-4) --------------------------------------------------------
#
# **Every intensity below is a real fixture's, in candela, divided by the
# luminance its colour carries.** The two halves matter separately. The
# candela come from the lamp's rated flux and the luminaire's distribution --
# or, for the deck lamps, from the road illuminance the DOE survey measured,
# solved backwards through this engine's own spot falloff by
# `derive_lamp_color.py`. The division is because the colours are
# peak-normalised so a colour picker can hold them: a saturated sodium orange
# carries 0.41 of the luminance a white of the same peak would, and a fixture
# authored without dividing it out emits 40% of the light it is meant to.
#
# **These are absolute, and that is the point of the item.** They put the
# roadway around fifteen times brighter, relative to the sky behind it, than
# the values chosen by eye did -- which is the correct direction for a night
# exterior and is the change WR-4 exists to make. If the frame needs bringing
# back, the dial for that is the camera's exposure, not these.
#
#   flux (lm) / effective solid angle (sr) = candela
#
# The solid angles are the engine's cone integrated in cos(theta), which is
# how `pbr_fragment.glsl` shapes a spot: flat inside the inner angle, linear
# in cos to the outer one.

# 250 W HPS, 28 000 lm, behind amber acrylic. Solved from 14 lux average on
# the drivelane -- run derive_lamp_color.py to reproduce the number.
DECK_LAMP_CANDELA = 4376.0
DECK_LAMP_INTENSITY = round(DECK_LAMP_CANDELA / bridge.SODIUM_LUMINANCE)

# The tower uplights: 400 W HPS is 48 000 lm, a recessed shielded floodlight
# passes about 0.6 of it, and this engine's 26/58-degree cone gathers into
# 1.79 sr. The other two wattages ride the survey's own grading, which is the
# ratio of their lamps and not a taper somebody liked.
FLOOD_400_CANDELA = 48000.0 * 0.6 / 1.7936
FLOOD_CANDELA = {400.0: FLOOD_400_CANDELA,
                 250.0: FLOOD_400_CANDELA * 0.625,
                 150.0: FLOOD_400_CANDELA * 0.375}

# 35 W low-pressure sodium post-top: 4 800 lm, a globe luminaire keeps about
# 0.7 of it and throws it all round, so 4*pi rather than a cone.
POST_TOP_CANDELA = 4800.0 * 0.7 / (4.0 * math.pi)

# FAA L-864: the red obstruction beacon on an aviation structure is specified
# at 2 000 cd, which is a number rather than an opinion.
BEACON_CANDELA = 2000.0
# Aviation red is far out at the end of the visible band, so it carries a
# quarter of the luminance its peak channel suggests -- the same peak-
# normalisation correction every colour here needs.
BEACON_LUMINANCE = 0.2505

# The midspan channel lights. A bridge navigation lantern is rated by nominal
# range rather than candela; these are the intensities a 3-5 nautical-mile
# lantern runs at.
NAV_WHITE_CANDELA = 300.0
NAV_GREEN_CANDELA = 200.0


def read_handle(path):
    """The registry's own handle for an asset, from the `.meta` beside it.

    Read and never pasted: the scene is regenerated whenever a model changes,
    and a handle copied by hand is a handle that goes stale silently -- the
    mesh simply stops being found and the entity draws nothing.
    """
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("Handle:"):
            return line.split(":", 1)[1].strip()
    return None


def handles():
    found = {}
    for name in ("bridge_towers", "bridge_deck", "bridge_cables",
                 "bridge_road", "bridge_piers", "bridge_paint",
                 "bridge_lamps", "bridge_beacons", "bridge_thin"):
        meta = MODELS / (name + ".fbx.meta")
        if not meta.exists():
            raise SystemExit(
                "no .meta for {0} -- run make_bridge_models.py, then open the "
                "project once in the runtime or the editor so the registry "
                "mints handles, then run this again.".format(name))
        found[name] = read_handle(meta)
    return found


# Grey, and the same grey for everything. A shape pass that tints the towers
# differently from the deck is a shape pass you can no longer read the
# silhouette off, because the eye follows the colour boundary instead of the
# outline.
# **Four components, not three.** `mesh_inline` writes the tuple straight
# into BaseColor, which is RGBA; a three-component one parses as a failure
# and the surface falls back to white -- which looked like a lighting
# problem and was a missing alpha.
STEEL = (0.55, 0.55, 0.55, 1)


# --- the backgrounds ---------------------------------------------------------
#
# Every colour here is **linear**, which is what the scene file stores and
# what reaches the target. The research quotes the night sky in sRGB hex --
# zenith #0C1620, horizon (city glow) #4A3520 -- and those are converted
# once, here, rather than each time somebody reads them.
#
# The gradient is `mix(horizon, zenith, |y|^0.45)`: the 0.45 means the
# horizon colour is spent within about ten degrees of the horizon, which is
# exactly the band the research calls the city glow, and exactly the band a
# sea reflects at grazing angles.
#
# **The ambient term falls when a real sky arrives**, and this is not a
# taste call. Sky irradiance is added *on top* of AmbientColor, so keeping a
# daylight fill of 0.14 under a dusk gradient lights every surface from a
# sky that is not there and flattens the whole frame.
SKIES = {
    # What stage 1 wore: a flat background to judge a silhouette against.
    # Kept so the difference the sky makes can be measured rather than
    # asserted.
    "flat": {
        "sky": "Color",
        "horizon": (0.62, 0.68, 0.76),
        "zenith": (0.28, 0.42, 0.66),
        "ground": (0.16, 0.18, 0.2),
        "ambient": (0.55, 0.62, 0.75),
        "ambient_intensity": 0.14,
        # The provisional sun: raking from the south-west, scaffolding for
        # judging shape.
        "sun": {"rotation": (-0.61, 0.9, 0.0), "color": (1.0, 0.97, 0.92),
                "intensity": 2.1},
    },
    # Late dusk, and the honest choice *until the lamps exist*. The target is
    # a night scene, but the 128 sodium lamps are build-order item 7: under a
    # true night sky with nothing lit, there is no picture to judge. Dusk
    # keeps a low sun in the sky, which is what the anisotropic Beckmann
    # track needs to show at all.
    "dusk": {
        "sky": "Gradient",
        "horizon": (0.290, 0.140, 0.062),
        "zenith": (0.020, 0.045, 0.105),
        "ground": (0.012, 0.013, 0.015),
        "ambient": (0.35, 0.42, 0.58),
        "ambient_intensity": 0.025,
        # **Low in the west, because that is where it goes.** The handoff
        # wanted the sun beyond the far end of the span so its track ran at
        # the lens; the hero looks south from the Marin cliff, and a sun in
        # the south is not a thing that happens here. West-south-west at 4.5
        # degrees is the real winter sunset through the Gate: the track runs
        # in diagonally from the right, and the towers get the side light
        # that separates them from the sky.
        "sun": {"elevation": 8.0, "bearing": 72.0,
                "color": (1.0, 0.55, 0.28), "intensity": 2.6},
    },
    # The target, previewed. Research numbers exactly; a moon stands in for
    # the sun, and the frame is meant to be nearly black until the lamps go
    # in.
    "night": {
        "sky": "Gradient",
        # **Darker, and the warm band cut back.** The gradient is a linear
        # mix from horizon to zenith with no falloff to shape it, so the only
        # way to shorten the glow is to lower it until it falls under
        # visibility sooner. A third of what it was: the city is a long way
        # off across the bay and what reaches the sky over the Gate is a
        # smudge on the horizon, not a third of the frame.
        "horizon": (0.0225, 0.0121, 0.0052),   # the city glow, well down
        "zenith": (0.0018, 0.0044, 0.0080),    # blue still 4.4x red
        "ground": (0.004, 0.005, 0.006),
        "ambient": (0.20, 0.26, 0.40),
        "ambient_intensity": 0.012,
        # **WR-1: recoloured from (0.62, 0.70, 0.95) -- movie-blue-night,
        # exactly the convention the research called out as backwards. Real
        # moonlight is ~4100 K, warmer than daylight; the bridge has no blue
        # light sources at all, so a blue directional here was fill light
        # nothing in the scene could cast. Elevation/bearing unchanged --
        # they are also the moon DISC's direction below, read from this same
        # dict entry rather than duplicated, so the light and what you see
        # standing in the sky cannot drift apart (WR-9 names this trap for
        # lamps; the same discipline applies here).
        "sun": {"elevation": 34.0, "bearing": 118.0,
                "color": (1.0, 0.89, 0.72), "intensity": 0.30},
        # **WR-1 additions.** SkyCurve, city glow and the moon disc all
        # default OFF/linear/black in the engine, so every OTHER sky preset
        # here (dusk, flat) is untouched -- these four keys exist only on
        # the night preset, which is the only one asking for a shaped sky.
        # Values are starting points from the research (case studies +
        # Garstang skyglow model + real lunar photometry), for the owner to
        # grade by eye against the reference photographs, not final numbers.
        #
        # **Two passes in and still a sunset, not a horizon band** -- 127x
        # then 30x zenith, K 6/12 then 8/20, both read almost the same. The
        # exposure multiplies this term too (Exposure 2.4), so a peak that
        # looks modest in raw linear terms is already well up the tonemap's
        # near-linear range at the horizon before the elevation falloff ever
        # gets a chance to matter. Cutting an order of magnitude harder this
        # time rather than nudging again.
        "sky_curve": 0.4,             # 0.3-0.5 crowds the glow to the horizon
        "city_glow_color": (0.025, 0.0105, 0.003),   # ~3.7x zenith luminance
        "city_glow_bearing": 0.0,     # +z, south -- San Francisco
        "city_azimuth_k": 8.0,
        "city_elevation_k": 20.0,
        # The disc is a photograph now (NASA/LRO, see the texture's
        # ATTRIBUTION), so this is a tint on it rather than the disc's whole
        # colour -- and it has to be **much** dimmer than a self-lit disc
        # would be, because the maria are the whole point and the tone curve
        # eats them first. Worked through the chain rather than guessed: at
        # Exposure 2.4, a value of 2.6 puts the highlands at ~1.3 into ACES,
        # which clips to white and takes the maria with it (rendered, and the
        # face came back blank). 0.30 lands the highlands near 0.85 display
        # and the maria near 0.75 -- about 26 levels apart, which is the
        # contrast the photograph actually has. Still far and away the
        # brightest thing in the sky, and still blooms at the rim.
        "moon_color": (0.30, 0.28, 0.25),
        # **Oversized, and it has to be.** The real moon's angular radius is
        # 0.0046 rad; measured 2026-09-02, a disc that small renders nothing
        # at all here and does not start appearing until roughly 0.02. This is
        # ~3x life size -- the licence every matte painter takes, and the
        # alternative is no moon.
        "moon_disc": 0.015,
        # **Night needs a profile of its own, and auto exposure off in it.**
        # Attached whether or not --cinematic is given, because the one thing
        # a night frame cannot have is an exposure that hunts for mid grey:
        # with it on, this scene came back looking like late afternoon --
        # correctly exposed and completely wrong. A fixed stop is what makes
        # a dark picture stay dark and the lamps read as the bright thing.
        #
        # The indirect is lifted because night leans on it: the direct light
        # is 128 small lamps and 48 graded floods, and everything not standing
        # under one of them is lit by what bounced off the ones that are.
        # **Exposure 0.21, and 2.4 was measured to be 11x too high.** WR-4 made
        # the lamps physical -- about 15x brighter in luminous terms than the
        # values chosen by eye -- and this number was left where it had been
        # tuned against the dim ones. Traced end to end: 14 lux on the
        # drivelane, asphalt at 0.070 reflectance, 0.31 cd/m^2 of road, times
        # 2.4 arrives at 222 of 255 before bloom is added. The deck read as
        # blown out from every camera standing on it. 0.21 puts the road near
        # 70, which is a lit road at night.
        #
        # **And the bloom threshold has to move with it.** bloom_prefilter
        # thresholds the raw HDR image and carries no exposure term at all, so
        # lowering the stop does not change one pixel of what feeds the glow.
        # The threshold means "where the picture is bright", and that point is
        # 1/Exposure -- so at 0.21 it is ~5, and leaving it at 1.05 blooms
        # everything above a fifth of mid-grey, which is what "soft af" was.
        # **Metering on, owner's call.** A fixed stop cannot serve both
        # framings: the deck is lit to 0.31 cd/m^2 while the wide shot is
        # mostly water and sky two decades below it, so 0.21 is right
        # standing on the road and nearly black from the headland.
        # With metering on, `Exposure` stops being the stop and becomes
        # compensation on top of the metered one -- so it goes back to 1.
        "post": {"AutoExposure": True, "Exposure": 1.0, "GiIntensity": 2.6,
                 "BloomThreshold": 5.0,
                 "BloomIntensity": 0.16,
                 # **0.5, and 30 was measured to be credit given to the wrong
                 # knob** (`5d28c0c`). The floor was turned up to 30 until the
                 # tower's reflection appeared; measurement then found the
                 # reflection identical at 0.05 and at 30 -- 798 pixels of a
                 # million differ, all isolated specks -- while what 30 did buy
                 # was 44 water pixels blinking per frame, which was the
                 # flicker the owner had reported. Swept 0.05 to 30: blinking
                 # starts at 2. The owner picked 0.5.
                 #
                 # **This line is why a regenerate used to be unsafe.** The
                 # generator kept writing 30 over the committed 0.5, silently,
                 # every time the scene was rebuilt -- the WR-0-shaped gap the
                 # handoff names. WR-4 has to regenerate, so it closes it.
                 "ReflectionFloor": 0.5},
    },
}


def sun_rotation(elevation, bearing):
    """Euler radians for a light standing `elevation` degrees over the
    horizon in the compass direction `bearing`.

    Bearing is measured the way this scene's axes run, not the way a compass
    does: **0 is south (+z), 90 is west (+x)**, 180 north, 270 east. So a
    setting sun is somewhere near 90 and the numbers read as the place.

    A light points down its own -Z, and the engine's forward is
    (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)). Setting that to
    the negative of the direction the sun stands in falls out as pitch =
    -elevation, yaw = bearing, which is why there is no matrix here.
    """
    return (-math.radians(elevation), math.radians(bearing), 0.0)


# --- the cameras -------------------------------------------------------------
#
# A camera looks down its own -Z, so a camera at +Z with no rotation is
# looking back along the bridge towards -Z.
CAMERAS = {
    # The shot the demo is for: standing on the roadway looking down the
    # span, 1.4 m over the deck in the south side span, the near tower 310 m
    # ahead and the far one 1.6 km beyond it. It is also, at deck height, the
    # angle that reads flattest on water.
    "deck": {"tag": "Deck Camera",
             "position": (0.0, bridge.ROADWAY + 1.4, 950.0),
             "rotation": (0.0, 0.0, 0.0), "fov": 50},
    # Off the west side, level with the towers' upper third, looking east.
    # Yaw +90 degrees turns the camera's -Z towards -X.
    "profile": {"tag": "Profile Camera",
                "position": (2400.0, 150.0, 0.0),
                "rotation": (0.0, 1.5708, 0.0), "fov": 42},
    # **Owner-approved framing, 2026-08-30, moved to the side it exists on.**
    # Elevated three-quarter looking down the span: the downward pitch is
    # what lets the sea show its shape. The approved numbers put it 170 m up
    # on the *San Francisco* side, and once the land is the real land there
    # is no 170 m anywhere near Fort Point -- the Presidio bluff is about 74.
    # The viewpoint this shot is is Battery Spencer, on the Marin cliff, so
    # the shot is mirrored across the strait: same pitch, same height, same
    # three-quarter, yaw turned to look back down the span. Y is not written
    # here -- `build` stands it on the ground the terrain actually has.
    # **Moved back off the headland, 2026-08-31, on the owner's call.** At
    # (300, -1010) the rock filled the right third of the frame from **two to
    # four metres** off the lens -- measured by ray-casting the frustum against
    # the heightfield, not guessed at. Nothing reaches that: a 1K map tiled
    # every 2 m is magnified past its own resolution, and a rock placed there
    # would be inside the camera. It was also the reason two sessions of
    # shader work on the cliffs showed nothing.
    #
    # `clear_eye` had done its job -- it clears the sight line to the *subject*
    # -- but nothing stopped ground sitting beside the lens. The replacement
    # was picked by scanning x/z for a spot that stands on land, still frames
    # the tower, and has no ground within reach anywhere in the frustum. This
    # one has 145 m clear, and being higher and further back is closer to what
    # Battery Spencer actually is.
    "headland": {"tag": "Headland Camera",
                 "position": (500.0, None, -1100.0),
                 "rotation": (-0.155, math.pi - 0.40, 0.0), "fov": 55,
                 "eye": 2.5, "sees": (0.0, 75.0, -640.1)},
    # The San Francisco bluff, kept because it is the approved shot's exact
    # numbers and the two are worth comparing. Stood on the ground for the
    # same reason.
    "bluff": {"tag": "Bluff Camera",
              "position": (45.0, None, 1150.0),
              "rotation": (-0.155, 0.088, 0.0), "fov": 55, "eye": 12.0},
    # **Owner-approved, 2026-08-30.** The pier close-up: the south tower's
    # near footing 107 m off, where the concrete carries under the surface.
    # The one frame that shows a contact-foam ring and bare open water at
    # once, which is where foam changes are judged.
    "pier": {"tag": "Pier Camera",
             "position": (70.0, 4.5, 705.0),
             "rotation": (0.05, 0.82, 0.0), "fov": 55},
    # **Square on to the Marin cliff face**, from 170 m out over the water at
    # 55 m up. Added 2026-08-31 because every other camera in this scene looks
    # *along* that coast rather than at it, so the cliff geometry could only be
    # judged edge-on -- which is exactly the angle that shows a face panel's
    # back instead of its front. The yaw is the inverse of the panels' own
    # facing: they look out along the fall line, this looks back down it.
    "cliff": {"tag": "Cliff Camera",
              "position": (316.0, 55.0, -810.0),
              "rotation": (-0.06, 0.585, 0.0), "fov": 55},
    # **Broadside across open water, for the lamps' glitter path.** Added
    # 2026-08-31, **moved lower and further out 2026-09-01** during the
    # realism research captures (hand-edited directly in the scene, same
    # trap as the lamps' -- see WR-0): 22 m up read the water almost flat,
    # so the streaks foreshortened to slivers; 2.5 m, nearly at the waterline,
    # is what actually shows a lamp row's reflection running the deck's
    # length. Moved out to 500 m off the west side to keep the same lamps in
    # frame from the new, shallower angle.
    "glitter": {"tag": "Glitter Camera",
                "position": (500.0, 2.5, 180.0),
                "rotation": (0.02, 1.5708, 0.0), "fov": 52},
    # Under the north tower at Lime Point, looking back across the strait.
    # The one place the water is genuinely shallow enough to see through --
    # the north pier stands on the shore, so the surf against it is the
    # contact-foam rule's own case.
    "lime": {"tag": "Lime Point Camera",
             "position": (185.0, None, -590.0),
             "rotation": (-0.02, 2.50, 0.0), "fov": 55, "eye": 5.5},
}

# Which camera is the hero by default. The deck shot is the demo's subject;
# it is not the shot that answers "does this read as water", and this
# generator exists at the moment to answer that.
DEFAULT_HERO = "headland"


# --- the look ----------------------------------------------------------------
#
# **Post is a profile asset a camera points at, not lines in the scene.** A
# `.rage` that writes `BloomEnabled` is writing a key nothing reads
# (ENGINE-NOTES 7s), so the generator writes a real profile beside the scene
# and names it from every camera -- the same path a person uses.
#
# What is in it and why, because "cinematic" is not a setting:
#
# - **Height fog.** The single biggest one, and not only for mood: the water
#   body ends at 2 800 m and from anything high up you can see the cut. Fog
#   is what a real 2 km of sea air does anyway, and it puts the far shore
#   behind something instead of against nothing.
# - **Bloom**, threshold just over white so it takes the sun track on the
#   water and the sky near the horizon and leaves the structure alone.
# - **Auto exposure**, because a dusk frame's range moves with the camera and
#   a fixed stop that suits the headland blows out the deck.
# - **A shallow depth of field** at 85 mm: focused past the near tower, so
#   only the water in the first fifty metres softens. Enough to say lens,
#   not enough to say toy.
# - **Vignette, chromatic aberration and grain**, all small. Each models the
#   camera rather than the scene, and each is the first thing to look like a
#   filter if it is pushed. Read the units before setting them: the
#   dispersion is a fraction of frame width, so it wants a thousandth where
#   an intensity would want a third.
CINEMATIC = {
    "Fog": True,
    "FogColor": None,              # filled from the sky, below
    # **Thin, and low.** At 0.0022 with a 0.92 ceiling the strait went to a
    # flat orange wash a kilometre out and the sea stopped existing -- fog
    # that hides the subject is not atmosphere, it is a lens cap. This reads
    # as two kilometres of sea air: the far tower softens, the near water
    # keeps its specular, and the water body's far edge still goes.
    # **A haze, not weather.** Twice this drowned the strait in an orange
    # wash and the sea stopped existing; fog that hides the subject is not
    # atmosphere, it is a lens cap. What is wanted is the far tower sitting
    # a little back from the near one and the water body's far edge going
    # quietly -- everything else keeps its contrast.
    # **The layer has to reach the towers, and it did not.** 0.11 per metre
    # e-folds the density in nine metres, so the whole fog lived in a band at
    # the waterline while the hero camera stands at 89 m and the towers at
    # 227 -- the camera was above its own atmosphere and WR-3's sky-coloured
    # inscatter had nothing to colour. 0.006 puts the e-folding height at
    # 165 m, which is a marine layer that the deck stands in and the tower
    # tops rise out of, and which is what the reference photographs show.
    #
    # The density comes down as the layer goes up, or the two multiply: this
    # is a light haze, deliberately. The owner asked for less of it than the
    # first pass carried.
    "FogDensity": 0.0008,
    "FogHeightFalloff": 0.006,
    "FogHeight": 2.0,
    "FogStartDistance": 420.0,
    "FogMaxOpacity": 0.42,
    # **The waterline.** Fog is atmosphere; below the sea there is none, and
    # the height falloff would otherwise give the seabed *more* of it than
    # the surface. Without this the strait had haze under it.
    "FogFloor": 0.0,
    # **WR-3, on.** The fog takes its colour from the sky in each view
    # direction rather than from the one constant below, which is what makes
    # the haze amber toward the city and near-black out toward the ocean --
    # the single most characteristic thing about this view, and nothing a
    # single colour can say. The occlusion term is well short of 1: at full
    # strength the fog eats the city glow entirely, and the references keep
    # the glow band clearly visible behind a softened skyline.
    "FogSkyAffect": 1.0,
    "FogSkyOcclusion": 0.3,

    # WR-5, the lights' glow: every light with a SourceRadius is drawn as a
    # soft disc a few pixels across at any distance, carrying its own
    # intensity, so the lamp row cannot blink under any AA once the lens is
    # smaller than a pixel. **The intensity is the owner's pick, not the
    # physical value** (2026-09-02): at 1.0 -- the lamps' own 4 375 cd --
    # the row read as blown-out stars beside lens boxes that were tuned by
    # eye, and the owner called the six-ray flare "out of place and not
    # real"; 0.05 with rays and 0.05 without were shot beside 0.02 with a
    # plain fifth-share halo, and the owner chose the last.
    "LightGlow": True,
    "LightGlowPixels": 4.0,
    "LightGlowIntensity": 0.02,
    "LightFlare": 0.2,
    "LightFlareSize": 24.0,
    "LightFlareRays": 0.0,

    "BloomEnabled": True,
    "BloomThreshold": 1.05,
    "BloomKnee": 0.55,
    "BloomIntensity": 0.085,
    "BloomClamp": 12.0,

    "AutoExposure": True,
    # **0.015, and 0.17 is a daylight convention.** 0.17 is metering to
    # 18% grey, which is what a light meter does in sunshine; pointed at
    # a night frame that is mostly dark water it lifts the whole picture
    # until the average reaches that target, and the bridge came back
    # looking like late afternoon -- correctly exposed and completely
    # wrong, which is the note that had metering switched off in the
    # first place. Metering a night scene low is what stops that.
    #
    # Swept 0.17 / 0.06 / 0.03 / 0.02 / 0.015 on the deck and the
    # headland; owner picked 0.015. Nothing is blown at any of them --
    # even 0.03 puts only 0.03% of pixels at 245.
    #
    # **Note the deck is on the floor at this key**: it renders the same
    # at 0.015 and 0.02 because AutoExposureMin (0.15) clamps it, so the
    # deck is being limited rather than metered. Lowering that floor is
    # the dial if it ever needs to go darker still.
    "AutoExposureKey": 0.015,
    "AutoExposureSpeed": 6.0,
    "AutoExposureMin": 0.15,
    "AutoExposureMax": 12.0,
    "Exposure": 1.05,              # compensation, once metering is on

    "DepthOfField": True,
    "FocalLength": 85.0,
    "Aperture": 2.8,
    "FocusDistance": 320.0,
    "MaxBokehRadius": 10.0,

    "VignetteIntensity": 0.34,
    "VignetteSmoothness": 0.62,
    # **Fractions of the frame's width, not an intensity.** 0.30 disperses
    # the channels across a third of the picture: the first render came back
    # as three separate colour layers of the whole bridge. A real lens is a
    # pixel or two at the corners, which on a 2560-wide frame is this.
    "ChromaticAberration": 0.0006,
    "FilmGrain": 0.045,
    "FilmGrainSize": 1.7,

    "ColorLutStrength": 0.80,
}


def write_look(sky):
    """The grade and the profile beside the scene. Returns the handle."""
    lut = make_lut.write(ASSETS / "luts" / "cinematic.cube", "cinematic")

    settings = dict(CINEMATIC)
    # Fog is the sky's own horizon colour, so the far shore dissolves into
    # the band it is standing against rather than into a grey nobody chose.
    settings["FogColor"] = "[{0:g}, {1:g}, {2:g}]".format(*sky["horizon"])
    settings["ColorLut"] = str(lut)
    # **How much of the bounced light reaches the frame.** One dial serves the
    # traced and the baked forms both. The baked field came out darker than
    # the realtime estimate it replaced, and it should have: the realtime one
    # was a handful of bounce rays at night, biased bright by the few that
    # found a lamp. Correct is not the same as matching, so this is the look
    # asking for more indirect, not the bake being wrong.
    settings.update(sky.get("post", {}))
    return postprofile.write_named(SCENE.with_name("bridge_cinematic.rvpostprofile"),
                             settings)


def build(sky_name="dusk", seabed_name="bay", hero=DEFAULT_HERO, grounded=None,
          cinematic=False):
    mesh = handles()
    sky = SKIES[sky_name]
    grounded = {} if grounded is None else grounded
    s = Scene()

    # --- the cameras --------------------------------------------------------
    #
    # All four, always, so a shot is picked rather than rebuilt. Only the
    # hero's rank matters -- the runtime opens the lowest -- but the others
    # keep a stable order so the editor's list does not shuffle.
    # **The post chain is this scene's default state, not an option.** It was
    # gated behind --cinematic, which meant the shot everybody actually looks
    # at was rendered without the fog, the grade or the exposure it was tuned
    # against -- and night without a fixed exposure is unviewable outright.
    # The flag stays only so a check can ask for the ungraded picture.
    look = write_look(sky)
    order = [hero] + [name for name in CAMERAS if name != hero]
    for rank, name in enumerate(order):
        camera = CAMERAS[name]
        position = camera["position"]
        if position[1] is None:
            # Stood on the land the terrain actually has. A typed-in Y is a
            # camera that ends up buried or hanging the next time the ground
            # is reshaped, and this one has already moved 130 m once.
            if "sees" in camera:
                y = seabed.clear_eye(position[0], position[2], camera["sees"],
                                     camera["eye"])
            else:
                y = max(seabed.height_at(position[0], position[2]), 0.0)                     + camera["eye"]
            position = (position[0], round(y, 2), position[2])
            grounded[name] = position[1]
        # **Rank 0 is the only camera that renders** -- the runtime opens the
        # lowest ViewRank -- so it is the only one the rock scatter should
        # spend its budget for. Captured here, after the grounded cameras have
        # been stood on the terrain, because the scatter needs the resolved Y.
        if rank == 0:
            pitch, yaw = camera["rotation"][0], camera["rotation"][1]
            level = math.cos(pitch)
            forward = (-math.sin(yaw) * level, math.sin(pitch),
                       -math.cos(yaw) * level)
            # Right is forward crossed with world up, which is well defined for
            # every camera here because none of them looks straight down.
            right = (math.cos(yaw), 0.0, -math.sin(yaw))
            up = (right[1] * forward[2] - right[2] * forward[1],
                  right[2] * forward[0] - right[0] * forward[2],
                  right[0] * forward[1] - right[1] * forward[0])
            # PerspectiveFOV is the vertical angle; the horizontal follows from
            # the aspect the demo is shot at.
            half_v = math.radians(camera["fov"]) * 0.5
            hero_view = (position, forward, right, up,
                         math.tan(half_v) * (16.0 / 9.0), math.tan(half_v))

        s.entity(camera["tag"], position=position,
                 rotation=camera["rotation"])
        s.block("CameraComponent", [
            ("ViewRank", rank),
            ("FixedAspectRatio", "false"),
            ("ProjectionType", "Perspective"),
            ("PerspectiveFOV", camera["fov"]),
            # **5 cm near against 8 km far.** Conventional depth could not
            # tell two surfaces apart at that ratio anywhere past the first
            # kilometre; the engine's depth is reversed, which is what makes
            # this pair spendable.
            ("PerspectiveNearClip", 0.05),
            ("PerspectiveFarClip", 8000),
            ("OrthographicScale", 10),
            ("OrthographicNearClip", -1),
            ("OrthographicFarClip", 1),
            ("PostProfile", look),
        ])

    # --- the bridge ---------------------------------------------------------
    #
    # Four entities and no more. The transform walk is this engine's real
    # ceiling, and a silhouette made of one entity per member would spend that
    # budget before a single detail existed.
    # **Five entities, one material each.** Stage 1 wore flat grey
    # deliberately -- you cannot read a silhouette off a shape with a colour
    # boundary in it -- and that stage is over: the bridge is International
    # Orange, its piers and pylons are concrete, its roadway is asphalt.
    #
    # A MeshComponent holds one material and a model's own are never used
    # (Scene.cpp resolves the component's handle and falls back to the
    # renderer's default; an FBX exported with a green material rendered
    # bit-identically to one exported orange). So the parts are split by
    # material at export and each gets its own `.rmat`.
    steel = seabed.terrain.handle_for("materials/bridge_orange.rmat")
    concrete = seabed.terrain.handle_for("materials/bridge_concrete.rmat")
    asphalt = seabed.terrain.handle_for("materials/bridge_asphalt.rmat")
    paint = seabed.terrain.handle_for("materials/bridge_paint.rmat")
    lamp = seabed.terrain.handle_for("materials/bridge_lamp.rmat")
    beacon = seabed.terrain.handle_for("materials/bridge_beacon.rmat")
    # The members that are nothing but noise at the hero distances, in the
    # material that dissolves with distance -- see MAT_THIN in
    # make_bridge_models.py for the measurement and the owner's line.
    thin = seabed.terrain.handle_for("materials/bridge_thin.rmat")
    for name, part, material in (
            ("Towers", "bridge_towers", steel),
            ("Deck", "bridge_deck", steel),
            ("Cables", "bridge_cables", steel),
            ("Roadway", "bridge_road", asphalt),
            ("Piers", "bridge_piers", concrete),
            ("Markings", "bridge_paint", paint),
            ("Lamps", "bridge_lamps", lamp),
            ("Beacons", "bridge_beacons", beacon),
            ("Thin members", "bridge_thin", thin),
    ):
        s.entity(name)
        s.mesh(mesh[part], material)

    # --- baked irradiance ----------------------------------------------------
    #
    # **The scene was asking for these every time it loaded.** The project's
    # GI is set to Baked and the runtime has been logging "no Irradiance
    # Volume in it, falling back to Realtime" ever since -- so the towers were
    # being lit by per-frame traced GI, and at night that is a handful of
    # bounce rays that almost never find anything lit. The result was the red
    # mottling over the shafts: not the metal, not ambient occlusion, and not
    # reflections, all three eliminated by A/B.
    #
    # **Independent volumes, not one box, because one box cannot work here.**
    # `MaxResolution` caps a volume's grid at 32 cells an axis, so a single
    # volume stretched over 2.8 km of strait would sample every 87 m and mean
    # nothing. Volumes have been independent since 2026-08-27 -- each keeps its
    # own grid and spacing, and the scene packs them side by side into one
    # atlas -- which is exactly the feature this needs.
    #
    # They cover the towers and not the whole scene, deliberately. A volume
    # must cover everything *visible* or its boundary reads as a line across
    # whatever crosses it, and the honest way to satisfy that at this scale is
    # to bound the thing that actually suffers: the towers are static, they are
    # where the artefact is, and the sea and sky around them have no bounced
    # light to bake.
    # **And the deck, in three.** `MaxResolution` clamps *per axis* and its
    # own ceiling is 256, not 32 -- so a long thin volume is legal, and three
    # of them carry the whole 2.7 km of truss at 5 m spacing. 5 m is the
    # number that matters: the truss is 7.6 m deep, and a cell any coarser
    # straddles the roadway and its soffit, so the underside would be handed
    # the light falling on the top of the deck.
    for z in (-913.0, 0.0, 913.0):
        s.entity("Deck irradiance {0:+.0f}".format(z),
                 position=(0.0, 72.0, z))
        s.block("IrradianceVolumeComponent", [
            ("AutoFit", "false"),
            # Wide enough for the service pipes outboard of the trusses,
            # and 58 m to 86 m in y -- soffit at 67.4, rail heads at 77.
            ("Extents", vec(22.0, 14.0, 460.0)),
            ("Spacing", 5.0),
            ("MaxResolution", 256),
            ("Passes", 8),
            ("RaysPerCell", 512),
        ])

    for z in (-bridge.HALF_SPAN, bridge.HALF_SPAN):
        s.entity("Tower irradiance {0:+.0f}".format(z),
                 position=(0.0, 100.0, z))
        s.block("IrradianceVolumeComponent", [
            ("AutoFit", "false"),
            # Half-extents: wide enough for the fender, tall enough to take
            # the pier base at -35 m and the saddle at 230.
            ("Extents", vec(58.0, 135.0, 40.0)),
            # 9 m puts the tall axis at 30 cells, just inside the 32 cap.
            ("Spacing", 9.0),
            ("MaxResolution", 32),
            ("Passes", 8),
            ("RaysPerCell", 512),
        ])

    # --- the sodium lamps ----------------------------------------------------
    #
    # **128 spot lights, and they are the scene's lighting at night.** The sun
    # in the night preset is a moon at 0.30 -- it exists to give the water a
    # sheen and the towers an edge, not to light anything. What actually lights
    # this bridge after dark is the sodium, which is also why the paint is
    # International Orange: the lamps emit at 589 nm and the paint peaks at
    # 600.
    #
    # **Spots aimed straight down, not points.** A street lamp is a reflector
    # over a road; a point light at this height throws as much into the sky as
    # onto the deck, which both looks wrong and doubles the number of clusters
    # each one touches. The cone is wide because the luminaire's is.
    #
    # Positions come from `bridge.lamp_light`, which is the same function the
    # geometry uses to place the lens -- a light that is not where its lamp is
    # reads as a lighting bug rather than a placement one.
    for index, (x, side, z) in enumerate(bridge.lamp_stations()):
        s.entity("Sodium {0}".format(index),
                 position=bridge.lamp_light(x, side, z),
                 rotation=(-math.pi * 0.5, 0.0, 0.0))
        s.block("LightComponent", [
            ("Type", "Spot"),
            ("Color", vec(*bridge.SODIUM)),
            # **WR-4: solved, not chosen.** `derive_lamp_color.py` sums this
            # engine's own spot falloff over a grid of the carriageway and
            # scales until the average lands on the 14 lux the DOE survey
            # measured; the colour is peak-normalised, so the candela is
            # divided by its luminance to get here. The 452 this replaced was
            # about fifteen times too dim in luminous terms -- the deck was
            # barely brighter than the sky behind it, which is the wrong way
            # round for a night exterior and is a live suspect for what
            # `GiIntensity: 2.6` has been compensating for.
            ("Intensity", DECK_LAMP_INTENSITY),
            # **Widened to 600 m and a 0.6 m source, 2026-09-01 (commit
            # dab692d).** The 44 m range above was right for the deck's own
            # illumination but wrong for the glitter path: the lamps' water
            # reflection needs the source's own extent (SourceRadius) to
            # produce a streak rather than a point, and the mirror ray that
            # finds it has to reach the sea from 83 m up, which 44 m never
            # could. Widening the cone to 85 degrees is what lets a shallow
            # mirror ray see the lens at all near the horizon.
            ("Range", 600),
            ("SourceRadius", 0.6),
            ("InnerCone", 34), ("OuterCone", 85),
        ])

    # --- the tower floodlighting --------------------------------------------
    #
    # **The towers are lit, and it is not the sodium doing it.** Every night
    # photograph of this bridge shows the towers washed warm from below while
    # the cables above go dark, and that is floodlighting aimed up the shafts.
    # Without it the tower is a black cut-out over a lit roadway, which is the
    # one thing a night frame of this bridge never looks like.
    #
    # **WR-4 replaced eight fixtures with forty-eight, and the count is the
    # survey's.** Twelve at sidewalk level and twelve below the roadway on
    # each tower, wattage-graded 4x150 / 4x250 / 4x400. The grading is the
    # whole reason not to paint the shaft with one uniform light: a real
    # installation fades up the tower because the fixtures differ *and*
    # because inverse-square does the rest, and one uniform source cancels
    # the first mechanism into a flat wash. They are neutral white rather
    # than sodium -- the towers are the one place International Orange
    # re-saturates at night, and it cannot do that under a light with no red
    # information in it to give back.
    #
    # **None of them casts a shadow**, and that is a measurement rather than
    # a preference: the 2026-09-02 pass found ray-traced shadows at ~15 ms of
    # a 50 ms frame with 128 casting lamps and nothing capping how many a
    # pixel traces. Forty more casters would be the largest single regression
    # in this scene's history. What they are for is the wash, and a wash needs
    # no shadow.
    for x, y, z, watts in bridge.tower_flood_stations():
        s.entity("Tower flood {0:.0f} {1:.0f} {2:.1f} {3:.0f}W".format(z, x, y, watts),
                 position=(x, y, z),
                 # Straight up the shaft. The 78-degree tilt this replaces
                 # belonged to fixtures standing on the pier 60 m below the
                 # deck; from the sidewalk the shaft is overhead.
                 rotation=(math.radians(88.0), 0.0, 0.0))
        s.block("LightComponent", [
            ("Type", "Spot"),
            ("Color", vec(*bridge.FLOOD_WHITE)),
            ("Intensity", round(FLOOD_CANDELA[watts] / bridge.FLOOD_WHITE_LUMINANCE)),
            # **Up the whole shaft.** 105 m died below the second portal and
            # left the top two thirds black, which is not what the references
            # show -- the wash reaches nearly to the saddle and only the
            # cables above it go dark.
            ("Range", 250),
            # A recessed floodlight's aperture, not a point -- gives the
            # shaft's own reflections a source to widen against.
            ("SourceRadius", 0.5),
            # **Wide and soft.** At a 9-degree inner cone these read as hot
            # blobs on the shaft rather than a wash -- a real floodlight is
            # recessed behind a shield and lights the whole face, and what
            # gives a spot away is its edge.
            ("InnerCone", 26), ("OuterCone", 58),
            ("CastShadows", "false"),
        ])

    # --- the tower-base post-tops -------------------------------------------
    #
    # **35 W low-pressure sodium, and the only fixtures on the bridge still
    # wearing the 1937 colour** rather than an imitation of it through amber
    # plastic. One monochromatic line at 589 nm, so the blue channel is
    # literally zero and everything they light goes to shades of orange and
    # black -- which is what CRI 22 light does, and is the character the deck
    # lamps only approximate.
    for index, (x, y, z) in enumerate(bridge.post_top_stations()):
        s.entity("Post top {0}".format(index), position=(x, y, z))
        s.block("LightComponent", [
            ("Type", "Point"),
            ("Color", vec(*bridge.LPS_589)),
            ("Intensity", round(POST_TOP_CANDELA / bridge.LPS_589_LUMINANCE)),
            ("Range", 60),
            ("SourceRadius", 0.22),
            ("CastShadows", "false"),
        ])

    # --- the midspan channel lights -----------------------------------------
    #
    # **Three white over one green, both faces, under the deck at mid-span.**
    # Not lighting: a navigation aid, saying where the centre of the channel
    # is and how much air draught is under it. They are in the scene because
    # a sea-level camera sees them, and because their reflection is the only
    # green in the frame -- everything else this bridge emits lies between
    # amber and red.
    # **Sectored, not point sources, and the first pass got this wrong.** As
    # bare points a metre off the truss they put 300 lux on the steel beside
    # them and read as one blown white blob under the deck -- the owner
    # spotted it in the first capture. A real lantern is a shielded sector
    # aimed at the channel: it is bright to a ship's master and puts nothing
    # on the structure carrying it. A spot aimed outboard is what this engine
    # has to say that with, and the shield is the cone rather than a number
    # somebody dialled down until the blob stopped.
    #
    # **They are still lights rather than markers**, which is the opposite of
    # the call made for the red beacons a few lines down. The difference is
    # what each is for: a beacon warns aircraft 200 m up and only has to be
    # seen, and it has emissive geometry to be seen by; these have no
    # geometry yet, so light is the only way they exist at all. When the
    # lanterns get modelled they should change sides -- emissive markers, no
    # light -- and this loop should go.
    for index, (x, y, z, kind) in enumerate(bridge.navigation_light_stations()):
        green = kind == "green"
        outboard = 1.0 if x > 0.0 else -1.0
        s.entity("Nav {0} {1}".format(kind, index),
                 # Clear of the steel, not against it.
                 position=(x + outboard * 1.4, y, z),
                 # Forward is -Z in a light's own frame, so a yaw of -90
                 # degrees aims it at +x and +90 at -x: outboard, across the
                 # channel, away from the structure behind it.
                 rotation=(0.0, -math.pi * 0.5 * outboard, 0.0))
        s.block("LightComponent", [
            ("Type", "Spot"),
            ("Color", vec(*(bridge.NAV_GREEN if green else bridge.NAV_WHITE))),
            ("Intensity", round((NAV_GREEN_CANDELA if green else NAV_WHITE_CANDELA)
                                / (bridge.NAV_GREEN_LUMINANCE if green
                                   else bridge.NAV_WHITE_LUMINANCE))),
            ("Range", 90),
            ("SourceRadius", 0.18),
            # The lantern's sector: wide enough to be seen from anywhere on
            # the water, and it stops well before the steel above and behind.
            ("InnerCone", 55), ("OuterCone", 78),
            ("CastShadows", "false"),
            ("IsBaked", "false"),
        ])

    # --- the tower-top beacons ----------------------------------------------
    #
    # **FAA L-864: 2 000 cd of aviation red, flashing.** The saddle housings
    # already carry emissive markers; what WR-4 adds is the light and the
    # flash, because a beacon that does not flash is a red dot, and one that
    # does is unmistakably an aircraft warning.
    #
    # **The flash is a script and the script is deterministic** -- elapsed
    # time, no clock and no RNG, the discipline `Flicker.cs` documents: every
    # screenshot comparison in this repository depends on frame N being the
    # same picture twice. `Beacon.cs` owns the cycle, and remember `Sample.dll`
    # has to be rebuilt (`dotnet build -c Release -o bin` from
    # SampleProject/Scripts) or the component resolves to nothing at all.
    #
    # **Not baked, and it must not be**: a light whose intensity moves is not
    # one a solve can pre-integrate, and leaving IsBaked on would put a
    # flashing source into a stored file -- the frame may guess, a store may
    # not.
    for index, z in enumerate((-bridge.HALF_SPAN, bridge.HALF_SPAN)):
        s.entity("Tower beacon {0}".format(index),
                 position=(0.0, bridge.TOWER_TOP + 3.4, z))
        s.block("LightComponent", [
            ("Type", "Point"),
            ("Color", vec(*bridge.BEACON)),
            ("Intensity", round(BEACON_CANDELA / BEACON_LUMINANCE)),
            ("Range", 400),
            ("SourceRadius", 0.3),
            ("CastShadows", "false"),
            ("IsBaked", "false"),
        ])
        # **`managed_script`, not `script`.** The latter writes a
        # NativeScriptComponent, which names something the C++ module
        # registered; pointing one at a C# class loads with "Scene references
        # unknown script" and an entity that silently does nothing. The camp
        # scene lost its camera move and its firelight to exactly that.
        #
        # 30 flashes a minute, the middle of the FAA 20-40 band, and the two
        # towers deliberately out of phase: a real pair is not synchronised,
        # and a synchronised pair reads as one object.
        s.managed_script("Beacon",
                         Peak=round(BEACON_CANDELA / BEACON_LUMINANCE),
                         Period=2.0,
                         Duty=0.18,
                         Phase=0.0 if index == 0 else 1.0)

    # **The red beacons are emissive and carry no light**, deliberately. They
    # are markers -- they exist to be seen, not to illuminate -- and 46 more
    # lights to light nothing would cost the cluster binning for no pixel that
    # is not already delivered by the emissive material and the bloom over it.
    #
    # **Except the four on the fenders' rims, which do.** They sit at the
    # waterline rather than 220 m up a tower, so unlike a saddle beacon or a
    # cable marker they land in the one place a light's own reflection is the
    # entire point: a boat-level camera reads them as a dim red glow *and* a
    # matching streak on the water, the way the marine lights on a real pier
    # fender do. Same fender-rim corner as the emissive markers themselves
    # (`beacon_positions`'s marine-light sub-loop: `end * fender_a * 0.94`,
    # `FENDER_TOP + FENDER_PARAPET + 0.6`) so the light cannot drift from its
    # lens -- but hand-rounded to 2 decimals, same as the mesh loop's `0.94`
    # fudge, when added directly to the scene at `dab692d`. Reproduced
    # verbatim (not re-derived at full precision) per WR-0, so a regenerate
    # cannot nudge the position and silently invalidate the bake.
    for name, position in (
            ("Marine light 0", (-35.23, 5.35, -bridge.HALF_SPAN)),
            ("Marine light 1", (35.23, 5.35, -bridge.HALF_SPAN)),
            ("Marine light 2", (-42.96, 5.35, bridge.HALF_SPAN)),
            ("Marine light 3", (42.96, 5.35, bridge.HALF_SPAN))):
        s.entity(name, position=position)
        s.block("LightComponent", [
            ("Type", "Point"),
            ("Color", vec(1.0, 0.08, 0.04)),
            ("Intensity", 60),
            ("Range", 130),
            ("SourceRadius", 0.4),
            ("CastShadows", "false"),
            ("IsBaked", "false"),
        ])

    # --- the floor of the strait --------------------------------------------
    #
    # A terrain, because a heightfield is what a seabed is: chunked, levelled
    # by distance, and traced at level 0 so the refraction ray and the drawn
    # surface are the same surface. `make_bridge_seabed.py` writes the asset
    # and owns every number in its shape; the scene only has to say where it
    # sits and how tall a full sample stands, and both come back from the
    # generator rather than being copied.
    if seabed_name != "none":
        floor = seabed.dimensions()
        silt, rock, sand, scrub = (seabed.terrain.handle_for(name) for name in (
            "materials/bay_silt.rmat", "materials/bay_rock.rmat",
            "materials/bay_sand.rmat", "materials/bay_scrub.rmat"))
        s.entity("Seabed", position=(0.0, floor["base"], 0.0))
        s.block("TerrainComponent", [
            ("Terrain", floor["handle"]),
            ("Size", "{0:g}".format(floor["size"])),
            ("Height", "{0:.4f}".format(floor["height"])),
            ("Material", silt),
            ("Layer1", rock),
            ("Layer2", sand),
            ("Layer3", scrub),
            ("TextureScale", "{0:g}".format(floor["texture_scale"])),
            # It *is* its own collider, and a bridge demo that lets you fall
            # through the headland is a demo with a hole in it.
            ("Collision", "true"),
        ])

    # --- rock ------------------------------------------------------------------
    #
    # Placed by rockscatter, which owns the reasoning: which family belongs on
    # which ground, and why an even sprinkle is the thing that gives scattered
    # geometry away. What it fixes here is the near headland -- a heightfield
    # cannot be vertical, so the cliffs are ramps wearing a stretched planar
    # uv, and no shader change reaches that.
    if seabed_name != "none":
        # The viewpoints, so the scatter can spend its budget on ground that
        # is actually looked at. Every camera in the scene, not just the hero:
        # the hero is a flag and the others are one regeneration away.
        stones = rockscatter.place(s, seabed, seabed.terrain.handle_for,
                                   bridge.handle_for, hero_view)

    # --- the sea ------------------------------------------------------------
    #
    # A WaterComponent rather than a mesh: the strait is a rectangle and the
    # only things worth saying about it are how far it runs. There is no
    # material to give it -- the engine owns the one water material, so this
    # sea is the same sea every other scene gets.
    #
    # **Square, and the same 2 800 m as the terrain.** It used to be 2 200
    # across, which was fine over a bottomless bay and is not fine over a
    # floor: 300 m of seabed either side would stand in the open air as a dry
    # pit around the water, and the frame's far corners look straight at it.
    # The cost is honest -- 27% more quads, 871k at 3 m spacing -- and the
    # real fix is water levels of detail, which do not exist yet.
    #
    # **Spacing has to be finer than the waves, and by a lot.** A 24 m wave on
    # a 25 m grid has one vertex per crest: it cannot be represented at all and
    # aliases into noise. Four vertices per wavelength is the floor, so 3 m
    # here.
    s.entity("Sea", position=(0.0, 0.0, 0.0))
    s.block("WaterComponent", [
        ("Width", 2800),
        ("Length", 2800),
        ("Spacing", 3),
        ("TextureScale", 12),
        ("WaveHeight", 0.85),
        ("WaveLength", 48),
        ("Choppiness", 1.25),
        ("WaveSpeed", 1.4),
        ("WaveDirection", 38),
        ("Foam", 0.75),
        ("ShallowColor", vec(0.05, 0.16, 0.19)),
        ("DeepColor", vec(0.008, 0.026, 0.048)),
        ("GradientDepth", 12),
    ])

    # --- the light ----------------------------------------------------------
    #
    # One directional light, placed by the sky it belongs to. Still
    # provisional: the night setup is 128 sodium lamps plus four shadow
    # casters (build order item 7), and this is the one light that lets the
    # water's specular track exist in the meantime.
    lamp = sky["sun"]
    rotation = lamp.get("rotation")
    if rotation is None:
        rotation = sun_rotation(lamp["elevation"], lamp["bearing"])
    s.entity("Sun", rotation=rotation)
    s.block("LightComponent", [
        ("Type", "Directional"),
        ("Color", vec(*lamp["color"])),
        ("Intensity", lamp["intensity"]),
        ("Range", 10),
        ("InnerCone", 20),
        ("OuterCone", 44),
        ("CastShadows", "true"),
    ])

    environment = [
        "Scene: Golden Gate at night -- stage 1, the silhouette",
        "Version: 6",
        "Environment:",
        "  AmbientColor: {0}".format(vec(*sky["ambient"])),
        "  AmbientIntensity: {0:g}".format(sky["ambient_intensity"]),
        "  Sky: {0}".format(sky["sky"]),
        "  SkyHorizon: {0}".format(vec(*sky["horizon"])),
        "  SkyZenith: {0}".format(vec(*sky["zenith"])),
        "  SkyGround: {0}".format(vec(*sky["ground"])),
        "  SkyIntensity: 1",
        "  SkyRotation: 0",
        "  SkyTexture: 0",
    ]

    # WR-1: only the presets that opt in (the "sky_curve" key's presence is
    # the signal) write these -- everything else keeps the exact Environment
    # block it always wrote, so a non-night scene regenerated with this
    # script is untouched to the byte.
    if "sky_curve" in sky:
        moon = sky["sun"]
        environment += [
            "  SkyCurve: {0:g}".format(sky["sky_curve"]),
            "  CityGlowColor: {0}".format(vec(*sky["city_glow_color"])),
            "  CityGlowBearing: {0:g}".format(sky["city_glow_bearing"]),
            "  CityAzimuthK: {0:g}".format(sky["city_azimuth_k"]),
            "  CityElevationK: {0:g}".format(sky["city_elevation_k"]),
            "  MoonColor: {0}".format(vec(*sky["moon_color"])),
            # Read from the same "sun" entry the moonlight itself uses, not
            # duplicated -- see the comment on "night"'s sun key.
            "  MoonElevation: {0:g}".format(moon["elevation"]),
            "  MoonBearing: {0:g}".format(moon["bearing"]),
            "  MoonDisc: {0:g}".format(sky["moon_disc"]),
        ]

        # The moon's face. Read from the .meta rather than pasted, the same
        # rule the models follow -- a handle copied by hand is a handle that
        # goes stale silently. Absent, the engine draws its analytic disc,
        # which is a sun; the scene simply has no moon until the texture is
        # generated (`tools/scripts/make_moon_texture.py`).
        face = ASSETS / "textures" / "moon.png.meta"
        if face.exists():
            environment.append("  MoonTexture: {0}".format(read_handle(face)))
        else:
            print("  no moon.png.meta -- run make_moon_texture.py; "
                  "the sky will draw an analytic disc instead")

    return "\n".join(environment + [
        "Entities:",
        s.text(),
        "",
    ])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sky", default="dusk", choices=sorted(SKIES),
                        help="what the sea has to reflect (default: dusk)")
    parser.add_argument("--seabed", default="bay", choices=("bay", "none"),
                        help="bay: the strait's real floor and its headlands; "
                             "none: the bottomless bay stage 1 had, for the A/B")
    parser.add_argument("--hero", default=DEFAULT_HERO, choices=sorted(CAMERAS),
                        help="which camera gets ViewRank 0")
    parser.add_argument("--cinematic", action="store_true",
                        help="write the graded post profile and point every "
                             "camera at it: fog, bloom, metering, a shallow "
                             "depth of field, and the lens effects")
    args = parser.parse_args(argv)

    SCENE.parent.mkdir(parents=True, exist_ok=True)
    grounded = {}
    with SCENE.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(build(args.sky, args.seabed, args.hero, grounded,
                           args.cinematic))

    print("wrote", SCENE)
    print("  sky {0}   seabed {1}   hero {2} ({3})".format(
        args.sky, args.seabed, args.hero, CAMERAS[args.hero]["tag"]))
    for name, y in sorted(grounded.items()):
        print("  {0:9s} stands on the ground at y = {1:.2f} m".format(name, y))
    print("  towers at z = +/-{0:.1f}   deck {1:.1f} to {2:.1f}   "
          "roadway {3:.1f} m".format(
              bridge.HALF_SPAN, -bridge.PYLON_X, bridge.PYLON_X, bridge.ROADWAY))


if __name__ == "__main__":
    main()
