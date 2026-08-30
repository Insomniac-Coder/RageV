#include <rvpch.h>
#include "Water.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/Log.h"
#include "RageV/Math/Math.h"

#include <algorithm>
#include <cmath>

namespace RageV
{
	namespace
	{
		// The one water material. A file-static rather than a member because
		// there is one of them for the whole engine, on the same terms as
		// Renderer3D's default material -- and because a component that could
		// hold its own would be a component somebody could point elsewhere.
		RHI::Ref<Material> s_Material;

		// The two generated tiles, one each for the whole engine on the same
		// terms as the material, and the foam pass's machinery beside them.
		RHI::Ref<RHI::RHITexture> s_DetailNormal;
		RHI::Ref<RHI::RHITexture> s_FoamPattern;
		RHI::Ref<RHI::RHIShader> s_FoamShader;
		RHI::Ref<RHI::RHIComputePipeline> s_FoamPipeline;
		RHI::Ref<RHI::RHISampler> s_FoamSampler;
		// Said once, not per frame: a device without compute renders the live
		// Jacobian foam and nothing accumulates.
		bool s_FoamUnavailableSaid = false;

		// What the foam dispatch pushes; must match water_foam.rvshader lane
		// for lane.
		struct FoamPushConstants
		{
			Vec4 Wave;    // height, length, choppiness, speed
			Vec4 Extra;   // direction (radians), time, spacing, dt
			Vec4 Size;    // width, length, unused, unused
		};
		static_assert(sizeof(FoamPushConstants) == 48,
					  "Must match Params in water_foam.rvshader");

		// A tiny deterministic integer hash for the two generated tiles --
		// fixed seeds, so the same tiles come out of every run and a
		// screenshot check can trust them.
		uint32_t TileHash(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7FEB352Du;
			x ^= x >> 15; x *= 0x846CA68Bu;
			x ^= x >> 16;
			return x;
		}

		float TileHash01(uint32_t x)
		{
			return (float)(TileHash(x) & 0x00FFFFFFu) / 16777216.0f;
		}

		// **The spacing is a request, not a promise.** An author who asks for
		// a 4 km bay at 10 cm spacing is asking for 1.6 billion quads, and the
		// honest answer is not to try. The cap is on the divisions per axis
		// rather than on the total, so a long thin channel keeps its length
		// resolution instead of being punished for its area.
		constexpr uint32_t kMaxDivisions = 1024;

		// Below this the rectangle is not a surface, and a zero-area mesh is a
		// mesh whose bounds are a point -- which culls at every angle and then
		// looks like a draw bug.
		constexpr float kMinExtent = 0.01f;

		uint32_t DivisionsFor(float extent, float spacing)
		{
			if (extent <= kMinExtent)
				return 0;

			const float step = std::max(spacing, 1.0e-3f);
			const uint32_t count = (uint32_t)std::ceil(extent / step);
			return std::clamp(count, 1u, kMaxDivisions);
		}
	}

	RHI::Ref<Material> Water::GetMaterial()
	{
		if (s_Material)
			return s_Material;
		if (!Renderer::HasDevice())
			return nullptr;

		s_Material = RHI::Ref<Material>(
			new Material(Renderer::GetDevice(), "Water"));

		MaterialParams& params = s_Material->GetParams();

		// Almost black, because water has hardly any colour of its own. What
		// is seen in it is the sky and the shore; a base colour bright enough
		// to notice is one that washes the reflection out and turns the sea
		// into painted plastic.
		//
		// The alpha is carried but not yet used -- see the blend note below.
		params.BaseColor = { 0.012f, 0.021f, 0.032f, 0.86f };
		params.Metallic = 0.0f;

		// **Opaque, and not because water is.** Setting this to Blend makes the
		// body vanish outright, and the reason is architectural rather than a
		// bug in this file: the transparent pass is fed by
		// `Renderer3D::DrawTransparentIndirect(m_CulledBlend, m_BlendMeshes)`,
		// and those two lists are built by the *MeshComponent* gather. A water
		// body is not a MeshComponent, so an immediate `DrawMesh` marked Blend
		// is sorted into a bucket that nothing ever submits, and the draw is
		// silently dropped.
		//
		// Terrain has the same ceiling and has never needed to notice it. Water
		// is the first thing that does, because a sea you cannot see into reads
		// as painted tarmac however well it reflects. Making it transparent
		// means giving the blend gather a way to take geometry that is not a
		// MeshComponent, which is a change to that path and not to this line.
		s_Material->SetBlendMode(BlendMode::Blend);

		// Low, but not zero. A mirror-flat surface reads as glass, and the
		// microfacet roughness is standing in for the ripple the geometry
		// cannot yet carry. It comes down when the waves go in.
		params.Roughness = 0.09f;

		// **0.25, not the 0.5 everything else uses.** F0 = 0.08 * Specular, so
		// this is the 2% water actually reflects head-on against the 4% of a
		// general dielectric. It is the difference between a sea you can see
		// into and one with a sheen on it, and it is the whole reason that
		// field exists.
		params.Specular = 0.25f;

		return s_Material;
	}

