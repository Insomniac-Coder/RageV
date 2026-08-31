#include <rvpch.h>
#include "TerrainOps.h"

#include "RageV/Math/Math.h"

#include <cmath>
#include <random>

namespace RageV
{
	namespace TerrainOps
	{
		namespace
		{
			// The heights as metres, so every angle below is an angle in the
			// world. Working in the asset's 16-bit units instead is what turns
			// an angle of repose into a number that depends on the grid.
			std::vector<float> ToMetres(const TerrainData& data, const Scale& scale)
			{
				std::vector<float> out((size_t)data.Resolution * data.Resolution);
				const float unit = scale.HeightMetres / 65535.0f;
				for (size_t i = 0; i < out.size(); ++i)
					out[i] = (float)data.Heights[i] * unit;
				return out;
			}

			// And back, clamped. **The clamp is the operator's contract**: a
			// heightfield cannot hold a sample below its base or above its
			// height, so erosion that would carry material out of the range
			// stops at the range instead of wrapping.
			void FromMetres(TerrainData& data, const Scale& scale,
							const std::vector<float>& metres)
			{
				const float unit = scale.HeightMetres > 1e-6f
								 ? 65535.0f / scale.HeightMetres : 0.0f;
				for (size_t i = 0; i < metres.size(); ++i)
				{
					const float v = Math::Round(metres[i] * unit);
					data.Heights[i] = (uint16_t)Math::Clamp(v, 0.0f, 65535.0f);
				}
			}

			float SampleBilinear(const std::vector<float>& field, uint32_t resolution,
								 float x, float z)
			{
				x = Math::Clamp(x, 0.0f, (float)resolution - 1.001f);
				z = Math::Clamp(z, 0.0f, (float)resolution - 1.001f);
				const uint32_t x0 = (uint32_t)x;
				const uint32_t z0 = (uint32_t)z;
				const float tx = x - (float)x0;
				const float tz = z - (float)z0;
				const size_t row = (size_t)z0 * resolution;
				const size_t next = row + resolution;
				return (field[row + x0] * (1.0f - tx) + field[row + x0 + 1] * tx) * (1.0f - tz)
					 + (field[next + x0] * (1.0f - tx) + field[next + x0 + 1] * tx) * tz;
			}

			// Add to a layer's weight, saturating, in the interleaved bytes.
			void AddWeight(TerrainData& data, size_t sample, int layer, float value)
			{
				if (layer < 0 || layer >= (int)TerrainData::kLayers)
					return;
				const size_t at = sample * TerrainData::kLayers + (size_t)layer;
				const float sum = (float)data.Weights[at] + value * 255.0f;
				data.Weights[at] = (uint8_t)Math::Clamp(Math::Round(sum), 0.0f, 255.0f);
			}
		}

		uint32_t ThermalErode(TerrainData& data, const Scale& scale,
							  const ThermalParams& params)
		{
			if (!data.IsValid() || params.Iterations <= 0)
				return 0;

			const uint32_t n = data.Resolution;
			std::vector<float> h = ToMetres(data, scale);
			std::vector<float> delta(h.size());

			const float talus = std::tan(Math::Radians(
									Math::Clamp(params.ReposeDegrees, 1.0f, 85.0f)))
							  * scale.Cell(n);
			const float rate = Math::Clamp(params.Rate, 0.0f, 1.0f) * 0.25f;
			// Excess below this is rounding, not material.
			const float kSettled = 1e-4f;

			uint32_t moved = 0;
			for (int pass = 0; pass < params.Iterations; ++pass)
			{
				std::fill(delta.begin(), delta.end(), 0.0f);
				moved = 0;

				// Four neighbours. The excess over the talus slope is what may
				// move; it is shared out in proportion to how far each
				// neighbour is below, so material runs down the steepest way
				// without ever overshooting into the shallowest.
				for (uint32_t z = 0; z < n; ++z)
				{
					for (uint32_t x = 0; x < n; ++x)
					{
						const size_t at = (size_t)z * n + x;
						const float here = h[at];

						float excess[4] = {};
						size_t nbr[4] = {};
						int count = 0;
						float total = 0.0f;
						float worst = 0.0f;

						const int offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
						for (const auto& o : offsets)
						{
							const int nx = (int)x + o[0];
							const int nz = (int)z + o[1];
							if (nx < 0 || nz < 0 || nx >= (int)n || nz >= (int)n)
								continue;
							const size_t other = (size_t)nz * n + (size_t)nx;
							const float drop = here - h[other] - talus;
							if (drop <= 0.0f)
								continue;
							excess[count] = drop;
							nbr[count] = other;
							total += drop;
							worst = Math::Max(worst, drop);
							++count;
						}

						// A settled slope sits *at* the repose angle, and
						// floating point leaves it a hair above: without a
						// floor here the "did anything move" answer is yes
						// forever and no caller can iterate to convergence.
						if (count == 0 || total <= kSettled)
							continue;

						// **Never more than half the largest step down.** A
						// cell with four downhill neighbours has four excesses
						// in `total`, and a rate applied to their sum can move
						// more material than the steepest of them was ever
						// above the repose angle -- which overshoots, digs the
						// cell below its neighbour, and sets the pair
						// oscillating instead of settling. Measured: a cone at
						// 3.8 that would not come below 1.0 in four hundred
						// passes.
						const float give = Math::Min(rate * total, 0.5f * worst);
						delta[at] -= give;
						for (int i = 0; i < count; ++i)
							delta[nbr[i]] += give * (excess[i] / total);
						++moved;
					}
				}

				if (moved == 0)
					break;
				for (size_t i = 0; i < h.size(); ++i)
					h[i] += delta[i];
			}

			FromMetres(data, scale, h);
			return moved;
		}

