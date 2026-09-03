// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// The vertex stage of a mesh that does not deform: the attributes and the
// main that takes them to world space. Included by pbr.rvshader and by
// pbr_layered.rvshader (ENGINE-NOTES 7aq), which differ only below the
// rasteriser -- one copy, so a terrain chunk projects, jitters and writes
// its motion vector exactly as the crate resting on it does. Expects
// include/scene_vertex.glsl to have been included first.

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;

void main()
{
	InstanceData instance = FetchInstance();

	vec4 world = instance.Model * vec4(a_Position, 1.0);

	v_WorldPos = world.xyz;
	// Non-uniform scale does not preserve normals under the model matrix.
	v_Normal   = mat3(instance.NormalMatrix) * a_Normal;
	v_TexCoord = a_TexCoord;

	v_BaseColor     = instance.BaseColor;
	v_EmissiveColor = instance.EmissiveColor;
	v_Surface       = instance.Surface;
	v_Instance      = vec2(instance.Indices.y, instance.Indices.w);
	v_MaterialIndex = instance.Indices.z;

	gl_Position = u_Scene.ViewProjection * world;
	v_ClipPos = gl_Position;
	v_PrevClipPos = u_Scene.PreviousViewProjection * instance.PreviousModel *
					vec4(a_Position, 1.0);
}
