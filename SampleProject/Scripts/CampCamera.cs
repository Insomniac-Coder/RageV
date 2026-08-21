using RageV;

// The camp's cinematic camera: a handful of framed shots, and eased travel
// between them.
//
// **Where the shots live is the interesting decision.** They used to be a
// `static readonly float[]` in this file -- twelve numbers a shot, positions
// and euler angles, typed in. That is unusable in two ways: nobody can look at
// `-4.6f, 1.5f, 3.2f` and know what it frames, and the scene generator that
// actually chooses the framing had no way to write them, so the two drifted
// apart the first time either changed.
//
// So the shots are **entities in the scene**, named "Shot 1", "Shot 2" and so
// on. The generator places them by solving a target, a distance and two angles
// -- which is how a shot is really chosen -- and a person can drag one in the
// editor and see what it does. This script just walks them in order.
//
// **Why a script and not a camera path asset.** A path would be a new asset
// type, a new editor and a new serializer for something an entity transform
// already expresses -- and the engine's argument for visual scripting was
// exactly this: the thing that moves an entity is a script, and adding a second
// mechanism for one scene is how an engine accumulates two of everything.
//
// The shots are held rather than continuously flown. A camera that never stops
// reads as a drone; a camera that arrives, holds, and moves on reads as
// somebody showing you a place.
public class CampCamera : Script
{
	// Seconds parked on a shot, and seconds travelling to the next. Both on the
	// component, so the pacing is tunable without a rebuild -- which is most of
	// what tuning a camera move is.
	//
	// **The defaults are the scene's values, not placeholders.** Keeping them
	// equal to what the scene asks for makes the timing right whether or not
	// the override reaches the field, and leaves the override as the tuning
	// knob it was meant to be rather than the only thing holding the pacing up.
	private float HoldSeconds = 3.4f;
	private float TravelSeconds = 4.2f;

	// No more than this many, and the search stops at the first gap. A scene
	// with "Shot 1" and "Shot 3" gets one shot and not a jump through the
	// origin, which is what looking up a missing entity would otherwise give.
	private const int MaxShots = 16;

	private Vector3[] m_Positions = new Vector3[0];
	private Vector3[] m_Rotations = new Vector3[0];

	// **A camera move is a curve, because a straight line between two good
	// stations is not a good move.** Shot 8 stands outside the clearing behind
	// the van and shot 9 is at the mirror by the fire; both are clear, and the
	// straight line between them went through the van and then through the
	// fire. No amount of moving either end fixes that -- the van is *between*
	// them, which is the whole point of shot 8.
	//
	// So a leg may carry a waypoint: an entity named "Via 8" bends the travel
	// out of shot 8, and the path becomes a quadratic Bezier through it. Most
	// legs have none and stay straight, which is both cheaper and what a move
	// with nothing in the way should be.
	//
	// An entity rather than numbers in this file, for the reason the shots are
	// entities: it can be dragged, and the generator that knows where the van
	// is can place it. A Bezier only bends *toward* its control point, so the
	// via is where the camera leans, not where it goes -- the generator solves
	// against the curve rather than against the point.
	private Vector3[] m_Vias = new Vector3[0];
	private bool[] m_HasVia = new bool[0];

	// Each shot's post profile, in shot order, comma separated. Written by the
	// scene generator from the same focus bands it built the profiles from.
	//
	// **A shot is a position, an aim and a lens**, and the lens is the part
	// that was missing. Twelve shots spanning 2.3 m to 9.6 m cannot share one
	// focus distance: at any single value either the close shots blur at the
	// front or the wides blur at the subject, and the fox's closing shot came
	// back as a soft smear because of it.
	//
	// **A table here rather than a CameraComponent on every marker**, which is
	// what this was. A marker only ever needed to say where the camera stands;
	// giving each one a whole camera -- projection, clip planes, aspect flag --
	// so it could carry one number left the scene reporting thirteen cameras
	// when it has one. Twelve numbers belong in a field of twelve numbers.
	//
	// Fewer entries than shots is fine and means the rest keep the lens they
	// have, which is the same thing an empty entry means.
	private string Profiles = "";

	// What each shot is *looking at*, in shot order, as "x y z" triples. Same
	// shape as Profiles above and written by the same generator, which solved
	// every shot from a target in the first place -- this is that target,
	// crossing over instead of being thrown away.
	//
	// **Interpolating euler angles is the wrong thing to interpolate.** It
	// worked while every move was a straight line and both ends looked at
	// roughly the same place. The moment shot 8 got an arc round the van, the
	// aim -- turning 176 degrees the short way -- swung to face outward for
	// most of the move, and three seconds of a four-second travel was empty
	// forest. Nothing was wrong with the path; the camera was pointing the
	// wrong way along it.
	//
	// Aiming at a point instead removes the question. There is no short way or
	// long way round to a place: the camera looks at where it is looking, from
	// wherever the curve has put it, and the subject stays in frame by
	// construction rather than by the two endpoints happening to agree.
	private string Targets = "";

