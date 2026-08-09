// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
// Shared by every shader that lights a surface.
//
// Included rather than copied. There are two mesh shaders -- static and
// skinned -- differing only in how a vertex reaches world space, and the
// lighting underneath them has to be one implementation. Two copies of six
// hundred lines of PBR would drift, and the way that failure shows up is two
// objects in the same scene shaded differently, which reads as a material
// problem rather than as a duplicated shader.

// The scene block, the instance buffer and the varyings a lit vertex
// stage produces. The attributes themselves are declared by the variant,
// because a skinned vertex carries two more.


layout(set = 0, binding = 0) uniform SceneData
{
	mat4 ViewProjection;
	vec4 CameraPosition;
	// rgb = ambient colour, a = ambient intensity
	vec4 Ambient;
	// x = environment intensity, y = its highest mip, zw = cos and sin of the
	// sky's rotation. Intensity is zero when there is nothing to reflect,
	// which is how the term is switched off without a branch.
	vec4 Environment;
	// x = the environment's mip-0 face size in texels. Passed rather than
	// derived from the mip count: exp2(highest mip) is only the face size when
	// the chain runs all the way down to one texel, and a prefiltered cube
	// stops at its roughness levels.
	vec4 EnvironmentSize;

	// The cluster grid. xyz = tiles across, tiles down, depth slices;
	// w = how many directional lights sit at the front of the light buffer.
	vec4 ClusterGrid;
	// x = near plane, y = far plane, zw = the scale and bias that turn a view
	// depth into a slice. Precomputed, so the shader does not repeat a log
	// division per fragment.
	vec4 ClusterDepth;

	// Shadows. CascadeLookup takes a world position straight to lookup
	// coordinates -- xy texture, z the depth to compare -- with the
	// texture-space bias and one backend's vertical flip already folded in.
	mat4 CascadeLookup[4];
	vec4 CascadeSplits;   // far view depth of each cascade
	vec4 CascadeTexel;    // world size of one texel in each
	vec4 CameraForward;   // selection needs a view depth, and there is no view matrix
	// x = cascades rendered (0 = none), y = normal offset scale,
	// z = one cascade texel in lookup coordinates, w unused
	vec4 ShadowParams;

	mat4 SpotLookup[4];

	int  LightCount;
} u_Scene;

// Per instance, indexed by the draw's instance number.
//
// A storage buffer rather than a uniform one because a batch is as long as the
// scene makes it, and a uniform block would cap it at whatever fits in 64 KB --
// a limit that differs per driver and would turn one draw into several on some
// machines and not others.
//
// The scalar surface parameters live here rather than in the material's uniform
// block, and that is the whole point: a thousand cubes that differ only in
// colour share one set of textures, so they can be one draw. If the colour
// stayed in the material block they would be a thousand.
struct InstanceData
{
	mat4 Model;
	// Precomputed on the CPU. It is transpose(inverse(mat3(Model))), which was
	// being computed per *vertex* -- two 3x3 inversions on every vertex of
	// every mesh, to produce a value constant across the whole instance.
	mat4 NormalMatrix;
	vec4 BaseColor;
	vec4 EmissiveColor;
	// metallic, roughness, occlusion, normal scale
	vec4 Surface;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBlock
{
	InstanceData Instances[];
} u_Instances;

// Where this batch starts in the buffer above.
//
// Deliberately not the draw's firstInstance parameter. Vulkan's
// gl_InstanceIndex includes the base and OpenGL's gl_InstanceID does not, so
// reading the base from the draw call would offset the instances twice on one
// backend and once on the other -- a difference that shows up as every batch
// after the first drawing the wrong objects, and only on one backend.
layout(push_constant) uniform ObjectData
{
	int BaseInstance;
} u_Object;

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

