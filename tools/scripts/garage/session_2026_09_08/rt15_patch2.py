"""RT-15, the shader half -- second attempt, with the two guards the first lacked.

The first attempt moved 49.7% of the parked garage's pixels. Measured, the cause
was in two places and neither was the reprojection on real geometry (that reads
0.000 there):

  * the on-screen UI strip writes o_Velocity = 0 unconditionally
    (ui_world.rvshader and its siblings), so for those pixels the lane and the
    matrix disagree and the correction was noise up to 3.9 texels. A pixel the
    renderer says did not move did not move: no correction.

  * ImageThen read the plane out of whichever candidate the nine-tap search
    returned. On a neighbour that plane belongs to a different point of the
    surface, so the residual is spatial variation and not motion, and doubling
    it threw the image lookup metres off. Only the texel's own history can say
    how far the plane travelled.

Every anchor must match exactly once, or nothing is written.
"""
import io, sys

P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')

if 'RT-15' in s:
    sys.exit('already patched -- revert to HEAD first')

T = '\t'
edits = []

# 1. HistoryAt takes the correction.
edits.append((
 'bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n'
 + T*3 + '   float eyeDistance, bool silhouette, out Candidate c)',
 'bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,\n'
 + T*3 + '   float eyeDistance, bool silhouette, vec2 objectShift,\n'
 + T*3 + '   out Candidate c)'))

# 2. ...and applies it to the reprojection.
edits.append((
 T + 'c.thenNdc = clipThen.xy / clipThen.w - u_Scene.Jitter.zw;',
 T + '// **RT-15: plus what the matrix cannot see.** The projection above is\n'
 + T + '// the camera answer alone; objectShift carries the surface own motion,\n'
 + T + '// and is exactly zero wherever the scene says nothing moved.\n'
 + T + 'c.thenNdc = clipThen.xy / clipThen.w - u_Scene.Jitter.zw + objectShift;'))

# 3. The plane test stops refusing a moving object its own history -- but only
#    where the residual can only be motion.
edits.append((
 T*3 + 'const float planeTolerance = 0.05 + 0.01 * eyeDistance;\n'
 + T*3 + 'const float offPlane = abs(dot(wasN, P) - c.reflector.b);\n'
 + T*3 + 'if (offPlane > planeTolerance)\n'
 + T*3 + '{\n'
 + T*4 + 'if (k == 0) g_Refusal = 4;\n'
 + T*4 + 'continue;\n'
 + T*3 + '}',
 T*3 + 'const float planeTolerance = 0.05 + 0.01 * eyeDistance;\n'
 + T*3 + 'const float offPlane = abs(dot(wasN, P) - c.reflector.b);\n'
 + T*3 + '// **RT-15: on the same moving object, this residual IS the motion.**\n'
 + T*3 + '// The test asks whether the surface is still in the plane it was in,\n'
 + T*3 + '// which is the right question for a scene that does not move and the\n'
 + T*3 + '// wrong one for a car driving along its own normal. Three conditions,\n'
 + T*3 + '// and all three are needed: the pixel actually moved (objectShift is\n'
 + T*3 + '// zero wherever the renderer says nothing did), the history is the\n'
 + T*3 + '// texel own rather than a neighbour the search reached (a neighbour\n'
 + T*3 + '// plane is a different point of the surface, so its residual is shape\n'
 + T*3 + '// and not travel), and the object id matches exactly.\n'
 + T*3 + 'const float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n'
 + T*3 + 'const bool sameMover = dot(objectShift, objectShift) > 0.0 && k == 0\n'
 + T*4 + '  && abs(wasId - g_ObjectId) < 0.5;\n'
 + T*3 + 'if (offPlane > planeTolerance && !sameMover)\n'
 + T*3 + '{\n'
 + T*4 + 'if (k == 0) g_Refusal = 4;\n'
 + T*4 + 'continue;\n'
 + T*3 + '}'))

# 4. The id fetch moved up; drop the second one.
edits.append((
 T*3 + 'const float hit = HitConfidence(g_FreshImage, c.reflector.a, roughness);\n'
 + T*3 + 'const float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n'
 + T*3 + 'const float idPenalty =',
 T*3 + 'const float hit = HitConfidence(g_FreshImage, c.reflector.a, roughness);\n'
 + T*3 + 'const float idPenalty ='))

