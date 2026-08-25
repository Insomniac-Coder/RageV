"""Builds the texel-emitters design document as a PDF.

    python tools/scripts/make_texel_pdf.py

Writes docs/texel-emitters-design.pdf from the pictures already committed in
docs/images/texel-emitters/. Needs reportlab and Pillow.

Generated rather than written by hand for the reason every other document-like
artefact in this repository is: the numbers in it come from measurements that
may be re-taken, and a PDF nobody can regenerate is a PDF that goes stale
silently.

Owner's brief: problem statement, idea, implementation -- explained the easy way
while carrying the real technical content, and using the before/after pictures.
"""
import pathlib

from reportlab.lib import colors
from reportlab.lib.enums import TA_JUSTIFY
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (BaseDocTemplate, Frame, Image, KeepTogether,
                                NextPageTemplate, PageBreak, PageTemplate,
                                Paragraph, Spacer, Table, TableStyle)

ROOT = pathlib.Path(__file__).resolve().parents[2]
IMG = ROOT / "docs" / "images" / "texel-emitters"
OUT = ROOT / "docs" / "texel-emitters-design.pdf"

INK = colors.HexColor("#17181B")
MUTED = colors.HexColor("#5B6068")
ACCENT = colors.HexColor("#B02A26")
RULE = colors.HexColor("#D8DBE0")
PANEL = colors.HexColor("#F4F5F7")

PAGE_W, PAGE_H = A4
MARGIN = 22 * mm
COL_W = PAGE_W - 2 * MARGIN

ss = getSampleStyleSheet()


def style(name, **kw):
    base = dict(fontName="Helvetica", fontSize=9.8, leading=15.2, textColor=INK,
                spaceAfter=7)
    base.update(kw)
    return ParagraphStyle(name, **base)


Body = style("Body", alignment=TA_JUSTIFY)
Lead = style("Lead", fontSize=11.4, leading=17.6, textColor=MUTED, spaceAfter=12)
H1 = style("H1", fontName="Helvetica-Bold", fontSize=17, leading=21,
           textColor=ACCENT, spaceBefore=16, spaceAfter=8)
H2 = style("H2", fontName="Helvetica-Bold", fontSize=11.2, leading=15,
           textColor=INK, spaceBefore=12, spaceAfter=5)
Caption = style("Caption", fontSize=8.2, leading=11.6, textColor=MUTED,
                spaceBefore=4, spaceAfter=14)
Quote = style("Quote", fontSize=10.6, leading=16.4, textColor=INK,
              leftIndent=10, rightIndent=10, spaceBefore=6, spaceAfter=10)
Small = style("Small", fontSize=8.6, leading=13, textColor=MUTED)
Cell = style("Cell", fontSize=8.8, leading=12.4, spaceAfter=0)
CellB = style("CellB", fontName="Helvetica-Bold", fontSize=8.8, leading=12.4,
              spaceAfter=0)
CellH = style("CellH", fontName="Helvetica-Bold", fontSize=8.4, leading=11.6,
              textColor=MUTED, spaceAfter=0)


def rule(space_before=2, space_after=10):
    t = Table([[""]], colWidths=[COL_W], rowHeights=[0.6])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), RULE)]))
    return [Spacer(1, space_before), t, Spacer(1, space_after)]


def picture(name, caption, width=COL_W):
    src = IMG / name
    from PIL import Image as PILImage
    w, h = PILImage.open(src).size
    img = Image(str(src), width=width, height=width * h / w)
    return KeepTogether([img, Paragraph(caption, Caption)])


