#pragma once

// A rectangular body of water as a source of one mesh.
//
// **A component, not an asset, and that is the point.** A terrain is a file --
// its heights are authored and stored. A water body has nothing to store: it
// is a rectangle, and the only things anyone needs to say about one are how
// long it is, how wide, and how finely to divide it. So the numbers live on
// the component and the geometry is built from them, which is what lets an
// author drag a bay to size in the inspector instead of regenerating an asset
// beside the scene.
//
// The renderer never learns the word water, on exactly the terms Terrain set:
// what comes out is an ordinary Mesh, drawn, shadowed, traced, culled and
// picked as a crate is, wearing an ordinary material. Everything that will
// make it *look* like water is a material and a shader, and none of it is
// here.
//
// **Why it is a grid and not two triangles.** Flat, a rectangle needs four
// vertices; the surface only ever wants a normal map. Waves that break the
// silhouette -- a horizon that is not a straight line, a bridge pier the swell
// rises against -- need vertices to move, and vertices cannot be added later
// without rebuilding every scene that placed one. So the subdivision exists
// from the start and is authored in metres, and a scene that wants the cheap
// version sets a coarse spacing rather than getting a different mesh.
//
// The pure builder (BuildGeometry) needs no device, which is what lets the
// suite assert vertex counts, winding and extents headlessly.

#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"

#include <vector>

namespace RageV
{
	struct WaterComponent;

	class Water
	{
	public:
		// The grid, in the component's own space: centred on the origin,
		// lying in the XZ plane at y = 0, `width` across X and `length`
		// along Z. The entity's transform places and orients it.
		//
		// Free of the device and of the component so the checks can call it
		// with numbers and compare what comes back.
		static void BuildGeometry(float width, float length, float spacing,
								  float textureScale,
								  std::vector<MeshVertex>& vertices,
								  std::vector<uint32_t>& indices);

		// Build the component's runtime mesh if it has none or if a dimension
		// has changed since it was built. Returns whether a mesh is available
		// afterwards.
		//
		// **Replaced, never mutated**, the rule Terrain::Resolve follows for
		// the same reason: Play's snapshot and a duplicated entity share the
		// pointer, so reshaping in place would resize the original too.
		static bool Resolve(WaterComponent& component);

		// How many quads a body of these dimensions comes to. Used by the
		// inspector to say the cost out loud, and by the checks.
		static uint32_t QuadCount(float width, float length, float spacing);

		// **The one water material, owned here (owner's call).** A body of
		// water has no material field: the engine's is the only one, so every
		// body in every scene is the same water and a scene file cannot point
		// one somewhere else. Built on first use because it needs a device, and
		// kept until Shutdown.
		//
		// The consequence worth stating: when the wave work lands it changes
		// this one material, and every body of water in the project gets it. A
		// per-body material would have made that a migration.
		static RHI::Ref<Material> GetMaterial();

		// Drops the material, so a device teardown does not leave one holding
		// GPU resources it outlived.
		static void Shutdown();
	};
}