	private Vector3[] m_Targets = new Vector3[0];
	private bool m_Aiming;

	private string[] m_Profiles = new string[0];
	private string m_Applied = "";

	private int m_Shot;
	private float m_Elapsed;
	private bool m_Travelling;

	// Paused by the HUD's button, which names this entity and the method below
	// rather than carrying a copy of the state -- one bool, one owner.
	private bool m_Paused;
	private Entity m_Label;

	public override void OnCreate()
	{
		var positions = new Vector3[MaxShots];
		var rotations = new Vector3[MaxShots];
		var vias = new Vector3[MaxShots];
		var hasVia = new bool[MaxShots];
		string[] table = Profiles.Length > 0
			? Profiles.Split(',')
			: new string[0];
		string[] aims = Targets.Length > 0
			? Targets.Split(',')
			: new string[0];

		int found = 0;
		for (int i = 1; i <= MaxShots; i++)
		{
			Entity marker = Entity.FindByName("Shot " + i);
			if (!marker)
				break;

			positions[found] = marker.Position;
			rotations[found] = marker.Rotation;

			// Looked up per shot and **not** stopped at the first gap, unlike
			// the shots above: vias are sparse by design -- two legs of twelve
			// need one -- so a missing "Via 3" says this leg is straight, not
			// that there are no more.
			Entity via = Entity.FindByName("Via " + i);
			hasVia[found] = (bool)via;
			if (hasVia[found])
				vias[found] = via.Position;

			found++;
		}

		m_Positions = new Vector3[found];
		m_Rotations = new Vector3[found];
		m_Vias = new Vector3[found];
		m_HasVia = new bool[found];
		m_Profiles = new string[found];
		m_Targets = new Vector3[found];

		// **All of them or none.** A half-filled table would aim some legs and
		// lerp the others, so a bad entry would show up as one move behaving
		// differently from its neighbours -- which is the hardest kind of
		// wrong to find. Fewer targets than shots means the field was not
		// written, and the euler path below is what the scene had before this
		// existed and still works.
		m_Aiming = aims.Length >= found;

		for (int i = 0; i < found; i++)
		{
			m_Positions[i] = positions[i];
			m_Rotations[i] = rotations[i];
			m_Vias[i] = vias[i];
			m_HasVia[i] = hasVia[i];
			m_Profiles[i] = i < table.Length ? table[i].Trim() : "";
			if (m_Aiming && !ParseVector(aims[i], out m_Targets[i]))
				m_Aiming = false;
		}

		if (!m_Aiming && Targets.Length > 0)
			Log.Warn("CampCamera: Targets has " + aims.Length + " usable entries for "
					 + found + " shots, so the camera falls back to interpolating "
					 + "its angles. A move that turns more than half a circle will "
					 + "swing the wrong way round.");

		if (found == 0)
		{
			// Said out loud rather than silently sitting still. A camera that
			// does not move looks exactly like a camera whose script is not
			// running, and this is the difference.
			Log.Warn("CampCamera: no 'Shot 1' entity in the scene, so there is "
					 + "nothing to move between. The camera will stay put.");
			return;
		}

		m_Shot = 0;
		m_Elapsed = 0.0f;
		m_Travelling = false;
		m_Paused = false;
		m_Label = Entity.FindByName("Pause Label");
		Apply(0, 0, 0.0f);
	}

	/// <summary>Stop the camera where it is, or start it again.</summary>
	/// <remarks>
	/// Called by name from the HUD button's <c>OnClickMethod</c>. Public, and
	/// that is the whole interface: the button knows the entity and the method
	/// name, and nothing else about how the camera works.
	///
	/// **It pauses where it is, mid-move if that is where it is.** Snapping to
	/// the nearest shot would be tidier and would also throw away the frame the
	/// person just pressed the button to keep, which is the only reason anybody
	/// pauses a cinematic.
	/// </remarks>
	public void TogglePause()
	{
		m_Paused = !m_Paused;

		// The label has to say what the button will *do*, not what the state
		// is. A button reading "paused" while paused tells you nothing about
		// what pressing it achieves.
		if (m_Label)
			m_Label.SetComponentField("UITextComponent", "Text",
									  m_Paused ? "resume" : "pause");
	}

	public override void OnFrame(float deltaTime)
	{
		// OnFrame rather than OnTick: a camera move is a *look* at the frame
		// being drawn, and running it on the fixed step makes it stutter
		// against a render rate that is not a multiple of it. The physics
		// nothing here depends on stays where it belongs.
		if (m_Positions.Length < 2 || m_Paused)
			return;

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

		int next = (m_Shot + 1) % m_Positions.Length;
		float t = TravelSeconds > 0.0f ? m_Elapsed / TravelSeconds : 1.0f;

		if (t >= 1.0f)
		{
			m_Shot = next;
			m_Travelling = false;
			m_Elapsed = 0.0f;
			Apply(m_Shot, m_Shot, 0.0f);
			return;
		}

		Apply(m_Shot, next, t);
	}

