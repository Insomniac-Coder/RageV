// Generated from Spinner.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;

public class Spinner : Script
{
	public override void OnCreate()
	{
		Log.Info("spinner ready");
	}

	public override void OnTick(float deltaTime)
	{
		var value8 = ((deltaTime * 180.0f) / 2.0f);

		if (value8 > 0.5f)
		{
			Entity.SetComponentField("TransformComponent", "Rotation", Text(value8));
			Log.Info("spun");
		}
		else
		{
			Log.Info("too slow");
		}
	}

	public override void OnCollisionEnter(Collision collision)
	{
		Log.Info(Entity.GetComponentField("TagComponent", "Tag"));
		Entity.SetComponentField("RigidbodyComponent", "LinearVelocity", Text((collision.ImpactSpeed - 1.0f)));
	}

	// The engine's named field API is text, and these are the forms it
	// parses. Invariant: a machine with a comma decimal point has to
	// write the same field value as one without.
	private static string Text(float value) =>
		value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}
