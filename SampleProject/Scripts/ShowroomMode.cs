using RageV;
using System.Collections.Generic;

// The showroom's lighting mode: the pill left of the light switch, which moves
// the room between the studio it opens in and the delivery bay it was first
// built as.
//
// **Mode 1 is a photograph of a dark room, not a dark photograph of a room.**
// The difference is one surface. Take every source away and the car is a black
// shape with a rim on it -- correct, and dead, because car paint has almost no
// diffuse: what you read as "expensive" is a *reflection* of a large soft
// source running down the roof and the bonnet. So mode 1 does not dim the
// ceiling: it **swaps the fitting** for one the same size with four cells lit
// and the rest dark, and the car keeps a real source to reflect.
//
// Dimming the whole panel was the first attempt and it is the wrong picture --
// a ceiling that is uniformly dull reads as a light nobody switched off. And
// shrinking it to a square over the car is the other trap: at this field of
// view the ceiling is only in frame beyond z = -1.3, so a panel centred on the
// car sits behind the camera's top edge and the mode loses its source.
//
// **What the mode moves is nine lights, four lights, two lights, four emissive
// lenses and one material**, and every one of them is authored in the scene at
// its mode-1 value. Nothing here creates or destroys anything: a light at zero
// costs a row in a storage buffer, which is the same argument the car's own
// lamps are held at zero by.
//
// **The probe is the part that is easy to miss.** A baked reflection probe is
// a photograph of the room taken once, and both modes are the room -- so the
// cube captured under one is wrong under the other, and wrong in the way that
// is hardest to attribute: the car keeps a bright ceiling in its paint and a
// bright ambient on its flanks in a room that has gone dark. There is no
// "re-bake" call to make, and there does not need to be one: `Update` is an
// ordinary component field, so this flips the probe to Realtime for a single
// frame, lets it re-capture all six faces under the new lighting, and puts it
// back to Baked. That is the whole mechanism, and it costs one frame per press
// and nothing at all between them.
public class ShowroomMode : Script
{
	// The nine ceiling fills, in two groups: the one under the cells the
	// studio fitting lights, which stays on and dimmed, and the eight that go
	// out with the cells above them. The light that casts is the one the
	// ceiling shows as lit, which is the pairing that makes a fitting
	// believable and the one the bay's downlights already keep.
	private string StudioFills = "Panel Light 01";
	private string ShowroomFills = "";
	private float FillIntensity = 14.7f;
	private float StudioFillIntensity = 11.0f;

	// The one shadow caster in the room, and the mode-1 picture: dimmer, and
	// narrowed so the pool dies before it reaches a wall.
	private string KeyLight = "Key Light";
	private float KeyIntensity = 46.0f;
	private float StudioKeyIntensity = 30.0f;
	private float KeyInner = 34.0f;
	private float KeyOuter = 62.0f;
	private float StudioKeyInner = 20.0f;
	private float StudioKeyOuter = 44.0f;

	// Rim lights, off in mode 1: there is no bright wall to separate the roof
	// from.
	private string Kickers = "Kicker Left,Kicker Right";
	private float KickerIntensity = 26.0f;
	private float StudioKickerIntensity = 0.0f;

	// The service bay: four lights, and the four lenses they sit in. Both, or
	// mode 1 keeps four white ellipses hanging in a room that is meant to have
	// gone dark -- and an emissive surface is visible from angles its light
	// never reaches.
	private string BayLights = "Bay Downlight Light 0,Bay Downlight Light 1,"
							 + "Bay Downlight Light 2,Bay Downlight Light 3";
	private float BayIntensity = 4.5f;
	private float StudioBayIntensity = 0.0f;
	private string BayLenses = "Bay Downlight 0,Bay Downlight 1,"
							 + "Bay Downlight 2,Bay Downlight 3";

	// The ceiling fitting, as two materials over one mesh. **The mode swaps
	// the fitting rather than dimming it**: same size, same mullions, same
	// normal map, and a different set of cells lit. Each fitting's values stay
	// in its own asset, so neither is copied into a field here to drift from.
	private string Luminaire = "Luminaire";
	private string LuminaireMaterial = "materials/showroom_panel.rmat";
	private string StudioLuminaireMaterial = "materials/showroom_panel_studio.rmat";

	// The probe whose capture both modes invalidate.
	private string ProbeName = "Showroom Probe";

	// The label, which this script owns for the reason ShowroomLights gives:
	// the canvas multiplies a button's tint into the image on that entity and
	// nothing else, children included.
	private string LabelName = "Mode Label";
	private string LabelInStudio = "MODE 2";
	private string LabelInShowroom = "MODE 1";
	private string LabelRest = "0.97 0.97 0.98 1";
	private string LabelHover = "0.05 0.05 0.06 1";

	// 1 studio, 2 showroom. An int rather than a bool because the button says
	// "MODE 2" and a field called `StartInShowroom` would be one more place to
	// work out which is which.
	private int StartMode = 1;

	[HideInEditor] private readonly List<Entity> m_StudioFills = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_ShowroomFills = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_Kickers = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_BayLights = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_BayLenses = new List<Entity>();

	[HideInEditor] private Entity m_Key;
	[HideInEditor] private Entity m_Luminaire;
	[HideInEditor] private Entity m_Probe;
	[HideInEditor] private Entity m_Label;

	[HideInEditor] private bool m_Studio;
	[HideInEditor] private bool m_Hovered;
	[HideInEditor] private bool m_Ready;

