using System;
using System.Globalization;
using RageV;

/// <summary>A crate that sounds like one when something hits it, and puffs.</summary>
/// <remarks>
/// The dust is fired from here rather than authored on the crate, because a
/// burst wants to happen <em>where the contact was</em> — the shared "Impact"
/// emitter is moved to the contact point and told to spawn. One emitter serves
/// every crate: two impacts in the same frame put the puff at the second one,
/// which nobody has ever noticed.
/// </remarks>
public class Crate : Script
{
	// Below this a contact is the stack settling, not a hit worth hearing --
	// without the threshold, a stack coming to rest plays like a drum roll.
	private float m_QuietestAudibleSpeed = 1.2f;
	private float m_FullVolumeSpeed = 8.0f;

	private int m_DustAtRest = 9;    // a nudge still raises something
	private int m_DustAtFull = 30;   // a solid hit

	private Entity m_Impact;

	public override void OnCreate()
	{
		// Once, here: FindByName is linear over the scene, and this crate is
		// about to be hit repeatedly.
		m_Impact = Entity.FindByName("Impact");
		if (!m_Impact.Exists)
			Log.Warn("Crate: no 'Impact' emitter in the scene; hits will be silent visually");
	}

	public override void OnCollisionEnter(Collision collision)
	{
		if (collision.ImpactSpeed < m_QuietestAudibleSpeed)
			return;

		// One number drives both the sound and the dust, so a light tap looks
		// as light as it sounds.
		float strength = MathF.Min(collision.ImpactSpeed / m_FullVolumeSpeed, 1.0f);

		PlayOneShot("audio/impact.wav", strength);
		Puff(collision.Point, strength);
	}

	private void Puff(Vector3 point, float strength)
	{
		if (!m_Impact.Exists)
			return;

		// World space on the emitter, so the dust hangs where it was born
		// rather than following the emitter to the next impact.
		m_Impact.Position = point;

		int count = m_DustAtRest + (int)((m_DustAtFull - m_DustAtRest) * strength);

		// Burst is an order, not a rate: the simulation consumes it on the
		// next step and zeroes it, whether or not Emit is on.
		m_Impact.SetComponentField("ParticleEmitterComponent", "Burst",
			count.ToString(CultureInfo.InvariantCulture));
	}
}
