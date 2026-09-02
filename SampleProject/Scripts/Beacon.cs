using RageV;

// An aviation obstruction beacon: on, off, and nothing in between worth
// looking at.
//
// **The flash is what makes it a beacon.** A steady red point on a tower top
// is a red point; the same point flashing at 30 a minute is unmistakably a
// warning light, and it is the thing every night photograph of a tall
// structure has in it. FAA L-864 specifies red, 2 000 cd, and 20 to 40
// flashes a minute -- the period below is the middle of that band and the
// scene sets the two towers out of phase, because a real pair is not
// synchronised and a synchronised pair reads as one object.
//
// **Square, not sinusoidal, and that is the point of `Duty`.** A xenon or LED
// beacon is a pulse: full output for a fraction of the cycle and off for the
// rest. A sine reads as a slow breathing glow, which is what a beacon
// specifically is not. The one concession is the short ramp at each edge --
// two frames' worth, not a fade -- which exists because an instantaneous step
// in a light this bright is a step in every temporal buffer downstream of it.
//
// **Deliberately not random, and driven by elapsed time rather than a clock**,
// the same discipline Flicker.cs documents: every screenshot comparison in
// this repository depends on frame N being the same picture twice, and an RNG
// or a wall clock here would make the bridge unusable as a fixture.
//
// The light this drives is `IsBaked: false`. It has to be: an intensity that
// moves is not one a solve can pre-integrate, and a flashing source blended
// into a stored bake is the frame guessing into a store.
public class Beacon : Script
{
	// Candela over the colour's luminance -- the authored Intensity, which the
	// generator computes and writes here so the two cannot drift apart.
	private float Peak = 7984.0f;
	private float Period = 2.0f;
	// The fraction of the cycle the beacon is lit. 0.18 of two seconds is a
	// 360 ms flash, which is what a medium-intensity red beacon looks like.
	private float Duty = 0.18f;
	// Seconds to offset this beacon's cycle by, so a pair does not blink
	// together.
	private float Phase = 0.0f;

	private float m_Elapsed;

	public override void OnFrame(float deltaTime)
	{
		m_Elapsed += deltaTime;

		float period = Mathf.Max(Period, 0.05f);
		float cycle = (m_Elapsed + Phase) % period;
		float lit = Mathf.Clamp(Duty, 0.01f, 1.0f) * period;

		// The ramp: a fixed slice of the lit window at each end rather than a
		// fixed number of seconds, so shortening the flash cannot leave a
		// beacon that is all ramp and never reaches full.
		float edge = lit * 0.12f;

		float level;
		if (cycle >= lit)
			level = 0.0f;
		else if (cycle < edge)
			level = cycle / edge;
		else if (cycle > lit - edge)
			level = (lit - cycle) / edge;
		else
			level = 1.0f;

		Entity.SetComponentField("LightComponent", "Intensity", Text(Peak * level));
	}

	private static string Text(float value) =>
		value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}
