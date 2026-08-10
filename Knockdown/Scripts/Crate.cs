using System;
using RageV;

/// <summary>A crate that sounds like one when something hits it.</summary>
public class Crate : Script
{
	// Below this a contact is the stack settling, not a hit worth hearing --
	// without the threshold, a stack coming to rest plays like a drum roll.
	private float m_QuietestAudibleSpeed = 1.2f;
	private float m_FullVolumeSpeed = 8.0f;

	public override void OnCollisionEnter(Collision collision)
	{
		if (collision.ImpactSpeed < m_QuietestAudibleSpeed)
			return;

		PlayOneShot("audio/impact.wav",
					MathF.Min(collision.ImpactSpeed / m_FullVolumeSpeed, 1.0f));
	}
}
