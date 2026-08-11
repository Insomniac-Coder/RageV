namespace RageV.Builtin;

/// <summary>
/// Scripts that ship with the engine, mirroring the native ones in
/// <c>RageV/Scripts/BuiltinScripts.cpp</c>.
/// </summary>
/// <remarks>
/// Two reasons they exist rather than being sample code somewhere. First, until
/// projects can build their own script assembly there is otherwise no C# script
/// anywhere, and a language you cannot run is hard to tell from one that does
/// not work. Second, they are the worked examples: the C++ and C# versions of
/// Spinner are deliberately line-for-line comparable, which is the clearest
/// possible statement that the two APIs are one API.
/// </remarks>
public class Spinner : Script
{
	private float m_Speed = 1.2f;

	public override void OnTick(float deltaTime)
	{
		// Radians per second, applied per fixed step. Multiplying by deltaTime
		// is what keeps the rate the same at any simulation frequency.
		Rotate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));
	}
}

/// <summary>Follows another entity by name, at an offset.</summary>
/// <remarks>
/// The worked example of *why* there are two rates, and the mirror of the
/// native <c>Follow</c>. A follow camera is presentation: nothing collides with
/// it and nothing scores off it, and the thing it chases has already been
/// interpolated for this frame by the time <see cref="Script.OnFrame"/> runs.
/// Chasing from <see cref="Script.OnTick"/> at 240 Hz would move the camera on
/// one frame in four while the world moved on all four — which reads worse than
/// no smoothing at all, because the stutter is differential.
/// </remarks>
public class Follow : Script
{
	private string m_TargetName = "Player";
	private float m_OffsetY = 3.0f;
	private float m_OffsetZ = 8.0f;
	private float m_Sharpness = 4.0f;

	private Entity m_Target;

	public override void OnCreate()
	{
		// Once, here: FindByName is linear over the scene, and this runs every
		// single frame.
		m_Target = Entity.FindByName(m_TargetName);
		if (!m_Target.Exists)
			Log.Warn($"Follow: no entity named '{m_TargetName}'");
	}

	public override void OnFrame(float deltaTime)
	{
		if (!m_Target.Exists)
			return;

		Vector3 goal = m_Target.Position + new Vector3(0.0f, m_OffsetY, m_OffsetZ);
		Vector3 here = Position;

		// Framerate-independent smoothing. A plain lerp by a constant factor
		// converges at a rate that depends on the step size — which on a frame,
		// where deltaTime varies, would mean the camera lagged further behind
		// whenever the frame rate dipped.
		float t = 1.0f - System.MathF.Exp(-m_Sharpness * deltaTime);
		Position = here + (goal - here) * t;
	}
}

/// <summary>Counts contacts into fields the inspector can watch.</summary>
/// <remarks>
/// The worked example for the physics callbacks, and the proof they arrive:
/// attach it to anything with a collider and play. Collisions and triggers
/// are counted together, so the one script serves solid and sensor shapes
/// alike.
/// </remarks>
public class ContactCounter : Script
{
	private int m_Entered;
	private int m_Exited;
	private float m_HardestHit;

	public override void OnCollisionEnter(Collision collision)
	{
		m_Entered++;
		m_HardestHit = System.MathF.Max(m_HardestHit, collision.ImpactSpeed);
	}

	public override void OnCollisionExit(Collision collision) => m_Exited++;
	public override void OnTriggerEnter(Collision collision) => m_Entered++;
	public override void OnTriggerExit(Collision collision) => m_Exited++;
}

/// <summary>Moves with the movement axes, relative to its own orientation.</summary>
/// <remarks>
/// Reads through actions rather than key codes, so it works with whatever the
/// input map is bound to and keeps working when the player rebinds.
/// </remarks>
public class Mover : Script
{
	private float m_Speed = 4.0f;

	public override void OnTick(float deltaTime)
	{
		Vector3 direction =
			Forward * Input.GetAxis("MoveForward") +
			Right * Input.GetAxis("MoveRight") +
			Vector3.Up * Input.GetAxis("MoveUp");

		// A zero-length direction normalises to NaNs, and a NaN in a transform
		// spreads to every transform derived from it -- the object and all its
		// children vanish with no error anywhere.
		float lengthSquared = direction.X * direction.X
							+ direction.Y * direction.Y
							+ direction.Z * direction.Z;
		if (lengthSquared <= 0.0f)
			return;

		float length = (float)System.Math.Sqrt(lengthSquared);
		float speed = Input.IsActionDown("Sprint") ? m_Speed * 3.0f : m_Speed;

		Translate(direction * (speed * deltaTime / length));
	}
}
