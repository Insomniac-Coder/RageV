// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
// Shared by every *vertex* stage that lights a surface; the blocks
// themselves live in scene_block.glsl, shared further -- the meshlet lit
// stage reads them too, and a mesh shader can take neither gl_InstanceIndex
// nor scalar varyings, which is exactly what stays here.

#include "scene_block.glsl"

// This vertex's instance, through the indirection above. Every vertex stage
// that draws scene geometry goes through here rather than indexing the
// instance buffer itself, so there is one place the indirection can be wrong.
InstanceData FetchInstance()
{
	return u_Instances.Instances[u_Visible.Visible[u_Object.BaseInstance + gl_InstanceIndex]];
}

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
// flat: these are per instance, so interpolating them across a triangle would
// be interpolating a constant at the cost of a varying slot's worth of work.
layout(location = 3) flat out vec4 v_BaseColor;
layout(location = 4) flat out vec4 v_EmissiveColor;
layout(location = 5) flat out vec4 v_Surface;
// The clip-space position, so the fragment can recover its own normalised
// device coordinate. That is the space the light grid's tiles were cut in, and
// unlike gl_FragCoord it needs neither the target's pixel size nor a per-backend
// flip -- the two backends differ in where NDC lands on the framebuffer, not in
// the NDC itself.
layout(location = 6) out vec4 v_ClipPos;
// x: which cube of the probe arrays this object reflects; y: whether the
// object is static (MeshComponent::Static, from Indices.w) -- both straight
// out of the instance, and flat for the same reason the surface parameters
// are: one value for the whole object. The skinned and water stages write y
// as 0: a pose moves, and the water stays live for the lamps' streak.
layout(location = 7) flat out vec2 v_Instance;

// Where this vertex was last frame, in clip space. The fragment differences
// the two projections to get its motion vector.
layout(location = 8) out vec4 v_PrevClipPos;
// Which record of the frame's material buffer this instance reads, straight
// out of Indices.z. Read only by the bindless fragment variant.
layout(location = 9) flat out float v_MaterialIndex;

