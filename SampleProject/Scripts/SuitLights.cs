using RageV;
using System.Collections.Generic;

// The showroom2 light switch: the grey pill at the right-hand end of the
// credit bar, which powers the suit up and puts it back to standby.
//
// **The sibling of ShowroomLights, and it is a separate file rather than a
// wider version of that one.** A car's switch has a front group and a rear
// group because a car has headlamps and tail lamps; a suit has a core and two
// hands, and calling those Front and Rear to save a file would leave every
// field name in showroom2.rage lying about what it drives.
//
// **What differs from the car, and it is the whole design of this one.** A
// headlamp is off until somebody turns it on, so ShowroomLights authors the
// lamp elements at black and raises them. The suit is never off: the arc
// reactor, the eye slits, the palms and the boot jets glow in the material
// itself, because that is the character and not a state. So this script's
// "off" is *no override at all* -- the surfaces fall back to the material's
// own emissive and keep glowing -- and its "on" is the powered-up level plus
// the lights that make the glow do something.
//
// **The glow is masked by the texture, not by the entity**, which is why one
// group covers the whole figure. The model is a single material over three
// meshes, and its emissive map is black everywhere except 28,012 texels: the
// eyes, the palms, the chest, the boot jets and the helmet temples. The
// shader multiplies the entity's emissive by that map, so raising all three
// meshes raises exactly those texels and nothing else. Naming parts here, the
// way the car has to, would have nothing to name.
//
// **Emissive lights nothing in this engine**, which is the other half. The
// glow makes those surfaces bright and feeds bloom, and it is visible from any
// angle; the point lights beside them are the reactor and the repulsors
// *doing something* -- the pool of blue on the polished floor between the
// feet, and the light thrown up under the chin. Without them a powered suit in
// a dark studio casts exactly as much light as a switched-off one.
public class SuitLights : Script
{
	// The figure, and the meshes that carry the emissive map. Substrings,
	// comma separated, matched against entity names anywhere under the root.
	private string SuitRoot = "iron_man";
	private string GlowParts = "Object_";

	// The scene's own lamps, by name. These are ordinary light entities
	// authored at zero intensity; nothing here creates them.
	//
	// Two groups because they are two different lights. The reactor is a point
	// source inside a chest -- it wraps, and it is what puts blue on the floor
	// and under the jaw. The repulsors are aimed: a palm faces down, and what
	// sells it is two crisp pools beside the feet rather than a general wash.
	private string CoreLamps = "Arc Reactor Glow";
	private string HandLamps = "Repulsor Left,Repulsor Right";

	private float CoreIntensity = 22.0f;
	private float HandIntensity = 14.0f;

	// What the emissive texels give off once powered.
	//
	// **A factor, not a colour, and the difference matters.** The shader
	// multiplies this by the emissive map, whose lit texels are all the same
	// pale blue -- so what reaches the screen is this times (0.30, 0.50, 1.00).
	// The generator states the radiance it wants and divides that constant out;
	// the number here is the result. 53 / 40 / 26 emits (16, 20, 26), a third
	// above the material's resting (11, 14, 20) and under the post profile's
	// bloom clamp of 28, so the press is visible on the surfaces themselves and
	// not only in the pools the lamps throw.
	private Vector3 PoweredEmissive = new Vector3(53.24f, 40.25f, 26.0f);

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

	[HideInEditor] private readonly List<Entity> m_Glow = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_Core = new List<Entity>();
	[HideInEditor] private readonly List<Entity> m_Hands = new List<Entity>();

	[HideInEditor] private Entity m_Label;
	[HideInEditor] private bool m_On;
	[HideInEditor] private bool m_Hovered;
	[HideInEditor] private bool m_Ready;

	public override void OnCreate()
	{
		Entity root = Entity.FindByName(SuitRoot);
		if (!root)
		{
			// Named rather than silent. Every part of this script is keyed to
			// the figure's subtree, so a root that is not there is the
			// difference between "the button does nothing" and something
			// worth reading.
			Log.Warn($"SuitLights: no entity named '{SuitRoot}', so the suit's "
					 + "glow will not change. The scene's own lights still will.");
		}
		else
		{
			// **Collected once.** FindByName is linear over the scene, and
			// doing it per click would be visible.
			Collect(root, Split(GlowParts), m_Glow);
		}

		Find(CoreLamps, m_Core);
		Find(HandLamps, m_Hands);

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
		Entity self = Entity;
		bool hovered = self.IsButtonHovered();
		if (hovered == m_Hovered)
			return;

		m_Hovered = hovered;
		Paint();
	}

	// The suit, both halves of it.
	private void Apply()
	{
		// **Off is the override switched off, not an override holding black.**
		// The material's own emissive is the suit at rest and it is meant to
		// be seen; writing black here would put the reactor out, which is a
		// state this character does not have.
		string powered = m_On ? "true" : "false";
		string level = Vec4(PoweredEmissive);

		foreach (Entity part in m_Glow)
		{
			part.SetComponentField("MeshComponent", "OverrideEmissive", powered);
			if (m_On)
				part.SetComponentField("MeshComponent", "EmissiveColor", level);
		}

		string core = Number(m_On ? CoreIntensity : 0.0f);
		string hand = Number(m_On ? HandIntensity : 0.0f);

		foreach (Entity lamp in m_Core)
			lamp.SetComponentField("LightComponent", "Intensity", core);
		foreach (Entity lamp in m_Hands)
			lamp.SetComponentField("LightComponent", "Intensity", hand);
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
	// node hierarchy -- four -- and not something that can run away.
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
				Log.Warn($"SuitLights: no entity named '{name}'.");
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
