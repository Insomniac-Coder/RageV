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

	namespace
	{
		// Which face a direction lands on, and where. The inverse of
		// CubeFaceDirection, and it has to stay its inverse -- the test suite
		// checks the round trip, because the two disagreeing would show up as
		// an irradiance cube that is subtly rotated relative to the sky it came
		// from.
		glm::vec3 SampleCube(const CubeFaces& cube, const glm::vec3& direction)
		{
			const float ax = std::fabs(direction.x);
			const float ay = std::fabs(direction.y);
			const float az = std::fabs(direction.z);

			uint32_t face;
			float sc, tc, ma;

			if (ax >= ay && ax >= az)
			{
				ma = ax;
				if (direction.x > 0.0f) { face = 0; sc = -direction.z; tc = -direction.y; }
				else                    { face = 1; sc =  direction.z; tc = -direction.y; }
			}
			else if (ay >= az)
			{
				ma = ay;
				if (direction.y > 0.0f) { face = 2; sc = direction.x; tc =  direction.z; }
				else                    { face = 3; sc = direction.x; tc = -direction.z; }
			}
			else
			{
				ma = az;
				if (direction.z > 0.0f) { face = 4; sc =  direction.x; tc = -direction.y; }
				else                    { face = 5; sc = -direction.x; tc = -direction.y; }
			}

			if (ma < 1e-9f)
				return glm::vec3(0.0f);

			const float u = (sc / ma) * 0.5f + 0.5f;
			const float v = (tc / ma) * 0.5f + 0.5f;

			const uint32_t x = (uint32_t)glm::clamp((int)(u * (float)cube.Size), 0,
													(int)cube.Size - 1);
			const uint32_t y = (uint32_t)glm::clamp((int)(v * (float)cube.Size), 0,
													(int)cube.Size - 1);

			return cube.Sample(face, x, y);
		}
	}

	CubeFaces IrradianceFromCube(const CubeFaces& source, uint32_t faceSize,
								 uint32_t samplesPerAxis)
	{
		CubeFaces result;
		if (!source.Valid() || faceSize == 0)
			return result;

		samplesPerAxis = glm::clamp(samplesPerAxis, 4u, 256u);

		result.Size = faceSize;
		result.Pixels.resize((size_t)CubeFaces::kFaceCount * faceSize * faceSize * 4);

		const float inverse = 1.0f / (float)faceSize;
		const float deltaPhi = glm::two_pi<float>() / (float)samplesPerAxis;
		const float deltaTheta = glm::half_pi<float>() / (float)samplesPerAxis;

		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			float* output = result.Face(face);

			for (uint32_t y = 0; y < faceSize; y++)
			{
				for (uint32_t x = 0; x < faceSize; x++)
				{
					const glm::vec3 normal =
						CubeFaceDirection(face, ((float)x + 0.5f) * inverse,
												((float)y + 0.5f) * inverse);

					// A frame around the normal. Any tangent will do -- the
					// integral is rotationally symmetric about the normal --
					// as long as it is not parallel to it.
					glm::vec3 up = std::fabs(normal.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
															   : glm::vec3(0.0f, 1.0f, 0.0f);
					const glm::vec3 right = glm::normalize(glm::cross(up, normal));
					up = glm::cross(normal, right);

					glm::vec3 sum(0.0f);
					float weight = 0.0f;

					// Riemann sum over the hemisphere in spherical coordinates.
					// sin(theta) is the area of the ring being sampled and
					// cos(theta) is Lambert's law; the two together are what
					// make this irradiance rather than an average.
					for (float phi = 0.0f; phi < glm::two_pi<float>(); phi += deltaPhi)
					{
						for (float theta = 0.0f; theta < glm::half_pi<float>(); theta += deltaTheta)
						{
							const float sinTheta = std::sin(theta);
							const float cosTheta = std::cos(theta);

							const glm::vec3 tangentSample(sinTheta * std::cos(phi),
														  sinTheta * std::sin(phi),
														  cosTheta);

							const glm::vec3 direction = right * tangentSample.x +
														up * tangentSample.y +
														normal * tangentSample.z;

							sum += SampleCube(source, direction) * cosTheta * sinTheta;
							weight += cosTheta * sinTheta;
						}
					}

					// Normalised by the weights rather than by pi times the
					// sample count: the loops step in fixed increments and do
					// not land exactly on the hemisphere's edge, so the exact
					// count is not what was actually summed.
					const glm::vec3 irradiance = weight > 0.0f ? sum / weight : glm::vec3(0.0f);

					float* texel = output + ((size_t)y * faceSize + x) * 4;
					texel[0] = irradiance.r;
					texel[1] = irradiance.g;
					texel[2] = irradiance.b;
					texel[3] = 1.0f;
				}
			}
		}

		return result;
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
