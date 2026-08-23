"""Generates the showroom's F3 statistics overlay, as a script graph.

**The whole overlay is nodes.** Not one line of C# or C++ was written for it:
the frame time, the frame rate, the graphics API, the list of enabled render
and post settings, and the F3 toggle itself are all wired in the graph. The
point was to find out whether the graph can reach what the other two languages
reach, so anything it could not do had to become a node rather than a helper
written in C#.

Generated rather than dragged out by hand because it is about a hundred and
thirty nodes for something a C# script does in fourteen lines. That ratio is
the measurement, not a complaint -- see ENGINE-NOTES 7cm.

    python tools/scripts/make_stats_graph.py

Writes SampleProject/assets/graphs/Stats.rvgraph.
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "SampleProject" / "assets" / "graphs" / "Stats.rvgraph"

# The entity names the scene generator gives the four lines. This list and
# STATS_LINES in make_showroom_scene.py are the contract between the two files.
LINE_FRAME = "Stats Frame"
LINE_API = "Stats Api"
LINE_RENDER = "Stats Render"
LINE_POST = "Stats Post"

# What the overlay reports, as (setting name, label).
#
# The names are the settings registries' own keys -- the same strings the scene
# and project files use -- because the by-name bridge resolves them through
# those registries. A key that does not exist reads as empty, which shows up as
# a feature that never appears rather than as an error, so these are worth
# checking against RenderSettings.h and PostSettings.h when either moves.
# What the overlay reports, as (setting name, label, test).
#
# "flag" is a setting that is already true or false. "amount" is one that is a
# number, where anything above zero means the effect is running -- the vignette
# and the grain are intensities, not switches, and reading them needs Text To
# Number because the settings bridge answers in text.
#
# The names are the settings registries' own keys, which are also the keys the
# project and profile files use, so a name that stops existing shows up as a
# feature that never appears.
RENDER_FEATURES = [
    ("Shadows", "Shadows", "active"),
    ("RayTracing", "RayTracing", "active"),
    ("RTReflections", "RT Reflections", "active"),
    ("RTGlobalIllumination", "RT GI", "active"),
    ("RTAmbientOcclusion", "RT AO", "active"),
    ("VoxelGlobalIllumination", "VoxelGI", "flag"),
]
POST_FEATURES = [
    ("BloomEnabled", "Bloom", "flag"),
    ("ColorLutStrength", "LUT", "amount"),
    ("DepthOfField", "DoF", "flag"),
    ("AmbientOcclusion", "AO", "active"),
    ("SSR", "SSR", "active"),
    ("SSGI", "SSGI", "active"),
    ("MotionBlur", "MotionBlur", "flag"),
    ("AutoExposure", "AutoExposure", "flag"),
    ("VignetteIntensity", "Vignette", "amount"),
    ("ChromaticAberration", "Chromatic", "amount"),
    ("FilmGrain", "Grain", "amount"),
]

# Everything the toggle shows and hides. The panel is the entity the graph runs
# on, so it uses Set Field; the rest need Set Field On.
GROUP = [
    "Stats Frame", "Stats Mode", "Stats Rule",
    "Stats Render Caption", "Stats Render",
    "Stats Post Caption", "Stats Post",
]


class Graph:
    """Nodes and links, laid out on a grid so the canvas is readable."""

    def __init__(self):
        self.nodes = []
        self.links = []

    def node(self, kind, x, y, text=None, value=None):
        ident = len(self.nodes) + 1
        entry = {"Id": ident, "Type": kind, "Pos": [int(x), int(y)]}
        if text is not None:
            entry["Text"] = text
        if value is not None:
            entry["Value"] = value
        self.nodes.append(entry)
        return ident

    def number(self, x, y, value):
        """A float literal. **Its value is Value, not Text** -- a Float node
        written with Text reads as zero and generates `(deltaTime * 0.0f)`,
        which compiles perfectly and reports nothing."""
        return self.node("LiteralFloat", x, y, value=value)

    def link(self, source, source_pin, target, target_pin):
        self.links.append({
            "Id": len(self.links) + 1,
            "From": [source, source_pin],
            "To": [target, target_pin],
        })

    def dump(self):
        out = ["ScriptGraph: 1",
               f"NextNodeId: {len(self.nodes) + 1}",
               f"NextLinkId: {len(self.links) + 1}",
               "Nodes:"]
        for n in self.nodes:
            out.append(f"  - Id: {n['Id']}")
            out.append(f"    Type: {n['Type']}")
            out.append(f"    Pos: [{n['Pos'][0]}, {n['Pos'][1]}]")
            if "Text" in n:
                # Single quoted: the labels contain no single quote and this
                # keeps a value like "0.00" from parsing as a number.
                out.append("    Text: '" + n["Text"].replace("'", "''") + "'")
            if "Value" in n:
                out.append(f"    Value: [{n['Value']}, 0, 0, 0]")
        out.append("Links:")
        for l in self.links:
            out.append(f"  - Id: {l['Id']}")
            out.append(f"    From: [{l['From'][0]}, {l['From'][1]}]")
            out.append(f"    To: [{l['To'][0]}, {l['To'][1]}]")
        return "\n".join(out) + "\n"


g = Graph()
COL = 260          # horizontal step between columns
ROW = 150          # vertical step between rows

# ---------------------------------------------------------------------------
# The frame event, and the two things it drives
# ---------------------------------------------------------------------------
#
# A Sequence rather than one chain, because **exec cannot merge**: an input pin
# takes one link, so a Branch's two sides can never come back together. Every
# "do this, then carry on regardless" in this graph is a Sequence whose second
# output is the rest of the program.
on_frame = g.node("OnFrame", -1400, 0)

# --- the toggle, on the *step* and not the frame -----------------------------
#
# **Input edges belong to On Tick.** `WasActionPressed` is raised once per frame
# by InputMap::Update and cleared by EndFixedStep, which runs inside the step
# loop -- so the first fixed step consumes it and On Frame, which runs after,
# sees nothing. A frame that happened to run no step let it through, which is
# why F3 worked "sometimes": the odds are the ratio of frame rate to the fixed
# 60 Hz, and at 70 FPS almost every frame runs a step.
#
# On Tick sees every press, exactly once, by design. ENGINE-NOTES 7cn.
on_tick = g.node("OnTick", -1400, -700)
root = g.node("Sequence", -1150, -700)
g.link(on_tick, 0, root, 0)

# The action's *name is a pin*, not the node's text -- the validator says so
# plainly ("Action Pressed's Action has nothing feeding it") which is the kind
# of message that turns a wrong graph into a fixed one in a single read.
action_name = g.node("LiteralString", -1400, -320, "ToggleStats")
pressed = g.node("ActionPressed", -1150, -320)
g.link(action_name, 0, pressed, 0)

toggle_branch = g.node("Branch", -880, -400)
g.link(root, 0, toggle_branch, 0)
g.link(pressed, 0, toggle_branch, 1)

shown_get = g.node("GetFlag", -1150, -180, "shown")
flip = g.node("NotOf", -940, -180)
shown_set = g.node("SetFlag", -640, -400, "shown")
g.link(shown_get, 0, flip, 0)
g.link(flip, 0, shown_set, 1)
g.link(toggle_branch, 0, shown_set, 0)

# The panel's own visibility, and every element on it.
#
# **Set Field reaches only the entity the script runs on**, which is why the
# graph lives on the panel -- and why Set Field On had to exist before the
# seven things *inside* the panel could be hidden with it. A C# script could
# always do this; until now a graph could not.
show_check = g.node("Branch", -400, -400)
shown_again = g.node("GetFlag", -640, -250, "shown")
g.link(shown_set, 0, show_check, 0)
g.link(shown_again, 0, show_check, 1)


def visibility(from_node, from_pin, literal, y0):
    """Set Visible on the panel and on everything in it."""
    word = g.node("LiteralString", -400, y0 - 80, literal)
    panel = g.node("SetField", -140, y0, "UIRectComponent.Visible")
    g.link(from_node, from_pin, panel, 0)
    g.link(word, 0, panel, 1)

    previous = panel
    yy = y0
    for name in GROUP:
        who = g.node("LiteralString", 120, yy + 70, name)
        find = g.node("FindByName", 360, yy + 70)
        g.link(who, 0, find, 0)

        setter = g.node("SetFieldOn", 600, yy, "UIRectComponent.Visible")
        g.link(previous, 0, setter, 0)
        g.link(find, 0, setter, 1)
        g.link(word, 0, setter, 2)
        previous = setter
        yy += 150
    return yy


visibility(show_check, 0, "true", -1400)
visibility(show_check, 1, "false", -140)

# --- is the overlay up? -----------------------------------------------------
# The display half runs on the frame, because that is where the frame time is
# and because a number redrawn 60 times a second is redrawn often enough.
live = g.node("GetFlag", -1150, 1400, "shown")
live_branch = g.node("Branch", -880, 1340)
g.link(on_frame, 0, live_branch, 0)
g.link(live, 0, live_branch, 1)


def label(entity_name, x, y):
    """A Find By Name feeding a Set UI Text, which is every line's tail."""
    find = g.node("FindByName", x, y + 90)
    name = g.node("LiteralString", x - 220, y + 90, entity_name)
    setter = g.node("SetUIText", x + 240, y)
    g.link(name, 0, find, 0)
    g.link(find, 0, setter, 1)
    return setter


