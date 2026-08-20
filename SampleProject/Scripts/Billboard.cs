using RageV;

// Turns an entity to face the camera, and only about Y.
//
// **Why this exists at all.** The engine's UI is screen space -- a
// `UICanvasComponent` has a scale mode and a reference resolution and no world
// mode -- so a label that belongs to something *in* the scene cannot be a
// `UIText`. It has to be geometry: a quad wearing a texture, standing where the
// thing it names is standing. Which is fine, and arguably better, because it is
// then lit by the scene, occluded by the scene, and reflected in the mirror
// like anything else. It just has to turn to face you.
//
// **It pitches as well as yaws, and that was not the first answer.** Turning
// only about Y keeps the sign vertical in the world, which sounds like what a
// standing sign does -- and then the camp's establishing shot looks down at it
// from thirty degrees and the sign is foreshortened to a third of its height,
// reading as a slab lying on the ground. A name tag has to stay legible from
// wherever it is being read, which means facing the camera squarely, which
// means pitching. The roll stays at zero, so it is still upright on screen.
//
// The quad primitive faces **+Z**, and the engine's zero rotation faces -Z --
// so the sign faces the camera when the entity's forward points *away* from it.
// That is one minus sign, and getting it wrong leaves a label that is correct
// in every respect except that it is showing you its back.
public class Billboard : Script
{
	// The entity to face. A name rather than a reference because the camera is
	// the camera for the whole scene and this is the only thing that needs it.
	private string TargetName = "Camp Camera";

	private Entity m_Target;

	public override void OnCreate()
	{
		// Once, here: FindByName is linear over the scene and this runs every
		// frame. The camp has a thousand entities.
		m_Target = Entity.FindByName(TargetName);
		if (!m_Target)
			Log.Warn("Billboard: no entity named '" + TargetName
					 + "', so this will not turn");
	}

	public override void OnFrame(float deltaTime)
	{
		if (!m_Target)
			return;

		// Away from the camera, because forward is -Z and the quad is +Z.
		Vector3 away = Position - m_Target.Position;

		float flat = Mathf.Sqrt(away.X * away.X + away.Z * away.Z);
		if (flat < 1.0e-4f)
			return;

		// Solved from the same convention the rest of the scene uses: forward
		// is (-sin yaw cos pitch, sin pitch, -cos yaw cos pitch), so the yaw
		// falls out of the horizontal part and the pitch out of the height.
		float yaw = Mathf.Atan2(-away.X, -away.Z);
		float pitch = Mathf.Atan2(away.Y, flat);

		Rotation = new Vector3(pitch, yaw, 0.0f);
	}
}
