#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Renderer/IrradianceVolume.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

#include <vector>

namespace RageV
{
	// **The realtime cache's own field: one box that follows the view.**
	//
	// Separate from the authored volumes, and deliberately a separate *object*
	// rather than a variant of one. The two answer different questions and have
	// different lifetimes:
	//
	//   IrradianceVolume   boxes somebody placed around rooms, holding an
	//                      answer solved offline and loaded from disk. Bound
	//                      when the GI source says Baked.
	//   this               one box centred on the camera, holding an answer
	//                      being solved right now, at a fixed ray cost a frame.
	//                      Exists only while the source says Realtime.
	//
	// **Composition, not inheritance, and not a shared texture.** It owns a
	// volume rather than being one, so `IrradianceVolume` is untouched by any
	// of this -- no protected members opened up for a subclass, no virtuals
	// added to a class every frame samples. What this adds is placement policy,
	// which is the only thing the realtime case actually needs that the baked
	// one does not.
	//
	// Sharing one texture between the two was the first shape and it was wrong:
	// a Realtime session overwrote the bake in memory, and Baked afterwards
	// meant whatever the cache had last computed. Nothing here can do that,
	// because the two never touch the same object.
	class RuntimeIrradianceField
	{
	public:
		// **Fixed cell count, so the cost does not depend on the world.** This
		// is the whole reason a following grid beats one stretched over the
		// scene: a showroom and a bridge cost exactly the same, because the
		// grid is sized by the ray budget rather than by what it stands in.
		// What world size changes is only how far the cache reaches; past its
		// edge the readers fall back to what they do with no field at all.
		static constexpr uint32_t kCellsX = 24;
		static constexpr uint32_t kCellsY = 12;
		static constexpr uint32_t kCellsZ = 24;
		static constexpr float    kSpacing = 1.0f;

		// **The box slides; it is not rebuilt.**
		//
		// It was rebuilt once, on a six-metre threshold, because moving it
		// seemed to need a setter on IrradianceVolume. It does not: the box
		// travels with the *request* and with the readers' copy, and the
		// texture never moves. Rebuilding meant discarding every solved cell
		// and going dark for about seven frames each time the view crossed the
		// threshold, which is not a trade-off, it is a defect.
		//
		// Snapped to whole cells and moved as soon as it is a cell out, so the
		// grid always straddles the view and the cells it holds describe places
		// a whole number of cells away rather than a fraction.
		static constexpr float kRecentreDistance = kSpacing;

		// Rays a cell casts each time the sweep reaches it. Not the number the
		// answer converges from -- a cell is revisited every sweep and blends by
		// the hysteresis, so what settles there averages far more than this.
		static constexpr uint32_t kRaysPerCell = 64;
		// Rays a frame across the grid. Fixed and independent of screen
		// resolution, which is the property this whole arrangement buys. At 64 a
		// cell that is 1024 cells a frame, so the grid goes round in about seven
		// frames.
		static constexpr uint32_t kRayBudget = 1u << 16;
		// How much of a fresh estimate a revisited cell takes. Low, because
		// stillness is the product: a cell revisited forever must not step
		// visibly, and the sweep comes round often enough that this still
		// follows a light being switched on in well under a second.
		static constexpr float kHysteresis = 0.05f;

		// Creates the box, or replaces it when the view has left it. Returns
		// whether a volume is standing afterwards -- false only if the device
		// could not allocate one, in which case the caller carries on with no
		// field, exactly as a scene with no volumes does.
		bool Update(RHI::RHIDevice& device, const Vec3& cameraPosition);

		// Drops the volume and forgets where it was. Called when the mode stops
		// wanting one: holding it would be megabytes for a mode that cannot read
		// it, and a grid left standing is what the separation exists to prevent.
		void Release();

		const RHI::Ref<IrradianceVolume>& Volume() const { return m_Volume; }

		// The box as it stands now, which after any movement is not the one the
		// volume was built with. Both the solve and the readers take this.
		const IrradianceVolume::Region& Region() const { return m_Region; }
		const std::vector<IrradianceVolume::Region>& Regions() const { return m_Regions; }
		bool IsPlaced() const { return m_Volume != nullptr; }

		// **Whether the cache has been round once.**
		//
		// A freshly built grid is black, and a room fading up from black is
		// worse than anything this buys. So the caller keeps the baked field
		// bound until this says yes, and only then releases it. Replacing the
		// box clears it: a new grid is black again.
		bool IsWarm() const { return m_Warm; }
		void MarkWarm() { m_Warm = true; }

		// Where the box is centred, snapped to whole cells.
		const Vec3& Centre() const { return m_Centre; }

	private:
		RHI::Ref<IrradianceVolume> m_Volume;
		IrradianceVolume::Region m_Region{};
		// The same region as a list, because that is the shape the readers take.
		std::vector<IrradianceVolume::Region> m_Regions;
		Vec3 m_Centre{ 0.0f };
		bool m_Warm = false;
	};
}