		uint32_t HydraulicErode(TerrainData& data, const Scale& scale,
								const HydraulicParams& params)
		{
			if (!data.IsValid() || params.Droplets == 0 || params.Steps <= 0)
				return 0;

			const uint32_t n = data.Resolution;
			std::vector<float> h = ToMetres(data, scale);

			std::mt19937 rng(params.Seed ? params.Seed : 1u);
			std::uniform_real_distribution<float> place(1.0f, (float)n - 2.0f);

			uint32_t stillMoving = 0;
			for (uint32_t d = 0; d < params.Droplets; ++d)
			{
				float px = place(rng);
				float pz = place(rng);
				float dx = 0.0f, dz = 0.0f;
				float speed = 1.0f, water = 1.0f, sediment = 0.0f;

				for (int step = 0; step < params.Steps; ++step)
				{
					const uint32_t x0 = (uint32_t)px;
					const uint32_t z0 = (uint32_t)pz;
					const float tx = px - (float)x0;
					const float tz = pz - (float)z0;

					const size_t row = (size_t)z0 * n;
					const size_t next = row + n;
					const float h00 = h[row + x0];
					const float h10 = h[row + x0 + 1];
					const float h01 = h[next + x0];
					const float h11 = h[next + x0 + 1];

					// The gradient of the *cell's* plane, not a difference of
					// neighbouring samples: a droplet lives between samples,
					// and stepping it by whole-sample differences makes it
					// walk the grid's axes.
					const float gx = (h10 - h00) * (1.0f - tz) + (h11 - h01) * tz;
					const float gz = (h01 - h00) * (1.0f - tx) + (h11 - h10) * tx;
					const float height = (h00 * (1.0f - tx) + h10 * tx) * (1.0f - tz)
									   + (h01 * (1.0f - tx) + h11 * tx) * tz;

					dx = dx * params.Inertia - gx * (1.0f - params.Inertia);
					dz = dz * params.Inertia - gz * (1.0f - params.Inertia);
					const float length = std::sqrt(dx * dx + dz * dz);
					if (length <= 1e-8f)
						break;
					dx /= length;
					dz /= length;

					const float nx = px + dx;
					const float nz = pz + dz;
					if (nx < 1.0f || nz < 1.0f ||
						nx > (float)n - 2.0f || nz > (float)n - 2.0f)
						break;

					const float nextHeight = SampleBilinear(h, n, nx, nz);
					const float drop = height - nextHeight;   // positive downhill

					const float cap = Math::Min(
						Math::Max(drop, params.MinSlope) * speed * water * params.Capacity,
						params.MaxCapacity);

					float change;
					if (drop < 0.0f)
					{
						// Climbing out of a hole: it can fill the hole and
						// nothing else.
						change = Math::Min(sediment, -drop);
					}
					else if (sediment > cap)
					{
						change = (sediment - cap) * params.DepositRate;
					}
					else
					{
						change = -Math::Min((cap - sediment) * params.ErodeRate, drop);
					}
					change = Math::Clamp(change, -params.MaxChangeMetres,
										 params.MaxChangeMetres);

					// Spread over the cell's four samples by area. A droplet
					// that dumps its load on one sample makes a spike, and a
					// field of spikes is what unfiltered erosion looks like.
					h[row + x0] += change * (1.0f - tx) * (1.0f - tz);
					h[row + x0 + 1] += change * tx * (1.0f - tz);
					h[next + x0] += change * (1.0f - tx) * tz;
					h[next + x0 + 1] += change * tx * tz;
					sediment -= change;

					speed = Math::Min(
						std::sqrt(Math::Max(speed * speed + drop * params.Gravity, 0.0f)),
						params.MaxSpeed);
					water *= (1.0f - params.Evaporate);
					px = nx;
					pz = nz;

					if (step == params.Steps - 1)
						++stillMoving;
				}
			}

			FromMetres(data, scale, h);
			return stillMoving;
		}

