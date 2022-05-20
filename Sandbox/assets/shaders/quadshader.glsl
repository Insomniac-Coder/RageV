#type vertex
#version 460 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TexCord;
layout(location = 3) in float a_TextureID;
layout(location = 4) in float a_TilingFactor;

out vec2 v_TexCord;
out vec4 v_Color;
out float v_TextureID;
out float v_TilingFactor;

uniform mat4 u_ViewProjection;

void main()
{	
	v_Color = a_Color;
	v_TexCord = a_TexCord;
	v_TextureID = a_TextureID;
	v_TilingFactor = a_TilingFactor;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 460 core

layout(location = 0) out vec4 a_Color;

in vec2 v_TexCord;
in vec4 v_Color;
in float v_TextureID;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];

void main()
{	
	//a_Color = vec4(v_TexCord, 0.0, 0.0);
	a_Color = texture(u_Textures[v_TextureID], v_TexCord * v_TilingFactor) * v_Color;
}