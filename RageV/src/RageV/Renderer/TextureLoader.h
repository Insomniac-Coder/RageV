#pragma once

// Image files to RHI textures, plus the 1x1 fallbacks a material binds when a
// map is absent. Every sampler in a descriptor set must be written even when a
// draw will not read it, so "no texture" has to mean "a texture that reads as
// the neutral value" rather than nothing.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Cubemap.h"
#include <string>

namespace RageV
{
	class TextureLoader
	{
	public:
		// How big each face of a loaded environment map is. 512 is a compromise:
		// a sky is low-frequency enough that the difference from 1024 is hard to
		// see, and this is six faces of RGBA16F, so the step to 1024 is 50 MB
		// rather than 12.
		static constexpr uint32_t kDefaultFaceSize = 512;

		// sRGB for colour maps (albedo, emissive); linear for data maps
		// (normal, metallic-roughness, occlusion). Getting this wrong is the
		// most common cause of PBR that looks subtly washed out or too dark.
		static RHI::Ref<RHI::RHITexture> Load2D(RHI::RHIDevice& device,
												const std::string& path,
												bool srgb = true,
												bool generateMips = true);

		// An environment map, always linear and always float: a sky is the one
		// thing in a scene that is genuinely brighter than white, and clipping
		// it to 1.0 removes exactly the values bloom and (later) IBL exist to
		// use.
		//
		// The path may be an equirectangular panorama -- .hdr, or any LDR format
		// which is then un-gamma'd on the way in -- or one face of a six-file
		// set, recognised by a _px/_nx/_py/_ny/_pz/_nz suffix, in which case its
		// five siblings are loaded with it.
		static RHI::Ref<RHI::RHITexture> LoadCube(RHI::RHIDevice& device,
												  const std::string& path,
												  uint32_t faceSize = kDefaultFaceSize);

		// The diffuse irradiance of the same environment map, convolved while
		// its faces are still in memory and cached in their place.
		//
		// Computed during LoadCube rather than on demand, because the input is
		// 25 MB of float faces and the output is 6 KB: keeping the answer and
		// throwing away the question is the whole trade.
		static RHI::Ref<RHI::RHITexture> LoadIrradiance(RHI::RHIDevice& device,
														const std::string& path,
														uint32_t faceSize = kDefaultFaceSize);

		// Uploads faces that are already in memory. Separate from LoadCube so
		// the conversion can be tested without a file and a cube can be built
		// from something other than an image.
		static RHI::Ref<RHI::RHITexture> CreateCube(RHI::RHIDevice& device,
													const CubeFaces& faces,
													const std::string& debugName);

		// 1x1 black on every face. Bound when a shader declares a cube sampler
		// it will not read -- the binding still has to be filled.
		static RHI::Ref<RHI::RHITexture> BlackCube(RHI::RHIDevice& device);

		// The same, for a samplerCubeArray binding. A cube and a cube array are
		// different descriptor types, so the plain black cube cannot stand in
		// for one -- filling a cube-array binding with it is a validation error
		// on one backend and a wrong sample on the other.
		static RHI::Ref<RHI::RHITexture> BlackCubeArray(RHI::RHIDevice& device);

		// Cached 1x1 defaults, created on first use.
		static RHI::Ref<RHI::RHITexture> White(RHI::RHIDevice& device);
		static RHI::Ref<RHI::RHITexture> Black(RHI::RHIDevice& device);
		// (0.5, 0.5, 1.0) -- a normal pointing straight out of the surface.
		static RHI::Ref<RHI::RHITexture> FlatNormal(RHI::RHIDevice& device);

		static void ClearCache();
	};
}