x = -600
y = 1340

# --- "4.50 ms   222 FPS" ----------------------------------------------------
delta_ms = g.node("Multiply", x, y + 260)
thousand = g.number(x - 240, y + 320, 1000)
g.link(on_frame, 1, delta_ms, 0)
g.link(thousand, 0, delta_ms, 1)

ms_text = g.node("NumberToText", x + 220, y + 260)
ms_places = g.number(x, y + 380, 2)
g.link(delta_ms, 0, ms_text, 0)
g.link(ms_places, 0, ms_text, 1)

fps = g.node("Divide", x, y + 460)
one = g.number(x - 240, y + 440, 1)
g.link(one, 0, fps, 0)
g.link(on_frame, 1, fps, 1)

fps_text = g.node("NumberToText", x + 220, y + 460)
fps_places = g.number(x, y + 560, 0)
g.link(fps, 0, fps_text, 0)
g.link(fps_places, 0, fps_text, 1)

ms_unit = g.node("LiteralString", x + 220, y + 600, " ms      ")
fps_unit = g.node("LiteralString", x + 220, y + 660, " FPS")

join_ms = g.node("JoinText", x + 460, y + 260)
g.link(ms_text, 0, join_ms, 0)
g.link(ms_unit, 0, join_ms, 1)

join_fps = g.node("JoinText", x + 460, y + 460)
g.link(fps_text, 0, join_fps, 0)
g.link(fps_unit, 0, join_fps, 1)

