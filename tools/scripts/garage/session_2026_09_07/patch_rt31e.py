"""RT-3.1, part E: the upsample weighs normals as well as depth.

Filtering at half resolution costs edges, and the measurement says exactly how
much: the occlusion alone, full-resolution contract against half, is 0.081
levels mean with p99 2.00 -- and the diff image puts all of it on one-texel
lines along the ceiling beams, the pillars, the car's outline and the floor
seam. That is the guidance downsample choosing one of four surfaces per texel,
and the upsample then spreading the winner across the pixels of the other three.

Depth agreement alone cannot separate them: two faces of the same beam meeting
at an edge are at the same distance and differ only in which way they face. So
the upsample gets the second half of the standard bilateral test -- the tap's
normal against this pixel's -- which is the term that tells a corner from a
plane. The normals are already there: the G-buffer's lane at full resolution
for this pixel, and the guidance's for the taps, on the same grid as the signal
being upsampled.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

S = 'RageVEditor/assets/shaders/signal_upsample.rvshader'
s = read(S)
if has(s, 'u_GuideSurface'):
    print('signal_upsample.rvshader already weighs normals')
else:
    s = rep(s,
        "// The G-buffer's depth, at this pass's resolution -- read both for this\n"
        "// pixel's own surface and at each tap's centre.\n"
        "layout(set = 0, binding = 1) uniform sampler2D u_Depth;\n",
        "// The G-buffer's depth, at this pass's resolution -- read both for this\n"
        "// pixel's own surface and at each tap's centre.\n"
        "layout(set = 0, binding = 1) uniform sampler2D u_Depth;\n"
        "// **And the normals, which is what tells a corner from a plane.** Two faces\n"
        "// of the same beam meeting at an edge sit at the same distance and differ\n"
        "// only in the way they face, so a depth test alone lets a tap from one\n"
        "// vote for pixels of the other -- which is where every level of the\n"
        "// half-resolution contract's error was measured to be. This pixel's normal\n"
        "// comes from the G-buffer's lane at full resolution; each tap's from the\n"
        "// guidance, which is on the same grid as the signal being read and holds\n"
        "// the one surface the contract actually filtered for that texel.\n"
        "layout(set = 0, binding = 2) uniform sampler2D u_Normal;        // full resolution\n"
        "layout(set = 0, binding = 3) uniform sampler2D u_GuideSurface;  // the signal's grid\n")
    s = rep(s,
        "#include \"include/view_reconstruction.glsl\"\n",
        "#include \"include/view_reconstruction.glsl\"\n"
        "\n"
        "// The G-buffer stores normals octahedrally; the same decode the contract and\n"
        "// the traces use, kept local so this pass pulls in no lighting header.\n"
        "vec3 DecodeOct(vec2 e)\n"
        "{\n"
        "\tvec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));\n"
        "\tconst float t = max(-n.z, 0.0);\n"
        "\tn.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);\n"
        "\treturn normalize(n);\n"
        "}\n")
    s = rep(s,
        "\tconst float z = LinearDepth(texture(u_Depth, uv).r,\n"
        "\t\t\t\t\t\t\t\tu_Params.NearClip, u_Params.FarClip);\n",
        "\tconst float z = LinearDepth(texture(u_Depth, uv).r,\n"
        "\t\t\t\t\t\t\t\tu_Params.NearClip, u_Params.FarClip);\n"
        "\tconst vec4 here = texture(u_Normal, uv);\n"
        "\t// No surface under this pixel -- sky, or a lane never written. There is\n"
        "\t// nothing for a normal test to compare against, so it stands down and the\n"
        "\t// depth term decides alone, as it did before this test existed.\n"
        "\tconst bool haveNormal = here.b > 0.0;\n"
        "\tconst vec3 n = DecodeOct(here.rg);\n")
    s = rep(s,
        "\t\tconst float tapZ = LinearDepth(texture(u_Depth, tapUv).r,\n"
        "\t\t\t\t\t\t\t\t\t   u_Params.NearClip, u_Params.FarClip);\n"
        "\t\tconst float dw = 1.0 / (1.0e-3 + abs(tapZ - z) / max(z, 1.0e-3));\n"
        "\n"
        "\t\tsum += tap.rgb * bw * dw;\n"
        "\t\talpha += tap.a * bw * dw;\n"
        "\t\tweight += bw * dw;\n",
        "\t\tconst float tapZ = LinearDepth(texture(u_Depth, tapUv).r,\n"
        "\t\t\t\t\t\t\t\t\t   u_Params.NearClip, u_Params.FarClip);\n"
        "\t\tconst float dw = 1.0 / (1.0e-3 + abs(tapZ - z) / max(z, 1.0e-3));\n"
        "\n"
        "\t\t// **And how nearly the tap faces the way this pixel does.** A cosine\n"
        "\t\t// raised to a power rather than a hard threshold: a threshold would\n"
        "\t\t// make a curved surface's upsample switch taps abruptly and show the\n"
        "\t\t// half-resolution grid as a staircase. Sixteen is steep enough that a\n"
        "\t\t// perpendicular face contributes essentially nothing while a\n"
        "\t\t// degree or two of curvature costs almost none of its vote.\n"
        "\t\tconst vec4 tapSurface = texture(u_GuideSurface, tapUv);\n"
        "\t\tconst float nw = (haveNormal && tapSurface.b > 0.0)\n"
        "\t\t\t\t\t\t? pow(max(dot(DecodeOct(tapSurface.rg), n), 0.0), 16.0)\n"
        "\t\t\t\t\t\t: 1.0;\n"
        "\t\tconst float w = bw * dw * max(nw, 1.0e-4);\n"
        "\n"
        "\t\tsum += tap.rgb * w;\n"
        "\t\talpha += tap.a * w;\n"
        "\t\tweight += w;\n")
    write(S, s)
    print('signal_upsample.rvshader: normal weighting added')

# --- the two extra textures through PostProcess --------------------------
H = 'RageV/src/RageV/Renderer/PostProcess.h'
s = read(H)
if not has(s, 'const RHI::Ref<RHI::RHITexture>& guideSurface'):
    s = rep(s,
        "\t\tstatic void SignalUpsample(RHI::RHICommandList& cmd,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& signal,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t   uint32_t srcWidth, uint32_t srcHeight,\n"
        "\t\t\t\t\t\t\t   float nearClip, float farClip,\n"
        "\t\t\t\t\t\t\t   RHI::Format outputFormat);\n",
        "\t\tstatic void SignalUpsample(RHI::RHICommandList& cmd,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& signal,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t   // The G-buffer's normal lane at this pass's resolution,\n"
        "\t\t\t\t\t\t\t   // and the guidance's on the signal's -- the bilateral's\n"
        "\t\t\t\t\t\t\t   // second term. Null on both leaves it depth-only.\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& normal,\n"
        "\t\t\t\t\t\t\t   const RHI::Ref<RHI::RHITexture>& guideSurface,\n"
        "\t\t\t\t\t\t\t   uint32_t srcWidth, uint32_t srcHeight,\n"
        "\t\t\t\t\t\t\t   float nearClip, float farClip,\n"
        "\t\t\t\t\t\t\t   RHI::Format outputFormat);\n")
    write(H, s)
    print('PostProcess.h: SignalUpsample takes the normals')

C = 'RageV/src/RageV/Renderer/PostProcess.cpp'
s = read(C)
if not has(s, 'const Ref<RHITexture>& guideSurface'):
    s = rep(s,
        "\tvoid PostProcess::SignalUpsample(RHICommandList& cmd, const Ref<RHITexture>& signal,\n"
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t uint32_t srcWidth, uint32_t srcHeight,\n"
        "\t\t\t\t\t\t\t\t float nearClip, float farClip, Format outputFormat)\n",
        "\tvoid PostProcess::SignalUpsample(RHICommandList& cmd, const Ref<RHITexture>& signal,\n"
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& depth,\n"
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& normal,\n"
        "\t\t\t\t\t\t\t\t const Ref<RHITexture>& guideSurface,\n"
        "\t\t\t\t\t\t\t\t uint32_t srcWidth, uint32_t srcHeight,\n"
        "\t\t\t\t\t\t\t\t float nearClip, float farClip, Format outputFormat)\n")
    s = rep(s,
        "\t\tDispatch(cmd, Shader::SignalUpsample, outputFormat, signal, depth,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Linear, Sampling::Point);\n",
        "\t\t// The signal linear -- the taps land on exact source texel centres, so\n"
        "\t\t// the filter hands each one back whole and the weighting is the\n"
        "\t\t// shader's own. Everything else point: a depth or a normal halfway\n"
        "\t\t// between two surfaces describes neither.\n"
        "\t\tDispatch(cmd, Shader::SignalUpsample, outputFormat, signal, depth,\n"
        "\t\t\t\t &params, sizeof(params), Sampling::Linear, Sampling::Point,\n"
        "\t\t\t\t normal ? normal : TextureLoader::TransparentBlack(*s_Data->Device),\n"
        "\t\t\t\t Sampling::Point,\n"
        "\t\t\t\t guideSurface ? guideSurface : TextureLoader::TransparentBlack(*s_Data->Device),\n"
        "\t\t\t\t Sampling::Point);\n")
    write(C, s)
    print('PostProcess.cpp: SignalUpsample binds the normals')
