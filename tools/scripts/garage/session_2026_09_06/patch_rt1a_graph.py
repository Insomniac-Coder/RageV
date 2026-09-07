"""RT-first step 1a: the G-buffer pass. The depth prepass becomes a
G-buffer pass in a graph pass of its own ("GBuffer": velocity, normal +
roughness, albedo + metallic, surface id, depth), and the lit draws move
to the "Scene" pass that follows it, which preserves the depth. The lit
shader is unchanged in what it computes, so raster stays pixel-identical;
what changes is that every ray-traced signal can now run between the two
passes with a G-buffer to read. See docs/RT-FIRST.md."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1, skip_if=None):
    if skip_if and skip_if in s:
        return s
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)

"""The frame-graph half of step 1a, split out so it can run alone."""
# ------------------------------------------------------------ frame graph
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
s = rep(s, nl, """		constexpr Format kIndirectFormat = Format::R16G16B16A16_SFLOAT;
""", """		constexpr Format kIndirectFormat = Format::R16G16B16A16_SFLOAT;
		// The G-buffer's albedo + metallic and the surface id (RT-first step
		// 1a). Albedo in eight bits linear for now (sRGB storage once the
		// attachment path is checked); the id a float, exact to 2^24, so the
		// graph's Vec4 clear serves it.
		constexpr Format kAlbedoFormat = Format::R8G8B8A8_UNORM;
		constexpr Format kSurfaceIdFormat = Format::R32_SFLOAT;
""")
s = rep(s, nl, """		Renderer::SetTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
								   Format::R16G16_SFLOAT, kNormalFormat, kIndirectFormat);""",
"""		Renderer::SetTargetFormats(sceneDesc.Color, sceneDesc.Depth, (uint32_t)msaa,
								   Format::R16G16_SFLOAT, kNormalFormat, kIndirectFormat,
								   kAlbedoFormat, kSurfaceIdFormat);""")
s = rep(s, nl, """		const uint32_t indirectIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kIndirectFormat);
""", """		const uint32_t indirectIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kIndirectFormat);
		// The G-buffer's two (RT-first step 1a).
		const uint32_t albedoIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kAlbedoFormat);
		const uint32_t surfaceIdIndex = (uint32_t)sceneDesc.ExtraColors.size() + 1;
		sceneDesc.ExtraColors.push_back(kSurfaceIdFormat);
""")
# the Scene pass: split into GBuffer + Scene
a = s.index('\t\tgraph.AddPass("Scene",'.replace('\n', nl))
lam_a = s.index('\t\t\t[draw = desc.DrawScene,'.replace('\n', nl), a)
draw_line = ('\t\t\t\t\tdraw(context);' + nl)
lam_b = s.index(draw_line, lam_a) + len(draw_line)
# the lambda goes on after draw(context) (viewport, motion, jitter and
# reflections reset) and closes on the line "\t\t\t});" that also ends the call
close_line = '\t\t\t});' + nl
lam_close = s.index(close_line, lam_b)
lambda_text = s[lam_a:lam_close] + '\t\t\t}'
scene_pass_end = lam_close + len(close_line)
builder_text = s[a:lam_a]   # graph.AddPass("Scene",\n [&](RGPassBuilder& builder) {...},\n
old_builder_core = """				builder.WriteAttachments(sceneHDR,
					{ { 0, desc.ClearColor },
					  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { indirectIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
				builder.SetClearColor(desc.ClearColor);
				if (previousReflections != kRGInvalid)
					builder.Sample(previousReflections);
				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
""".replace('\n', nl)
new_text = ("""		// **RT-first step 1a: the G-buffer pass, then the lit pass.** The scene
		// callback (uploads, the cull, the G-buffer draw) runs in "GBuffer",
		// which binds velocity, normal, albedo, id and the depth; "Scene"
		// preserves that depth and draws the lighting through DrawLit. Any
		// ray-traced signal pass added between the two reads a finished
		// G-buffer. Without the G-buffer shader the old single pass stands.
		auto drawScene = """ + lambda_text.rstrip('\r\n') + """;
		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		Renderer3D::SetGBufferPassActive(gbufferPass);
		if (gbufferPass)
		{
			graph.AddPass("GBuffer",
				[&](RGPassBuilder& builder)
				{
					builder.WriteAttachments(sceneHDR,
						{ { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { albedoIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
						  { surfaceIdIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
					builder.SetClearColor(desc.ClearColor);
					if (previousReflections != kRGInvalid)
						builder.Sample(previousReflections);
					if (previousIndirect != kRGInvalid)
						builder.Sample(previousIndirect);
				},
				drawScene);
		}
		graph.AddPass("Scene",
			[&](RGPassBuilder& builder)
			{
				builder.WriteAttachments(sceneHDR,
					{ { 0, desc.ClearColor },
					  { velocityIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { normalIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) },
					  { indirectIndex, Vec4(0.0f, 0.0f, 0.0f, 0.0f) } });
				builder.SetClearColor(desc.ClearColor);
				if (gbufferPass)
					builder.PreserveDepth();
				if (previousReflections != kRGInvalid)
					builder.Sample(previousReflections);
				if (previousIndirect != kRGInvalid)
					builder.Sample(previousIndirect);
			},
			gbufferPass ? std::function<void(RGPassContext&)>([](RGPassContext& context)
						  {
							  // The light glow draws at the end of the lit half and
							  // needs the viewport the scene callback set and reset.
							  LightGlow::SetViewport(context.Width, context.Height);
							  Renderer3D::DrawLit();
							  LightGlow::SetViewport(0, 0);
						  })
						: std::function<void(RGPassContext&)>(drawScene));
""").replace('\n', nl)
s = s[:a] + new_text + s[scene_pass_end:]
save(p, s)
print('step 1a patched')
