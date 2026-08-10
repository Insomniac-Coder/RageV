using RageV;

/// <summary>Keeps a quiet wind loop under the whole round.</summary>
/// <remarks>
/// The entity carries no AudioSourceComponent in the scene file; this script
/// assembles one at runtime through the component bridge and starts it. That
/// is deliberate -- it means the ambience works the moment the script is
/// attached to anything, with nothing else to set up.
/// </remarks>
public class Ambience : Script
{
	public override void OnCreate()
	{
		if (!Entity.HasComponent("AudioSourceComponent"))
			Entity.AddComponent("AudioSourceComponent");

		Entity.SetComponentField("AudioSourceComponent", "Clip", "audio/wind.wav");
		Entity.SetComponentField("AudioSourceComponent", "Loop", "true");
		Entity.SetComponentField("AudioSourceComponent", "Volume", "0.35");
		// Heard evenly everywhere rather than radiating from a point; wind
		// that gets quieter as you turn away is a haunted speaker, not wind.
		Entity.SetComponentField("AudioSourceComponent", "Spatial", "false");

		PlaySource();
	}
}