		void Stratify(TerrainData& data, const Scale& scale,
					  const StratifyParams& params)
		{
			if (!data.IsValid() || params.ThicknessMetres <= 1e-4f)
				return;

			const uint32_t n = data.Resolution;
			const float cell = scale.Cell(n);
			std::vector<float> h = ToMetres(data, scale);
			const std::vector<float> source = h;

			for (uint32_t z = 0; z < n; ++z)
			{
				for (uint32_t x = 0; x < n; ++x)
				{
					const size_t at = (size_t)z * n + x;

					const uint32_t xl = x > 0 ? x - 1 : x;
					const uint32_t xr = x + 1 < n ? x + 1 : x;
					const uint32_t zl = z > 0 ? z - 1 : z;
					const uint32_t zr = z + 1 < n ? z + 1 : z;
					const float dhdx = (source[(size_t)z * n + xr] - source[(size_t)z * n + xl])
									 / ((float)(xr - xl) * cell);
					const float dhdz = (source[(size_t)zr * n + x] - source[(size_t)zl * n + x])
									 / ((float)(zr - zl) * cell);
					const float slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);

					const float steep = Math::SmoothStep(params.SlopeFrom, params.SlopeTo, slope);
					if (steep <= 0.0f)
						continue;

					// A softened sawtooth of the height: the riser steeper
					// than the ledge, and the whole thing a function of height
					// alone, so the beds run level across a face and wrap
					// round a spur exactly as real bedding does.
					const float level = source[at] + params.Dip * (float)x * cell;
					const float band = level / params.ThicknessMetres;
					const float frac = band - std::floor(band);
					const float step = frac * frac * (3.0f - 2.0f * frac);
					h[at] += (step - 0.5f) * params.AmountMetres * steep;
				}
			}

			FromMetres(data, scale, h);
		}

