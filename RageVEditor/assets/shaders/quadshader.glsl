#type vertex
#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec2 a_TexCord;
layout(location = 4) in float a_TextureID;
layout(location = 5) in float a_TilingFactor;

out vec3 v_Pos;
out vec2 v_TexCord;
out vec3 v_Normal;
out vec4 v_Color;
flat out int v_TextureID;
out float v_TilingFactor;

uniform mat4 u_ViewProjection;

void main()
{
	// Renderer2D pre-transforms vertices on the CPU, so a_Position is already
	// world space. v_Pos was declared but never written, which left the
	// fragment stage lighting against uninitialised varyings.
	v_Pos = a_Position;
	v_Normal = a_Normal;
	v_Color = a_Color;
	v_TexCord = a_TexCord;
	// Carried as a flat int: interpolating a float slot index and rounding it
	// in the fragment shader is not guaranteed to be dynamically uniform.
	v_TextureID = int(a_TextureID + 0.5);
	v_TilingFactor = a_TilingFactor;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 450 core

const int MAX_LIGHTS = 8;

layout(location = 0) out vec4 o_Color;

in vec3 v_Pos;
in vec2 v_TexCord;
in vec3 v_Normal;
in vec4 v_Color;
flat in int v_TextureID;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];
uniform vec3 u_CamPos;
// These were declared as unsized arrays (`uniform vec3 u_LightPos[];`), which
// is not legal GLSL and failed to compile.
uniform vec3 u_LightPos[MAX_LIGHTS];
uniform vec3 u_LightColor[MAX_LIGHTS];
uniform int  u_LightCount;

void main()
{
	vec4 albedo = texture(u_Textures[v_TextureID], v_TexCord * v_TilingFactor) * v_Color;

	vec3 norm = normalize(v_Normal);
	vec3 viewDir = normalize(u_CamPos - v_Pos);

	const float ambientStrength = 0.1;
	const float specularStrength = 0.5;

	vec3 lighting = vec3(0.0);

	for (int i = 0; i < u_LightCount && i < MAX_LIGHTS; ++i)
	{
		vec3 lightColor = u_LightColor[i];

		vec3 ambient = ambientStrength * lightColor;

		vec3 lightDir = normalize(u_LightPos[i] - v_Pos);
		float diff = max(dot(norm, lightDir), 0.0);
		vec3 diffuse = diff * lightColor;

		vec3 reflectDir = reflect(-lightDir, norm);
		// The exponent was the integer literal 128, an implicit int/float cast
		// the driver rejects.
		float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0);
		vec3 specular = specularStrength * spec * lightColor;

		lighting += ambient + diffuse + specular;
	}

	// With no lights in the scene, show the unlit albedo rather than black.
	if (u_LightCount == 0)
		lighting = vec3(1.0);

	o_Color = vec4(lighting * albedo.rgb, albedo.a);
}
