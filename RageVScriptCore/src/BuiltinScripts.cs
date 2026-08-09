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

	public override void OnUpdate(float deltaTime)
	{
		// Radians per second, applied per fixed step. Multiplying by deltaTime
		// is what keeps the rate the same at any simulation frequency.
		Rotate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));
	}
}

/// <summary>Moves with the movement axes, relative to its own orientation.</summary>
/// <remarks>
/// Reads through actions rather than key codes, so it works with whatever the
/// input map is bound to and keeps working when the player rebinds.
/// </remarks>
public class Mover : Script
{
	private float m_Speed = 4.0f;

	public override void OnUpdate(float deltaTime)
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
