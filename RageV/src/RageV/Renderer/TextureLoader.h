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
	// What this loader learned about an image's *content*, for the traced
	// bounce's emitter list.
	struct TextureStats
	{
		// The average, in linear space. Makes an emitter's total power
		// right whatever its map looks like.
		Vec3 Mean{ 1.0f, 1.0f, 1.0f };

		// And where in the image that power is. `Grid` cells a side, each
		// holding the running sum of the cells before it -- a cumulative
		// distribution over cell luminance, so a sampler can pick a cell
		// in proportion to how much light it emits and land on the four
		// lit ones of a hundred and forty-four rather than uniformly over
		// a mostly dark ceiling. Zero when the image is uniform enough
		// that aiming would buy nothing, or entirely black.
		uint32_t Grid = 0;
		std::vector<float> Cdf;
	};

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

		// **Three single-channel maps into one RGB texture**, red, green and
		// blue, built in memory and cached like any other.
		//
		// This exists because the layered terrain variant has three samplers
		// per layer and cannot grow a fourth: set 0 already spends 16 of the
		// 32 texture units OpenGL guarantees a fragment stage, and four more
		// arrays of four would be 33. Roughness, ambient occlusion and height
		// are one channel each, so all three fit in one texture at one
		// sampler and nothing has to be given up.
		//
		// **The material still declares them separately**, which is the point.
		// Packing is a fact about how this renderer binds things, not about
		// how a surface is authored, so it happens here rather than being
		// pushed onto whoever writes the `.rmat`. An empty path takes the
		// neutral given for its channel, so a set that ships fewer maps packs
		// as though it shipped them all.
		//
		// In memory rather than on disk, unlike the glTF metallic-roughness
		// split, and the difference is real: that one is written out because
		// a *material stores handles* and an in-memory texture has none. This
		// is derived state that nothing references by handle -- it is rebuilt
		// from the three sources whenever the material is loaded.
		//
		// Always linear. Every channel here is data, never a picture.
		static RHI::Ref<RHI::RHITexture> PackChannels(RHI::RHIDevice& device,
													  const std::string& red,
													  const std::string& green,
													  const std::string& blue,
													  uint8_t redNeutral,
													  uint8_t greenNeutral,
													  uint8_t blueNeutral,
													  const std::string& debugName);

		// **A larger, non-repeating tile synthesised from `path`.**
		//
		// Heitz & Neyret's histogram-preserving synthesis, run once into an
		// output `scale` times bigger rather than three fetches a pixel
		// forever. See Assets::SynthesiseTiling for the method and for why the
		// lattice is square rather than hexagonal.
		//
		// **Cached on disk**, keyed by the source's bytes together with every
		// number that shapes the result -- so it runs once per material and a
		// changed source or a changed scale invalidates it by itself. 4K from
		// 1K takes seconds; doing it on every load would be unusable, which is
		// what makes the cache part of the feature rather than an optimisation.
		//
		// `regenerate` forces it again, for the case a stored answer cannot
		// detect: the same shape IrradianceVolumeComponent::Recapture has.
		static RHI::Ref<RHI::RHITexture> SynthesisedTiling(RHI::RHIDevice& device,
														   const std::string& path,
														   int scale, int cells,
														   uint32_t seed, bool srgb,
														   bool regenerate);

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

		// A one-texel volume of zeroes, for the irradiance bindings on a frame
		// with no field in the scene. A declared binding left unwritten is a
		// validation error rather than a harmless omission, and a *2D* stand-in
		// in a sampler3D slot is a different descriptor type -- which is also
		// a validation error, and the one that is easy to reach for.
		static RHI::Ref<RHI::RHITexture> BlackVolume(RHI::RHIDevice& device);
		// Zero in every channel, alpha included -- Black is opaque. Bound
		// where a shader reads alpha as a weight and "nothing" has to weigh
		// nothing: last frame's reflection trace before there is one.
		static RHI::Ref<RHI::RHITexture> TransparentBlack(RHI::RHIDevice& device);
		// (0.5, 0.5, 1.0) -- a normal pointing straight out of the surface.
		static RHI::Ref<RHI::RHITexture> FlatNormal(RHI::RHIDevice& device);
		// The colour of a wrong index: slot 0 of the bindless heap and every
		// slot nobody has written (ENGINE-NOTES 7al). Deliberately the one
		// colour no material in a real scene is, so it reads as a bug and not
		// as a look.
		static RHI::Ref<RHI::RHITexture> Magenta(RHI::RHIDevice& device);
		// (1, 0, 0, 0): a layer-weight texel that says "all of layer 0". The
		// weight map of a terrain nobody has painted (ENGINE-NOTES 7aq).
		static RHI::Ref<RHI::RHITexture> Red(RHI::RHIDevice& device);

		// This texture's stats, or null when the loader has none for it.
		static std::shared_ptr<const TextureStats> Stats(const RHI::Ref<RHI::RHITexture>& texture);

		// The average colour of a texture this loader read, in **linear**
		// space, or white when it read none under that name.
		//
		// It exists for the traced bounce's emitter list. That list stands a
		// rectangle in for an emissive mesh and takes the material's scalar
		// as the whole surface's radiance -- which is right for a panel that
		// glows evenly and wrong by the ratio of lit to unlit area for one
		// whose map is mostly dark. A luminaire lighting four cells of a
		// hundred and forty-four lit the room thirty-six times too brightly
		// (see docs/TEXEL-EMITTERS.md); folding this in makes the emitted
		// *power* right for any map, however it is painted.
		//
		// White for the unknown, deliberately: a material with no emissive
		// map, or a texture that arrived by some other route, then behaves
		// exactly as it did before this existed.
		static Vec3 MeanColor(const RHI::Ref<RHI::RHITexture>& texture);

		static void ClearCache();
	};
}
