// Generated from Stats.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;

public class Stats : Script
{
	// The graph's variables. Fields rather than locals, because
	// that is what makes them survive between one event and the next.
	private string feat;
	private float frameMs;
	private bool shown;

	public override void OnFrame(float deltaTime)
	{
		var value66 = (frameMs + (((deltaTime * 1000.0f) - frameMs) * 0.0500000007f));
		var value261 = "";

		frameMs = value66;
		if (shown)
		{
			var target81 = Entity.FindByName("Stats Frame");
			target81.Text = Text.Join(Text.Join(Text.FromNumber(value66, 2.0f), " ms      "), Text.Join(Text.FromNumber((1000.0f / value66), 0.0f), " FPS"));
			var target90 = Entity.FindByName("Stats Mode");
			target90.Text = Text.Join(Text.Join(Graphics.Api, "      "), RenderSettings.Get("AntiAliasing"));
			feat = "";
			if (Graphics.IsActive("Shadows"))
			{
				feat = Text.Join(feat, "Shadows  ");
			}
			if (Graphics.IsActive("RayTracing"))
			{
				feat = Text.Join(feat, "RayTracing  ");
			}
			if (Graphics.IsActive("RTReflections"))
			{
				feat = Text.Join(feat, "RT Reflections  ");
			}
			if (Graphics.IsActive("RTGlobalIllumination"))
			{
				feat = Text.Join(feat, "RT GI (realtime)  ");
			}
			if (Graphics.IsActive("RTGIBaked"))
			{
				feat = Text.Join(feat, "RT GI (baked)  ");
			}
			if (Graphics.IsActive("RTAmbientOcclusion"))
			{
				feat = Text.Join(feat, "RT AO  ");
			}
			if (Graphics.IsActive("VoxelGI"))
			{
				feat = Text.Join(feat, "VoxelGI  ");
			}
			var target146 = Entity.FindByName("Stats Render");
			target146.Text = feat;
			feat = "";
			if (Text.Same(RenderSettings.Get("BloomEnabled"), "true"))
			{
				feat = Text.Join(feat, "Bloom  ");
			}
			if (Text.ToNumber(RenderSettings.Get("ColorLutStrength")) > 0.0f)
			{
				feat = Text.Join(feat, "LUT  ");
			}
			if (Text.Same(RenderSettings.Get("DepthOfField"), "true"))
			{
				feat = Text.Join(feat, "DoF  ");
			}
			if (Graphics.IsActive("AmbientOcclusion"))
			{
				feat = Text.Join(feat, "AO  ");
			}
			if (Graphics.IsActive("SSR"))
			{
				feat = Text.Join(feat, "SSR  ");
			}
			if (Graphics.IsActive("SSGI"))
			{
				feat = Text.Join(feat, "SSGI (realtime)  ");
			}
			if (Graphics.IsActive("SSGIBaked"))
			{
				feat = Text.Join(feat, "SSGI (baked)  ");
			}
			if (Text.Same(RenderSettings.Get("MotionBlur"), "true"))
			{
				feat = Text.Join(feat, "MotionBlur  ");
			}
			if (Text.Same(RenderSettings.Get("AutoExposure"), "true"))
			{
				feat = Text.Join(feat, "AutoExposure  ");
			}
			if (Text.ToNumber(RenderSettings.Get("VignetteIntensity")) > 0.0f)
			{
				feat = Text.Join(feat, "Vignette  ");
			}
			if (Text.ToNumber(RenderSettings.Get("ChromaticAberration")) > 0.0f)
			{
				feat = Text.Join(feat, "Chromatic  ");
			}
			if (Text.ToNumber(RenderSettings.Get("FilmGrain")) > 0.0f)
			{
				feat = Text.Join(feat, "Grain  ");
			}
			var target260 = Entity.FindByName("Stats Post");
			target260.Text = feat;
		}
		else
		{
			var target264 = Entity.FindByName("Stats Frame");
			target264.Text = value261;
			var target267 = Entity.FindByName("Stats Mode");
			target267.Text = value261;
			var target270 = Entity.FindByName("Stats Render");
			target270.Text = value261;
			var target273 = Entity.FindByName("Stats Post");
			target273.Text = value261;
		}
	}

	public override void OnTick(float deltaTime)
	{
		var value12 = "true";
		var value35 = "false";

		if (Input.WasActionPressed("ToggleStats"))
		{
			shown = (!shown);
			if (shown)
			{
				Entity.SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Frame").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Mode").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Rule").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Render Caption").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Render").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Post Caption").SetComponentField("UIRectComponent", "Visible", value12);
				Entity.FindByName("Stats Post").SetComponentField("UIRectComponent", "Visible", value12);
			}
			else
			{
				Entity.SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Frame").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Mode").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Rule").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Render Caption").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Render").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Post Caption").SetComponentField("UIRectComponent", "Visible", value35);
				Entity.FindByName("Stats Post").SetComponentField("UIRectComponent", "Visible", value35);
			}
		}
	}
}
