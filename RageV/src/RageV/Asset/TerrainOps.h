#pragma once

// Whole-terrain generation operators: erosion, bedding, and the landform
// masks that painting should follow (ENGINE-NOTES 7at).
//
// **What this is for, and why the brush is not it.** `TerrainBrush` is a
// hand: a kernel under a cursor, one step of a stroke, bounded by a
// footprint. It already erodes -- droplets, under the brush -- and that is
// the right shape for a tool you drag. It is the wrong shape for *making a
// landscape*, because the thing that makes ground read as ground is a
// process that ran over the whole field: water moving material downhill
// until the valleys branch and join, and gravity pulling anything steeper
// than the material can stand down to the angle it can. Those are field
// operations with an iteration count, not strokes, and every terrain tool
// worth the name exposes them as such.
//
// A field built from formulas -- domes, sines, summed noise -- makes a
// *shape*. Only these make a *place*.
//
//   HydraulicErode   droplets that pick up, carry and drop sediment. The
//                    single highest-value operator here: the dendritic
//                    valley network and the fans at its mouths come from
//                    material actually being carried, and no amount of
//                    noise, blurring or smoothing produces either.
//   ThermalErode     talus slippage to an angle of repose. Planes slopes to
//                    one angle, sharpens the ridges between them, piles the
//                    debris at the bottom -- the difference between a hill
//                    and a heap.
//   Stratify         horizontal bedding, stepped into the steep ground only.
//                    A cliff cut from smooth noise is a smooth cliff, and no
//                    rock is smooth at a hundred metres.
//   Analyse          slope, curvature and flow accumulation as float fields.
//   PaintByLandform  layer weights from those fields, so material follows
//                    the landform instead of being sprinkled over it: rock
//                    on the convex ground being stripped, soil and scrub in
//                    the concave ground receiving, sand at the waterline,
//                    and the damp channel where the water actually goes.
//
// Everything here is a function of a TerrainData and its parameters -- no
// device, no scene, no editor -- so the suite can assert the arithmetic and
// a tool, a script or an importer can all call the same thing. Everything
// is seeded, so the same call gives the same terrain twice.
//
// Heights are the asset's 16-bit samples throughout. Metres enter only
// through `HeightMetres` and `SizeMetres`, because an angle of repose and an
// angle of rest are angles in the world and mean nothing on a unit grid.

#include "TerrainData.h"
#include "TerrainBrush.h"

#include <cstdint>
#include <vector>

namespace RageV
{
	namespace TerrainOps
	{
		// The dimensions an operator needs to talk about the world: how many
		// metres the grid spans and how many a full sample stands. Both are
		// the TerrainComponent's, and both are needed -- a slope is a ratio of
		// the two, and every angle here is a slope.
		struct Scale
		{
			float SizeMetres = 256.0f;
			float HeightMetres = 40.0f;

			// Metres between neighbouring samples.
			float Cell(uint32_t resolution) const
			{
				return resolution > 1 ? SizeMetres / (float)(resolution - 1) : SizeMetres;
			}
		};

		// --- thermal ---------------------------------------------------------
		struct ThermalParams
		{
			// Passes. Each moves material at most one sample, so this is also
			// how far a grain can travel.
			int Iterations = 60;
			// The angle the material stands at. About 33 for loose scree,
			// 40-45 for the rock a sea cliff is made of. **Eroding rock at a
			// scree angle takes a cliff apart into a mound**, which is the one
			// way to get this obviously wrong.
			float ReposeDegrees = 40.0f;
			// How much of the excess moves per pass, 0..1.
			float Rate = 0.55f;
		};

		// Slides everything steeper than the repose angle downhill. Returns
		// the number of samples that moved at all, which is zero once the
		// field has settled -- so a caller can iterate to convergence.
		uint32_t ThermalErode(TerrainData& data, const Scale& scale,
							  const ThermalParams& params);

		// --- hydraulic -------------------------------------------------------
		struct HydraulicParams
		{
			// Droplets dropped over the whole field, and how far each walks.
			// Steps is how long the channels get; droplets is how continuous
			// they are.
			uint32_t Droplets = 200000;
			int Steps = 56;
			uint32_t Seed = 7;

			// How much of the last heading a droplet keeps. Pure gradient
			// descent traps it in every dimple; a little inertia carries it
			// out, and carrying it out is what makes a channel rather than a
			// pit.
			float Inertia = 0.06f;
			// How much sediment a droplet may carry per unit of slope, speed
			// and water. The dial that sets how deep the channels cut.
			float Capacity = 3.2f;
			// The capacity floor, so a droplet on flat ground does not drop
			// its whole load at once and pockmark the plain.
			float MinSlope = 0.02f;
			// Cutting against filling. **A landscape that only cuts is a
			// badland**; the fans at the valley mouths are deposition.
			float ErodeRate = 0.35f;
			float DepositRate = 0.28f;
			float Evaporate = 0.02f;
			float Gravity = 6.0f;

