"""RT-6.2: the temporal resolve reads the material, and clamps by what the
surface actually is.

**The problem, stated exactly.** The resolve holds every pixel's history to a
box built from this frame's 3x3 neighbourhood. That box is the same shape
whatever the pixel is made of -- and on a detailed, view-stable surface it is
the blur. Wet concrete, brick, a graffiti wall: their shading does not change
as the eye moves, so their history is *right*, and clipping it toward the local
neighbourhood mean every frame throws away exactly the detail the accumulation
was building. On a smooth metal the opposite holds: its shading is a function of
view direction, last frame's is genuinely wrong by this frame, and the box
cannot be tight enough.

One rule for both is a compromise that suits neither, and this engine has been
paying it on the side that matters most -- the surfaces carrying the texture.

**What the material says.** The G-buffer's normal lane already carries the two
numbers that decide it: roughness in B, metallic in A. A rough dielectric is
view-stable; a smooth metal is not; and the useful measure is the product of the
two, which this calls the surface's *stability*. The box is widened by it, so
the history of a rough wall is held only against the pixel's own measured
fluctuation and the neighbourhood's genuine spread, while a chrome pole's is
clamped exactly as tightly as it was.

Read from the scene target the resolve is already reading, so this costs one
binding and no pass.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

# --- an eighth image on Dispatch -----------------------------------------
H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if not has(s, 'seventh'):
    s = rep(s,
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& sixth = nullptr,\n"
        "\t\t\t\t\t\t\t Sampling sixthSampling = Sampling::Point);\n",
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& sixth = nullptr,\n"
        "\t\t\t\t\t\t\t Sampling sixthSampling = Sampling::Point,\n"
        "\t\t\t\t\t\t\t // RT-6.2: binding 8. The temporal resolve's material lane --\n"
        "\t\t\t\t\t\t\t // the G-buffer's normal attachment, whose B and A are the\n"
        "\t\t\t\t\t\t\t // roughness and the metallic the clamp is shaped by.\n"
        "\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& seventh = nullptr,\n"
        "\t\t\t\t\t\t\t Sampling seventhSampling = Sampling::Point);\n")
    write(H, s)
    print('PostProcess.h: an eighth binding')
else:
    print('PostProcess.h already has it')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
# Guard on a whole distinctive line, never a word: 'seventh' matched a comment
# in this very file ("what keeps that true when somebody adds a seventh"), which
# is the substring trap this session already paid for once with '16777216'.
if not has(s, 'const Ref<RHITexture>& seventh, Sampling seventhSampling)'):
    s = rep(s,
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& sixth, Sampling sixthSampling)\n",
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& sixth, Sampling sixthSampling,\n"
        "\t\t\t\t\t\t\t   const Ref<RHITexture>& seventh, Sampling seventhSampling)\n")
    s = rep(s,
        "\t\tif (sixth)\n"
        "\t\t\tset->SetTexture(7, sixth, samplerFor(sixthSampling));\n",
        "\t\tif (sixth)\n"
        "\t\t\tset->SetTexture(7, sixth, samplerFor(sixthSampling));\n"
        "\t\t// RT-6.2: the material lane, for the resolve alone.\n"
        "\t\tif (seventh)\n"
        "\t\t\tset->SetTexture(8, seventh, samplerFor(seventhSampling));\n")
    # TemporalResolve takes it and passes it
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guideCurrent,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guidePrevious)\n",
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guideCurrent,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& guidePrevious,\n"
        "\t\t\t\t\t\t\t\t\t  const Ref<RHITexture>& material)\n")
    s = rep(s,
        "\t\t\t\t guideCurrent, Sampling::Point, guidePrevious, Sampling::Point);\n",
        "\t\t\t\t guideCurrent, Sampling::Point, guidePrevious, Sampling::Point,\n"
        "\t\t\t\t // RT-6.2: the roughness and metallic under this pixel. Point, like\n"
        "\t\t\t\t // everything else describing a surface: halfway between two\n"
        "\t\t\t\t // materials is a third material that is not there.\n"
        "\t\t\t\t material, Sampling::Point);\n")
    s = rep(s,
        "\t\t\t// RT-6: whether the geometric test may run this frame.\n"
        "\t\t\tfloat Geometry = 0.0f;\n"
        "\t\t};\n",
        "\t\t\t// RT-6: whether the geometric test may run this frame.\n"
        "\t\t\tfloat Geometry = 0.0f;\n"
        "\t\t\t// RT-6.2: whether the material lane is bound.\n"
        "\t\t\tfloat Material = 0.0f;\n"
        "\t\t\tfloat Pad2 = 0.0f;\n"
        "\t\t\tfloat Pad3 = 0.0f;\n"
        "\t\t};\n")
    s = rep(s,
        "\t\tfull.Geometry = (guideCurrent && guidePrevious && hasHistory) ? 1.0f : 0.0f;\n",
        "\t\tfull.Geometry = (guideCurrent && guidePrevious && hasHistory) ? 1.0f : 0.0f;\n"
        "\t\tfull.Material = material ? 1.0f : 0.0f;\n")
    write(C, s)
    print('PostProcess.cpp: the material lane through Dispatch')
else:
    print('PostProcess.cpp already has it')

H2 = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H2)
if not has(s, 'const RHI::Ref<RHI::RHITexture>& material'):
    s = rep(s,
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& guidePrevious = nullptr);\n",
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& guidePrevious = nullptr,\n"
        "\t\t\t\t\t\t\t\t\t // RT-6.2: the G-buffer's normal attachment, whose B and A\n"
        "\t\t\t\t\t\t\t\t\t // are roughness and metallic. Null leaves the clamp one\n"
        "\t\t\t\t\t\t\t\t\t // shape for every surface, which is what it was.\n"
        "\t\t\t\t\t\t\t\t\t const RHI::Ref<RHI::RHITexture>& material = nullptr);\n")
    write(H2, s)
    print('PostProcess.h: TemporalResolve takes the material')
