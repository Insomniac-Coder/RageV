"""WR-16 R5: at a silhouette, the accumulator's memory is shortened by the
faster of the two sides' motion, read from the scene's velocity attachment
over the 3x3, so a jitter flip keeps the long memory and an object moving
on its own leaves no trail in the reflection beside it."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def patch(p, pairs):
    s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        o = old.replace('\n', nl); n = new.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:60]); s = s.replace(o, n)
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)

patch('RageVEditor/assets/shaders/reflection_accumulate.rvshader', [(
"""layout(set = 3, binding = 5) uniform sampler2D u_HistoryExtra;
""",
"""layout(set = 3, binding = 5) uniform sampler2D u_HistoryExtra;
// The scene's velocity attachment (clip units, as TAA reads it): what the
// surfaces around a silhouette are doing, for the rule below.
layout(set = 3, binding = 6) uniform sampler2D u_Velocity;
"""),
("""const float kSmearTexels = 4.0;
""" if False else """const float kSmearTexels = 6.0;
""",
"""const float kSmearTexels = 6.0;
// **The silhouette rule's other half (WR-16 R5, 2026-09-06).** The rule
// keeps a silhouette texel's own history across the side flip the jitter
// causes, with the full memory, so the two sides average into the
// coverage. When one side is an object moving on its own with the camera
// still, that history is the other side's picture from where the object
// was: a trail. So at a silhouette the memory is also shortened by the
// fastest motion in the 3x3, the same shape as the `moved` term, down to
// kSilhouetteMemory frames. A jitter-only flip reads zero velocity
// (still geometry reprojects within 1e-5 texel) and keeps everything.
const float kSilhouetteMemory = 4.0;
"""),
("""// Whether a 3x3 neighbour lies on another surface: a silhouette texel,
// whose own surface flips sides with the jitter every frame.
bool AtSilhouette(ivec2 texel, ivec2 size, vec3 P, vec3 N, float eyeDistance)
{
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			if (x == 0 && y == 0)
				continue;
			vec3 Pn, Nn;
			if (!SurfaceAt(clamp(texel + ivec2(x, y), ivec2(0), size - 1), size, Pn, Nn))
				return true;
			if (dot(N, Nn) < 0.8 || abs(dot(N, Pn - P)) > 0.05 + 0.01 * eyeDistance)
				return true;
		}
	return false;
}
""",
"""// Whether a 3x3 neighbour lies on another surface: a silhouette texel,
// whose own surface flips sides with the jitter every frame. Also the
// fastest motion in that 3x3, in texels a frame, from the velocity
// attachment -- whichever side is moving.
bool AtSilhouette(ivec2 texel, ivec2 size, vec3 P, vec3 N, float eyeDistance, out float neighbourMotion)
{
	bool silhouette = false;
	neighbourMotion = 0.0;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			neighbourMotion = max(neighbourMotion, length(texelFetch(u_Velocity, at, 0).xy * 0.5 * vec2(size)));
			if (x == 0 && y == 0)
				continue;
			vec3 Pn, Nn;
			if (!SurfaceAt(at, size, Pn, Nn))
			{
				silhouette = true;
				continue;
			}
			if (dot(N, Nn) < 0.8 || abs(dot(N, Pn - P)) > 0.05 + 0.01 * eyeDistance)
				silhouette = true;
		}
	return silhouette;
}
"""),
("""		const bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance);""",
"""		float neighbourMotion = 0.0;
		const bool silhouette = AtSilhouette(texel, size, P, N, eyeDistance, neighbourMotion);"""),
("""			memory = min(memory, max(kSmearTexels / max(moved, 1.0e-3), kMovingMemory));
			memory = max(memory, fewest);
""",
"""			memory = min(memory, max(kSmearTexels / max(moved, 1.0e-3), kMovingMemory));
			if (silhouette)
				memory = min(memory, max(max(u_Reflection.History.y, 1.0) / (1.0 + neighbourMotion / slack),
										 kSilhouetteMemory));
			memory = max(memory, fewest);
""")])

patch('RageV/src/RageV/Renderer/Renderer3D.h', [(
"""										  const RHI::Ref<RHI::RHITexture>& previousExtra,
										  CameraMotion& motion, bool hasHistory);""",
"""										  const RHI::Ref<RHI::RHITexture>& previousExtra,
										  const RHI::Ref<RHI::RHITexture>& velocity,
										  CameraMotion& motion, bool hasHistory);""")])

patch('RageV/src/RageV/Renderer/Renderer3D.cpp', [(
"""										   const RHI::Ref<RHITexture>& previousExtra,
										   CameraMotion& motion, bool hasHistory)
	{""",
"""										   const RHI::Ref<RHITexture>& previousExtra,
										   const RHI::Ref<RHITexture>& velocity,
										   CameraMotion& motion, bool hasHistory)
	{"""),
("""		slot.ReflectionAccumulateInputs->SetTexture(5, previousExtra && hasHistory""",
"""		// The scene's velocity, for the silhouette rule (R5); black where the
		// target has none, which reads as still.
		slot.ReflectionAccumulateInputs->SetTexture(6, velocity ? velocity : s_Data->TransparentBlack,
													s_Data->PointSampler);
		slot.ReflectionAccumulateInputs->SetTexture(5, previousExtra && hasHistory""")])

patch('RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [(
"""				[resolved, sceneHDR, normalIndex, previousReflections, reflectionHistory,
				 motion = &desc.Reflections->Motion()](RGPassContext& context)
				{
					Renderer3D::AccumulateReflections(
						context.Color(resolved), context.Depth(sceneHDR),
						context.Color(sceneHDR, normalIndex),
						reflectionHistory ? context.Color(previousReflections) : nullptr,
						reflectionHistory ? context.Color(previousReflections, 1) : nullptr,
						reflectionHistory ? context.Color(previousReflections, 2) : nullptr,
						*motion, reflectionHistory);""",
"""				[resolved, sceneHDR, normalIndex, velocityIndex, previousReflections, reflectionHistory,
				 motion = &desc.Reflections->Motion()](RGPassContext& context)
				{
					Renderer3D::AccumulateReflections(
						context.Color(resolved), context.Depth(sceneHDR),
						context.Color(sceneHDR, normalIndex),
						reflectionHistory ? context.Color(previousReflections) : nullptr,
						reflectionHistory ? context.Color(previousReflections, 1) : nullptr,
						reflectionHistory ? context.Color(previousReflections, 2) : nullptr,
						context.Color(sceneHDR, velocityIndex),
						*motion, reflectionHistory);""")])
print('R5 patched')
