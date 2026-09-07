"""RT-6.1, part B: the fourth attachment, and the composite that reads it.

The specular accumulate grows an attachment for the motion lane; the reflection
history grows the target to hold it. Then the composite -- which already knows
exactly how much of each pixel the reflection is, because the lit shader wrote
that weight into the scene's alpha -- emits a second output: the velocity the
temporal resolve should reproject this pixel by.

**Chosen, not blended.** Where the reflection is more than half of what the
pixel is made of, the resolve follows the image; otherwise it follows the
surface. A weighted average of two motions is the motion of nothing -- the same
reason this codebase point-samples its velocity lane everywhere -- and a pixel
that is mostly floor with a faint sheen wants the floor's.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

R = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
s = read(R)
if not has(s, 'RT-6.1'):
    s = rep(s,
        "\t\t\t// The trace writes its ray's direction and pdf beside the radiance;\n"
        "\t\t\t// the accumulator its surface and moments beside the picture.\n"
        "\t\t\t// The accumulators write the picture, the surface and the extra;\n"
        "\t\t\t// the pair kind a fourth (the twin) and its blur a second.\n"
        "\t\t\tconst int extras = pass == 0 ? 1 : (pass == 2 || pass == 4) ? 2 : pass == 6 ? 3 : pass == 7 ? 1 : 0;\n",
        "\t\t\t// The trace writes its ray's direction and pdf beside the radiance;\n"
        "\t\t\t// the accumulator its surface and moments beside the picture.\n"
        "\t\t\t// The accumulators write the picture, the surface and the extra;\n"
        "\t\t\t// the pair kind a fourth (the twin) and its blur a second.\n"
        "\t\t\t// **RT-6.1: and the specular accumulate a fourth of its own** -- the\n"
        "\t\t\t// virtual image's screen motion, which is the one thing the temporal\n"
        "\t\t\t// resolve cannot work out for itself and needs if the reflection is\n"
        "\t\t\t// ever to pass through it.\n"
        "\t\t\tconst int extras = pass == 0 ? 1 : pass == 2 ? 3 : pass == 4 ? 2 : pass == 6 ? 3 : pass == 7 ? 1 : 0;\n")
    write(R, s)
    print('Renderer3D.cpp: the specular accumulate gets a fourth attachment')
else:
    print('Renderer3D.cpp already done')

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
s = read(F)
if not has(s, 'RT-6.1: the fourth'):
    s = rep(s,
        "\t\t\treflections.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,\n"
        "\t\t\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT, \"ScreenReflections\",\n"
        "\t\t\t\t\t\t\t\ttracedReflections ? Format::R16G16B16A16_SFLOAT\n"
        "\t\t\t\t\t\t\t\t\t\t\t\t  : Format::Undefined,\n"
        "\t\t\t\t\t\t\t\ttracedReflections ? Format::R16G16B16A16_SFLOAT\n"
        "\t\t\t\t\t\t\t\t\t\t\t\t  : Format::Undefined);\n",
        "\t\t\t// RT-6.1: the fourth lane is the virtual image's screen motion, which\n"
        "\t\t\t// the composite hands to the temporal resolve so a reflection can be\n"
        "\t\t\t// reprojected by its own movement rather than by the floor's.\n"
        "\t\t\treflections.Prepare(Renderer::GetDevice(), desc.Width, desc.Height,\n"
        "\t\t\t\t\t\t\t\tFormat::R16G16B16A16_SFLOAT, \"ScreenReflections\",\n"
        "\t\t\t\t\t\t\t\ttracedReflections ? Format::R16G16B16A16_SFLOAT\n"
        "\t\t\t\t\t\t\t\t\t\t\t\t  : Format::Undefined,\n"
        "\t\t\t\t\t\t\t\ttracedReflections ? Format::R16G16B16A16_SFLOAT\n"
        "\t\t\t\t\t\t\t\t\t\t\t\t  : Format::Undefined,\n"
        "\t\t\t\t\t\t\t\ttracedReflections ? Format::R16G16B16A16_SFLOAT\n"
        "\t\t\t\t\t\t\t\t\t\t\t\t  : Format::Undefined);\n")
    write(F, s)
    print('FrameGraphBuilder.cpp: the reflection history holds four lanes')
else:
    print('FrameGraphBuilder.cpp already done')
