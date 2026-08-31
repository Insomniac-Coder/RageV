// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// The bound material: the parameter block and the nine maps Material::Bind
// writes at set 1, exactly as it writes them. Included by pbr_fragment.glsl
// on the bound path and by voxelize.rvshader on every path (ENGINE-NOTES
// 7bc) -- one declaration, because Material::Bind writes every binding here
// whether or not the shader reads it, and a set 1 that declares fewer is a
// validation error rather than an unused one.

layout(set = 1, binding = 0) uniform MaterialData
{
	vec4  BaseColor;
	vec4  EmissiveColor;
	float Metallic;
	float Roughness;
	float Occlusion;
	float NormalScale;
	int   MapFlags;
	// F0 = 0.08 * Specular for anything that is not metal. 0.5 is the 4% this
	// used to hardcode.
	float Specular;
	// Depth of the parallax displacement, in UV units.
	float HeightScale;
	// Below this, a masked fragment is discarded. This word was the padding
	// std140 needs to reach a vec4 boundary -- the one that had to be declared
	// or the offsets here and in MaterialParams would disagree and the block
	// would read the wrong sixteen bytes rather than fail. It is still that
	// padding; it now carries something.
	float AlphaCutoff;
	// xy scale, zw offset. See MaterialParams::UvTransform.
	vec4  UvTransform;
	// **Large-scale variation from world position.** x metres per cycle, y
	// strength, zw spare. Zero is off, which is what every material authored
	// before this carries. See MaterialParams::Macro.
	vec4  Macro;
	// Named on both sides. Four floats and a vec3+float occupy exactly the two
	// vec4s' worth of std140 they replace. See MaterialParams.
	float Clearcoat;
	float ClearcoatRoughness;
	float Anisotropy;
	float Subsurface;
	vec3  SheenColor;
	float SheenRoughness;
} u_Material;

layout(set = 1, binding = 1) uniform sampler2D u_BaseColorMap;
layout(set = 1, binding = 2) uniform sampler2D u_NormalMap;
// Binding 3 held the packed metallic-roughness map. glTF was the only source
// of one and the importer splits it now, so nothing writes this -- the slot
// stays declared because the descriptor set layout is built from the shader.
layout(set = 1, binding = 3) uniform sampler2D u_Unused3;
layout(set = 1, binding = 4) uniform sampler2D u_OcclusionMap;
layout(set = 1, binding = 5) uniform sampler2D u_EmissiveMap;
// Separate greyscale roughness and metallic, read from red. Every texture
// library that is not a glTF ships them this way.
layout(set = 1, binding = 6) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 7) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 8) uniform sampler2D u_SpecularMap;
layout(set = 1, binding = 9) uniform sampler2D u_HeightMap;
