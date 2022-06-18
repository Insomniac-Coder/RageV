#type vertex
#version 330 core

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
out float v_TextureID;
out float v_TilingFactor;

uniform mat4 u_ViewProjection;

void main()
{	
	v_Normal = a_Normal;
	v_Color = a_Color;
	v_TexCord = a_TexCord;
	v_TextureID = a_TextureID;
	v_TilingFactor = a_TilingFactor;
	gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 a_Color;

in vec3 v_Pos;
in vec2 v_TexCord;
in vec3 v_Normal;
in vec4 v_Color;
in float v_TextureID;
in float v_TilingFactor;

uniform sampler2D u_Textures[32];
uniform vec3 u_CamPos;
uniform vec3 u_LightPos[];
uniform vec3 u_LightColor[];

void main()
{	
	// ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * u_LightColor[0];
  	
    // diffuse 
    vec3 norm = normalize(v_Normal);
    vec3 lightDir = normalize(u_LightPos[0] - v_Pos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor[0];
    
    // specular
    float specularStrength = 0.5;
    vec3 viewDir = normalize(u_CamPos - v_Pos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128);
    vec3 specular = specularStrength * spec * u_LightColor[0];  

    vec3 v_Temp = vec3(texture(u_Textures[int(v_TextureID)], v_TexCord * v_TilingFactor) * v_Color);
	a_Color = vec4((ambient + diffuse + specular) * v_Temp, 1.0f);
}