	void Water::Shutdown()
	{
		s_Material.reset();
		s_DetailNormal.reset();
		s_FoamPattern.reset();
		s_FoamPipeline.reset();
		s_FoamShader.reset();
		s_FoamSampler.reset();
		s_FoamUnavailableSaid = false;
	}

	uint32_t Water::QuadCount(float width, float length, float spacing)
	{
		return DivisionsFor(width, spacing) * DivisionsFor(length, spacing);
	}

	void Water::BuildGeometry(float width, float length, float spacing,
							  float textureScale,
							  std::vector<MeshVertex>& vertices,
							  std::vector<uint32_t>& indices)
	{
		vertices.clear();
		indices.clear();

		const uint32_t columns = DivisionsFor(width, spacing);
		const uint32_t rows = DivisionsFor(length, spacing);
		if (columns == 0 || rows == 0)
			return;

		const float halfWidth = width * 0.5f;
		const float halfLength = length * 0.5f;

		// **Metres per repeat, taken off the position and not off the index.**
		// A UV that runs 0..1 across the body would stretch the texture as the
		// body is resized, so the same water would have a different wave scale
		// in a pond and in a bay. Off the position it does not: resizing adds
		// surface, it does not magnify it.
		const float repeat = std::max(textureScale, 1.0e-3f);

		vertices.reserve((size_t)(columns + 1) * (rows + 1));
		for (uint32_t row = 0; row <= rows; row++)
		{
			const float z = -halfLength + length * (float)row / (float)rows;
			for (uint32_t column = 0; column <= columns; column++)
			{
				const float x = -halfWidth + width * (float)column / (float)columns;

				MeshVertex vertex;
				vertex.Position = { x, 0.0f, z };
				vertex.Normal = { 0.0f, 1.0f, 0.0f };
				vertex.TexCoord = { x / repeat, z / repeat };
				vertices.push_back(vertex);
			}
		}

		// Counter-clockwise seen from above, which is the winding every other
		// upward-facing surface in this engine uses -- a plane wound the other
		// way is not merely invisible under backface culling, it is lit from
		// underneath, and a body of water lit from underneath reads as a hole.
		indices.reserve((size_t)columns * rows * 6);
		const uint32_t stride = columns + 1;
		for (uint32_t row = 0; row < rows; row++)
		{
			for (uint32_t column = 0; column < columns; column++)
			{
				const uint32_t base = row * stride + column;
				const uint32_t next = base + stride;

				indices.push_back(base);
				indices.push_back(next);
				indices.push_back(next + 1);

				indices.push_back(base);
				indices.push_back(next + 1);
				indices.push_back(base + 1);
			}
		}
	}

	bool Water::Resolve(WaterComponent& component)
	{
		const bool matches = component.Runtime
						  && component.BuiltWidth == component.Width
						  && component.BuiltLength == component.Length
						  && component.BuiltSpacing == component.Spacing
						  && component.BuiltTextureScale == component.TextureScale;
		if (matches)
			return true;

		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		BuildGeometry(component.Width, component.Length, component.Spacing,
					  component.TextureScale, vertices, indices);

		if (vertices.empty() || indices.empty())
		{
			// A body with no area is not an error -- it is a component someone
			// has just added and not yet sized. It draws nothing and says so
			// only once, because the alternative is a line per frame.
			component.Runtime.reset();
			component.BuiltWidth = component.Width;
			component.BuiltLength = component.Length;
			component.BuiltSpacing = component.Spacing;
			component.BuiltTextureScale = component.TextureScale;
			return false;
		}

		component.Runtime = RHI::Ref<Mesh>(new Mesh(
			Renderer::GetDevice(), vertices, indices, "Water"));

		component.BuiltWidth = component.Width;
		component.BuiltLength = component.Length;
		component.BuiltSpacing = component.Spacing;
		component.BuiltTextureScale = component.TextureScale;
		return true;
	}

	// --- the generated tiles ----------------------------------------------