			// **The three limiters, and they are not decoration.** Cutting a
			// cell deepens it, which steepens the drop into it, which raises
			// the next droplet's capacity, which cuts it deeper. Left alone
			// that loop diverges -- measured going to 1e33 within thirty
			// steps on a 257 grid. MaxSpeed stops the velocity term
			// compounding; MaxCapacity caps what a droplet may carry however
			// fast it is going; MaxChangeMetres says no single droplet-step
			// moves more than this much ground, which is also just true.
			float MaxSpeed = 6.0f;
			float MaxCapacity = 8.0f;
			float MaxChangeMetres = 0.5f;
		};

		// Runs the droplets over the whole field. Returns how many of them
		// were still moving at the last step -- a run where that is near zero
		// has spent itself and more steps would cost nothing.
		uint32_t HydraulicErode(TerrainData& data, const Scale& scale,
								const HydraulicParams& params);

		// --- bedding ---------------------------------------------------------
		struct StratifyParams
		{
			// How far a bed is pushed out, in metres, peak to trough.
			float AmountMetres = 3.5f;
			// Metres of height between one bed and the next.
			float ThicknessMetres = 16.0f;
			// Metres of height the beds gain per metre of x. Perfectly level
			// bedding is its own tell.
			float Dip = 0.004f;
			// The slope band it applies over: none below the first, all above
			// the second. Bedding shows on a face and not on a floor.
			float SlopeFrom = 0.55f;
			float SlopeTo = 1.05f;
		};

		void Stratify(TerrainData& data, const Scale& scale,
					  const StratifyParams& params);

		// --- the landform masks ----------------------------------------------
		//
		// One float per sample, row-major on the heights' grid.
		struct Landform
		{
			uint32_t Resolution = 0;
			// |grad h|, dimensionless: 1.0 is 45 degrees.
			std::vector<float> Slope;
			// The Laplacian in metres per metre squared: positive on convex
			// ground -- ridges, noses, the shoulders being stripped -- and
			// negative in the concave ground that receives what they shed.
			// **The mask nobody thinks of and every painted terrain needs**:
			// painting by height and slope alone gives contour-following
			// bands, which is the other way a terrain announces itself.
			std::vector<float> Curvature;
			// How much water passes through each sample, in units of the rain
			// that falls on one. Multiple-flow-direction, advected a fixed
			// number of steps: not a solved drainage network -- no depression
			// filling, no ordering -- but it lands water in the draws in the
			// right proportions, which is what a paint mask needs.
			std::vector<float> Flow;

			float SlopeAt(uint32_t x, uint32_t z) const { return Slope[(size_t)z * Resolution + x]; }
			float CurvatureAt(uint32_t x, uint32_t z) const { return Curvature[(size_t)z * Resolution + x]; }
			float FlowAt(uint32_t x, uint32_t z) const { return Flow[(size_t)z * Resolution + x]; }
		};

		// Passes to advect the water for. More reaches further down the hill.
		Landform Analyse(const TerrainData& data, const Scale& scale,
						 int flowIterations = 48);

		// --- painting by landform --------------------------------------------
		struct PaintParams
		{
			// Which layer each material is. -1 leaves that material unused.
			int SoilLayer = 0;
			int RockLayer = 1;
			int SandLayer = 2;
			int ScrubLayer = 3;

			// Rock takes over across this slope band.
			float RockSlopeFrom = 0.55f;
			float RockSlopeTo = 1.05f;
			// And on convex ground, which is where it is exposed.
			float RockCurvature = 0.02f;

			// Sand from this far below the waterline to this far above it.
			float SeaLevelMetres = 0.0f;
			float SandBelowMetres = 8.0f;
			float SandAboveMetres = 3.0f;

			// Scrub above the beach, less of it where the rock is.
			float ScrubFromMetres = 2.0f;
			float ScrubToMetres = 22.0f;

			// Where the flow exceeds this, the channel's own material shows
			// through -- soil in the draws, whatever the slope says.
			float ChannelFlow = 6.0f;
		};

		// Writes `data.Weights` from the landform. Allocates them if the
		// terrain was unpainted.
		void PaintByLandform(TerrainData& data, const Scale& scale,
							 const Landform& landform, const PaintParams& params);
	}
}
