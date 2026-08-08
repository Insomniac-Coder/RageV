#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "glm/glm.hpp"

namespace RageV
{
	// The two precomputed halves of the split-sum approximation.
	//
	// Split-sum says the specular response of a surface to an environment
	// factors into two integrals that can each be computed ahead of time: one
	// over the environment, which depends on roughness and direction, and one
	// over the BRDF, which depends on roughness and view angle and on nothing
	// else at all. The first is a cube per roughness level; the second is a
	// single small table shared by every material in every scene.
	class EnvironmentIBL
	{
	public:
		// Roughness levels in the prefiltered cube, mip 0 being a mirror. Five
		// steps is what everyone uses: the lobe widens fast enough that the
		// difference between five and ten is not visible on anything.
		static constexpr uint32_t kRoughnessLevels = 6;

		// Samples per texel of the GGX convolution. Importance sampled, so this
		// buys accuracy far faster than a uniform estimator would.
		static constexpr uint32_t kPrefilterSamples = 128;

		// How many roughness levels a source of this size can actually carry:
		// one per mip it has, capped at kRoughnessLevels, and 0 when it is too
		// small to be worth filtering at all.
		//
		// Exists as a function because the guard it replaces was wrong by one
		// power of two, and rejected the gradient sky -- the default, and so
		// the common case -- from being prefiltered at all.
		static uint32_t LevelsFor(uint32_t faceSize);

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Frees the per-frame descriptor pool. Called by Renderer::BeginFrame.
		static void BeginFrame();
		static bool IsReady();

		// The environment BRDF table, computed once at Init.
		static RHI::Ref<RHI::RHITexture> GetBRDF();
		static RHI::Ref<RHI::RHISampler> GetBRDFSampler();

		// Convolves `source` into a cube whose mips are roughness levels, and
		// remembers the result. Cheap after the first call for a given source.
		//
		// Must be called outside a render pass -- it opens one per face per
		// level. Returns the prefiltered cube, or `source` unchanged if it
		// cannot be built, which degrades to the box-filtered chain rather than
		// to nothing.
		static RHI::Ref<RHI::RHITexture> Prefilter(RHI::RHICommandList& cmd,
												   const RHI::Ref<RHI::RHITexture>& source);

		// What Prefilter last produced for this source, without building it.
		// Returns `source` when there is nothing yet.
		static RHI::Ref<RHI::RHITexture> GetPrefiltered(const RHI::Ref<RHI::RHITexture>& source);

		// Says the source has changed and its filter is stale. The next
		// Prefilter refills the cube it already made rather than allocating
		// another -- which is what makes this affordable for a reflection probe
		// that re-captures itself.
		static void Invalidate(const RHI::Ref<RHI::RHITexture>& source);

		// Frees the cached cubes. Called when the project changes, since the
		// sources they were built from are gone.
		static void ClearCache();
	};
}
