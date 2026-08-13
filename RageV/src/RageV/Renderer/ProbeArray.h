#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	class ReflectionProbe;

	// Every environment a scene can reflect, in two cube arrays a shader
	// indexes per object.
	//
	// The alternative was to bind one probe's cube per draw and choose it
	// against the camera, which is one scene descriptor set per probe in view
	// and -- worse -- makes the probe part of the sort key, so two objects near
	// different probes can no longer share an instanced draw. Here the choice
	// rides in the instance beside the model matrix, and a run never splits.
	//
	// Slot 0 is the sky, always. "No probe reaches this object" is then an
	// index rather than a branch, and a scene with no probes at all is a
	// one-slot array that costs what a single cube did.
	class ProbeArray
	{
	public:
		// Plus the sky, so sixteen slices. Sixteen cubes at 128 pixels with a
		// full roughness chain is about 25 MB, which is a reasonable ceiling
		// for something a scene gets for free; a scene that wants more wants
		// probe streaming, which is a different feature.
		static constexpr uint32_t kMaxProbes = 15;
		static constexpr uint32_t kSlots = kMaxProbes + 1;
		static constexpr uint32_t kSkySlot = 0;

		// Irradiance has no detail worth resolving -- every texel is a whole
		// hemisphere -- so this is the same 16 the CPU convolution uses.
		static constexpr uint32_t kIrradianceSize = 16;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Drops both arrays. Called when the project changes, since every
		// source they were filled from is gone.
		static void ClearCache();

		static bool IsReady();

		// Makes sure the arrays exist, hold cubes of `faceSize`, and have room
		// for `slots` cubes.
		//
		// `slots` is 1 + the scene's probe count, not the fixed maximum,
		// because face size now follows the *sky's* resolution as well as the
		// probes' -- a 512px HDR sky reflected through a 128px slice loses
		// three quarters of its detail, which reads as everything reflective
		// going soft. Sixteen fixed slots at 512 would be a quarter gigabyte;
		// two slots at 512 is 33 MB, and two is what a scene with one probe
		// needs.
		//
		// Makes sure the arrays exist and hold cubes of `faceSize`.
		//
		// Changing the face size reallocates, which empties every slot. That is
		// the whole cost of letting probes carry their own resolution: the
		// array's faces are as large as the largest probe in the scene, and a
		// smaller probe is resampled up on the way into its slice rather than
		// getting an array of its own. One array is what makes the per-object
		// index sufficient -- with two, an object would have to select the
		// binding as well as the slice, which is the thing this design exists
		// to avoid.
		static void Begin(uint32_t faceSize, uint32_t slots);

		// Fills slot 0 from the scene's sky. A no-op when the same cube is
		// already there, which is every frame after the first.
		static void SetSky(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& sky);

		// Fills `slot` from a probe's capture, if it is not already holding
		// that probe at that generation. Returns false when the slot could not
		// be filled, and the caller should point its objects at the sky.
		static bool SetProbe(RHI::RHICommandList& cmd, uint32_t slot,
							 const ReflectionProbe& probe);

		// The two arrays, for binding. Null before Init or after Shutdown.
		static RHI::Ref<RHI::RHITexture> GetRadiance();
		static RHI::Ref<RHI::RHITexture> GetIrradiance();

		// The face size the arrays currently hold, or 0 when there are none.
		static uint32_t GetFaceSize();
	};
}
