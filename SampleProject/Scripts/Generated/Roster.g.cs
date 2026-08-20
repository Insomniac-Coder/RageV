// Generated from Roster.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;
using System.Collections.Generic;

public class Roster : Script
{
	// The graph's variables. Fields rather than locals, because
	// that is what makes them survive between one event and the next.
	private List<float> scores = new List<float>();
	private Dictionary<string, float> totals = new Dictionary<string, float>();

	public override void OnCreate()
	{
		scores.Add(7.5f);
		for (int index5 = 0; index5 < scores.Count; index5++)
		{
			var element5 = scores[index5];
			Log.Info(Text(element5));
		}
		var target10 = totals;
		target10["alpha"] = (float)scores.Count;
		Log.Warn("roster ready");
	}

	// The engine's named field API is text, and these are the forms it
	// parses. Invariant: a machine with a comma decimal point has to
	// write the same field value as one without.
	private static string Text(float value) =>
		value.ToString(System.Globalization.CultureInfo.InvariantCulture);
}
