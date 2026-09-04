#pragma once

// **How many rays the frame casts, by kind, counted where they are cast.**
// WR-16 step S0 (docs/RAY-BUDGET-DESIGN.md Part IV; ENGINE-NOTES 7cy).
//
// The frame's ray cost is spread across passes that a timer cannot split:
// the shadow rays and the water's mirror and refraction rays are cast from
// inside the lit fragment shader, beside the BRDF work, and the by-pass table
// reports the pass. A budget that spends rays has to know how many there
// are, and the only place that knows is the shader that casts them.
//
// So every launch site increments a lane -- TraceShadowFromMasked for the
// shadow rays, TraceSurface for the mirror, refraction and bounce rays, the
// occlusion pass for its taps -- into per-invocation registers, and each
// shader adds them into one small storage buffer once at the end of its
// main: a subgroup reduction and a single atomic per lane per wave, so a
// million pixels cost a few thousand atomics rather than a million. Beside
// the rays, what the budget will need next: how many lights a pixel walks,
// how many a traced hit walks, and how many pixels the temporal resolve
// trusted its history for.
//
// The buffer is read back one frame late through RHIDevice::ReadBuffer, the
// way the GPU timestamps are, so the numbers cost no stall; and like the
// timestamps they describe a frame a couple back, which over a benchmark is
// the same average and for a single frame is not.
//
// Vulkan only, because the counters are a ray-tracing instrument: they count
// what the ray-query path casts, and OpenGL casts nothing. The bindings are
// declared under RV_RAY_SHADOWS in the lit shaders, so the OpenGL layouts
// never see them.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include <cstdint>

namespace RageV
{
	class RayCounters
	{
	public:
		// One lane per number. The order is the shader's
		// (`RAY_LANE_*` in pbr_fragment.glsl); the two must agree.
		enum Lane : uint32_t
		{
			// Every shadow ray: the lamps' and the sun's on screen, the
			// sun's at a traced hit, and the moving-only ray a static pixel
			// traces toward a fully baked lamp (7cx).
			ShadowRays = 0,
			// The water's mirror and refraction rays (WR-18: one per quad
			// lane that traces, not one per pixel).
			WaterRays,
			// The opaque surfaces' mirror rays -- the steel at roughness
			// under the gloss window.
			ReflectionRays,
			// The traced bounce's rays, both bounces. Zero on a frame whose
			// indirect light is baked, which is the point of counting it.
			GiRays,
			// The ray-traced occlusion pass's taps.
			AoRays,
			// Fragments the lit shader shaded: opaque and water, not the sky.
			// What the per-pixel averages divide by.
			LitFragments,
			// Summed over those fragments: the lights in the fragment's
			// cluster list, directional ones included -- what the loop walks.
			LightsWalked,
			// The largest such list any fragment walked.
			LightsMax,
			// Traced surfaces that were shaded (ShadeTraced): mirror,
			// refraction and bounce hits alike.
			Hits,
			// Summed over those hits: the lights the hit walked.
			HitLightsWalked,
			// Pixels the temporal resolve wrote, and of them the ones that
			// reused their history -- the validity lane of o_Moments.w.
			TaaPixels,
			TaaReused,
			// Room to grow without a layout change. Sixteen words, one
			// cache line.
			Count = 16
		};

		// Set 0, binding 21: declared by every lit pipeline family under
		// RV_RAY_SHADOWS, beside the acceleration structure at 14, the
		// instance table at 15 and the field at 18 (Part IV finding 5).
		static constexpr uint32_t kBinding = 21;
		// The post-process passes that count -- the occlusion pass and the
		// temporal resolve -- have layouts of their own, and bind it here.
		static constexpr uint32_t kPostBinding = 5;
		// **Sixty-four copies of the lanes, one per screen tile, summed here
		// after the readback.** Every wave in the frame adds into the buffer
		// and atomics to one address serialise in the L2, so the lanes are
		// spread by the wave's position on screen; the 4 KB read back is
		// nothing. Kept as the right shape for a counter every wave touches,
		// not as a fix for anything measured: on this GPU the spread changed
		// nothing, and the flush's first cost was the lost early depth test
		// (ENGINE-NOTES 7cy). `RAY_COUNTER_SLOTS` in the shaders; the two
		// must agree.
		static constexpr uint32_t kSlots = 64;
		static constexpr uint64_t kBytes = (uint64_t)kSlots * Count * sizeof(uint32_t);

		// One frame's numbers. `Valid` is false until the first readback
		// lands and on any frame the device had nothing to hand back.
		struct Sample
		{
			uint32_t Lanes[Count]{};
			bool     Valid = false;

			uint64_t TotalRays() const
			{
				return (uint64_t)Lanes[ShadowRays] + Lanes[WaterRays] + Lanes[ReflectionRays]
					 + Lanes[GiRays] + Lanes[AoRays];
			}
			// Rays per lit fragment; zero when nothing was lit.
			float RaysPerFragment() const;
			float LightsPerFragment() const;
			float LightsPerHit() const;
			// The share of temporally resolved pixels that reused history,
			// in [0, 1]; negative when no temporal resolve ran.
			float TemporalConfidence() const;
		};

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Whether the buffers exist to bind: a device that traces. False on
		// OpenGL, always, which is also where nothing declares the binding.
		static bool IsAvailable();

		// Once a frame, from Renderer::BeginFrame, outside any render pass:
		// takes back what this frame slot counted the last time round,
		// zeroes the slot's buffer for this frame, and orders the zero
		// before the shaders that add to it.
		static void BeginFrame(RHI::RHICommandList& cmd);

		// This frame slot's buffer, for the sets that bind it. Null when
		// unavailable.
		static const RHI::Ref<RHI::RHIBuffer>& Buffer();

		// The most recent frame that came back -- what a HUD shows.
		static const Sample& Last();
		// The sample that came back *this* frame, if one did -- what a
		// benchmark files per frame, so a frame the device answered nothing
		// for is not counted twice. Valid is false on such frames.
		static const Sample& Fresh();
	};
}
