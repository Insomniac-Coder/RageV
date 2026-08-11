using System.Collections.Generic;
using System.Globalization;
using RageV;

/// <summary>The rules: crates out of the stack and onto the ground score.</summary>
/// <remarks>
/// Lives on the OutZone trigger. Each crate that lands in the zone counts
/// once, the beacon light walks from red to green with the count, and the
/// last one plays the win chime. Interact (F) resets the round by destroying
/// what is left and stamping a fresh stack from the prefab.
///
/// There is deliberately no text and no UI anywhere in this game: the light,
/// the sounds and the crates themselves are the whole scoreboard.
/// </remarks>
public class GameManager : Script
{
	private int m_TotalCrates = 6;

	private Entity m_BeaconLight;
	private Entity m_Confetti;
	private readonly HashSet<Entity> m_Out = new HashSet<Entity>();
	private bool m_Won;

	// A burst big enough to read as a celebration rather than a hiccup.
	private int m_ConfettiOnWin = 180;

	public override void OnCreate()
	{
		m_BeaconLight = Entity.FindByName("Beacon Light");
		if (!m_BeaconLight.Exists)
			Log.Warn("GameManager: no 'Beacon Light' to signal with");

		m_Confetti = Entity.FindByName("Confetti");
		if (!m_Confetti.Exists)
			Log.Warn("GameManager: no 'Confetti' emitter to fire on the win");

		SetBeacon(0.0f);
	}

	public override void OnTick(float deltaTime)
	{
		if (Input.WasActionPressed("Interact"))
			ResetRound();
	}

	public override void OnTriggerEnter(Collision collision)
	{
		if (m_Won || !collision.Other.Exists || collision.Other.Name != "Crate")
			return;

		// A crate that bounces, leaves and lands again still counts once.
		if (!m_Out.Add(collision.Other))
			return;

		SetBeacon((float)m_Out.Count / m_TotalCrates);

		if (m_Out.Count >= m_TotalCrates)
		{
			m_Won = true;
			Audio.PlayOneShot2D("audio/win.wav", 0.9f);
			Celebrate();
			Log.Info("GameManager: all crates down");
		}
		else
		{
			PlayOneShot("audio/plink.wav", 0.8f);
		}
	}

	/// <summary>The win, in particles. Additive, so it is correct from any angle.</summary>
	private void Celebrate()
	{
		if (!m_Confetti.Exists)
			return;

		// The emitter sits above the platform and emits nothing until asked.
		// One field, written as text through the component bridge -- the same
		// spelling the scene file uses.
		m_Confetti.SetComponentField("ParticleEmitterComponent", "Burst",
			m_ConfettiOnWin.ToString(CultureInfo.InvariantCulture));
	}

	private void SetBeacon(float progress)
	{
		if (!m_BeaconLight.Exists)
			return;

		// Red at rest, green at done, and brighter for the finale.
		float r = 1.0f - progress * 0.9f;
		float g = 0.12f + progress * 0.88f;
		m_BeaconLight.SetComponentField("LightComponent", "Color", Vec(r, g, 0.1f));
		m_BeaconLight.SetComponentField("LightComponent", "Intensity",
			(progress >= 1.0f ? 120.0f : 60.0f).ToString(CultureInfo.InvariantCulture));
	}

	// The bridge takes text in the scene file's forms: floats invariant,
	// vectors space-separated. A locale that writes 0,5 must not leak in.
	private static string Vec(float x, float y, float z) =>
		string.Format(CultureInfo.InvariantCulture, "{0} {1} {2}", x, y, z);

	private void ResetRound()
	{
		// Everything a round leaves behind: crates wherever they lie, balls
		// still rolling, and the old stack root itself.
		foreach (Entity crate in Entity.FindAllByName("Crate"))
			crate.Destroy();
		foreach (Entity ball in Entity.FindAllByName("Ball"))
			ball.Destroy();
		foreach (Entity stack in Entity.FindAllByName("CrateStack"))
			stack.Destroy();

		Entity.SpawnPrefab("prefabs/CrateStack.rprefab");

		m_Out.Clear();
		m_Won = false;
		SetBeacon(0.0f);
		Log.Info("GameManager: round reset");
	}
}
