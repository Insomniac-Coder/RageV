using RageV;
using System.Collections.Generic;

// The showroom's light switch: the grey pill at the right-hand end of the
// credit bar, which turns the car's headlamps and tail lamps on and off.
//
// **A lamp is two things and both are needed.** The emissive on the lamp
// elements is the lamp *being on* -- it makes those surfaces bright and it
// feeds bloom, and it is the only one of the two that is visible from any
// angle. The spot lights beside them are the lamp *doing something*: emissive
// in this engine lights nothing at all, so without them the beams throw no
// pool on the floor, the red does not reach the service bay, and a lit car in
// a dark room casts exactly as much light as an unlit one.
//
// **What lights up is chosen by entity name, not by material**, which looks
// like the lazier option and is the correct one here. The model has an
// `AUX_LIGHT_Porsche992` material and only three of the nine meshes wearing it
// are lights -- the rest is roll cage and roof trim, which happened to be
// assigned the same material by whoever built the file. Matching the material
// would light the cage.
public class ShowroomLights : Script
{
	// The car, and the parts of it that light up. Substrings, comma separated,
	// matched against entity names anywhere under the root.
	private string CarRoot = "porsche_992_gt3_r";
	private string FrontParts = "EXT_Emissive_Light_Front,ST_FRONT_";
	private string RearParts = "EXT_Emissive_Light_Rear,EXT_Glass_Emissive_Rear";

	// The scene's own lamps, by name. These are ordinary spot light entities
	// authored at zero intensity; nothing here creates them.
	private string FrontLamps = "Headlamp Beam Left,Headlamp Beam Right";
	private string RearLamps = "Tail Glow Left,Tail Glow Right";

	private float FrontIntensity = 88.0f;
	private float RearIntensity = 30.0f;

	// What the lamp elements themselves give off. Well above 1: bloom
	// thresholds just over it, and a surface meant to read as a source has to
	// clear that or the tone mapper brings it back to the grey of its housing.
	private Vector3 FrontEmissive = new Vector3(19.0f, 21.0f, 24.0f);
	private Vector3 RearEmissive = new Vector3(16.0f, 0.7f, 0.35f);

	// The label on the button, which this script owns: the canvas multiplies a
	// button's tint into the image on that same entity and nothing else, so a
	// child label does not follow the plate from grey to white by itself.
	private string LabelName = "Lights Label";
	private string LabelOn = "LIGHTS OFF";
	private string LabelOff = "LIGHTS ON";

	// White on the grey plate, black on the white one.
	private string LabelRest = "0.97 0.97 0.98 1";
	private string LabelHover = "0.05 0.05 0.06 1";

	private bool StartOn = false;

	[HideInEditor] private readonly List<Entity> m_Front = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_Rear = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_FrontLamps = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_RearLamps = new List<Entity>();

	[HideInEditor] private Entity m_Label;
	[HideInEditor] private bool m_On;
	[HideInEditor] private bool m_Hovered;
	[HideInEditor] private bool m_Ready;

	public override void OnCreate()
	{
		Entity root = Entity.FindByName(CarRoot);
		if (!root)
		{
			// Named rather than silent. Every part of this script is keyed to
			// the car's subtree, so a root that is not there is the difference
			// between "the button does nothing" and something worth reading.
			Log.Warn($"ShowroomLights: no entity named '{CarRoot}', so the car's "
					 + "lamps will not light. The scene's own spot lights still will.");
		}
		else
		{
			// **Collected once.** FindByName is linear over the scene and this
			// subtree is three hundred and thirty-seven entities; doing it per
			// click would be visible, and doing it per frame would be the
			// script's entire cost.
			Collect(root, Split(FrontParts), m_Front);
			Collect(root, Split(RearParts), m_Rear);

			// Switched on here and left on, with the colour carrying the
			// state. The override is what makes the entity's own emissive
			// count instead of the material's, and an override holding black
			// is indistinguishable from no override -- so this costs one write
			// per mesh at load and none per toggle.
			foreach (Entity part in m_Front)
				part.SetComponentField("MeshComponent", "OverrideEmissive", "true");
			foreach (Entity part in m_Rear)
				part.SetComponentField("MeshComponent", "OverrideEmissive", "true");
		}

		Find(FrontLamps, m_FrontLamps);
		Find(RearLamps, m_RearLamps);

		m_Label = Entity.FindByName(LabelName);
		m_Ready = true;

		m_On = StartOn;
		Apply();
		Paint();
	}