def duo(left, right, caption):
    """Two pictures side by side, then one caption under both."""
    from PIL import Image as PILImage
    gap = 5 * mm
    w = (COL_W - gap) / 2
    cells = []
    for name in (left, right):
        iw, ih = PILImage.open(IMG / name).size
        cells.append(Image(str(IMG / name), width=w, height=w * ih / iw))
    t = Table([cells], colWidths=[w, w], hAlign="LEFT")
    t.setStyle(TableStyle([("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (0, 0), gap),
                           ("RIGHTPADDING", (1, 0), (1, 0), 0),
                           ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
                           ("TOPPADDING", (0, 0), (-1, -1), 0)]))
    return KeepTogether([t, Paragraph(caption, Caption)])


def table(rows, widths, header=True, highlight=None):
    data = []
    for r, row in enumerate(rows):
        line = []
        for c, text in enumerate(row):
            st = CellH if (header and r == 0) else Cell
            if highlight is not None and r == highlight and not (header and r == 0):
                st = CellB
            line.append(Paragraph(text, st))
        data.append(line)
    t = Table(data, colWidths=widths, hAlign="LEFT")
    cmds = [("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("TOPPADDING", (0, 0), (-1, -1), 5),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ("LEFTPADDING", (0, 0), (-1, -1), 7),
            ("RIGHTPADDING", (0, 0), (-1, -1), 7),
            ("LINEBELOW", (0, 0), (-1, -2), 0.4, RULE)]
    if header:
        cmds += [("LINEBELOW", (0, 0), (-1, 0), 0.8, MUTED)]
    if highlight is not None:
        cmds += [("BACKGROUND", (0, highlight), (-1, highlight), PANEL)]
    t.setStyle(TableStyle(cmds))
    return KeepTogether([t, Spacer(1, 12)])


def chrome(canvas, doc, first=False):
    canvas.saveState()
    if not first:
        canvas.setFont("Helvetica", 7.6)
        canvas.setFillColor(MUTED)
        canvas.drawString(MARGIN, PAGE_H - MARGIN + 8,
                          "RageV  \u2014  Texel emitters")
        canvas.drawRightString(PAGE_W - MARGIN, PAGE_H - MARGIN + 8,
                               "Design document")
        canvas.setStrokeColor(RULE)
        canvas.setLineWidth(0.5)
        canvas.line(MARGIN, PAGE_H - MARGIN + 3, PAGE_W - MARGIN,
                    PAGE_H - MARGIN + 3)
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(MUTED)
    canvas.drawCentredString(PAGE_W / 2, MARGIN - 12, str(doc.page))
    canvas.restoreState()


frame = Frame(MARGIN, MARGIN, COL_W, PAGE_H - 2 * MARGIN, id="body",
              leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)

doc = BaseDocTemplate(str(OUT), pagesize=A4, title="Texel emitters",
                      author="RageV", leftMargin=MARGIN, rightMargin=MARGIN,
                      topMargin=MARGIN, bottomMargin=MARGIN)
doc.addPageTemplates([
    PageTemplate(id="first", frames=[frame],
                 onPage=lambda c, d: chrome(c, d, first=True)),
    PageTemplate(id="rest", frames=[frame], onPage=chrome),
])

S = []

# ---------------------------------------------------------------- title -----
S.append(Spacer(1, 14 * mm))
S.append(Paragraph(
    '<font color="#B02A26">Texel emitters</font>', style(
        "Title", fontName="Helvetica-Bold", fontSize=30, leading=34,
        spaceAfter=6)))
S.append(Paragraph("Light through the holes in the black paper", style(
    "Sub", fontName="Helvetica", fontSize=14.5, leading=19, textColor=MUTED,
    spaceAfter=16)))
S += rule(0, 12)
S.append(Paragraph(
    "A design document for the RageV renderer. It explains why a glowing "
    "texture used to light a room from the wrong place, what was done about "
    "it, and what the result measures.", Lead))
S.append(Spacer(1, 4))
S.append(Paragraph(
    "<b>The short version.</b> A light fitting is usually a texture: a panel "
    "with the lit parts painted into it. The renderer treated the whole panel "
    "as glowing evenly, so a ceiling with four lit cells out of a hundred and "
    "forty-four lit the room as though all one hundred and forty-four were on. "
    "Thirty-six times too much light, arriving from the wrong place. This "
    "document is about teaching the renderer to aim at the four.", Body))
S.append(Spacer(1, 10))
S.append(duo("before-phantom.jpg", "after-aimed.jpg",
             "<b>The same scene, the same frame, the same settings.</b> Only "
             "the renderer differs: on the left the whole ceiling acts as a "
             "light, on the right the bounce is aimed at the four cells that "
             "are actually lit."))
S.append(NextPageTemplate("rest"))
S.append(PageBreak())

# -------------------------------------------------------------- problem -----
S.append(Paragraph("1 &nbsp;&nbsp;The problem", H1))
S.append(Paragraph(
    "An emissive material makes a surface glow, and the traced bounce has to "
    "turn that glow into light on other surfaces. It did that by standing a "
    "<b>rectangle</b> in for the mesh and treating the whole rectangle as "
    "glowing evenly at the material's emissive value.", Body))
S.append(Paragraph(
    "That is right for a panel that glows all over. It is wrong for a light "
    "<i>fitting</i>, where the emission is painted into a texture &mdash; four "
    "lit cells of a hundred and forty-four in the showroom's ceiling. The "
    "bounce then lit the room as though all hundred and forty-four were on: "
    "<b>thirty-six times too much light</b>, arriving from an eighty-six "
    "square metre phantom instead of from the four places the artist painted.", Body))
S.append(Paragraph(
    "Its enormous sample-to-sample variance is what showed up as grain "
    "crawling over the car's paint with the camera perfectly still.", Body))

S.append(picture("before-phantom.jpg",
                 "<b>Before.</b> One textured mesh for the ceiling. The whole "
                 "panel acts as a light, so the room is flooded and the paint "
                 "crawls with grain that never settles."))

S.append(Paragraph("Why it was hard to see", H2))
S.append(Paragraph(
    "The ceiling <i>looked</i> right. Only what it emitted into the room was "
    "wrong, and nothing on screen said so. Worse, the workaround &mdash; "
    "splitting the lit cells out into their own mesh &mdash; made the "
    "rectangle true again, so the picture came good for a reason that had "
    "nothing to do with the code. Two wrong diagnoses came first.", Body))

# ------------------------------------------------------------------ idea ----
S.append(Paragraph("2 &nbsp;&nbsp;The idea", H1))
S.append(Paragraph(
    "A pixel shader runs per pixel, so a pixel whose emissive value clears a "
    "threshold could be counted as a source of light &mdash; a directional "
    "light behind black paper with holes, where light escapes only through "
    "the holes.", Quote))
S.append(Paragraph(
    "The research placed that in the technique landscape. It is "
    "<b>textured-light importance sampling</b>, it has an established "
    "literature, and the <b>texture domain is its correct home</b>: screen "
    "space is view-dependent by construction &mdash; a hole that leaves the "
    "frame stops existing as a light &mdash; and this engine's screen-space "
    "GI already does that half. Texels are known ahead of time, from any "
    "camera. It is what offline renderers do for textured area lights, and it "
    "scales down to real time almost unchanged: the tables build at load, and "
    "the shader pays a couple of fetches per sample.", Body))
S.append(Paragraph(
    "It also fitted the shape the engine already had. Analytic lights travel "
    "one road; emissive surfaces travel another. The second road was the one "
    "assuming a mesh glows uniformly, and it is the only road this work "
    "touches.", Body))

# -------------------------------------------------------- implementation ----
S.append(Paragraph("3 &nbsp;&nbsp;The implementation", H1))
S.append(Paragraph(
    "Three stages, each shippable and testable on its own.", Body))
S.append(table([
    ["Stage", "What it does", "Why it was needed"],
    ["<b>0</b>", "The bounce subtracts a surface's emissive only where the "
                 "emitter list actually answers for it.",
     "It subtracted whenever the list was non-empty <i>at all</i>, though "
     "membership is filtered three ways &mdash; so light from anything the "
     "list left out was removed by one estimator and added by none. It simply "
     "vanished."],
    ["<b>1</b>", "An emissive map's <b>mean</b>, read at load, folded into the "
                 "emitter's radiance.",
     "Makes the <i>total power</i> right for any map, in any scene, however "
     "authored. The phantom becomes impossible."],
    ["<b>2</b>", "A <b>32&times;32 luminance table</b> per map. The sampler "
                 "picks a cell in proportion to what it emits, maps it back "
                 "through an affine uv map, and reads the real texel.",
     "Makes the light arrive from <i>where it is painted</i>, not spread over "
     "the whole rectangle."],
], [16 * mm, 63 * mm, COL_W - 79 * mm]))

S.append(Paragraph("What it will and will not do", H2))
S.append(Paragraph(
    "Stage 2 engages for the flat primitives whose texture coordinates the "
    "engine can state exactly, with untransformed uv, and a map whose "
    "distribution is worth aiming at. Anything else &mdash; a modelled "
    "fitting, a tiled material, a uniform map &mdash; falls back to stage 1, "
    "<b>which is never wrong, only average</b>. That fallback is the whole "
    "safety argument: the worst case is the behaviour the engine had before, "
    "not a new failure.", Body))

S.append(PageBreak())

# --------------------------------------------------------------- results ----
S.append(Paragraph("4 &nbsp;&nbsp;What it measures", H1))
S.append(Paragraph(
    "Same scene file, same frame, same settings. Only the binary differs.", Body))

S.append(duo("before-phantom.jpg", "after-aimed.jpg",
             "<b>Left, before.</b> The whole ceiling acts as a light. "
             "<b>Right, after.</b> The same scene and the same frame, with the "
             "bounce aimed at the lit texels. The two differ over 1.5 million "
             "pixels, by up to 175 levels."))

S.append(table([
    ["", "grain on the paint", "car", "wall", "floor", "frame-to-frame crawl"],
    ["before", "3.31", "64.5", "19", "17", "91 levels"],
    ["after", "<b>2.31</b>", "35.9", "11", "11", "<b>11 levels</b>"],
    ["the hand-split workaround, for reference", "2.34", "34.8", "&mdash;",
     "&mdash;", "7 levels"],
], [52 * mm, 26 * mm, 13 * mm, 13 * mm, 13 * mm, COL_W - 117 * mm],
    highlight=2))

S.append(Paragraph(
    "<b>The last row is the point.</b> The hand-split version is what the "
    "scene looked like after an artist pulled the lit cells out into their "
    "own mesh by hand &mdash; surgery on the scene to work around a defect in "
    "the renderer. The feature reaches the same picture from plain authoring, "
    "and spends no emitter slots doing it.", Body))

S.append(picture("before-workaround-split-mesh.jpg",
                 "<b>The workaround this replaces.</b> Correct, but it cost a "
                 "second mesh, a second material, and an artist's afternoon "
                 "&mdash; per fitting.", width=COL_W * 0.72))

S.append(Paragraph("On a purpose-built fixture", H2))
S.append(Paragraph(
    "A dark room, one ceiling, four lit cells of a hundred and forty-four. "
    "The middle row is the ground truth: the same four cells built as four "
    "separate emitters, which the estimator represents exactly.", Body))

S.append(table([
    ["", "floor brightness", "far-to-near gradient"],
    ["textured mesh, aimed", "<b>82.53</b>", "<b>1.40</b>"],
    ["four separate emitters (ground truth)", "82.54", "1.41"],
    ["the same total power spread evenly", "74.23", "0.99"],
], [70 * mm, 38 * mm, COL_W - 108 * mm], highlight=1))

S.append(Paragraph(
    "The gradient is the claim that only aiming can satisfy. Spread the same "
    "power evenly over the ceiling and the floor lights flatly &mdash; 0.99, "
    "no gradient at all. Aim it, and the light falls off from the fittings the "
    "way it does when the fittings are really there.", Body))

S.append(duo("fixture-textured.jpg", "fixture-split-truth.jpg",
             "<b>Left:</b> one textured mesh, aimed. <b>Right:</b> the same "
             "four cells as four separate emitters &mdash; the ground truth. "
             "82.53 against 82.54, and 1.40 against 1.41."))

S.append(picture("fixture-uniform.jpg",
                 "<b>And the same total power spread evenly</b>, which is what "
                 "the renderer did before: the floor lights flatly, gradient "
                 "0.99.", width=COL_W * 0.72))

S.append(PageBreak())

# ---------------------------------------------------------------- guards ----
S.append(Paragraph("5 &nbsp;&nbsp;How it is guarded", H1))
S.append(Paragraph(
    "Four claims, and <b>every one of them was watched to fail</b> before it "
    "was trusted &mdash; a threshold nobody has seen go red is a threshold "
    "nobody has read.", Body))
S.append(table([
    ["The claim", "What breaking it does"],
    ["A partly-lit surface emits its own power, not its whole rectangle's.",
     "Revert the fold: 3.33&times; too bright."],
    ["An emitter over there does not change a glow over here.",
     "Revert the flag: the room goes <b>exactly black</b> &mdash; a sealed "
     "emitter in a corner deleting every photon in the room."],
    ["The cooked and the raw texture paths agree on the map's mean.",
     "Revert the mip choice: 1.73&times; apart."],
    ["The light arrives from where it is painted.",
     "Disable the aiming: the gradient collapses from 1.40 to 1.00."],
], [76 * mm, COL_W - 76 * mm]))

# ------------------------------------------------------------------ cost ----
S.append(Paragraph("6 &nbsp;&nbsp;What it costs", H1))
S.append(Paragraph(
    "Nine interleaved pairs in both orders measured <b>7.13 ms before and "
    "7.50 ms after</b>, about five per cent. That number should be read with "
    "care, and the reasons are worth stating plainly rather than burying.", Body))
S.append(Paragraph(
    "The traced GI pass itself did not get slower, and a pass-by-pass "
    "comparison put the two builds within 0.05 ms &mdash; so the five per cent "
    "never landed on any nameable pass. A later investigation went looking for "
    "it and <b>could not find a mechanism</b>: every candidate path, costed "
    "from the code, came to single-digit microseconds against the three "
    "hundred and seventy the gap needed.", Body))
S.append(Paragraph(
    "Two things undermine the measurement itself. The comparison set a "
    "smoothed running average for the pass against a true mean for the frame, "
    "which are different statistics and not differenceable. And the scene it "
    "was taken on is not the one that ships &mdash; it had to be rebuilt by "
    "hand for the test and was not kept.", Body))
S.append(Paragraph(
    "<b>The honest position: an unexplained and unreproduced five per cent, on "
    "a build that is also doing more correct work than the one it is compared "
    "against.</b> It is recorded here because it was measured, not because it "
    "is understood.", Body))

# -------------------------------------------------------------- mistakes ----
S.append(Paragraph("7 &nbsp;&nbsp;The mistakes worth retelling", H1))
for head, text in [
    ("A careless revert filter cost the shader fix three times.",
     "A pattern meant to match one family of files also matched the trace "
     "shader. Twice the fix was committed as dead code and the checks still "
     "passed &mdash; because the runtime compiles the shaders staged beside "
     "the executable, and those still held it."),
    ("A cooked texture's smallest mip is not its mean.",
     "The cooker's box filter drops an odd level's tail row, so walking all "
     "the way down discards five texels of nine: 2.27&times; wrong, and a "
     "shifted variant reads exactly zero. Walk to the deepest evenly-halved "
     "level instead."),
    ("A check that tests the path nothing ships is not a check.",
     "The raw texture path was exact while the cooked one &mdash; the one that "
     "ships &mdash; was 2.27&times; wrong. The check was only looking at the "
     "raw one."),
    ("The split-mesh room is not a ground truth for total power.",
     "Equal power in a different place delivers a different fraction of itself "
     "to the floor. It is the right reference for <i>where</i> the light "
     "lands, and the wrong one for <i>how much</i>."),
    ("A plane's v runs opposite to an image's rows, and the two cancel.",
     "Get it wrong and the light goes to the opposite half of the room, "
     "showing up in no total anywhere. Count cells from the corner."),
]:
    S.append(Paragraph(f"<b>{head}</b> {text}", Body))

S += rule(8, 6)
S.append(Paragraph(
    "Full engineering record, every measurement and the rejected alternatives: "
    "<b>docs/TEXEL-EMITTERS.md</b>. Guarded by "
    "<b>tools/scripts/check_emitters.py</b> over the fixtures "
    "<b>make_emitter_scene.py</b> writes.", Small))

doc.build(S)
print("wrote", OUT)
