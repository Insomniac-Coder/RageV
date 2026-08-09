#include <rvpch.h>
#include "Cubemap.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	namespace
	{
		// Bilinear fetch. Longitude wraps, because the panorama's left and right
		// edges are the same meridian; latitude clamps, because there is nothing
		// above the north pole.
		Vec4 SampleEquirect(const float* pixels, uint32_t width, uint32_t height,
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

			Vec4 result(0.0f);
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
					result += weights[j][i] * Vec4(pixels[index + 0], pixels[index + 1],
														pixels[index + 2], pixels[index + 3]);
				}
			}

			return result;
		}
	}

	Vec3 CubeFaces::Sample(uint32_t face, uint32_t x, uint32_t y) const
	{
		if (!Valid() || face >= kFaceCount || x >= Size || y >= Size)
			return Vec3(0.0f);

		const float* data = Face(face) + ((size_t)y * Size + x) * 4;
		return { data[0], data[1], data[2] };
	}

	Vec3 CubeFaceDirection(uint32_t face, float u, float v)
	{
		// Face-local coordinates in [-1, 1]. s runs left to right across the
		// face image and t runs top to bottom, which is why t is negated below
		// wherever the axis it maps to points up.
		const float s = 2.0f * u - 1.0f;
		const float t = 2.0f * v - 1.0f;

		switch (face)
		{
			case 0: return Math::Normalize(Vec3( 1.0f, -t, -s));   // +X
			case 1: return Math::Normalize(Vec3(-1.0f, -t,  s));   // -X
			case 2: return Math::Normalize(Vec3( s,  1.0f,  t));   // +Y
			case 3: return Math::Normalize(Vec3( s, -1.0f, -t));   // -Y
			case 4: return Math::Normalize(Vec3( s, -t,  1.0f));   // +Z
			case 5: return Math::Normalize(Vec3(-s, -t, -1.0f));   // -Z
		}

		return Vec3(0.0f, 0.0f, 1.0f);
	}

	namespace
	{
		// Which face a direction lands on, and where. The inverse of
		// CubeFaceDirection, and it has to stay its inverse -- the test suite
		// checks the round trip, because the two disagreeing would show up as
		// an irradiance cube that is subtly rotated relative to the sky it came
		// from.
		Vec3 SampleCube(const CubeFaces& cube, const Vec3& direction)
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
				return Vec3(0.0f);

			const float u = (sc / ma) * 0.5f + 0.5f;
			const float v = (tc / ma) * 0.5f + 0.5f;

			const uint32_t x = (uint32_t)Math::Clamp((int)(u * (float)cube.Size), 0,
													(int)cube.Size - 1);
			const uint32_t y = (uint32_t)Math::Clamp((int)(v * (float)cube.Size), 0,
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

		samplesPerAxis = Math::Clamp(samplesPerAxis, 4u, 256u);

		result.Size = faceSize;
		result.Pixels.resize((size_t)CubeFaces::kFaceCount * faceSize * faceSize * 4);

		const float inverse = 1.0f / (float)faceSize;
		const float deltaPhi = Math::TwoPi / (float)samplesPerAxis;
		const float deltaTheta = Math::HalfPi / (float)samplesPerAxis;

		for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
		{
			float* output = result.Face(face);

			for (uint32_t y = 0; y < faceSize; y++)
			{
				for (uint32_t x = 0; x < faceSize; x++)
				{
					const Vec3 normal =
						CubeFaceDirection(face, ((float)x + 0.5f) * inverse,
												((float)y + 0.5f) * inverse);

					// A frame around the normal. Any tangent will do -- the
					// integral is rotationally symmetric about the normal --
					// as long as it is not parallel to it.
					Vec3 up = std::fabs(normal.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f)
															   : Vec3(0.0f, 1.0f, 0.0f);
					const Vec3 right = Math::Normalize(Math::Cross(up, normal));
					up = Math::Cross(normal, right);

					Vec3 sum(0.0f);
					float weight = 0.0f;

					// Riemann sum over the hemisphere in spherical coordinates.
					// sin(theta) is the area of the ring being sampled and
					// cos(theta) is Lambert's law; the two together are what
					// make this irradiance rather than an average.
					for (float phi = 0.0f; phi < Math::TwoPi; phi += deltaPhi)
					{
						for (float theta = 0.0f; theta < Math::HalfPi; theta += deltaTheta)
						{
							const float sinTheta = std::sin(theta);
							const float cosTheta = std::cos(theta);

							const Vec3 tangentSample(sinTheta * std::cos(phi),
														  sinTheta * std::sin(phi),
														  cosTheta);

							const Vec3 direction = right * tangentSample.x +
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
					const Vec3 irradiance = weight > 0.0f ? sum / weight : Vec3(0.0f);

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

	namespace
	{
		// Van der Corput radical inverse: the second dimension of a Hammersley
		// sequence. A low-discrepancy pair covers the hemisphere far more
		// evenly than random numbers do, which is what lets 512 samples stand
		// in for an integral.
		float RadicalInverse(uint32_t bits)
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return (float)bits * 2.3283064365386963e-10f;
		}

		// A half vector drawn from the GGX distribution for this roughness, in
		// tangent space. Importance sampling: the samples are placed where the
		// lobe actually is, so the weighting cancels out of the estimator.
		Vec3 ImportanceSampleGGX(const Vec2& random, float roughness)
		{
			const float a = roughness * roughness;

			const float phi = Math::TwoPi * random.x;
			const float cosTheta = std::sqrt((1.0f - random.y) /
											 (1.0f + (a * a - 1.0f) * random.y));
			const float sinTheta = std::sqrt(Math::Max(1.0f - cosTheta * cosTheta, 0.0f));

			return { std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta };
		}

		// Smith's geometry term with the IBL remapping of k. Direct lighting
		// uses (r+1)^2/8 and image-based lighting uses r^2/2; using the direct
		// one here darkens every rough surface.
		float GeometrySmithIBL(float NdotV, float NdotL, float roughness)
		{
			const float k = (roughness * roughness) / 2.0f;
			const float ggxV = NdotV / (NdotV * (1.0f - k) + k);
			const float ggxL = NdotL / (NdotL * (1.0f - k) + k);
			return ggxV * ggxL;
		}
	}

	std::vector<float> IntegrateEnvironmentBRDF(uint32_t size, uint32_t samples)
	{
		size = Math::Clamp(size, 8u, 1024u);
		samples = Math::Clamp(samples, 16u, 4096u);

		std::vector<float> table((size_t)size * size * 2, 0.0f);

		for (uint32_t y = 0; y < size; y++)
		{
			// Row is roughness, column is how directly the surface faces the
			// viewer. Texel centres, so the table's ends are not half a texel
			// short of 0 and 1.
			const float roughness = ((float)y + 0.5f) / (float)size;

			for (uint32_t x = 0; x < size; x++)
			{
				const float NdotV = Math::Max(((float)x + 0.5f) / (float)size, 1e-3f);

				// The view direction in tangent space. Only its angle to the
				// normal matters, so it can lie in the xz plane.
				const Vec3 view(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

				float scale = 0.0f;
				float bias = 0.0f;

				for (uint32_t i = 0; i < samples; i++)
				{
					const Vec2 random((float)i / (float)samples, RadicalInverse(i));
					const Vec3 half = ImportanceSampleGGX(random, roughness);
					const Vec3 light = Math::Normalize(2.0f * Math::Dot(view, half) * half - view);

					const float NdotL = light.z;
					if (NdotL <= 0.0f)
						continue;

					const float NdotH = Math::Max(half.z, 0.0f);
					const float VdotH = Math::Max(Math::Dot(view, half), 0.0f);

					const float geometry = GeometrySmithIBL(NdotV, NdotL, roughness);
					const float visibility = geometry * VdotH / Math::Max(NdotH * NdotV, 1e-6f);

					// Fresnel split into the two terms F0 multiplies and adds.
					// That split is the whole point: it takes F0 out of the
					// integral, so one table serves every material.
					const float fresnel = std::pow(1.0f - VdotH, 5.0f);

					scale += (1.0f - fresnel) * visibility;
					bias += fresnel * visibility;
				}

				float* texel = table.data() + ((size_t)y * size + x) * 2;
				texel[0] = scale / (float)samples;
				texel[1] = bias / (float)samples;
			}
		}

		return table;
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
					const Vec3 direction =
						CubeFaceDirection(face, ((float)x + 0.5f) * inverse,
												((float)y + 0.5f) * inverse);

					// Measured from -Z, increasing towards +X, which puts the
					// panorama's centre column straight ahead of a camera at
					// its default orientation and the seam -- the one place a
					// bilinear filter cannot help -- directly behind it.
					// Latitude runs from the top.
					const float longitude = std::atan2(direction.x, -direction.z);
					const float latitude = std::acos(std::clamp(direction.y, -1.0f, 1.0f));

					const float u = longitude / Math::TwoPi + 0.5f;
					const float v = latitude / Math::Pi;

					const Vec4 colour = SampleEquirect(pixels, width, height, u, v);

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
