using RageV;

// The camp's cinematic camera: four framed shots, and eased travel between
// them.
//
// **Why a script and not a camera path asset.** A path would be a new asset
// type, a new editor, and a new serializer for something four numbers and a
// curve already express -- and the engine's argument for visual scripting was
// exactly this: the thing that moves an entity is a script, and adding a
// second mechanism for one scene is how an engine accumulates two of
// everything.
//
// The shots are held rather than continuously flown. A camera that never stops
// reads as a drone; a camera that arrives, holds, and moves on reads as
// somebody showing you a place.
public class CampCamera : Script
{
	// Seconds parked on a shot, and seconds travelling to the next. Both on the
	// component, so the pacing is tunable without a rebuild -- which is most of
	// what tuning a camera move is.
	private float HoldSeconds = 3.4f;
	private float TravelSeconds = 4.2f;

	// Where the shots are. Position then euler rotation in degrees, because
	// degrees are what the inspector shows and a camera note written in radians
	// is a camera note nobody can read.
	//
	// 1. the establishing wide, from above the treeline
	// 2. across the fire, low, with the tent behind it
	// 3. the tent's own three-quarter, close
	// 4. a slow push toward the fox at the treeline
	// No [HideInEditor] needed: the inspector already skips `static` and
	// `readonly`, and this is both. The attribute is for a field that is
	// genuinely per-instance and genuinely not for tuning.
	private static readonly float[] Shots =
	{
		 6.2f, 3.1f,  6.6f,   -17.0f,  43.0f, 0.0f,
		 1.9f, 0.55f, 3.6f,    -5.0f,  16.0f, 0.0f,
		-4.6f, 1.5f,  3.2f,    -8.0f, -38.0f, 0.0f,
		 4.4f, 1.0f,  4.6f,    -5.0f,  33.0f, 0.0f,
	};

	private int m_Shot;
	private float m_Elapsed;
	private bool m_Travelling;

	public override void OnCreate()
	{
		m_Shot = 0;
		m_Elapsed = 0.0f;
		m_Travelling = false;
		Apply(0, 0, 0.0f);
	}

	public override void OnFrame(float deltaTime)
	{
		// OnFrame rather than OnTick: a camera move is a *look* at the frame
		// being drawn, and running it on the fixed step makes it stutter
		// against a render rate that is not a multiple of it. The physics
		// nothing here depends on stays where it belongs.
		m_Elapsed += deltaTime;

		if (!m_Travelling)
		{
			if (m_Elapsed < HoldSeconds)
			{
				Apply(m_Shot, m_Shot, 0.0f);
				return;
			}

			m_Travelling = true;
			m_Elapsed = 0.0f;
		}

		int next = (m_Shot + 1) % (Shots.Length / 6);
		float t = TravelSeconds > 0.0f ? m_Elapsed / TravelSeconds : 1.0f;

		if (t >= 1.0f)
		{
			m_Shot = next;
			m_Travelling = false;
			m_Elapsed = 0.0f;
			Apply(m_Shot, m_Shot, 0.0f);
			return;
		}

		Apply(m_Shot, next, Ease(t));
	}

	// Smoothstep. A linear move starts and stops abruptly, which is the single
	// thing that makes a camera move look like a slideshow rather than a shot.
	private static float Ease(float t)
	{
		return t * t * (3.0f - 2.0f * t);
	}

	private void Apply(int from, int to, float t)
	{
		int a = from * 6;
		int b = to * 6;

		Position = new Vector3(
			Mix(Shots[a + 0], Shots[b + 0], t),
			Mix(Shots[a + 1], Shots[b + 1], t),
			Mix(Shots[a + 2], Shots[b + 2], t));

		// Euler interpolation, and it is correct *here* because no two shots
		// are more than a half turn apart and none of them rolls. A camera that
		// needed to pass through a pole would need quaternions and this comment
		// would be an excuse instead of a reason.
		Rotation = new Vector3(
			Mathf.Radians(Mix(Shots[a + 3], Shots[b + 3], t)),
			Mathf.Radians(Mix(Shots[a + 4], Shots[b + 4], t)),
			Mathf.Radians(Mix(Shots[a + 5], Shots[b + 5], t)));
	}

	private static float Mix(float a, float b, float t)
	{
		return a + (b - a) * t;
	}
}
