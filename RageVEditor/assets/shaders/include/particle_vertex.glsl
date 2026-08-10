// The particle vertex stage, shared by every way of blending them.
//
// Factored out when weighted-blended transparency arrived and needed the same
// geometry with a different fragment shader. Two copies of quad expansion
// would drift the first time a facing mode or a spin convention changed, and
// the drift would look like a blending bug rather than a geometry one.

layout(set = 0, binding = 0) uniform SceneData
{
	mat4 ViewProjection;
	vec4 CameraRight;   // xyz used
	vec4 CameraUp;
} u_Scene;

struct ParticleInstance
{
	vec4 PositionSize;   // xyz world position, w size
	vec4 Color;
	vec4 Params;         // x rotation (radians), yzw unused
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceData
{
	ParticleInstance Instances[];
} u_Instances;

layout(push_constant) uniform DrawParams
{
	int BaseInstance;   // Vulkan's gl_InstanceIndex includes the base, GL's
	                    // gl_InstanceID does not -- so the base always comes
	                    // through here and the built-in is used relative.
	int Flat;           // 1 lies in the XY plane; 0 turns to the camera
} u_Draw;

layout(location = 0) out vec4 v_Color;
layout(location = 1) out vec2 v_UV;

void main()
{
	// A unit quad as two triangles, six vertices, no vertex buffer at all:
	// the corner comes from gl_VertexIndex and everything else from the
	// instance.
	const vec2 corners[6] = vec2[6](
		vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
		vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));

	ParticleInstance instance = u_Instances.Instances[u_Draw.BaseInstance + gl_InstanceIndex];

	vec2 corner = corners[gl_VertexIndex];

	float spin = instance.Params.x;
	float c = cos(spin);
	float s = sin(spin);
	vec2 turned = vec2(corner.x * c - corner.y * s,
	                   corner.x * s + corner.y * c);

	vec3 right = u_Draw.Flat != 0 ? vec3(1.0, 0.0, 0.0) : u_Scene.CameraRight.xyz;
	vec3 up    = u_Draw.Flat != 0 ? vec3(0.0, 1.0, 0.0) : u_Scene.CameraUp.xyz;

	vec3 world = instance.PositionSize.xyz
	           + (right * turned.x + up * turned.y) * instance.PositionSize.w;

	v_Color = instance.Color;
	v_UV = corner + 0.5;
	gl_Position = u_Scene.ViewProjection * vec4(world, 1.0);
}
