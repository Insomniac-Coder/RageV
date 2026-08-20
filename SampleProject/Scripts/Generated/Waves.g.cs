// Generated from Waves.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;

public class Waves : Script
{
	// The graph's variables. Fields rather than locals, because
	// that is what makes them survive between one event and the next.
	private float total;

	public override void OnCreate()
	{
		for (int index2 = 0; index2 < (int)(4.0f); index2++)
		{
			Log.Info(Text((float)index2));
		}
		Settle();
	}

	private void Settle()
	{
		total = (total + 1.0f);
		Log.Warn("settled");
	}

	// The engine's named field API is text, and these are the forms it
	// parses. Invariant: a machine with a comma decimal point has to
	// write the same field value as one without.
	private static string Text(float value) =>
		value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}