	RHI::Ref<RHI::RHITexture> Water::GetDetailNormal()
	{
		using namespace RHI;

		if (s_DetailNormal)
			return s_DetailNormal;
		if (!Renderer::HasDevice())
			return nullptr;

		// A periodic Gerstner-flavoured slope field: the same physics as the
		// geometry's own sum, an octave band below what any grid can carry.
		// **Integer wave-vectors are what make it tile** -- a wave whose
		// vector is a whole number of cycles per side meets itself exactly at
		// the seam, so the sum of any set of them does too. Amplitude falls
		// as 1/|k|, which is the constant-steepness spectrum the big waves
		// use, quoted per cycle instead of per metre.
		constexpr uint32_t kSize = 256;
		constexpr int kWaves = 24;

		std::vector<uint32_t> texels((size_t)kSize * kSize);

		struct Wavelet { float kx, ky, amplitude, phase; };
		std::vector<Wavelet> waves;
		waves.reserve(kWaves);
		for (int i = 0; i < kWaves; i++)
		{
			// Frequencies from 2 to about 24 cycles per tile, jittered in a
			// forward-peaked fan the way the geometry's spectrum is.
			const float t = (float)i / (float)(kWaves - 1);
			const float cycles = 2.0f + t * 22.0f + TileHash01(i * 5u + 1u) * 2.0f;
			const float heading = (TileHash01(i * 5u + 2u) - 0.5f) * 2.4f;

			Wavelet w;
			w.kx = std::round(cycles * std::cos(heading));
			w.ky = std::round(cycles * std::sin(heading));
			if (w.kx == 0.0f && w.ky == 0.0f)
				w.kx = 1.0f;
			const float k = std::sqrt(w.kx * w.kx + w.ky * w.ky);
			w.amplitude = 0.055f / k;
			w.phase = TileHash01(i * 5u + 3u) * 6.2831853f;
			waves.push_back(w);
		}

		for (uint32_t y = 0; y < kSize; y++)
		{
			for (uint32_t x = 0; x < kSize; x++)
			{
				const float u = (float)x / (float)kSize;
				const float v = (float)y / (float)kSize;

				float dhdx = 0.0f, dhdy = 0.0f;
				for (const Wavelet& w : waves)
				{
					const float angle = 6.2831853f * (w.kx * u + w.ky * v) + w.phase;
					const float c = std::cos(angle);
					dhdx += w.amplitude * w.kx * 6.2831853f * c;
					dhdy += w.amplitude * w.ky * 6.2831853f * c;
				}

				// The slope straight into RG, biased to 0.5 -- the fragment
				// decodes xy and rebuilds its own z, so B carries the flat
				// answer for anything that samples it as a plain normal map.
				const float sx = std::clamp(dhdx * 0.5f + 0.5f, 0.0f, 1.0f);
				const float sy = std::clamp(dhdy * 0.5f + 0.5f, 0.0f, 1.0f);
				const uint32_t r = (uint32_t)(sx * 255.0f + 0.5f);
				const uint32_t g = (uint32_t)(sy * 255.0f + 0.5f);
				texels[(size_t)y * kSize + x] = 0xFF000000u | (255u << 16) | (g << 8) | r;
			}
		}

		TextureDesc desc;
		desc.Width = kSize;
		desc.Height = kSize;
		desc.Format = Format::R8G8B8A8_UNORM;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		desc.DebugName = "Water.detailNormal";

		s_DetailNormal = Renderer::GetDevice().CreateTexture(desc);
		if (s_DetailNormal)
			s_DetailNormal->Upload(texels.data(), texels.size() * sizeof(uint32_t));
		return s_DetailNormal;
	}