# 5. ...and the confidence does not count that motion against it either.
edits.append((
 T*3 + 'c.matchConfidence = smoothstep(0.8, 0.94, facing)\n'
 + T*7 + '  * (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n'
 + T*12 + '  offPlane))\n'
 + T*7 + '  * material * idPenalty * hit;',
 T*3 + '// RT-15: and neither does the confidence, on the same three counts --\n'
 + T*3 + '// a moving object scored by the plane residual would keep its history\n'
 + T*3 + '// and then be trusted with none of it, which is the same defect one\n'
 + T*3 + '// step further down.\n'
 + T*3 + 'const float planeAgree = sameMover ? 1.0\n'
 + T*7 + '  : (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n'
 + T*12 + '  offPlane));\n'
 + T*3 + 'c.matchConfidence = smoothstep(0.8, 0.94, facing)\n'
 + T*7 + '  * planeAgree\n'
 + T*7 + '  * material * idPenalty * hit;'))

# 6. The two helpers, before HistoryAt.
helpers = (
 '// **RT-15: how far this pixel surface moved on its own**, in the same\n'
 '// projected coordinate the history is addressed in.\n'
 '//\n'
 '// PreviousViewProjection asks where a world point would have been last\n'
 '// frame *if it had not moved*. For anything that did move that is the wrong\n'
 '// place: with the camera standing still it names this very texel, where the\n'
 '// wall behind the object was, so every test refuses, frames falls to one\n'
 '// and a near-mirror pixel shows a single ray. The scene velocity lane holds\n'
 '// the right answer -- the rasteriser writes it through PreviousModel, so\n'
 '// object motion is in it -- and this pass has had it bound at binding 6 all\n'
 '// along for the silhouette rule, which only ever took its length.\n'
 '//\n'
 '// **Returned as the difference between the two answers, not as the answer**,\n'
 '// so a surface that did not move contributes nothing and the tuning measured\n'
 '// on still scenes is untouched. Measured on the parked garage: zero for 95%\n'
 '// of the frame and below a sixtieth of a texel elsewhere -- except on the\n'
 '// on-screen UI strip, whose shaders write o_Velocity = 0 unconditionally\n'
 '// (ui_world.rvshader and its siblings) so that the lane and the matrix\n'
 '// disagree by up to 3.9 texels there. Hence the first line: a pixel the\n'
 '// renderer reports as not having moved has not moved, and gets no\n'
 '// correction. That also covers the sky, the grid and the particles.\n'
 'vec2 ObjectShift(ivec2 texel, vec3 P, vec2 nowNdc)\n'
 '{\n'
 + T + 'const vec2 velocity = texelFetch(u_Velocity, texel, 0).xy;\n'
 + T + 'if (dot(velocity, velocity) <= 0.0)\n'
 + T*2 + 'return vec2(0.0);\n'
 + T + 'const vec4 clipThen = u_Reflection.PreviousViewProjection * vec4(P, 1.0);\n'
 + T + 'if (clipThen.w <= 0.0)\n'
 + T*2 + 'return vec2(0.0);\n'
 + T + 'const vec2 byCamera = clipThen.xy / clipThen.w - u_Scene.Jitter.zw;\n'
 + T + 'const vec2 byMotion = (nowNdc - u_Scene.Jitter.xy) - 2.0 * velocity;\n'
 + T + 'return byMotion - byCamera;\n'
 '}\n'
 '\n'
 '// **RT-15: and where the picture behind the surface stood last frame.**\n'
 '//\n'
 '// The reflection is not painted on the reflector, so it does not travel\n'
 '// with it. What is seen stands as far behind the plane as the thing itself\n'
 '// stands in front, which gives two cases and no others: a face sliding\n'
 '// along its own plane leaves the image exactly where it was, and a face\n'
 '// moving along its normal by d moves the image by **2d** -- once because\n'
 '// the plane carried it, and once because the gap behind has to match the\n'
 '// gap in front, which just grew by d. Shifting the image by the surface\n'
 '// own motion, which is what the velocity lane alone would say, is wrong in\n'
 '// both: too far in the first, half as far in the second.\n'
 '//\n'
 '// Both numbers are already written every frame for the history tests -- the\n'
 '// reflector normal and its plane, in the second attachment -- so the\n'
 '// general case, a reflector that also **turns**, costs two reflections and\n'
 '// no memory: mirror the image back through this frame plane to recover the\n'
 '// point the ray sees, then mirror that through last frame one.\n'
 '//\n'
 '// **Only ever called with the texel own history** (see the call site): on a\n'
 '// neighbour the search reached, the stored plane belongs to a different\n'
 '// point of the surface and the residual is its shape, not its travel.\n'
 'vec3 ImageThen(vec3 P, vec3 N, vec3 sight, float image, Candidate atSurface)\n'
 '{\n'
 + T + 'const vec3 I = P + sight * image;\n'
 + T + 'const vec3 wasN = OctDecode(atSurface.reflector.rg);\n'
 + T + '// It did not turn: the shift is along the normal, and is exactly zero\n'
 + T + '// where the plane did not move, so a still reflector returns the image\n'
 + T + '// it had bit for bit.\n'
 + T + 'if (dot(wasN, N) > 0.9999)\n'
 + T*2 + 'return I - 2.0 * (dot(N, P) - atSurface.reflector.b) * N;\n'
 + T + 'const vec3 seen = I - 2.0 * (dot(N, I) - dot(N, P)) * N;\n'
 + T + 'return seen - 2.0 * (dot(wasN, seen) - atSurface.reflector.b) * wasN;\n'
 '}\n'
 '\n')
