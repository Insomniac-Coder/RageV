#include <rvpch.h>
#include "Cubemap.h"
#include <glm/gtc/constants.hpp>

namespace RageV
{
	namespace
	{
		// Bilinear fetch. Longitude wraps, because the panorama's left and right
		// edges are the same meridian; latitude clamps, because there is nothing
		// above the north pole.
		glm::vec4 SampleEquirect(const float* pixels, uint32_t width, uint32_t height,
								 float u, float v)
		{
			// Texel centres: the -0.5 is what stops the whole image drifting half
			// a texel, which shows up as a visible seam down the back of the sky.
			const float x = u * (float)width - 0.5f;
			const float y = v * (float)height - 0.5f;

			const int x0 = (int)std::floor(x);
			const int y0 = (int)std::floor(y);
			const float fx = x - (float)x0;
			const float fy = y - (float)y0;

			auto wrapX = [width](int value)
			{
				const int w = (int)width;
				value %= w;
				return value < 0 ? value + w : value;
			};
			auto clampY = [height](int value)
			{
				return std::clamp(value, 0, (int)height - 1);
			};

			const int xs[2] = { wrapX(x0), wrapX(x0 + 1) };
			const int ys[2] = { clampY(y0), clampY(y0 + 1) };

			glm::vec4 result(0.0f);
			const float weights[2][2] =
			{
				{ (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy) },
				{ (1.0f - fx) * fy,          fx * fy          },
			};

			for (int j = 0; j < 2; j++)
			{
				for (int i = 0; i < 2; i++)
				{
					const size_t index = ((size_t)ys[j] * width + xs[i]) * 4;
					result += weights[j][i] * glm::vec4(pixels[index + 0], pixels[index + 1],
														pixels[index + 2], pixels[index + 3]);
				}
			}

			return result;
		}
	}

	glm::vec3 CubeFaces::Sample(uint32_t face, uint32_t x, uint32_t y) const
	{
		if (!Valid() || face >= kFaceCount || x >= Size || y >= Size)
			return glm::vec3(0.0f);

		const float* data = Face(face) + ((size_t)y * Size + x) * 4;
		return { data[0], data[1], data[2] };
	}

	glm::vec3 CubeFaceDirection(uint32_t face, float u, float v)
	{
		// Face-local coordinates in [-1, 1]. s runs left to right across the
		// face image and t runs top to bottom, which is why t is negated below
		// wherever the axis it maps to points up.
		const float s = 2.0f * u - 1.0f;
		const float t = 2.0f * v - 1.0f;

		switch (face)
		{
			case 0: return glm::normalize(glm::vec3( 1.0f, -t, -s));   // +X
			case 1: return glm::normalize(glm::vec3(-1.0f, -t,  s));   // -X
			case 2: return glm::normalize(glm::vec3( s,  1.0f,  t));   // +Y
			case 3: return glm::normalize(glm::vec3( s, -1.0f, -t));   // -Y
			case 4: return glm::normalize(glm::vec3( s, -t,  1.0f));   // +Z
			case 5: return glm::normalize(glm::vec3(-s, -t, -1.0f));   // -Z
		}

		return glm::vec3(0.0f, 0.0f, 1.0f);
	}

	CubeFaces EquirectangularToCube(const float* pixels, uint32_t width, uint32_t height,
									uint32_t faceSize)
	{
		CubeFaces faces;
		if (!pixels || width == 0 || height == 0 || faceSize == 0)
			return faces;

		faces.Size = faceSize;
		faces.Pixels.resize((size_t)CubeFaces::kFaceCount * faceSize * faceSize * 4);

		const float inverse = 1.0f / (float)faceSize;

		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			float* output = faces.Face(face);

			for (uint32_t y = 0; y < faceSize; y++)
			{
				for (uint32_t x = 0; x < faceSize; x++)
				{
					// Texel centres again, so the six faces meet along their
					// shared edges instead of overlapping by half a texel.
					const glm::vec3 direction =
						CubeFaceDirection(face, ((float)x + 0.5f) * inverse,
												((float)y + 0.5f) * inverse);

					// Measured from -Z, increasing towards +X, which puts the
					// panorama's centre column straight ahead of a camera at
					// its default orientation and the seam -- the one place a
					// bilinear filter cannot help -- directly behind it.
					// Latitude runs from the top.
					const float longitude = std::atan2(direction.x, -direction.z);
					const float latitude = std::acos(std::clamp(direction.y, -1.0f, 1.0f));

					const float u = longitude / glm::two_pi<float>() + 0.5f;
					const float v = latitude / glm::pi<float>();

					const glm::vec4 colour = SampleEquirect(pixels, width, height, u, v);

					float* texel = output + ((size_t)y * faceSize + x) * 4;
					texel[0] = colour.r;
					texel[1] = colour.g;
					texel[2] = colour.b;
					texel[3] = colour.a;
				}
			}
		}

		return faces;
	}
}