	RHI::Ref<RHI::RHITexture> Water::GetFoamPattern()
	{
		using namespace RHI;

		if (s_FoamPattern)
			return s_FoamPattern;
		if (!Renderer::HasDevice())
			return nullptr;

		// Ridged periodic value noise: whitewater drains into a web of
		// bright strands around dark cells, which is 1 - |noise| of a smooth
		// field. Three octaves whose lattices each divide the tile evenly, so
		// the seam is exact.
		constexpr uint32_t kSize = 256;

		auto lattice = [](uint32_t cells, uint32_t cx, uint32_t cy, uint32_t seed)
		{
			return TileHash01((cy % cells) * 977u + (cx % cells) * 331u + seed * 7919u);
		};

		auto valueNoise = [&](float u, float v, uint32_t cells, uint32_t seed)
		{
			const float fx = u * (float)cells;
			const float fy = v * (float)cells;
			const uint32_t x0 = (uint32_t)fx;
			const uint32_t y0 = (uint32_t)fy;
			float tx = fx - (float)x0;
			float ty = fy - (float)y0;
			// Smoothstep, so the lattice does not show as a grid of creases.
			tx = tx * tx * (3.0f - 2.0f * tx);
			ty = ty * ty * (3.0f - 2.0f * ty);

			const float a = lattice(cells, x0, y0, seed);
			const float b = lattice(cells, x0 + 1u, y0, seed);
			const float c = lattice(cells, x0, y0 + 1u, seed);
			const float d = lattice(cells, x0 + 1u, y0 + 1u, seed);
			return (a * (1.0f - tx) + b * tx) * (1.0f - ty)
				 + (c * (1.0f - tx) + d * tx) * ty;
		};

		std::vector<uint32_t> texels((size_t)kSize * kSize);
		for (uint32_t y = 0; y < kSize; y++)
		{
			for (uint32_t x = 0; x < kSize; x++)
			{
				const float u = (float)x / (float)kSize;
				const float v = (float)y / (float)kSize;

				float noise = 0.0f;
				noise += (1.0f - std::abs(valueNoise(u, v, 6, 1u) * 2.0f - 1.0f)) * 0.5f;
				noise += (1.0f - std::abs(valueNoise(u, v, 13, 2u) * 2.0f - 1.0f)) * 0.3f;
				noise += (1.0f - std::abs(valueNoise(u, v, 27, 3u) * 2.0f - 1.0f)) * 0.2f;

				// Sharpened toward the strands: most of the tile sits in the
				// low half so the lace reads as strands over water rather
				// than white with cracks.
				const float lace = std::pow(std::clamp((noise - 0.42f) / 0.58f, 0.0f, 1.0f), 1.6f);
				const uint32_t g = (uint32_t)(lace * 255.0f + 0.5f);
				texels[(size_t)y * kSize + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
			}
		}

		TextureDesc desc;
		desc.Width = kSize;
		desc.Height = kSize;
		desc.Format = Format::R8G8B8A8_UNORM;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		desc.DebugName = "Water.foamPattern";

		s_FoamPattern = Renderer::GetDevice().CreateTexture(desc);
		if (s_FoamPattern)
			s_FoamPattern->Upload(texels.data(), texels.size() * sizeof(uint32_t));
		return s_FoamPattern;
	}

	// --- the foam accumulation step ---------------------------------------

	namespace
	{
		bool EnsureFoamPipeline()
		{
			using namespace RHI;

			if (s_FoamPipeline)
				return true;
			if (!Renderer::HasDevice())
				return false;

			RHIDevice& device = Renderer::GetDevice();
			if (!device.GetCaps().SupportsCompute)
			{
				if (!s_FoamUnavailableSaid)
				{
					RV_CORE_WARN("Water: no compute support; foam stays the live "
								 "Jacobian and nothing accumulates or drifts");
					s_FoamUnavailableSaid = true;
				}
				return false;
			}

			ShaderCompiler::Init();
			auto compiled = ShaderCompiler::CompileFromFile(
				"assets/shaders/water_foam.rvshader");
			if (!compiled)
			{
				if (!s_FoamUnavailableSaid)
				{
					RV_CORE_ERROR("Water: water_foam.rvshader did not compile; foam "
								  "stays the live Jacobian");
					s_FoamUnavailableSaid = true;
				}
				return false;
			}

			s_FoamShader = device.CreateShader(*compiled);
			if (!s_FoamShader)
				return false;

			ComputePipelineDesc desc;
			desc.Name = "Water.foam";
			desc.Shader = s_FoamShader;
			s_FoamPipeline = device.CreateComputePipeline(desc);

			SamplerDesc sampler;
			// Clamped: the sim reads its own previous frame, and a body's
			// edge must not inherit foam from the opposite shore.
			sampler.WrapU = WrapMode::ClampToEdge;
			sampler.WrapV = WrapMode::ClampToEdge;
			sampler.WrapW = WrapMode::ClampToEdge;
			sampler.MaxLod = 0.0f;
			s_FoamSampler = device.CreateSampler(sampler);

			return s_FoamPipeline && s_FoamSampler;
		}
	}

	RHI::Ref<RHI::RHITexture> Water::CurrentFoam(const WaterComponent& component)
	{
		if (!component.FoamState)
			return nullptr;
		return component.FoamState->Textures[component.FoamState->Current];
	}

	void Water::UpdateFoam(RHI::RHICommandList& cmd, WaterComponent& component,
						   float deltaSeconds)
	{
		using namespace RHI;

		// A flat sea makes no foam and a zero dial asks for none; skipping
		// the dispatch entirely is what makes both free.
		if (component.WaveHeight <= 0.0f || component.Foam <= 0.0f)
			return;
		if (!EnsureFoamPipeline())
			return;

		RHIDevice& device = Renderer::GetDevice();

		// One texel per two metres, bounded: a pond gets crisp foam, a
		// four-kilometre bay gets what a 1024 texture can say about it. The
		// cap is cost, stated: each step evaluates the full 32-wave sum per
		// texel, and 1024 squared of that is about the price of one extra
		// water view.
		const float extent = std::max(component.Width, component.Length);
		const uint32_t resolution = std::clamp(
			((uint32_t)(extent / 2.0f) + 7u) & ~7u, 64u, 1024u);

		std::shared_ptr<WaterFoam>& state = component.FoamState;
		if (state && state->Resolution != resolution)
			state.reset();

		if (!state)
		{
			state = std::make_shared<WaterFoam>();
			state->Resolution = resolution;

			TextureDesc desc;
			desc.Width = resolution;
			desc.Height = resolution;
			desc.Format = Format::R16G16_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::Storage
					   | TextureUsage::TransferDst;

			// Zeroed, or the first frame advects whatever the allocator left
			// -- which reads as a sea that remembers a storm nobody ran.
			const std::vector<uint32_t> zeroes((size_t)resolution * resolution, 0u);
			for (int i = 0; i < 2; i++)
			{
				desc.DebugName = i == 0 ? "Water.foamA" : "Water.foamB";
				state->Textures[i] = device.CreateTexture(desc);
				if (!state->Textures[i])
				{
					state.reset();
					return;
				}
				state->Textures[i]->Upload(zeroes.data(),
										   zeroes.size() * sizeof(uint32_t));
			}
		}

		// At most one step per frame, however many views draw the scene: the
		// editor renders a scene view and a game view from one world, and a
		// second step would run the decay twice as fast as time passes.
		const uint64_t frame = Renderer::GetFrameCount();
		if (state->Frame == frame)
			return;
		state->Frame = frame;

		const uint32_t next = state->Current ^ 1u;

		// **Both barriers, and the second is the one HANDOFF paid for.** The
		// half being written was sampled by earlier draws; the half just
		// written must be visible to the ones that follow.
		cmd.TextureBarrier(state->Textures[next],
						   TextureSync::ShaderRead, TextureSync::ComputeWrite);

		// The sets ride on the state: two fixed directions, built once with
		// the textures, so no set is ever rewritten under a recorded bind.
		if (!state->Sets[0])
		{
			for (uint32_t i = 0; i < 2; i++)
			{
				state->Sets[i] = device.CreateResourceSet(s_FoamPipeline, 0);
				if (!state->Sets[i])
					return;
				state->Sets[i]->SetTexture(0, state->Textures[i], s_FoamSampler);
				state->Sets[i]->SetStorageImage(1, state->Textures[i ^ 1u]);
			}
		}

		// **Committed every step, not once at creation, and this is the
		// reverted attempt's last mystery solved.** A resource set keeps one
		// descriptor set per frame in flight, and Commit writes the *current*
		// frame's; a set committed only on the frame it was born leaves every
		// other slot unwritten, which reads as a buffer of zero -- exactly
		// what the first foam attempt recorded before it was reverted. With
		// nothing newly set, Commit replays the last full write into this
		// frame's slot and is a no-op once every slot has one.
		state->Sets[state->Current]->Commit();

		FoamPushConstants push;
		push.Wave = { component.WaveHeight, component.WaveLength,
					  component.Choppiness, component.WaveSpeed };
		push.Extra = { Math::Radians(component.WaveDirection), component.Time,
					   component.Spacing, std::max(deltaSeconds, 0.0f) };
		push.Size = { component.Width, component.Length, 0.0f, 0.0f };

		cmd.BindComputePipeline(s_FoamPipeline);
		cmd.BindResourceSet(0, state->Sets[state->Current]);
		cmd.PushConstants(ShaderStage::Compute, 0, sizeof(push), &push);

		const uint32_t groups = (resolution + 7u) / 8u;
		cmd.Dispatch(groups, groups);

		cmd.TextureBarrier(state->Textures[next],
						   TextureSync::ComputeWrite, TextureSync::ShaderRead);

		state->Current = next;
	}
}
