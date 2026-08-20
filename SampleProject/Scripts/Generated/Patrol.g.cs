// Generated from Patrol.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;

public class Patrol : Script
{
	// The graph's variables. Fields rather than locals, because
	// that is what makes them survive between one event and the next.
	private float timer;

	public override void OnCreate()
	{
		Log.Info("patrolling");
	}

	public override void OnTick(float deltaTime)
	{
		timer = (timer + deltaTime);
		var target14 = Entity;
		target14.Position = new Vector3((Mathf.Sin(timer) * 3.0f), 1.0f, 0.0f);
	}
}
