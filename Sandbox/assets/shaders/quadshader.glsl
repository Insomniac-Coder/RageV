#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCord;

out vec2 v_TexCord;

uniform mat4 u_ViewProjection;
uniform mat4 u_Transform;

void main()
{	
	v_TexCord = a_TexCord;
	gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 a_Color;

in vec2 v_TexCord;

uniform sampler2D a_Tex;
uniform vec4 u_Color;

void main()
{	
	//a_Color = vec4(v_TexCord, 0.0, 0.0);
	a_Color = texture(a_Tex, v_TexCord) * u_Color;
}