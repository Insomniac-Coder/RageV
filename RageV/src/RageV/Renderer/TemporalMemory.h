#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace RageV
{
	// **RT2-2a: how long the frame remembers, so a capture can wait for it.**
	//
	// Every pass that blends this frame into a history notes the most frames it
	// keeps (Note); Longest() is the most any pass kept, this frame or the last.
	// A camera held still for that many frames, with the scene's bakes, probes
	// and solves done, has every history in force full: `--screenshot-frame=
	// settled` captures there (Application.cpp) instead of at a frame number
	// that was right for whichever memory was longest when it was chosen.
	//
	// The limits that live in shaders are defined here and handed to every
	// shader as defines (ShaderDefines, Renderer::Init), so a shader and this
	// table cannot disagree.
	//
	// Out of it on purpose: auto exposure. Its adaptation is a time constant,
	// not a count of frames -- under a pinned clock it barely moves -- and a
	// chain's first frame adopts its measurement outright.
	class TemporalMemory
	{
	public:
		static constexpr uint32_t kTaa = 64;          // taa_resolve: RV_MEMORY_TAA
		static constexpr uint32_t kSettled = 32;      // reflection_accumulate: RV_MEMORY_SETTLED
		static constexpr uint32_t kGiDenoise = 16;    // gi_denoise: RV_MEMORY_GI

		static std::vector<std::string> ShaderDefines();

		// Called by a pass as it is recorded; fractional frames round up.
		static void Note(float frames);
		// Renderer::BeginFrame.
		static void BeginFrame();
		static uint32_t Longest();

		// The layer's word, once a frame: whether the camera stood where it
		// stood last frame and the scene's lighting is ready. Settled() is true
		// once both have held for longer than Longest().
		static void Observe(bool cameraStill, bool lightingReady);
		static bool Settled();
		static uint32_t StillFrames();
	};
}
