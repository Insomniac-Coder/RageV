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

	// One body's foam accumulation state: the ping-pong pair, which half is
	// readable, and when it last stepped. Owned by the component (so a body's
	// foam dies with it) and driven by Water::UpdateFoam. Opaque outside
	// Water.cpp on purpose -- nothing else has an opinion about which half is
	// which, and the one bug that design rules out is somebody sampling the
	// half a dispatch is writing.
	class WaterFoam
	{
	public:
		RHI::Ref<RHI::RHITexture> Textures[2];
		// Two fixed directions -- Sets[i] reads Textures[i] and writes the
		// other -- built once with the textures, so no set is ever rewritten
		// under a recorded bind.
		RHI::Ref<RHI::RHIResourceSet> Sets[2];
		uint32_t Current = 0;      // the readable half after the last step
		uint64_t Frame = 0;        // Renderer::GetFrameCount() at that step
		uint32_t Resolution = 0;
	};

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

		// One step of the foam accumulation buffer: advect what was there
		// downwind, decay it, age fresh into residual, and inject where the
		// Jacobian says the surface is folding *now*. Creates the ping-pong
		// pair on first use, sized from the body; dispatches at most once per
		// frame however many views draw the scene (the editor draws two, and
		// a double step would double the decay). Records its own barriers, so
		// it must be called outside a render pass -- the frame graph's
		// compute pass is where it lives.
		//
		// `time` and `deltaSeconds` come from the component's own clock, the
		// same one the vertex stage displaces by, so the injection lands on
		// the crests the geometry draws.
		static void UpdateFoam(RHI::RHICommandList& cmd, WaterComponent& component,
							   float deltaSeconds);

		// The readable half of a body's pair after UpdateFoam ran, or null
		// before the first step -- the caller binds the shared black
		// stand-in, which is a calm sea.
		static RHI::Ref<RHI::RHITexture> CurrentFoam(const WaterComponent& component);

		// The two generated tiles, built once on first use from fixed seeds.
		//
		// **Generated at startup, not authored**, and that is the component's
		// own philosophy applied to its textures: a body of water has no
		// material field, so it cannot have texture fields either -- the
		// engine builds the look. The detail map is a periodic Gerstner sum
		// baked to slopes (the waves below what any grid can carry); the
		// pattern is ridged periodic noise, the lace a drained sheet of
		// whitewater leaves.
		static RHI::Ref<RHI::RHITexture> GetDetailNormal();
		static RHI::Ref<RHI::RHITexture> GetFoamPattern();

		// Drops the material, the generated tiles and the foam pipeline, so a
		// device teardown does not leave any of them holding GPU resources
		// they outlived.
		static void Shutdown();
	};
}