	private void Apply(int from, int to, float t)
	{
		// Smoothstep, so the move starts and ends at rest. Linear travel between
		// two held shots reads as a machine: it is the acceleration at the ends
		// that makes it look like a decision rather than a slide.
		float eased = t * t * (3.0f - 2.0f * t);

		// The waypoint belongs to the leg *leaving* `from`, so a shot's via is
		// read only while travelling away from it -- holding on a shot is
		// from == to and must stay exactly on the marker.
		bool curved = from != to && m_HasVia[from];

		// A local copy, because `Entity` is a property returning a readonly
		// struct and C# will not let a setter be called on the returned value.
		// The struct is a bare id -- the setters go straight to the engine --
		// so the copy costs nothing and changes nothing.
		Entity self = Entity;
		self.Position = curved
			? Bezier(m_Positions[from], m_Vias[from], m_Positions[to], eased)
			: Vector3.Lerp(m_Positions[from], m_Positions[to], eased);

		// Aimed at the subject where the scene said what the subject is, and
		// interpolating angles where it did not.
		//
		// **The engine's own LookAt, not arithmetic here.** Writing the inverse
		// of the generator's solve by hand works and is four lines, and it is
		// also a second opinion about which way -Z points that nothing would
		// check. Entity.LookAt is the one the rest of the engine turns things
		// with, and it is set after the position because it reads where the
		// entity now is.
		if (m_Aiming)
			self.LookAt(Vector3.Lerp(m_Targets[from], m_Targets[to], eased));
		else
			self.Rotation = LerpAngles(m_Rotations[from], m_Rotations[to], eased);

		// **Switched at the halfway point, not at either end.** Two profiles
		// cannot be blended -- it is one asset reference, not a number -- so
		// the focus changes in a single step whenever it changes at all. Doing
		// it halfway through the travel puts that step where both ends are
		// equally wrong and the camera is moving fastest, which is the one
		// moment nobody can see it.
		Focus(eased < 0.5f ? from : to);
	}

	private void Focus(int shot)
	{
		string profile = m_Profiles[shot];
		if (profile.Length == 0 || profile == m_Applied)
			return;

		// Written every time it *changes* rather than every frame: this crosses
		// the managed boundary with three strings and re-derives the camera's
		// cached projection behind it, which is not a per-frame cost worth
		// paying for a value that changes twelve times in four minutes.
		// **The path, not the handle.** The bridge resolves an asset field with
		// Registry::GetHandle, which matches on the path the registry stores;
		// a handle is the engine's internal name and the getter never hands
		// one out. Passing one fails the lookup and SetComponentField returns
		// false -- and a refused write says nothing to the scene, so every
		// shot quietly kept the lens the first one set.
		Entity self = Entity;
		if (self.SetComponentField("CameraComponent", "PostProfile", profile))
			m_Applied = profile;
	}

	// Quadratic, not cubic: one control point is what a leg needs to get round
	// one obstacle, and two would be two things to solve and two to drag. The
	// curve stays inside the triangle a-v-b, which is what lets the generator
	// clear trees against the two straight segments and know the curve is
	// clear as well.
	private static Vector3 Bezier(Vector3 a, Vector3 v, Vector3 b, float t)
	{
		float u = 1.0f - t;
		return a * (u * u) + v * (2.0f * u * t) + b * (t * t);
	}

	// "x y z" -- space separated inside the entry, because the table itself is
	// comma separated and nesting the same delimiter is how a table becomes
	// unparseable the first time somebody adds a negative number with a space
	// after the minus.
	private static bool ParseVector(string text, out Vector3 value)
	{
		value = Vector3.Zero;
		string[] parts = text.Trim().Split(' ');
		if (parts.Length != 3)
			return false;

		float x, y, z;
		if (!float.TryParse(parts[0], System.Globalization.NumberStyles.Float,
							System.Globalization.CultureInfo.InvariantCulture, out x)
			|| !float.TryParse(parts[1], System.Globalization.NumberStyles.Float,
							   System.Globalization.CultureInfo.InvariantCulture, out y)
			|| !float.TryParse(parts[2], System.Globalization.NumberStyles.Float,
							   System.Globalization.CultureInfo.InvariantCulture, out z))
			return false;

		value = new Vector3(x, y, z);
		return true;
	}

	private static Vector3 LerpAngles(Vector3 a, Vector3 b, float t) =>
		new Vector3(LerpAngle(a.X, b.X, t), LerpAngle(a.Y, b.Y, t),
					LerpAngle(a.Z, b.Z, t));

	// Round the short way. Two yaws either side of the wrap are numerically far
	// apart and visually adjacent, and lerping them directly spins the camera
	// most of the way round the scene to get somewhere it was already looking.
	private static float LerpAngle(float a, float b, float t)
	{
		float delta = b - a;
		while (delta > Mathf.Pi) delta -= Mathf.TwoPi;
		while (delta < -Mathf.Pi) delta += Mathf.TwoPi;
		return a + delta * t;
	}
}
