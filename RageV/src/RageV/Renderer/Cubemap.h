#pragma once
#include "glm/glm.hpp"
#include <cstdint>
#include <vector>

namespace RageV
{
	// The six faces of a cube map as linear RGBA floats, in the +X -X +Y -Y
	// +Z -Z order both APIs index array layers by.
	//
	// The conversion below runs on the CPU on purpose. Rendering into cube
	// faces needs layered rendering, six view matrices and a framebuffer whose
	// Y convention differs between the two backends -- three things that can
	// only be checked by looking at a picture. A function from pixels to pixels
	// can be checked by the test suite, on a machine with no GPU at all, and
	// the answer is the same on both backends because it never reaches one.
	//
	// The cost is load time, which is paid once per environment map and is
	// roughly 30 ms for a 512-pixel face from a 2k panorama.
	struct CubeFaces
	{
		static constexpr uint32_t kFaceCount = 6;

		uint32_t Size = 0;
		std::vector<float> Pixels;    // kFaceCount * Size * Size * 4

		bool Valid() const
		{
			return Size > 0 && Pixels.size() == (size_t)kFaceCount * Size * Size * 4;
		}

		uint64_t FaceFloats() const { return (uint64_t)Size * Size * 4; }
		uint64_t FaceBytes()  const { return FaceFloats() * sizeof(float); }

		const float* Face(uint32_t face) const { return Pixels.data() + face * FaceFloats(); }
		float* Face(uint32_t face) { return Pixels.data() + face * FaceFloats(); }

		// Convenience for tests and for the neutral fallback: one texel.
		glm::vec3 Sample(uint32_t face, uint32_t x, uint32_t y) const;
	};

	// The direction a face texel looks along. u and v run across the face image
	// in [0,1], v downwards from its top row.
	//
	// This is the face table both the OpenGL and Vulkan specifications give,
	// and they give the same one -- cube map addressing was inherited from
	// RenderMan by way of Direct3D and never diverged. That is why a single
	// conversion feeds both backends, and why the framebuffer-origin difference
	// that complicates everything else here does not apply.
	glm::vec3 CubeFaceDirection(uint32_t face, float u, float v);

	// Equirectangular (latitude-longitude) panorama to cube faces. Bilinear,
	// wrapping in longitude and clamping in latitude, so the seam behind the
	// camera and the poles both stay continuous.
	//
	// `pixels` is width * height * 4 floats, row 0 at the top, which is what
	// every loader hands back and what the format's +Y-up convention means.
	CubeFaces EquirectangularToCube(const float* pixels, uint32_t width, uint32_t height,
									uint32_t faceSize);

	// Diffuse irradiance: for every direction, the cosine-weighted average of
	// everything the hemisphere around it can see.
	//
	// This is the half of image-based lighting that replaces a flat ambient
	// colour. The specular half asks "what does this surface reflect"; this one
	// asks "how much light arrives here, and from where" -- and the answer
	// being different on the sky side of an object from the ground side is
	// most of what makes a scene look lit by its surroundings rather than by a
	// constant.
	//
	// The result is deliberately tiny. Irradiance is the integral of a whole
	// hemisphere, so it has no detail worth resolving: 16 texels a side is
	// indistinguishable from 64 and costs a sixteenth as much to compute.
	//
	// Convolved on the CPU, for the same reasons the panorama conversion is --
	// it can be checked by the test suite, and it gives the same answer on both
	// backends because it never reaches one. The cost is roughly 200 ms for a
	// 512-pixel source, once per environment map.
	CubeFaces IrradianceFromCube(const CubeFaces& source, uint32_t faceSize,
								 uint32_t samplesPerAxis = 32);

	// The environment BRDF: the second half of the split-sum approximation.
	//
	// Given how directly a surface faces the viewer and how rough it is, this
	// returns the scale and bias to apply to a material's F0 -- so the whole
	// specular response becomes one cube lookup times two numbers from here.
	// It depends on nothing else: not the environment, not the material's
	// colour, not the scene. Which is exactly why it can be a table.
	//
	// Two channels, red then green, `size` by `size`. R16G16 is enough -- it is
	// a smooth function of two variables with no detail anywhere in it, which
	// is also why 128 is a generous size.
	//
	// This is what replaces Lazarov's analytic fit. The fit is within a couple
	// of percent across most of the range and visibly off at grazing angles on
	// smooth metal, which is the one case people look at.
	std::vector<float> IntegrateEnvironmentBRDF(uint32_t size, uint32_t samples = 512);
}
