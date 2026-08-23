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
		var value11 = "true";
		var value34 = "false";
		var value254 = "";

		if (Input.WasActionPressed("ToggleStats"))
		{
			shown = (!shown);
			if (shown)
			{
				Entity.SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Frame").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Mode").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Rule").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Render Caption").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Render").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Post Caption").SetComponentField("UIRectComponent", "Visible", value11);
				Entity.FindByName("Stats Post").SetComponentField("UIRectComponent", "Visible", value11);
			}
			else
			{
				Entity.SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Frame").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Mode").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Rule").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Render Caption").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Render").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Post Caption").SetComponentField("UIRectComponent", "Visible", value34);
				Entity.FindByName("Stats Post").SetComponentField("UIRectComponent", "Visible", value34);
			}
		}
		if (shown)
		{
			var target74 = Entity.FindByName("Stats Frame");
			target74.Text = Text.Join(Text.Join(Text.FromNumber((deltaTime * 1000.0f), 2.0f), " ms      "), Text.Join(Text.FromNumber((1.0f / deltaTime), 0.0f), " FPS"));
			var target83 = Entity.FindByName("Stats Mode");
			target83.Text = Text.Join(Text.Join(Graphics.Api, "      "), RenderSettings.Get("AntiAliasing"));
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
			var target139 = Entity.FindByName("Stats Render");
			target139.Text = feat;
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
			var target253 = Entity.FindByName("Stats Post");
			target253.Text = feat;
		}
		else
		{
			var target257 = Entity.FindByName("Stats Frame");
			target257.Text = value254;
			var target260 = Entity.FindByName("Stats Mode");
			target260.Text = value254;
			var target263 = Entity.FindByName("Stats Render");
			target263.Text = value254;
			var target266 = Entity.FindByName("Stats Post");
			target266.Text = value254;
		}
	}
}
