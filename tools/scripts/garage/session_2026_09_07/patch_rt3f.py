"""RT-3, part F: the signal's alpha is a frame count, not a confidence.

The one-frame-late buffer and the contract disagree about what alpha means,
and the lit shader was reading the contract's with the old buffer's meaning.

  gi_denoise writes alpha = 1, always -- a validity flag, and the lit shader
  multiplies the bounce by it.
  The contract writes alpha = the number of frames standing behind the value
  (reflection_accumulate: `o_Accumulated = vec4(kept, frames)`), and zero on a
  texel where no surface stood at all.

So the multiply turned the bounce into the bounce times a frame count -- and
where the accumulate found no surface, into nothing at all. This is the same
class of defect ENGINE-NOTES 7ay recorded when the screen-space chain arrived
at gi_denoise with linear depth in the alpha: measured +6.93 against a correct
+1.27, and it looked like a feedback loop rather than a unit error.

Under the signal, then: the irradiance is taken whole, and the alpha is read
as what it is -- one or more frames means this pixel has an estimate, zero
means the accumulate found no surface here and the field should answer.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import read, write, rep, has

G = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'
s = read(G)
if has(s, 'the contract counts frames in the alpha'):
    print('pbr_fragment.glsl already has the alpha fix')
    raise SystemExit(0)

s = rep(s,
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
    "\t\tirradiance += indirectTerm;\n",
    "\t\tconst vec4 bounced = texelFetch(u_Indirect, ivec2(gl_FragCoord.xy), 0);\n"
    "\t\t// **Not multiplied by the alpha, because the contract counts frames in\n"
    "\t\t// the alpha and gi_denoise put a validity flag there.** The two paths\n"
    "\t\t// into this binding do not agree about what the fourth channel means:\n"
    "\t\t// gi_denoise writes 1 and the lit shader multiplied by it; the\n"
    "\t\t// accumulate writes how many frames stand behind the value (up to the\n"
    "\t\t// signal's Memory) and zero where no surface stood. Multiplying by that\n"
    "\t\t// scales a converged pixel's bounce by sixty-four and a fresh one's by\n"
    "\t\t// one -- the same class of unit error 7ay recorded when linear depth\n"
    "\t\t// arrived in this channel and read as a feedback loop.\n"
    "\t\tindirectTerm = max(bounced.rgb, vec3(0.0)) * u_Scene.Indirect.x;\n"
    "\t\t// **And the count read as what it is.** One frame or more means the\n"
    "\t\t// accumulate had a surface here and this pixel has an estimate; zero\n"
    "\t\t// means it had none -- sky, or a texel the G-buffer never covered --\n"
    "\t\t// and the field answers for it, which is the same rule as before by a\n"
    "\t\t// different measurement. Traced from *this* frame's G-buffer there is\n"
    "\t\t// no third case: the reprojection's hole -- off the edge of last\n"
    "\t\t// frame, or freshly uncovered -- cannot happen to a signal that was\n"
    "\t\t// computed for this frame's pixels.\n"
    "\t\tbounceAnswered = bounced.a > 0.0 ? 1.0 : 0.0;\n"
    "\t\tirradiance += indirectTerm * bounceAnswered;\n")

write(G, s)
print('pbr_fragment.glsl patched')
