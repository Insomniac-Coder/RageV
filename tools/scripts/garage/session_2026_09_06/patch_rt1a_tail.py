"""RT-first step 1a, second half: what the scene draws after its meshes
(the sky, the viewport grid, the world text, the editor icons, the
particles) belongs to the lit pass, not the G-buffer pass. Scene::OnRender
stashes the tail when the G-buffer pass is active and Scene::OnRenderLit
draws it, after Renderer3D::DrawLit, from the "Scene" pass. The G-buffer
pass sets the active flag only around the scene callback, so a probe face
or a shadow caster drawn outside the graph keeps the single-pass path."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')

def load(p):
    s = open(p, 'rb').read().decode('utf-8'); return s, ('\r\n' if '\r\n' in s else '\n')
def save(p, s):
    open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def rep(s, nl, old, new, count=1):
    o = old.replace('\n', nl); n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old[:70]); return s.replace(o, n)

# ---- Renderer3D: the active flag is readable
p = 'RageV/src/RageV/Renderer/Renderer3D.h'; s, nl = load(p)
if 'IsGBufferPassActive' not in s:
    s = rep(s, nl, """		static void SetGBufferPassActive(bool active);
""", """		static void SetGBufferPassActive(bool active);
		static bool IsGBufferPassActive();
"""); save(p, s)
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
if 'IsGBufferPassActive' not in s:
  s = rep(s, nl, """	void Renderer3D::SetGBufferPassActive(bool active)
	{
		if (s_Data)
			s_Data->GBufferPassActive = active;
	}
""", """	void Renderer3D::SetGBufferPassActive(bool active)
	{
		if (s_Data)
			s_Data->GBufferPassActive = active;
	}

	bool Renderer3D::IsGBufferPassActive()
	{
		return s_Data && s_Data->GBufferPassActive;
	}
"""); save(p, s)

# ---- Scene: the tail is a function; stashed under the G-buffer pass
p = 'RageV/src/RageV/Scene/Scene.h'; s, nl = load(p)
if 'OnRenderLit' in s:
    print('Scene.h already patched')
else:
  s = rep(s, nl, """		void OnRenderRuntime(float aspectRatio = 0.0f);
""", """		void OnRenderRuntime(float aspectRatio = 0.0f);
		// The lit half of a frame drawn under the G-buffer pass (RT-first
		// step 1a, docs/RT-FIRST.md): the meshes' lighting, then everything
		// OnRender draws after its meshes -- sky, grid, world text, icons,
		// particles -- which OnRender stashed instead of drawing. Called by
		// the frame graph's "Scene" pass; a no-op when nothing was stashed.
		void OnRenderLit();
""")
  # a private helper: the tail itself
  i = s.index('\t\tbool m_CapturingProbes = false;')
  s = s[:i] + ("""		// What OnRender draws after its meshes, as one function so the lit
		// pass can draw it later (RT-first step 1a).
		void RenderTail(const Camera& camera, const Mat4& cameraTransform,
						const RHI::Ref<RHI::RHITexture>& sky, Vec2 jitter,
						const ViewportGridSettings* grid, const EditorIconSettings* icons);