	// The UI Button's OnClick calls this by name, so it has to stay public,
	// take nothing and return nothing.
	public void Toggle()
	{
		if (!m_Ready)
			return;

		m_On = !m_On;
		Apply();
		Paint();
	}

	public override void OnFrame(float deltaTime)
	{
		if (!m_Ready)
			return;

		// A level, not an edge: the pointer is either on the button or it is
		// not, and the label follows. Compared before it is written so the
		// common frame -- the pointer somewhere else entirely -- costs one
		// call across the boundary rather than two and a string.
		//
		// Through a local, because `Script.Entity` is a property whose name is
		// also the type's, and reading it once is clearer than relying on the
		// language to work out which of the two every mention meant.
		Entity self = Entity;
		bool hovered = self.IsButtonHovered();
		if (hovered == m_Hovered)
			return;

		m_Hovered = hovered;
		Paint();
	}

	// The lamps, both halves of them.
	private void Apply()
	{
		string front = m_On ? Vec4(FrontEmissive) : "0 0 0 1";
		string rear = m_On ? Vec4(RearEmissive) : "0 0 0 1";

		foreach (Entity part in m_Front)
			part.SetComponentField("MeshComponent", "EmissiveColor", front);
		foreach (Entity part in m_Rear)
			part.SetComponentField("MeshComponent", "EmissiveColor", rear);

		string frontLevel = Number(m_On ? FrontIntensity : 0.0f);
		string rearLevel = Number(m_On ? RearIntensity : 0.0f);

		foreach (Entity lamp in m_FrontLamps)
			lamp.SetComponentField("LightComponent", "Intensity", frontLevel);
		foreach (Entity lamp in m_RearLamps)
			lamp.SetComponentField("LightComponent", "Intensity", rearLevel);
	}

	// The label: what the press will do, in whichever colour reads against the
	// plate underneath it.
	private void Paint()
	{
		if (!m_Label)
			return;

		m_Label.Text = m_On ? LabelOn : LabelOff;
		m_Label.SetComponentField("UITextComponent", "Color",
								  m_Hovered ? LabelHover : LabelRest);
	}

	// Every descendant whose name contains one of `parts`, the root included.
	// Recursive rather than a stack, because the depth here is the model's
	// node hierarchy -- six or seven -- and not something that can run away.
	private static void Collect(Entity entity, string[] parts, List<Entity> into)
	{
		string name = entity.Name;
		foreach (string part in parts)
		{
			if (part.Length > 0 && name.Contains(part))
			{
				into.Add(entity);
				break;
			}
		}

		foreach (Entity child in entity.Children)
			Collect(child, parts, into);
	}

	private static void Find(string names, List<Entity> into)
	{
		foreach (string name in Split(names))
		{
			Entity found = Entity.FindByName(name);
			if (found)
				into.Add(found);
			else
				Log.Warn($"ShowroomLights: no entity named '{name}'.");
		}
	}

	private static string[] Split(string list) =>
		list.Split(',', System.StringSplitOptions.RemoveEmptyEntries
					  | System.StringSplitOptions.TrimEntries);

	// The forms the component bridge parses: a Vec4 is four numbers separated
	// by spaces, invariant, and *not* the bracketed form the scene file uses.
	private static string Vec4(Vector3 colour) =>
		$"{Number(colour.X)} {Number(colour.Y)} {Number(colour.Z)} 1";

	private static string Number(float value) =>
		value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
}
