# RT-15: the reflection accumulator reprojects by object motion, and the image
# by the mirror rule. Every anchor must match exactly once, or nothing is written.
import io, sys

P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in src
s = src.replace('\r\n', '\n')

if 'RT-15' in s:
    sys.exit('already patched -- refusing to run again')

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
 + T + '// and is exactly zero for anything the scene marks Static.\n'
 + T + 'c.thenNdc = clipThen.xy / clipThen.w - u_Scene.Jitter.zw + objectShift;'))

# 3. The plane test stops refusing a moving object its own history.
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
 + T*3 + '// wrong one for a car: it is the same object -- the id says so\n'
 + T*3 + '// exactly, and the reprojection above now lands on it -- and what the\n'
 + T*3 + '// residual measures is how far it travelled along its own normal,\n'
 + T*3 + '// which is the number ImageThen wants rather than grounds for a\n'
 + T*3 + '// refusal. Kept whole for a Static surface (a negative id), where\n'
 + T*3 + '// nothing should have moved and a residual really is another surface.\n'
 + T*3 + 'const float wasId = texelFetch(u_HistoryIdent, pastTexel, 0).r;\n'
 + T*3 + 'const bool sameMover = g_ObjectId > 0.0 && abs(wasId - g_ObjectId) < 0.5;\n'
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

# 5. ...and the confidence does not count the motion against it either.
edits.append((
 T*3 + 'c.matchConfidence = smoothstep(0.8, 0.94, facing)\n'
 + T*7 + '  * (1.0 - smoothstep(0.35 * planeTolerance, planeTolerance,\n'
 + T*12 + '  offPlane))\n'
 + T*7 + '  * material * idPenalty * hit;',
 T*3 + '// RT-15: and neither does the confidence, for the same reason -- a\n'
 + T*3 + '// moving object scored by the plane residual would keep its history\n'
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
 '// and a near-mirror pixel shows a single ray. The scene velocity lane\n'
 '// already holds the right answer -- the rasteriser writes it through\n'
 '// PreviousModel, so object motion is in it (pbr_fragment.glsl, o_Velocity)\n'
 '// -- and this pass has had it bound at binding 6 all along for the\n'
 '// silhouette rule, which only ever took its length.\n'
 '//\n'
 '// **Returned as the difference between the two answers, not as the answer.**\n'
 '// A Static surface then contributes exactly zero rather than the rounding\n'
 '// between a rebuilt depth and an interpolated clip position, so a scene with\n'
 '// nothing moving in it measures bit-identical to the build before this. The\n'
 '// id lane sign is what says Static (pbr_fragment.glsl negates it).\n'
 'vec2 ObjectShift(ivec2 texel, vec3 P, vec2 nowNdc)\n'
 '{\n'
 + T + 'if (g_ObjectId <= 0.0)\n'
 + T*2 + 'return vec2(0.0);\n'
 + T + 'const vec4 clipThen = u_Reflection.PreviousViewProjection * vec4(P, 1.0);\n'
 + T + 'if (clipThen.w <= 0.0)\n'
 + T*2 + 'return vec2(0.0);\n'
 + T + 'const vec2 byCamera = clipThen.xy / clipThen.w - u_Scene.Jitter.zw;\n'
 + T + 'const vec2 byMotion = (nowNdc - u_Scene.Jitter.xy)\n'
 + T*6 + '  - 2.0 * texelFetch(u_Velocity, texel, 0).xy;\n'
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
 '// no memory: mirror the image back through this frame plane to recover\n'
 '// the point the ray sees, then mirror that through last frame one.\n'
 'vec3 ImageThen(vec3 P, vec3 N, vec3 sight, float image, Candidate atSurface)\n'
 '{\n'
 + T + 'const vec3 I = P + sight * image;\n'
 + T + 'const vec3 wasN = OctDecode(atSurface.reflector.rg);\n'
 + T + '// It did not turn: the shift is along the normal, and is exactly zero\n'
 + T + '// where the plane did not move, so a static reflector returns the\n'
 + T + '// image it had bit for bit.\n'
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
 + T*2 + '// Without a surface history there is no previous plane to move it by,\n'
 + T*2 + '// and the camera answer is kept as it was before this item.\n'
 + T*2 + 'vec3 imagePoint = P + sight * image;\n'
 + T*2 + 'if (haveSurface)\n'
 + T*2 + '{\n'
 + T*3 + 'image = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));\n'
 + T*3 + 'imagePoint = ImageThen(P, N, sight, image, atSurface);\n'
 + T*2 + '}\n'
 + T*2 + 'Candidate c;\n'
 + T*2 + 'bool have = HistoryAt(imagePoint, size, P, N, roughness, eyeDistance,\n'
 + T*2 + '                      silhouette, vec2(0.0), c);'))

# "shift" lands only the reprojection and the mirror rule, leaving the history
# tests exactly as they are -- the arm that says whether the reprojection alone
# is safe on a scene where nothing moves.
if (sys.argv[1] if len(sys.argv) > 1 else 'all') == 'shift':
    edits = [edits[i] for i in (0, 1, 5, 6, 7)]

for i, (old, new) in enumerate(edits, 1):
    n = s.count(old)
    if n != 1:
        sys.exit('anchor %d matched %d times, expected 1' % (i, n))
    s = s.replace(old, new, 1)

io.open(P, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
print('patched %d anchors, %d -> %d chars' % (len(edits), len(src), len(s)))