		Landform Analyse(const TerrainData& data, const Scale& scale, int flowIterations)
		{
			Landform out;
			if (!data.IsValid())
				return out;

			const uint32_t n = data.Resolution;
			out.Resolution = n;
			const std::vector<float> h = ToMetres(data, scale);
			const float cell = scale.Cell(n);

			out.Slope.assign(h.size(), 0.0f);
			out.Curvature.assign(h.size(), 0.0f);
			for (uint32_t z = 0; z < n; ++z)
			{
				for (uint32_t x = 0; x < n; ++x)
				{
					const uint32_t xl = x > 0 ? x - 1 : x;
					const uint32_t xr = x + 1 < n ? x + 1 : x;
					const uint32_t zl = z > 0 ? z - 1 : z;
					const uint32_t zr = z + 1 < n ? z + 1 : z;
					const size_t at = (size_t)z * n + x;

					const float dhdx = (h[(size_t)z * n + xr] - h[(size_t)z * n + xl])
									 / ((float)(xr - xl) * cell);
					const float dhdz = (h[(size_t)zr * n + x] - h[(size_t)zl * n + x])
									 / ((float)(zr - zl) * cell);
					out.Slope[at] = std::sqrt(dhdx * dhdx + dhdz * dhdz);

					const float lap = h[(size_t)z * n + xl] + h[(size_t)z * n + xr]
									+ h[(size_t)zl * n + x] + h[(size_t)zr * n + x]
									- 4.0f * h[at];
					// The Laplacian is positive in a hollow, so it is negated:
					// convex ground reads positive, which is the way round
					// every caller wants it.
					out.Curvature[at] = -lap / (cell * cell);
				}
			}

			// Multiple-flow-direction accumulation, advected. Every sample
			// sends its water to all eight lower neighbours in proportion to
			// the drop toward each; what has nowhere to go stays put, which is
			// what makes a hollow read as wet.
			out.Flow.assign(h.size(), 0.0f);
			std::vector<float> water(h.size(), 1.0f);
			std::vector<float> moved(h.size(), 0.0f);

			const int offsets[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
										{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
			for (int pass = 0; pass < Math::Max(flowIterations, 0); ++pass)
			{
				std::fill(moved.begin(), moved.end(), 0.0f);
				for (uint32_t z = 0; z < n; ++z)
				{
					for (uint32_t x = 0; x < n; ++x)
					{
						const size_t at = (size_t)z * n + x;
						float drops[8] = {};
						size_t nbr[8] = {};
						int count = 0;
						float total = 0.0f;

						for (const auto& o : offsets)
						{
							const int nx = (int)x + o[0];
							const int nz = (int)z + o[1];
							if (nx < 0 || nz < 0 || nx >= (int)n || nz >= (int)n)
								continue;
							const size_t other = (size_t)nz * n + (size_t)nx;
							const float drop = h[at] - h[other];
							if (drop <= 0.0f)
								continue;
							drops[count] = drop;
							nbr[count] = other;
							total += drop;
							++count;
						}

						if (count == 0 || total <= 0.0f)
						{
							moved[at] += water[at];       // a sink: it stays
							continue;
						}
						for (int i = 0; i < count; ++i)
							moved[nbr[i]] += water[at] * (drops[i] / total);
					}
				}
				water.swap(moved);
				for (size_t i = 0; i < out.Flow.size(); ++i)
					out.Flow[i] += water[i];
			}
			if (flowIterations > 0)
			{
				for (float& f : out.Flow)
					f /= (float)flowIterations;
			}

			return out;
		}

		void PaintByLandform(TerrainData& data, const Scale& scale,
							 const Landform& landform, const PaintParams& params)
		{
			if (!data.IsValid() || landform.Resolution != data.Resolution)
				return;

			const uint32_t n = data.Resolution;
			data.Weights.assign((size_t)n * n * TerrainData::kLayers, 0);
			const std::vector<float> h = ToMetres(data, scale);

			for (uint32_t z = 0; z < n; ++z)
			{
				for (uint32_t x = 0; x < n; ++x)
				{
					const size_t at = (size_t)z * n + x;
					const float height = h[at] - params.SeaLevelMetres;

					// Rock where the ground is steep, and more of it where it
					// is also convex -- that is the ground being stripped, and
					// stripped ground is where rock shows.
					float rock = Math::SmoothStep(params.RockSlopeFrom,
												  params.RockSlopeTo,
												  landform.Slope[at]);
					if (params.RockCurvature > 0.0f)
					{
						const float convex = Math::SmoothStep(
							0.0f, params.RockCurvature, landform.Curvature[at]);
						// **A nudge, not a veto.** This used to scale a
						// planar slope's rock down to 0.65 because a plane
						// has no curvature -- so the most obviously rocky
						// thing in a landscape, a straight cliff face, came
						// out mostly soil. Curvature says where rock is
						// *extra* likely, and that is all it should say.
						rock = Math::Clamp(rock * (0.85f + 0.15f * convex)
										   + 0.15f * convex * rock, 0.0f, 1.0f);
					}

					// Sand across the waterline.
					const float sand = Math::SmoothStep(-params.SandBelowMetres,
														-params.SandBelowMetres * 0.15f,
														height)
									 * Math::SmoothStep(params.SandAboveMetres * 4.0f,
														params.SandAboveMetres, height)
									 * (1.0f - rock);

					// Scrub above the beach, out of the rock's way.
					const float scrub = Math::SmoothStep(params.ScrubFromMetres,
														 params.ScrubToMetres, height)
									  * (1.0f - rock) * (1.0f - sand);

					// And the draws: where the water actually runs, the
					// channel's own material shows through whatever the slope
					// would otherwise say.
					const float channel = params.ChannelFlow > 0.0f
						? Math::SmoothStep(params.ChannelFlow,
										   params.ChannelFlow * 3.0f, landform.Flow[at])
						: 0.0f;

					const float soil = Math::Clamp(1.0f - rock - sand - scrub, 0.0f, 1.0f)
									 + channel * rock * 0.75f;

					AddWeight(data, at, params.RockLayer, rock * (1.0f - channel * 0.75f));
					AddWeight(data, at, params.SandLayer, sand);
					AddWeight(data, at, params.ScrubLayer, scrub);
					AddWeight(data, at, params.SoilLayer, soil);
				}
			}
		}
	}
}
