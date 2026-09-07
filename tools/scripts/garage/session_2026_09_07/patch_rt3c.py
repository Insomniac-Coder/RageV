"""RT-3, part C: the lit shader reads this frame's bounce by texel."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

G = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'

s = read(G)
if has(s, 'const bool giSignal ='):
    print('pbr_fragment.glsl already has the GI signal read')
    raise SystemExit(0)

s = rep(s,
    "\tif (u_Scene.Indirect.x > 0.0)\n"
    "\t{\n"
    "\t\tvec2 previousIndirectNDC = thenNDC - u_Scene.Jitter.zw;\n"
    "\t\tvec2 indirectUV = vec2(previousIndirectNDC.x,\n"
    "\t\t\t\t\t\t\t   previousIndirectNDC.y * u_Scene.Indirect.y) * 0.5 + 0.5;\n"
    "\n"
    "\t\tif (all(greaterThanEqual(indirectUV, vec2(0.0))) &&\n"
    "\t\t\tall(lessThanEqual(indirectUV, vec2(1.0))))\n"
    "\t\t{\n"
    "\t\t\tvec4 bounced = texture(u_Indirect, indirectUV);\n"
    "\t\t\tindirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a * u_Scene.Indirect.x;\n"
    "\t\t\tbounceAnswered = clamp(bounced.a, 0.0, 1.0);\n"
    "\t\t\tirradiance += indirectTerm;\n"
    "\t\t}\n"
    "\t}\n",
    "\t// **RT-3: whether this is this frame's bounce or last frame's**\n"
    "\t// (RayRates.w bit 24). Under the signal the GI trace runs between the\n"
    "\t// G-buffer and this pass, is upsampled to this pass's resolution and\n"
    "\t// settled on the reconstruction contract, and arrives at binding 16 as a\n"
    "\t// picture at this pixel's own place -- so it is fetched by texel, the way\n"
    "\t// the direct pair and the occlusion are, and there is no reprojection to\n"
    "\t// be off screen. With the signal off this is the one-frame-late buffer of\n"
    "\t// 7av, which has to be reprojected because it was written for last\n"
    "\t// frame's camera; that is the reference arm, and the only path the\n"
    "\t// screen-space forms of GI can take at all -- their gather reads the lit\n"
    "\t// image, which does not exist until this pass has run.\n"
    "\tconst bool giSignal = (int(u_Scene.RayRates.w + 0.5) & 16777216) != 0;\n"
    "\tif (u_Scene.Indirect.x > 0.0 && giSignal)\n"
    "\t{\n"
    "\t\tconst vec4 bounced = texelFetch(u_Indirect, ivec2(gl_FragCoord.xy), 0);\n"
    "\t\tindirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a * u_Scene.Indirect.x;\n"
    "\t\t// **The confidence is honest now, and that is a change of meaning.**\n"
    "\t\t// Reprojected, alpha was zero wherever last frame had no answer for\n"
    "\t\t// this point -- off the edge, or freshly uncovered -- and the field\n"
    "\t\t// below filled the hole. Traced this frame from this frame's G-buffer\n"
    "\t\t// there is no such hole: every pixel with a surface got a real\n"
    "\t\t// estimate, so alpha is one and the field's bounce steps back to\n"
    "\t\t// where it belongs, which is the far field beyond the rays' reach.\n"
    "\t\tbounceAnswered = clamp(bounced.a, 0.0, 1.0);\n"
    "\t\tirradiance += indirectTerm;\n"
    "\t}\n"
    "\telse if (u_Scene.Indirect.x > 0.0)\n"
    "\t{\n"
    "\t\tvec2 previousIndirectNDC = thenNDC - u_Scene.Jitter.zw;\n"
    "\t\tvec2 indirectUV = vec2(previousIndirectNDC.x,\n"
    "\t\t\t\t\t\t\t   previousIndirectNDC.y * u_Scene.Indirect.y) * 0.5 + 0.5;\n"
    "\n"
    "\t\tif (all(greaterThanEqual(indirectUV, vec2(0.0))) &&\n"
    "\t\t\tall(lessThanEqual(indirectUV, vec2(1.0))))\n"
    "\t\t{\n"
    "\t\t\tvec4 bounced = texture(u_Indirect, indirectUV);\n"
    "\t\t\tindirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a * u_Scene.Indirect.x;\n"
    "\t\t\tbounceAnswered = clamp(bounced.a, 0.0, 1.0);\n"
    "\t\t\tirradiance += indirectTerm;\n"
    "\t\t}\n"
    "\t}\n")

write(G, s)
print('pbr_fragment.glsl patched')
