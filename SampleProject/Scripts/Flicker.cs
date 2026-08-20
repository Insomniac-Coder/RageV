using RageV;

// A fire's light, which is not a lamp's.
//
// Two sine waves at unrelated rates rather than one, and neither at a rate a
// person can count. One wave is a pulse; two that do not divide into each
// other never repeat audibly, which is the whole difference between firelight
// and a warning beacon.
//
// Deliberately not random: the scene has to render the same picture at the
// same frame for every screenshot comparison in the repository (7r, 7y). A
// clock or an RNG here would make the camp scene unusable as a fixture, and
// the flicker would look no better for it.
public class Flicker : Script
{
	private float Base = 34.0f;
	private float Amount = 0.22f;
	private float Rate = 9.0f;

	private float m_Elapsed;

	public override void OnFrame(float deltaTime)
	{
		m_Elapsed += deltaTime;

		float fast = Mathf.Sin(m_Elapsed * Rate);
		float slow = Mathf.Sin(m_Elapsed * Rate * 0.37f + 1.3f);

		// Weighted so the slow wave carries the shape and the fast one only
		// roughens it. Equal weights read as a wobble.
		float wobble = fast * 0.35f + slow * 0.65f;

		Entity.SetComponentField("LightComponent", "Intensity",
								 Text(Base * (1.0f + Amount * wobble)));
	}

	private static string Text(float value) =>
		value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}
