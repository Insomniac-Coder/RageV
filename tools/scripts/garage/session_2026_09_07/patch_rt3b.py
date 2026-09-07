"""RT-3, part B: the GI signal in Renderer3D -- the switch bit, the texture the
lit draw reads it from, and the contract tuning."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

H = 'RageV/src/RageV/Renderer/Renderer3D.h'
C = 'RageV/src/RageV/Renderer/Renderer3D.cpp'

s = read(H)
if not has(s, 'SetGiSignal'):
    s = rep(s,
        "\t\tstatic void SetScreenOcclusion(const RHI::Ref<RHI::RHITexture>& occlusion);\n"
        "\t\tstatic void DrawLit();\n",
        "\t\tstatic void SetScreenOcclusion(const RHI::Ref<RHI::RHITexture>& occlusion);\n"
        "\t\t// RT-3: whether the traced bounce is *this* frame's signal rather than\n"
        "\t\t// last frame's history (RayRates.w bit 24), and the texture the lit draw\n"
        "\t\t// reads it from. Under the signal the GI trace runs between the G-buffer\n"
        "\t\t// and the lit pass and the lit shader fetches by texel; with it off the\n"
        "\t\t// one-frame-late buffer of 7av is read through the reprojection, which is\n"
        "\t\t// the reference arm and the only path the screen-space forms can take.\n"
        "\t\tstatic void SetGiSignal(bool requested);\n"
        "\t\tstatic void SetScreenIndirectSignal(const RHI::Ref<RHI::RHITexture>& indirect);\n"
        "\t\tstatic void DrawLit();\n")
    s = rep(s,
        "\t\t// RT-2: the occlusion signal's tuning -- a diffuse-kind scalar in slot 2,\n"
        "\t\t// low-frequency enough to keep a young blur.\n"
        "\t\tstatic SignalParams AoSignal();\n",
        "\t\t// RT-2: the occlusion signal's tuning -- a diffuse-kind scalar in slot 2,\n"
        "\t\t// low-frequency enough to keep a young blur.\n"
        "\t\tstatic SignalParams AoSignal();\n"
        "\t\t// RT-3: the traced bounce's tuning -- a diffuse-kind RGB signal in slot 3.\n"
        "\t\tstatic SignalParams GiSignal();\n")
    write(H, s)
    print('Renderer3D.h patched')
else:
    print('Renderer3D.h already has SetGiSignal')

s = read(C)
if not has(s, 'GiSignalRequested'):
    # --- the members ------------------------------------------------------
    s = rep(s,
        "\t\t\tRef<RHITexture>  ScreenOcclusion;   // RT-2: the occlusion signal the lit draw reads\n",
        "\t\t\tRef<RHITexture>  ScreenOcclusion;   // RT-2: the occlusion signal the lit draw reads\n"
        "\t\t\t// RT-3: this frame's traced bounce, settled on the contract. Null\n"
        "\t\t\t// when the signal is off, and then binding 16 keeps last frame's\n"
        "\t\t\t// buffer as it always did.\n"
        "\t\t\tRef<RHITexture>  ScreenIndirectSignal;\n"
        "\t\t\tbool             GiSignalRequested = false;\n")
    # --- the switch bit ---------------------------------------------------
    s = rep(s,
        "\t\t\t\t\t\t+ (s_Data->DirectSignalRequested ? 4194304 : 0)\n"
        "\t\t\t\t\t\t+ (s_Data->AoSignalRequested ? 8388608 : 0)));\n",
        "\t\t\t\t\t\t+ (s_Data->DirectSignalRequested ? 4194304 : 0)\n"
        "\t\t\t\t\t\t+ (s_Data->AoSignalRequested ? 8388608 : 0)\n"
        "\t\t\t\t\t\t// bit 24: the traced bounce is this frame's, read by texel\n"
        "\t\t\t\t\t\t// rather than reprojected out of last frame's (RT-3).\n"
        "\t\t\t\t\t\t+ (s_Data->GiSignalRequested ? 16777216 : 0)));\n")
    # --- the setters, beside RT-2's ---------------------------------------
    s = rep(s,
        "\tvoid Renderer3D::SetScreenOcclusion(const RHI::Ref<RHITexture>& occlusion)\n"
        "\t{\n"
        "\t\tif (s_Data)\n"
        "\t\t\ts_Data->ScreenOcclusion = occlusion;\n"
        "\t}\n",
        "\tvoid Renderer3D::SetScreenOcclusion(const RHI::Ref<RHITexture>& occlusion)\n"
        "\t{\n"
        "\t\tif (s_Data)\n"
        "\t\t\ts_Data->ScreenOcclusion = occlusion;\n"
        "\t}\n\n"
        "\tvoid Renderer3D::SetGiSignal(bool requested)\n"
        "\t{\n"
        "\t\tif (s_Data)\n"
        "\t\t\ts_Data->GiSignalRequested = requested;\n"
        "\t}\n\n"
        "\tvoid Renderer3D::SetScreenIndirectSignal(const RHI::Ref<RHITexture>& indirect)\n"
        "\t{\n"
        "\t\tif (s_Data)\n"
        "\t\t\ts_Data->ScreenIndirectSignal = indirect;\n"
        "\t}\n")
    # --- the tuning -------------------------------------------------------
    s = rep(s,
        "\t\t// Occlusion is low-frequency: the young-history blur stays, bounded at\n"
        "\t\t// six texels, and the moving memory floor is the direct light's.\n"
        "\t\tsignal.YoungRadius = 6.0f;\n"
        "\t\tsignal.MaxRadius = 6.0f;\n"
        "\t\treturn signal;\n"
        "\t}\n",
        "\t\t// Occlusion is low-frequency: the young-history blur stays, bounded at\n"
        "\t\t// six texels, and the moving memory floor is the direct light's.\n"
        "\t\tsignal.YoungRadius = 6.0f;\n"
        "\t\tsignal.MaxRadius = 6.0f;\n"
        "\t\treturn signal;\n"
        "\t}\n\n"
        "\tRenderer3D::SignalParams Renderer3D::GiSignal()\n"
        "\t{\n"
        "\t\tSignalParams signal;\n"
        "\t\tsignal.Type = SignalParams::Kind::Diffuse;\n"
        "\t\tsignal.Slot = 3;\n"
        "\t\t// **The lowest-frequency signal in the frame, and the sparsest\n"
        "\t\t// estimate under it.** One to four cosine rays at half resolution is\n"
        "\t\t// four samples of a hemisphere: gi_denoise met that with a 0.98\n"
        "\t\t// feedback and a 3x3 spatial blend, and the contract meets it with a\n"
        "\t\t// long memory and the widest young blur any signal here takes.\n"
        "\t\tsignal.YoungRadius = 12.0f;\n"
        "\t\tsignal.MaxRadius = 10.0f;\n"
        "\t\t// **The bound, widened, and the reason is the upsample above it.**\n"
        "\t\t// The contract builds its bound from the fresh 3x3's spread, which\n"
        "\t\t// assumes the neighbours are independent estimates. After a joint\n"
        "\t\t// bilateral upsample from half resolution they are not -- four\n"
        "\t\t// full-resolution texels share one traced sample -- so the measured\n"
        "\t\t// spread understates the real variance and a bound built from it\n"
        "\t\t// would refuse a history that was never wrong. The temporal floor\n"
        "\t\t// (the pixel's own measured fluctuation) is what actually holds this\n"
        "\t\t// signal; this keeps the spatial ceiling from fighting it.\n"
        "\t\tsignal.BoundWidth = 6.0f;\n"
        "\t\treturn signal;\n"
        "\t}\n")
    # --- the lit-set re-commit: binding 16 with this frame's signal --------
    s = rep(s,
        "\t\tconst bool directPair = s_Data->RayShadowsOn && s_Data->DirectDiffuse && s_Data->DirectSpecular;\n"
        "\t\tif (directPair || s_Data->ScreenOcclusion)\n",
        "\t\tconst bool directPair = s_Data->RayShadowsOn && s_Data->DirectDiffuse && s_Data->DirectSpecular;\n"
        "\t\tif (directPair || s_Data->ScreenOcclusion || s_Data->ScreenIndirectSignal)\n")
    s = rep(s,
        "\t\t\t\tif (s_Data->ScreenOcclusion)\n"
        "\t\t\t\t\tlitSet->SetTexture(28, s_Data->ScreenOcclusion, s_Data->PointSampler);   // RT-2\n",
        "\t\t\t\tif (s_Data->ScreenOcclusion)\n"
        "\t\t\t\t\tlitSet->SetTexture(28, s_Data->ScreenOcclusion, s_Data->PointSampler);   // RT-2\n"
        "\t\t\t\t// RT-3: binding 16 stops being last frame's buffer and becomes this\n"
        "\t\t\t\t// frame's settled bounce. The same binding on purpose: the lit\n"
        "\t\t\t\t// shader reads one indirect image either way, and a set layout comes\n"
        "\t\t\t\t// from reflection -- a second sampler would need a second\n"
        "\t\t\t\t// declaration on six pipelines to carry the same picture. Bit 24 of\n"
        "\t\t\t\t// RayRates.w is what tells the shader which one it is holding.\n"
        "\t\t\t\tif (s_Data->ScreenIndirectSignal)\n"
        "\t\t\t\t\tlitSet->SetTexture(16, s_Data->ScreenIndirectSignal, s_Data->EnvironmentSampler);\n")
    write(C, s)
    print('Renderer3D.cpp patched')
else:
    print('Renderer3D.cpp already has GiSignalRequested')