edits.append(('bool HistoryAt(vec3 world', helpers + 'bool HistoryAt(vec3 world'))

# 7. main: the surface candidate.
edits.append((
 T*2 + 'const bool haveSurface = HistoryAt(P, size, P, N, roughness, eyeDistance, silhouette, atSurface);',
 T*2 + '// RT-15: the surface history is looked for where the surface was, not\n'
 + T*2 + '// where the camera alone would put it.\n'
 + T*2 + 'const vec2 objectShift = ObjectShift(texel, P, nowNdc);\n'
 + T*2 + 'const bool haveSurface = HistoryAt(P, size, P, N, roughness, eyeDistance,\n'
 + T*2 + '                                   silhouette, objectShift, atSurface);'))

# 8. main: the image candidate.
edits.append((
 T*2 + 'if (haveSurface)\n'
 + T*3 + 'image = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));\n'
 + T*2 + 'Candidate c;\n'
 + T*2 + 'bool have = HistoryAt(P + sight * image, size, P, N, roughness, eyeDistance, silhouette, c);',
 T*2 + '// RT-15: and the image is looked for where the *image* was, which is\n'
 + T*2 + '// only the same place when the reflector stood still. No shift is\n'
 + T*2 + '// passed with it: ImageThen has already moved the world point to\n'
 + T*2 + '// where it stood, so the camera matrix alone is the whole of it.\n'
 + T*2 + '//\n'
 + T*2 + '// `bilinear` is the search saying it took the texel own history\n'
 + T*2 + '// rather than a neighbour. Only that one knows where this surface\n'
 + T*2 + '// plane stood last frame; a neighbour plane is a different point of\n'
 + T*2 + '// the same surface, and doubling that difference throws the lookup\n'
 + T*2 + '// off by metres. Without it the camera answer is kept, as before.\n'
 + T*2 + 'vec3 imagePoint = P + sight * image;\n'
 + T*2 + 'if (haveSurface)\n'
 + T*2 + '{\n'
 + T*3 + 'image = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));\n'
 + T*3 + 'imagePoint = atSurface.bilinear ? ImageThen(P, N, sight, image, atSurface)\n'
 + T*3 + '                                : P + sight * image;\n'
 + T*2 + '}\n'
 + T*2 + 'Candidate c;\n'
 + T*2 + 'bool have = HistoryAt(imagePoint, size, P, N, roughness, eyeDistance,\n'
 + T*2 + '                      silhouette, vec2(0.0), c);'))

for i, (old, new) in enumerate(edits, 1):
    n = s.count(old)
    if n != 1:
        sys.exit('anchor %d matched %d times, expected 1' % (i, n))
    s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('patched %d anchors, %d -> %d chars' % (len(edits), len(src), len(s)))
