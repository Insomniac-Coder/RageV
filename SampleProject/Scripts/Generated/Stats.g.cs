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
	private bool shown;

	public override void OnFrame(float deltaTime)
	{
		var value255 = "";

		if (shown)
		{
			var target75 = Entity.FindByName("Stats Frame");
			target75.Text = Text.Join(Text.Join(Text.FromNumber((deltaTime * 1000.0f), 2.0f), " ms      "), Text.Join(Text.FromNumber((1.0f / deltaTime), 0.0f), " FPS"));
			var target84 = Entity.FindByName("Stats Mode");
			target84.Text = Text.Join(Text.Join(Graphics.Api, "      "), RenderSettings.Get("AntiAliasing"));
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
				feat = Text.Join(feat, "RT GI  ");
			}
			if (Graphics.IsActive("RTAmbientOcclusion"))
			{
				feat = Text.Join(feat, "RT AO  ");
			}
			if (Text.Same(RenderSettings.Get("VoxelGlobalIllumination"), "true"))
			{
				feat = Text.Join(feat, "VoxelGI  ");
			}
			var target140 = Entity.FindByName("Stats Render");
			target140.Text = feat;
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
				feat = Text.Join(feat, "SSGI  ");
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
			var target254 = Entity.FindByName("Stats Post");
			target254.Text = feat;
		}
		else
		{
			var target258 = Entity.FindByName("Stats Frame");
			target258.Text = value255;
			var target261 = Entity.FindByName("Stats Mode");
			target261.Text = value255;
			var target264 = Entity.FindByName("Stats Render");
			target264.Text = value255;
			var target267 = Entity.FindByName("Stats Post");
			target267.Text = value255;
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
