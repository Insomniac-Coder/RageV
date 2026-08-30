// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// The scene block, the instance buffer, the visible-index indirection and
// the batch push constant -- everything a stage that positions scene
// geometry needs, and nothing stage-specific. Split out of
// scene_vertex.glsl for the meshlet lit stage: a mesh shader reads the same
// blocks but has no gl_InstanceIndex and writes its varyings as arrays, so
// the declarations it cannot share stay behind in the vertex include.

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
	int  _pad0;
	int  _pad1;
	int  _pad2;

	// Last frame's ViewProjection. Appended, so every offset above is
	// unchanged -- this block is mirrored by hand in Renderer3D.cpp and the
	// two disagreeing is a picture that is wrong rather than a build that
	// fails. ENGINE-NOTES 7r.
	mat4 PreviousViewProjection;

	// xy = this frame's sub-pixel offset in NDC, zw = last frame's. Both
	// matrices above already carry theirs, and the fragment takes them back
	// out to get a motion vector that is the surface's movement rather than
	// the camera's dither. Zero for every mode but TAA.
	vec4 Jitter;

	// Screen-space reflections, read by the fragment stage from last frame's
	// trace. x = intensity (zero: nothing to read), y = the sign that takes
	// an NDC y-offset into the trace's row direction. ENGINE-NOTES 7af.
	vec4 ScreenReflections;

	// Ray-traced global illumination (ENGINE-NOTES 7at). x = intensity, zero
	// when the traced form is not running; y = a counter the bounce rays hash
	// so successive frames cast different directions. zw unused.
	//
	// Declared in *both* mirrors of this block -- pbr_fragment.glsl and
	// scene_vertex.glsl -- because OpenGL links the stages into one program
	// and two spellings of one uniform block is undefined ground: the first
	// draft added it to the fragment mirror alone and every OpenGL frame came
	// out different.
	vec4 GlobalIllumination;

	// Last frame's indirect diffuse (7av). Declared here because this block is
	// mirrored by hand and OpenGL links both stages into one program; the
	// fragment stage is the only one that reads it.
	vec4 Indirect;

	// **Where the reflection probes stand.** xyz the position, w the influence
	// radius, in world units; ProbeSlot[i].x is that probe's index into the
	// irradiance and environment cube arrays.
	//
	// Here rather than behind a binding of their own: set 0's bindings run 0
	// to 17 with none free, and every pipeline family allocates its own set
	// against its own layout, so a new binding would have to be declared and
	// written in each. This block is already bound everywhere and had room.
	//
	// Read by the lit shader, which blends the two that cover a fragment
	// instead of taking the one the CPU chose for the whole object. That is
	// what stops a mesh flipping its entire ambient and reflection in one
	// frame as it crosses an influence boundary.

	// **The irradiance atlas.** Centre.w is the atlas's depth -- the stride
	// from one tile of the texture to the next -- and Extents.w is how many
	// volumes it holds; zero means every reader falls back to the flat ambient
	// above. (Both carried a single composed box until 2026-08-27.)
	vec4 IrradianceCentre;
	vec4 IrradianceExtents;
	// Five rows a volume, up to eight: centre|zOffset, extents|spacing, and
	// the three rows that take a world direction into that box's own axes,
	// each carrying one of its cell counts in w.
	vec4 IrradianceBox[40];
	vec4 ProbeCount;              // x = how many rows are real
	vec4 ProbePlacement[15];
	vec4 ProbeSlot[15];
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
	// Where Model was last frame. The difference between the two projections
	// of a vertex is its motion vector -- see ENGINE-NOTES 7r.
	mat4 PreviousModel;
	// Precomputed on the CPU. It is transpose(inverse(mat3(Model))), which was
	// being computed per *vertex* -- two 3x3 inversions on every vertex of
	// every mesh, to produce a value constant across the whole instance.
	mat4 NormalMatrix;
	vec4 BaseColor;
	vec4 EmissiveColor;
	// metallic, roughness, occlusion, normal scale
	vec4 Surface;
	// x = where this instance's bones start in the bone buffer. Only the
	// skinned pipeline reads it; a static instance leaves it zero.
	//
	// y = which cube of u_Environment and u_Irradiance this object reflects.
	// Slot 0 holds the sky, so zero means "nothing better than the sky was in
	// range" rather than "unset".
	//
	// z = which record of the frame's material buffer describes this
	// instance's textures. Read only when the fragment stage is the bindless
	// variant (ENGINE-NOTES 7al); zero otherwise.
	//
	// Per instance rather than pushed per batch, because two characters sharing
	// one mesh are one instanced draw and each is in its own pose -- and,
	// for the probe, because two objects near different probes are still one
	// draw. A per-batch probe would have to be part of the sort key, and every
	// change of answer would cut a run in half. The material index is the same
	// argument taken to its end: with it here, the material is not in the sort
	// key at all.
	vec4 Indices;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBlock
{
	InstanceData Instances[];
} u_Instances;

// Which row of the buffer above each drawn instance reads (roadmap 8.16).
//
// **The instances are uploaded in submission order and never reordered; this
// is what carries the order instead.** Sorting used to be expressed by moving
// the instance data itself, so the frame ended with a gather -- fifteen
// megabytes read in scattered 256-byte runs to write one contiguous buffer.
// Four bytes an instance moves instead, and the 256 stay where they were
// written.
//
// It is also the layout GPU-driven rendering needs: a compute pass that culls
// cannot rewrite the instance data, only decide which rows survive and in what
// order. The CPU path and the GPU path therefore agree about what the shader
// reads, which is what lets one be the other's fallback.
layout(std430, set = 0, binding = 17) readonly buffer VisibleBlock
{
	uint Visible[];
} u_Visible;

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

#ifdef RV_WATER
	// **The water body's dials, in the block that already exists.** A stage may
	// declare exactly one push-constant block, and the vertex stage already has
	// this one -- so water extends it rather than adding a second, which is not
	// legal however tidy it would read.
	//
	// Padded to a vec4 boundary first: a vec4 aligns to sixteen bytes and the
	// int above occupies four, so without the three that follow, the C++ struct
	// and this block would disagree about where the first colour starts and
	// every dial would be read one lane out.
	int _WaterPad0;
	int _WaterPad1;
	int _WaterPad2;

	// rgb = the colour where the bottom is close, a = metres to reach deep.
	vec4 WaterShallow;
	// rgb = the open-water colour, a = seconds since the scene started.
	vec4 WaterDeep;
	// x = crest-to-trough metres, y = metres between crests, z = choppiness,
	// w = metres a second.
	vec4 WaterWave;
	// x = direction in radians, y = foam, z = the previous frame's time.
	vec4 WaterExtra;
	// xy = the body's rectangle in metres, z = the backdrop flag and NDC sign
	// (see water_params.glsl), w unused.
	vec4 WaterSize;
#endif
} u_Object;

