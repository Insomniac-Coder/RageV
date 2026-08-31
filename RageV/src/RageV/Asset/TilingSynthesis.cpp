#include <rvpch.h>
#include "TilingSynthesis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace RageV::Assets
{
	namespace
	{
		// Abramowitz & Stegun 7.1.26. The inverse below refines against it, and
		// the forward direction only ever feeds a 512-entry lookup, so this is
		// far more accuracy than either use needs.
		double Erf(double x)
		{
			const double sign = x < 0.0 ? -1.0 : 1.0;
			x = std::abs(x);
			const double t = 1.0 / (1.0 + 0.3275911 * x);
			const double y = 1.0 - (((((1.061405429 * t - 1.453152027) * t)
									  + 1.421413741) * t - 0.284496736) * t
								   + 0.254829592) * t * std::exp(-x * x);
			return sign * y;
		}

		// A rational first guess, then two Newton steps against Erf.
		double ErfInverse(double y)
		{
			y = std::clamp(y, -0.999999, 0.999999);
			constexpr double a = 0.147;
			const double ln = std::log(1.0 - y * y);
			const double term = 2.0 / (3.14159265358979323846 * a) + ln * 0.5;
			double x = (y < 0.0 ? -1.0 : 1.0)
					 * std::sqrt(std::sqrt(term * term - ln / a) - term);
			for (int i = 0; i < 2; i++)
			{
				const double error = Erf(x) - y;
				x -= error / (2.0 / std::sqrt(3.14159265358979323846) * std::exp(-x * x));
			}
			return x;
		}

		// The random offset for one lattice vertex, wrapped so the field is
		// periodic over the output. See the header on why the wrap is the whole
		// reason this tiles.
		void CellOffset(int64_t vx, int64_t vy, int cells, uint32_t seed,
						double& outX, double& outY)
		{
			auto wrap = [cells](int64_t v)
			{
				const int64_t m = v % cells;
				return (uint64_t)(m < 0 ? m + cells : m);
			};

			uint64_t h = (wrap(vx) * 73856093ull) ^ (wrap(vy) * 19349663ull)
					   ^ ((uint64_t)seed * 83492791ull);
			h ^= h >> 13;
			h *= 1274126177ull;
			h ^= h >> 16;
			outX = (double)(h & 0xFFFFull) / 65536.0;
			outY = (double)((h >> 16) & 0xFFFFull) / 65536.0;
		}
	}

	std::vector<uint8_t> SynthesiseTiling(const SynthesisRequest& request)
	{
		if (!request.Pixels || request.Width <= 0 || request.Height <= 0
			|| request.Channels <= 0 || request.Scale <= 0 || request.Cells <= 0)
			return {};

		const int width = request.Width;
		const int height = request.Height;
		const int channels = request.Channels;
		const size_t pixels = (size_t)width * height;

		const int outWidth = width * request.Scale;
		const int outHeight = height * request.Scale;
		std::vector<uint8_t> out((size_t)outWidth * outHeight * channels);

		// One channel at a time: each carries its own histogram, and a shared
		// transform would drag one channel's distribution onto another's.
		std::vector<double> gaussian(pixels);
		std::vector<uint32_t> order(pixels);
		constexpr int kTableSize = 512;
		std::vector<double> inverse(kTableSize);

		for (int c = 0; c < channels; c++)
		{
			// --- forward: this channel's histogram onto a unit Gaussian -------
			std::iota(order.begin(), order.end(), 0u);
			std::stable_sort(order.begin(), order.end(),
							 [&](uint32_t a, uint32_t b)
							 {
								 return request.Pixels[(size_t)a * channels + c]
									  < request.Pixels[(size_t)b * channels + c];
							 });

			for (size_t i = 0; i < pixels; i++)
			{
				// (i + 0.5)/n rather than i/n so neither tail lands on an
				// infinity when it is pushed through the inverse error function.
				const double quantile = ((double)i + 0.5) / (double)pixels;
				gaussian[order[i]] = std::sqrt(2.0) * ErfInverse(2.0 * quantile - 1.0);
			}

			// --- the inverse table -------------------------------------------
			for (int t = 0; t < kTableSize; t++)
			{
				const double quantile = ((double)t + 0.5) / (double)kTableSize;
				const double at = quantile * (double)pixels - 0.5;
				const int low = std::clamp((int)std::floor(at), 0, (int)pixels - 1);
				const int high = std::min(low + 1, (int)pixels - 1);
				const double f = std::clamp(at - (double)low, 0.0, 1.0);
				const double a = request.Pixels[(size_t)order[low] * channels + c];
				const double b = request.Pixels[(size_t)order[high] * channels + c];
				inverse[t] = a * (1.0 - f) + b * f;
			}

			// --- blend --------------------------------------------------------
			for (int y = 0; y < outHeight; y++)
			{
				const double v = ((double)y + 0.5) / (double)outHeight;
				for (int x = 0; x < outWidth; x++)
				{
					const double u = ((double)x + 0.5) / (double)outWidth;

					const double px = u * request.Cells;
					const double py = v * request.Cells;
					const double bx = std::floor(px);
					const double by = std::floor(py);
					const double fx = px - bx;
					const double fy = py - by;

					// Which of the square's two triangles the point is in.
					const bool upper = (fx + fy) > 1.0;
					const int64_t vx[3] = { (int64_t)bx + (upper ? 1 : 0),
											(int64_t)bx + 1,
											(int64_t)bx };
					const int64_t vy[3] = { (int64_t)by + (upper ? 1 : 0),
											(int64_t)by,
											(int64_t)by + 1 };
					const double w[3] = { upper ? fx + fy - 1.0 : 1.0 - fx - fy,
										  upper ? 1.0 - fy : fx,
										  upper ? 1.0 - fx : fy };

					double sum = 0.0;
					double squares = 0.0;
					for (int k = 0; k < 3; k++)
					{
						double ox = 0.0, oy = 0.0;
						// **One offset field for every channel and every map.**
						// Seeding per channel was wrong and not subtly: red,
						// green and blue would each be drawn from a different
						// part of the source, which is colour fringing -- and
						// across maps it is worse, because the normal would
						// then describe a bump where the colour has no
						// aggregate. The histogram transform is per channel,
						// because each channel has its own distribution; the
						// *offsets* must be shared or the maps stop being the
						// same surface.
						CellOffset(vx[k], vy[k], request.Cells, request.Seed, ox, oy);

						int sx = (int)std::floor((u + ox) * width) % width;
						int sy = (int)std::floor((v + oy) * height) % height;
						if (sx < 0) sx += width;
						if (sy < 0) sy += height;

						sum += w[k] * gaussian[(size_t)sy * width + sx];
						squares += w[k] * w[k];
					}

					// **Variance preserving.** A plain weighted mean of three
					// Gaussian draws has variance sum(w^2), which is less than
					// one -- and that missing variance is exactly the contrast
					// a naive blend loses.
					const double blended = squares > 1e-12 ? sum / std::sqrt(squares) : 0.0;

					const double quantile = 0.5 * (1.0 + Erf(blended / std::sqrt(2.0)));
					const double at = std::clamp(quantile * kTableSize - 0.5,
												 0.0, (double)kTableSize - 1.0);
					const int low = (int)std::floor(at);
					const int high = std::min(low + 1, kTableSize - 1);
					const double f = at - (double)low;
					const double value = inverse[low] * (1.0 - f) + inverse[high] * f;

					out[((size_t)y * outWidth + x) * channels + c] =
						(uint8_t)std::clamp(value + 0.5, 0.0, 255.0);
				}
			}
		}

		return out;
	}
}