	// Set the frame a mode is applied and cleared the frame after, which is
	// the whole of the probe's re-capture window: the render that ends the
	// frame this is set in is the one that takes the six faces.
	[HideInEditor] private bool m_Recapturing;

	public override void OnCreate()
	{
		Find(StudioFills, m_StudioFills);
		Find(ShowroomFills, m_ShowroomFills);
		Find(Kickers, m_Kickers);
		Find(BayLights, m_BayLights);
		Find(BayLenses, m_BayLenses);

		m_Key = Entity.FindByName(KeyLight);
		m_Luminaire = Entity.FindByName(Luminaire);
		m_Probe = Entity.FindByName(ProbeName);
		m_Label = Entity.FindByName(LabelName);

		if (!m_Key)
			Log.Warn($"ShowroomMode: no entity named '{KeyLight}', so the mode "
					 + "cannot move the light the room is built around.");
		if (!m_Probe)
			Log.Warn($"ShowroomMode: no entity named '{ProbeName}'. The modes still "
					 + "switch, but the reflection probe keeps the room it was "
					 + "baked in -- the car will reflect the other mode's ceiling.");

		m_Ready = true;
		m_Studio = StartMode != 2;

		// The scene is authored in mode 1, so opening in it is already the
		// state on disk and this writes the same numbers back. Applied anyway:
		// a default that is only correct because the file happens to agree
		// with it is one edit away from being wrong, and the write is free
		// once at load.
		Apply();
		Paint();
	}

	// The UI Button's OnClick calls this by name, so it stays public, takes
	// nothing and returns nothing.
	public void Toggle()
	{
		if (!m_Ready)
			return;

		m_Studio = !m_Studio;
		Apply();
		Paint();
	}

	public override void OnFrame(float deltaTime)
	{
		if (!m_Ready)
			return;

		// The probe has had its frame: put it back to Baked, where it costs
		// nothing until the next press.
		if (m_Recapturing)
		{
			m_Recapturing = false;
			if (m_Probe)
				m_Probe.SetComponentField("ReflectionProbeComponent", "Update", "Baked");
		}

		// A level, not an edge -- the pointer is either on the button or it is
		// not, and the label follows. Compared before it is written so the
		// common frame costs one call across the boundary rather than two and
		// a string.
		Entity self = Entity;
		bool hovered = self.IsButtonHovered();
		if (hovered == m_Hovered)
			return;

		m_Hovered = hovered;
		Paint();
	}

	private void Apply()
	{
		SetIntensity(m_StudioFills, m_Studio ? StudioFillIntensity : FillIntensity);
		SetIntensity(m_ShowroomFills, m_Studio ? 0.0f : FillIntensity);
		SetIntensity(m_Kickers, m_Studio ? StudioKickerIntensity : KickerIntensity);
		SetIntensity(m_BayLights, m_Studio ? StudioBayIntensity : BayIntensity);

		if (m_Key)
		{
			m_Key.SetComponentField("LightComponent", "Intensity",
									Number(m_Studio ? StudioKeyIntensity : KeyIntensity));
			m_Key.SetComponentField("LightComponent", "InnerCone",
									Number(m_Studio ? StudioKeyInner : KeyInner));
			m_Key.SetComponentField("LightComponent", "OuterCone",
									Number(m_Studio ? StudioKeyOuter : KeyOuter));
		}

		// The fitting itself, by asset path. A path the registry does not know
		// leaves the field alone rather than nulling it -- which is the right
		// refusal, and means a typo here is a mode that does not change the
		// ceiling rather than a ceiling that vanishes.
		if (m_Luminaire)
		{
			m_Luminaire.SetComponentField(
				"MeshComponent", "Material",
				m_Studio ? StudioLuminaireMaterial : LuminaireMaterial);
		}

		// The bay's lenses. **Mode 2 clears the override rather than writing a
		// bright value**: the material is where mode 2's emissive lives, and a
		// copy of it here would be a second place to edit and a first place to
		// forget. The scene authors the black the override holds.
		foreach (Entity lens in m_BayLenses)
			lens.SetComponentField("MeshComponent", "OverrideEmissive",
								   m_Studio ? "true" : "false");

		Recapture();
	}

	// The probe, for one frame. Realtime ignores the "already captured" test
	// that makes a baked probe free, and takes FacesPerFrame faces -- six, as
	// the scene authors it -- so the cube is entirely the new room rather than
	// part of each.
	private void Recapture()
	{
		if (!m_Probe)
			return;

		m_Probe.SetComponentField("ReflectionProbeComponent", "Update", "Realtime");
		m_Recapturing = true;
	}

	private void Paint()
	{
		if (!m_Label)
			return;

		m_Label.Text = m_Studio ? LabelInStudio : LabelInShowroom;
		m_Label.SetComponentField("UITextComponent", "Color",
								  m_Hovered ? LabelHover : LabelRest);
	}

	private static void SetIntensity(List<Entity> lights, float intensity)
	{
		string level = Number(intensity);
		foreach (Entity light in lights)
			light.SetComponentField("LightComponent", "Intensity", level);
	}

	private static void Find(string names, List<Entity> into)
	{
		foreach (string name in Split(names))
		{
			Entity found = Entity.FindByName(name);
			if (found)
				into.Add(found);
			else
				Log.Warn($"ShowroomMode: no entity named '{name}'.");
		}
	}

	private static string[] Split(string list) =>
		list.Split(',', System.StringSplitOptions.RemoveEmptyEntries
					  | System.StringSplitOptions.TrimEntries);

	// Invariant, and *not* the bracketed form the scene file uses.
	private static string Number(float value) =>
		value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
}
