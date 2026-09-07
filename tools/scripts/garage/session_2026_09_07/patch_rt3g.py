"""RT-3, part G: the intensity must not wait for the old buffer's history.

The lit shader reads `u_Scene.Indirect.x` as both the GI intensity and the
switch that says there is a bounce to add at all. It is filled from
Renderer::GetScreenIndirect(), whose `haveIndirect` demands a *texture*, and
that texture was only ever set when the one-frame-late history had a frame in
it. Under the signal that history is never advanced -- the whole chain it
belongs to is replaced -- so the intensity stayed at zero, the lit shader's
branch never ran, and the signal was computed every frame and read by nobody.

Measured, and this is the useful half: a probe writing a bright constant into
every texel of the upsample changed the frame by exactly as much as the real
signal did (mean 0.799 levels, max 53.0, to three decimal places the same) --
which is what "read by nobody" looks like from the outside, and what a mean
alone would never have told apart from a working signal.

Two edits: the gate moves above the block that fills the intensity, and the
"have" test accepts a signal with no history behind it, because a signal
computed for this frame has nothing to wait for.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if has(s, 'RT-3: and the intensity is live at once'):
    print('FrameGraphBuilder.cpp already has the intensity fix')
else:
    # 1. the gate moves up, above the wantIndirect block
    s = rep(s,
        "\t\t// RT-3: the traced bounce as a signal of *this* frame -- traced from the\n"
        "\t\t// G-buffer between it and the lit pass, upsampled to the lit pass's\n"
        "\t\t// resolution and settled on the same contract the direct light and the\n"
        "\t\t// occlusion take. The traced form only: the screen-space gather reads the\n"
        "\t\t// lit image, so it cannot run before the pass that makes it and stays on\n"
        "\t\t// the one-frame-late buffer of 7av. `--gi-signal=off` is the reference\n"
        "\t\t// arm and puts the traced form back on that buffer too.\n"
        "\t\tconst bool giSignal = gbufferPass && rayGi && wantIndirect && config.GiSignal\n"
        "\t\t\t\t\t\t   && PostProcess::IsReady() && desc.GiLight != nullptr\n"
        "\t\t\t\t\t\t   && Renderer3D::CanTraceGlobalIllumination();\n",
        "")
    s = rep(s,
        "\t\tRenderer::ScreenIndirect indirectForScene;\n",
        "\t\t// RT-3: the traced bounce as a signal of *this* frame -- traced from the\n"
        "\t\t// G-buffer between it and the lit pass, upsampled to the lit pass's\n"
        "\t\t// resolution and settled on the same contract the direct light and the\n"
        "\t\t// occlusion take. The traced form only: the screen-space gather reads the\n"
        "\t\t// lit image, so it cannot run before the pass that makes it and stays on\n"
        "\t\t// the one-frame-late buffer of 7av. `--gi-signal=off` is the reference\n"
        "\t\t// arm and puts the traced form back on that buffer too.\n"
        "\t\t//\n"
        "\t\t// Resolved *here*, above the block that fills the intensity, and not\n"
        "\t\t// beside RT-2's gate further down: what that block decides is whether\n"
        "\t\t// the lit shader believes there is a bounce at all, and under the\n"
        "\t\t// signal the answer cannot come from the old buffer's history.\n"
        "\t\tconst bool giSignal = Renderer3D::GBufferPassAvailable() && rayGi && wantIndirect\n"
        "\t\t\t\t\t\t   && config.GiSignal && PostProcess::IsReady()\n"
        "\t\t\t\t\t\t   && desc.GiLight != nullptr\n"
        "\t\t\t\t\t\t   && Renderer3D::CanTraceGlobalIllumination();\n"
        "\n"
        "\t\tRenderer::ScreenIndirect indirectForScene;\n")

    # 2. the intensity, live at once under the signal
    s = rep(s,
        "\t\tif (wantIndirect)\n"
        "\t\t{\n"
        "\t\t\tTemporalHistory& indirect = *desc.Indirect;\n",
        "\t\t// **RT-3: and the intensity is live at once, with no history behind\n"
        "\t\t// it.** Indirect.x is what the lit shader reads as \"there is a bounce\n"
        "\t\t// to add, at this strength\"; the block below sets it only when the\n"
        "\t\t// one-frame-late pair has a frame in it, which is right for a buffer\n"
        "\t\t// written last frame and wrong for a signal computed for this one.\n"
        "\t\t// The texture stays null on purpose -- binding 16 is overwritten with\n"
        "\t\t// the settled signal in DrawLit, and what is bound before that is the\n"
        "\t\t// 1x1 transparent black every other set gets.\n"
        "\t\tif (giSignal)\n"
        "\t\t\tindirectForScene.Intensity = Math::Max(desc.Post.GiIntensity, 0.0f);\n"
        "\n"
        "\t\tif (wantIndirect && !giSignal)\n"
        "\t\t{\n"
        "\t\t\tTemporalHistory& indirect = *desc.Indirect;\n")
    s = rep(s,
        "\t\telse if (desc.Indirect)\n"
        "\t\t{\n"
        "\t\t\tdesc.Indirect->Invalidate();\n"
        "\t\t}\n",
        "\t\telse if (desc.Indirect)\n"
        "\t\t{\n"
        "\t\t\t// Invalidated under the signal too: the one-frame-late chain is not\n"
        "\t\t\t// running, and a history left standing would be resumed as truth the\n"
        "\t\t\t// frame --gi-signal=off puts it back in service.\n"
        "\t\t\tdesc.Indirect->Invalidate();\n"
        "\t\t}\n")
    write(F, s)
    print('FrameGraphBuilder.cpp patched')

# 3. the "have" test accepts a signal with no texture yet
C = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
s = read(C)
if has(s, 'GiSignalRequested;   // RT-3'):
    print('Renderer3D.cpp already has the have-indirect fix')
else:
    s = rep(s,
        "\t\tconst bool haveIndirect = indirect && indirect->Texture && indirect->Intensity > 0.0f;\n",
        "\t\t// RT-3: under the signal there is no texture here -- the settled bounce\n"
        "\t\t// is bound onto binding 16 in DrawLit, after this runs -- so the test is\n"
        "\t\t// the intensity and the switch, not a texture that arrives later.\n"
        "\t\tconst bool haveIndirect = indirect && indirect->Intensity > 0.0f\n"
        "\t\t\t\t\t\t\t   && (indirect->Texture != nullptr\n"
        "\t\t\t\t\t\t\t\t   || s_Data->GiSignalRequested);   // RT-3\n")
    write(C, s)
    print('Renderer3D.cpp patched')