frame_line = g.node("JoinText", x + 700, y + 340)
g.link(join_ms, 0, frame_line, 0)
g.link(join_fps, 0, frame_line, 1)

frame_setter = label("Stats Frame", x + 960, y)
g.link(live_branch, 0, frame_setter, 0)
g.link(frame_line, 0, frame_setter, 2)

# --- "Vulkan  ·  TAA" -------------------------------------------------------
y += 760
api = g.node("GraphicsApi", x + 220, y + 120)
sep = g.node("LiteralString", x + 220, y + 190, "      ")
aa_name = g.node("LiteralString", x + 220, y + 260, "AntiAliasing")
aa = g.node("RenderSettingText", x + 460, y + 260)
g.link(aa_name, 0, aa, 0)

mode_a = g.node("JoinText", x + 700, y + 140)
g.link(api, 0, mode_a, 0)
g.link(sep, 0, mode_a, 1)
mode_line = g.node("JoinText", x + 700, y + 300)
g.link(mode_a, 0, mode_line, 0)
g.link(aa, 0, mode_line, 1)

mode_setter = label("Stats Mode", x + 960, y)
g.link(frame_setter, 0, mode_setter, 0)
g.link(mode_line, 0, mode_setter, 2)

previous = mode_setter


def feature_list(features, line_entity, y0):
    """A chain of "if this is on, append its label" steps.

    Each step is a Sequence: Then 0 tests and appends, **Then 1 carries the
    chain**. That shape exists because exec cannot merge -- an input pin takes
    one link, so a Branch's two sides can never rejoin, and the False side of
    every one of these simply dangles.
    """
    global previous
    yy = y0

    reset = g.node("SetText", x - 200, yy, "feat")
    empty = g.node("LiteralString", x - 440, yy + 70, "")
    g.link(previous, 0, reset, 0)
    g.link(empty, 0, reset, 1)
    tail = reset
    first = True

    for setting, text, kind in features:
        step = g.node("Sequence", x + 60, yy)
        g.link(tail, 0 if first else 1, step, 0)
        first = False

        name = g.node("LiteralString", x + 60, yy + 200, setting)
        if kind != "active":
            value = g.node("RenderSettingText", x + 300, yy + 200)
            g.link(name, 0, value, 0)

        if kind == "active":
            # **What ran, not what was asked for.** A setting says the project
            # wants ray-traced reflections; this says whether the frame graph
            # built them. On OpenGL it never did, and the box has to know that
            # without anybody writing "if OpenGL" anywhere.
            on = g.node("FeatureActive", x + 540, yy + 200)
            g.link(name, 0, on, 0)
        elif kind == "flag":
            yes = g.node("LiteralString", x + 300, yy + 270, "true")
            on = g.node("TextEquals", x + 540, yy + 210)
            g.link(value, 0, on, 0)
            g.link(yes, 0, on, 1)
        else:
            # An intensity: text, to a number, above zero.
            number = g.node("TextToNumber", x + 540, yy + 200)
            g.link(value, 0, number, 0)
            zero = g.number(x + 540, yy + 270, 0)
            on = g.node("Compare", x + 780, yy + 210, value=4)   # 4 is ">"
            g.link(number, 0, on, 0)
            g.link(zero, 0, on, 1)

        test = g.node("Branch", x + 1020, yy + 60)
        g.link(step, 0, test, 0)
        g.link(on, 0, test, 1)

        current = g.node("GetText", x + 1020, yy + 300, "feat")
        piece = g.node("LiteralString", x + 1020, yy + 370, text + "  ")
        joined = g.node("JoinText", x + 1260, yy + 320)
        g.link(current, 0, joined, 0)
        g.link(piece, 0, joined, 1)

        append = g.node("SetText", x + 1500, yy + 60, "feat")
        g.link(test, 0, append, 0)
        g.link(joined, 0, append, 1)

        tail = step
        yy += 460

    body = g.node("GetText", x + 460, yy, "feat")
    setter = label(line_entity, x + 960, yy - 60)
    g.link(tail, 1, setter, 0)
    g.link(body, 0, setter, 2)
    previous = setter
    return yy + 300


y = feature_list(RENDER_FEATURES, "Stats Render", y + 760)
y = feature_list(POST_FEATURES, "Stats Post", y)

# ---------------------------------------------------------------------------
# Hidden: blank the four value lines
# ---------------------------------------------------------------------------
#
# Belt and braces. Set Field On already hid them, and a hidden label draws
# nothing whatever it says -- but leaving last frame's numbers in the scene
# means a save taken while the overlay was up carries them, and a diagnostic
# should not leak into an asset.
blank = g.node("LiteralString", -600, y + 200, "")
previous = live_branch
pin = 1
for name in ("Stats Frame", "Stats Mode", "Stats Render", "Stats Post"):
    setter = label(name, -300, y + 300)
    g.link(previous, pin, setter, 0)
    g.link(blank, 0, setter, 2)
    previous = setter
    pin = 0
    y += 200

OUT.write_text(g.dump(), encoding="utf-8")
print(f"{OUT.relative_to(ROOT)}: {len(g.nodes)} nodes, {len(g.links)} links")
