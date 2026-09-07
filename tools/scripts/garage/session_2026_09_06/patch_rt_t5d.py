"""RT-first T5, the first parity finding: the lit shader shades analytic
lights with a specular-antialiased roughness (Kaplanyan's normal-variance
widening from the shading normal's screen derivatives) and keeps the raw
roughness in o_Surface for the traced paths. The DirectTrace pass must shade
with the same widened value, and at a silhouette a pass cannot rebuild it
from the G-buffer (the neighbour's normal is another surface's), so the
G-buffer carries it: the surface-id lane becomes id + shading roughness."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
s = rep(s, nl, "\t\tconstexpr Format kSurfaceIdFormat = Format::R32_SFLOAT;",
       "\t\t// r: the object's id, negative for a Static surface; g: the roughness\n\t\t// the lit shader shades analytic lights with (specular-antialiased,\n\t\t// RT-first T5) -- o_Surface keeps the raw one for the traced lobes.\n\t\tconstexpr Format kSurfaceIdFormat = Format::R32G32_SFLOAT;"); save(p, s)
p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'; s, nl = load(p)
s = rep(s, nl, "layout(location = 3) out float o_SurfaceId;  // v_ObjectId",
       "layout(location = 3) out vec2 o_SurfaceId;   // v_ObjectId (negative: Static), shading roughness")
s = rep(s, nl, "\to_SurfaceId = v_Instance.y > 0.5 ? -v_ObjectId : v_ObjectId;",
       "\to_SurfaceId = vec2(v_Instance.y > 0.5 ? -v_ObjectId : v_ObjectId, shadingRoughness);"); save(p, s)
p = 'RageVEditor/assets/shaders/direct_trace.rvshader'; s, nl = load(p)
s = rep(s, nl, "layout(set = 3, binding = 3) uniform sampler2D u_SurfaceIdIn;",
       "layout(set = 3, binding = 3) uniform sampler2D u_SurfaceIdIn;   // id (negative: Static), shading roughness")
s = rep(s, nl, "\tp.Roughness = clamp(surface.b, 0.0, 1.0);",
       "\t// The roughness the lit shader shades analytic lights with: the\n\t// specular-antialiased one it wrote beside the id, not o_Surface's raw\n\t// value (that one is the traced lobes'). Without this the chrome poles\n\t// came out three levels darker than the loop.\n\tconst vec2 surfaceId = texelFetch(u_SurfaceIdIn, texel, 0).rg;\n\tp.Roughness = clamp(surfaceId.g, 0.0, 1.0);")
s = rep(s, nl, "\tp.Static = texelFetch(u_SurfaceIdIn, texel, 0).r < 0.0;", "\tp.Static = surfaceId.r < 0.0;"); save(p, s)
print('T5d patched')