""").replace('\n', nl) + s[i:]
  save(p, s)

p = 'RageV/src/RageV/Scene/Scene.cpp'; s, nl = load(p)
# cut the tail out of OnRender
start_marker = ('\t\t\tRenderer3D::EndScene();' + nl + nl + '\t\t}' + nl)
a = s.index(start_marker) + len(start_marker)
tail_start = s.index('\t\tif (Renderer::HasDevice() && m_Environment.Sky != SkyType::Color)', a)
fn_end = s.index(nl + '\t}' + nl, tail_start)          # OnRender's closing brace
tail = s[tail_start:fn_end + len(nl)]
assert 'ParticleRenderer::EndScene();' in tail and 'Skybox::Draw(' in tail
comments = s[a:tail_start]                                # the comment lines between, kept with the tail
stash = """		// Under the G-buffer pass (RT-first step 1a) the meshes above wrote
		// the G-buffer and their lighting waits for the "Scene" pass; so does
		// everything below, which OnRenderLit draws there from this stash.
		if (Renderer3D::IsGBufferPassActive())
		{
			g_LitTail.Owner = this;
			g_LitTail.View = camera;
			g_LitTail.CameraTransform = cameraTransform;
			g_LitTail.Sky = sky;
			g_LitTail.Jitter = jitter;
			g_LitTail.HasGrid = grid != nullptr;
			if (grid) g_LitTail.Grid = *grid;
			g_LitTail.HasIcons = icons != nullptr;
			if (icons) g_LitTail.Icons = *icons;
			return;
		}
		RenderTail(camera, cameraTransform, sky, jitter, grid, icons);
	}

	void Scene::RenderTail(const Camera& camera, const Mat4& cameraTransform,
						   const RHI::Ref<RHI::RHITexture>& sky, Vec2 jitter,
						   const ViewportGridSettings* grid, const EditorIconSettings* icons)
	{
""".replace('\n', nl)
s = s[:a] + stash + comments + tail + s[fn_end + len(nl):]
# the stash itself, and OnRenderLit, before Scene::OnRender
s = rep(s, nl, """	void Scene::OnRender(const Camera& viewCamera, const Mat4& cameraTransform,
						 const ViewportGridSettings* grid,
						 const EditorIconSettings* icons)
	{""", """	namespace
	{
		// What OnRender stashes under the G-buffer pass for OnRenderLit
		// (RT-first step 1a). One scene draws at a time; the owner says whose.
		struct LitTail
		{
			Scene* Owner = nullptr;
			Camera View;
			Mat4 CameraTransform;
			RHI::Ref<RHI::RHITexture> Sky;
			Vec2 Jitter{ 0.0f, 0.0f };
			bool HasGrid = false;
			ViewportGridSettings Grid;
			bool HasIcons = false;
			EditorIconSettings Icons;
		};
		LitTail g_LitTail;
	}

	void Scene::OnRenderLit()
	{
		Renderer3D::DrawLit();
		if (g_LitTail.Owner != this)
			return;
		g_LitTail.Owner = nullptr;
		RenderTail(g_LitTail.View, g_LitTail.CameraTransform, g_LitTail.Sky, g_LitTail.Jitter,
				   g_LitTail.HasGrid ? &g_LitTail.Grid : nullptr,
				   g_LitTail.HasIcons ? &g_LitTail.Icons : nullptr);
		g_LitTail.Sky = nullptr;
	}

	void Scene::OnRender(const Camera& viewCamera, const Mat4& cameraTransform,
						 const ViewportGridSettings* grid,
						 const EditorIconSettings* icons)
	{""")
save(p, s)

# ---- the frame description and the two passes
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.h'; s, nl = load(p)
s = rep(s, nl, """		std::function<void(RGPassContext&)> DrawScene;
""", """		std::function<void(RGPassContext&)> DrawScene;
		// The lit half under the G-buffer pass (RT-first step 1a): the
		// scene's OnRenderLit. Null means Renderer3D::DrawLit alone.
		std::function<void(RGPassContext&)> DrawSceneLit;
"""); save(p, s)
p = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'; s, nl = load(p)
s = rep(s, nl, """		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		Renderer3D::SetGBufferPassActive(gbufferPass);
		if (gbufferPass)
		{""", """		const bool gbufferPass = Renderer3D::GBufferPassAvailable();
		// The flag is up only around the scene callback, so a probe face or a
		// shadow caster drawn outside the graph keeps the single-pass path.
		auto drawGBuffer = [drawScene](RGPassContext& context)
		{
			Renderer3D::SetGBufferPassActive(true);
			drawScene(context);
			Renderer3D::SetGBufferPassActive(false);
		};
		if (gbufferPass)
		{""")
s = rep(s, nl, """				},
				drawScene);
		}
		graph.AddPass("Scene",""", """				},
				drawGBuffer);
		}
		graph.AddPass("Scene",""")
s = rep(s, nl, """			gbufferPass ? std::function<void(RGPassContext&)>([](RGPassContext& context)
						  {
							  // The light glow draws at the end of the lit half and
							  // needs the viewport the scene callback set and reset.
							  LightGlow::SetViewport(context.Width, context.Height);
							  Renderer3D::DrawLit();
							  LightGlow::SetViewport(0, 0);
						  })
						: std::function<void(RGPassContext&)>(drawScene));""",
"""			gbufferPass ? std::function<void(RGPassContext&)>(
							  [drawLit = desc.DrawSceneLit, jitter,
							   motion = desc.History ? &desc.History->Motion() : nullptr](RGPassContext& context)
							  {
								  // The lit half: the same edges the scene callback
								  // had (viewport, jitter, camera motion), for the
								  // glow, the sky and the particles it draws.
								  LightGlow::SetViewport(context.Width, context.Height);
								  Renderer::SetJitter(jitter);
								  Renderer::SetCameraMotion(motion);
								  if (drawLit)
									  drawLit(context);
								  else
									  Renderer3D::DrawLit();
								  Renderer::SetCameraMotion(nullptr);
								  Renderer::SetJitter(Vec2(0.0f, 0.0f));
								  LightGlow::SetViewport(0, 0);
							  })
						: std::function<void(RGPassContext&)>(drawScene));""")
save(p, s)

# ---- the layers hand over the lit half
p = 'RageVRuntime/src/RuntimeLayer.cpp'; s, nl = load(p)
s = rep(s, nl, """	frame.DrawScene = [this](RGPassContext& context)
	{
		m_Scene->OnRenderRuntime((float)context.Width / (float)context.Height);
	};
""", """	frame.DrawScene = [this](RGPassContext& context)
	{
		m_Scene->OnRenderRuntime((float)context.Width / (float)context.Height);
	};
	frame.DrawSceneLit = [this](RGPassContext&)
	{
		m_Scene->OnRenderLit();
	};
"""); save(p, s)
p = 'RageVEditor/src/EditorLayer.cpp'; s, nl = load(p)
s = rep(s, nl, """	scene.DrawScene = [this](RGPassContext&)
	{""", """	scene.DrawSceneLit = [this](RGPassContext&)
	{
		m_Scene->OnRenderLit();
	};
	scene.DrawScene = [this](RGPassContext&)
	{""")
s = rep(s, nl, """		game.DrawScene = [this](RGPassContext&)
		{""", """		game.DrawSceneLit = [this](RGPassContext&)
		{
			m_Scene->OnRenderLit();
		};
		game.DrawScene = [this](RGPassContext&)
		{""")
save(p, s)
print('tail split patched')
