// Generated from Spin.rvgraph. Do not edit.
//
// Rewritten whenever the graph is saved or the scripts are built, so an
// edit here survives exactly until the next one. Change the graph.
// ENGINE-NOTES 7bh.

using RageV;

public class Spin : Script
{
	public override void OnTick(float deltaTime)
	{
		Entity.SetComponentField("TransformComponent", "Rotation", "0 1 0");
		Log.Info("spun");
	}
}
