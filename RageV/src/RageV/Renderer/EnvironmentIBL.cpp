#include <rvpch.h>
#include "EnvironmentIBL.h"
#include "Renderer.h"
#include "Cubemap.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include <map>
#include <set>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		struct PrefilterParams
		{
			// The face's direction table, as the three vectors it is built
			// from. Passed rather than switched on an index in the shader, so
			// the table stays in one place and the two cannot drift.
			Vec4 Axis{ 0.0f };
			Vec4 Right{ 0.0f };
			Vec4 Up{ 0.0f };
			// x = roughness, y = source face size, z = sample count
			Vec4 Settings{ 0.0f };
		};

		// CubeFaceDirection(face, u, v) = normalize(Axis + Right * s + Up * t)
		// with s = 2u-1 and t = 2v-1. Kept beside the table it mirrors.
		struct FaceBasis { Vec3 Axis, Right, Up; };

		constexpr FaceBasis kBasis[CubeFaces::kFaceCount] =
		{
			{ {  1,  0,  0 }, {  0,  0, -1 }, {  0, -1,  0 } },   // +X
			{ { -1,  0,  0 }, {  0,  0,  1 }, {  0, -1,  0 } },   // -X
			{ {  0,  1,  0 }, {  1,  0,  0 }, {  0,  0,  1 } },   // +Y
			{ {  0, -1,  0 }, {  1,  0,  0 }, {  0,  0, -1 } },   // -Y
			{ {  0,  0,  1 }, {  1,  0,  0 }, {  0, -1,  0 } },   // +Z
			{ {  0,  0, -1 }, { -1,  0,  0 }, {  0, -1,  0 } },   // -Z
		};

		constexpr uint32_t kBRDFSize = 128;

		struct IBLData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			Ref<RHISampler>  Sampler;

			// The other half of split-sum's environment term. Same face table,
			// same scratch targets, same copy -- a different integral, which is
			// why it is a second shader rather than a branch in the first.
			Ref<RHIShader>   IrradianceShader;
			Ref<RHIPipeline> IrradiancePipeline;

			Ref<RHITexture> BRDF;
			Ref<RHISampler> BRDFSampler;

			// Scratch targets keyed by their own size, not by which environment
			// asked for them.
			//
			// Keyed by base size, this held one chain and threw it away when a
			// different environment arrived -- which happened mid-frame, the
			// moment a 128 pixel probe was filtered after the 512 pixel sky,
			// and destroyed images the command buffer still had bound. Sizes
			// are powers of two, so this map stays small and nothing in it is
			// ever destroyed while recording.
			std::map<uint32_t, Ref<RHIRenderTarget>> Scratch;

			// Descriptor sets, one per face per level, so none is rewritten
			// while still bound. The cursor resets once a frame, not once a
			// call -- see BeginFrame.
			std::vector<Ref<RHIResourceSet>> Sets;
			uint32_t SetCursor = 0;

			// Keyed on the source texture: a scene keeps one environment and
			// rebuilding it every frame would be thirty-six renders a frame.
			std::map<const RHITexture*, Ref<RHITexture>> Prefiltered;

			// Sources whose filter is stale. A reflection probe re-captures
			// itself, so its filter has to be redone -- into the cube already
			// allocated for it, not a new one.
			std::set<const RHITexture*> Stale;

			bool Ready = false;
		};

		std::unique_ptr<IBLData> s_Data;

		// Both convolutions render a full-screen triangle into a colour-only
		// HDR target, so the pipeline differs only in its shader.
		bool EnsurePipeline(const Ref<RHIShader>& shader, Ref<RHIPipeline>& cached, const char* name)
		{
			if (cached)
				return true;
			if (!shader || !s_Data || !s_Data->Device)
				return false;

			GraphicsPipelineDesc pipeline;
			pipeline.Name = name;
			pipeline.Shader = shader;
			pipeline.Topology = PrimitiveTopology::TriangleList;
			pipeline.Rasterizer.Cull = CullMode::None;
			pipeline.DepthStencil.DepthTestEnable = false;
			pipeline.DepthStencil.DepthWriteEnable = false;
			pipeline.Blend = BlendPreset::Opaque;
			pipeline.ColorFormats = { Format::R16G16B16A16_SFLOAT };
			pipeline.DepthFormat = Format::Undefined;

			cached = s_Data->Device->CreatePipeline(pipeline);
			return cached != nullptr;
		}

		// Six faces per level, rendered side by side into one strip-shaped
		// scratch target -- six viewports in one render pass -- and copied
		// into the destination in one call.
		//
		// The destination is a cube *or* a slice of a cube array, which is the
		// whole reason this is a function rather than the body of Prefilter:
		// filling probe slot k is the same renders with baseLayer set to 6k.
		// Nothing here reads the destination's identity beyond its size, so the
		// two callers cannot drift in the part that is hard to get right -- the
		// face basis and the row order.
		//
		// **One pass and one copy per level, not one of each per face.** The
		// first version rendered each face into its own square scratch and
		// copied it on its own: thirty-six render passes and thirty-six copies
		// for a six-level probe, each copy a pair of full-image layout
		// transitions on a cube *array* -- and on Vulkan a small pass between
		// two barriers is a GPU idling for the pass's length. The probes phase
		// read 0.77 ms against OpenGL's 0.24 for the same work. Six faces
		// across one target is the same shading in one pass, and a strip copy
		// is one blit with six regions between one pair of transitions.
		// ENGINE-NOTES 7ah.
		//
		// Sizes come from the destination, not the source. They used to be the
		// same number because a cube was always filtered into a cube of its own
		// size; an array slice may be smaller than the probe that feeds it, and
		// then the copy is a resample.
		void Convolve(RHICommandList& cmd, const Ref<RHIPipeline>& pipeline,
					  const Ref<RHITexture>& source, const Ref<RHITexture>& destination,
					  uint32_t baseLayer, uint32_t levels)
		{
			if (!pipeline || !source || !destination || levels == 0)
				return;

			const uint32_t base = destination->GetWidth();

			for (uint32_t level = 0; level < levels; level++)
			{
				const uint32_t size = Math::Max(base >> level, 1u);

				Ref<RHIRenderTarget>& scratch = s_Data->Scratch[size];
				if (!scratch)
				{
					RenderTargetDesc target;
					target.Width = size * CubeFaces::kFaceCount;
					target.Height = size;
					target.ColorAttachments = { { Format::R16G16B16A16_SFLOAT } };
					target.HasDepth = false;
					target.DebugName = "ibl.prefilter" + std::to_string(size);

					scratch = s_Data->Device->CreateRenderTarget(target);
				}

				if (!scratch)
					continue;

				// A single-level convolution -- irradiance -- has no sweep to
				// make, and dividing by levels-1 there is a division by zero.
				const float roughness = levels > 1 ? (float)level / (float)(levels - 1) : 0.0f;
				const Ref<RHITexture> rendered = scratch->GetColorTexture(0);

				RenderPassBeginInfo begin;
				begin.Target = scratch.get();
				begin.ClearColor = true;
				begin.UseDepth = false;

				cmd.BeginRenderPass(begin);

				// One set for the six faces: they read the same source. The
				// cursor is per frame, not per call -- resetting it here meant a
				// second environment filtered in the same frame rewrote sets the
				// command buffer had already bound.
				while (s_Data->SetCursor >= s_Data->Sets.size())
					s_Data->Sets.push_back(s_Data->Device->CreateResourceSet(pipeline, 0));

				Ref<RHIResourceSet>& set = s_Data->Sets[s_Data->SetCursor++];
				if (!set)
					set = s_Data->Device->CreateResourceSet(pipeline, 0);

				set->SetTexture(0, source, s_Data->Sampler);
				set->Commit();

				cmd.BindPipeline(pipeline);
				cmd.BindResourceSet(0, set);

				// The Vulkan backend renders through a negative-height viewport
				// so its picture comes out the same way up as OpenGL's; the
				// pass's own default viewport does that for the whole target,
				// and a viewport set by hand has to do it too or this backend's
				// faces come out upside down while the other's do not.
				const bool flipped = s_Data->Device->GetBackend() == Backend::Vulkan;

				for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
				{
					// This face's column of the strip. The fullscreen triangle
					// covers whatever the viewport is, and the scissor keeps its
					// clear-and-draw inside the column.
					Viewport viewport;
					viewport.X = (float)(face * size);
					viewport.Y = flipped ? (float)size : 0.0f;
					viewport.Width = (float)size;
					viewport.Height = flipped ? -(float)size : (float)size;
					cmd.SetViewport(viewport);

					Rect2D scissor;
					scissor.X = (int32_t)(face * size);
					scissor.Y = 0;
					scissor.Width = size;
					scissor.Height = size;
					cmd.SetScissor(scissor);

					PrefilterParams params;
					params.Axis = Vec4(kBasis[face].Axis, 0.0f);
					params.Right = Vec4(kBasis[face].Right, 0.0f);
					params.Up = Vec4(kBasis[face].Up, 0.0f);
					// The *source's* face size: it is what the sample-density
					// term is measured against, and it is not the destination's
					// any more.
					params.Settings = { roughness, (float)source->GetWidth(),
										(float)EnvironmentIBL::kPrefilterSamples, 0.0f };

					cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
					cmd.Draw(3);
				}

				cmd.EndRenderPass();

				// The six columns into this level's mip of the destination's six
				// faces, in one copy. The copy is where the two backends' row
				// order is reconciled, exactly as it is for a reflection probe's
				// faces.
				cmd.CopyStripToTextureLayers(rendered, destination, baseLayer,
											 CubeFaces::kFaceCount, level);
			}
		}

		uint16_t ToHalf(float value)
		{
			value = std::clamp(value, -65504.0f, 65504.0f);

			uint32_t bits = 0;
			memcpy(&bits, &value, sizeof(bits));

			const uint32_t sign = (bits >> 16) & 0x8000u;
			const int exponent = (int)((bits >> 23) & 0xffu) - 127 + 15;
			const uint32_t mantissa = bits & 0x7fffffu;

			if (exponent <= 0)  return (uint16_t)sign;
			if (exponent >= 31) return (uint16_t)(sign | 0x7bffu);

			return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
		}
	}

	uint32_t EnvironmentIBL::LevelsFor(uint32_t faceSize)
	{
		// Two levels is the least that means anything: one mirror and one
		// blur. Below that the answer really is "do not bother".
		if (faceSize < 2)
			return 0;

		uint32_t levels = 1;
		while ((faceSize >> levels) >= 1 && levels < kRoughnessLevels)
			levels++;

		return levels >= 2 ? levels : 0;
	}

	void EnvironmentIBL::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<IBLData>();
		s_Data->Device = &device;

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/prefilter.rvshader"))
			s_Data->Shader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("EnvironmentIBL: failed to compile assets/shaders/prefilter.rvshader");

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/irradiance.rvshader"))
			s_Data->IrradianceShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("EnvironmentIBL: failed to compile assets/shaders/irradiance.rvshader");

		{
			SamplerDesc sampler;
			sampler.WrapU = WrapMode::ClampToEdge;
			sampler.WrapV = WrapMode::ClampToEdge;
			sampler.WrapW = WrapMode::ClampToEdge;
			sampler.MaxLod = 16.0f;
			s_Data->Sampler = device.CreateSampler(sampler);
		}

		{
			// The BRDF table. Computed on the CPU because it depends on nothing
			// -- not the scene, not the environment, not the material -- so it
			// is the same numbers on every machine and can be checked against
			// values that are known analytically.
			const std::vector<float> table = IntegrateEnvironmentBRDF(kBRDFSize);

			TextureDesc desc;
			desc.Width = kBRDFSize;
			desc.Height = kBRDFSize;
			desc.Format = Format::R16G16_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.MipLevels = 1;
			desc.DebugName = "ibl.brdf";

			s_Data->BRDF = device.CreateTexture(desc);

			std::vector<uint16_t> halves(table.size());
			for (size_t i = 0; i < table.size(); i++)
				halves[i] = ToHalf(table[i]);

			if (s_Data->BRDF)
				s_Data->BRDF->Upload(halves.data(), halves.size() * sizeof(uint16_t));

			SamplerDesc sampler;
			// Clamped, and it matters more here than usual: the table's edges
			// are roughness 0 and grazing incidence, and wrapping either would
			// make a smooth surface sample a rough answer.
			sampler.WrapU = WrapMode::ClampToEdge;
			sampler.WrapV = WrapMode::ClampToEdge;
			sampler.MaxLod = 0.0f;
			s_Data->BRDFSampler = device.CreateSampler(sampler);
		}

		s_Data->Ready = s_Data->Shader != nullptr && s_Data->BRDF != nullptr;

		// Reported honestly. Announcing readiness unconditionally is how a
		// failed shader compile ended up looking like a working feature that
		// silently did nothing.
		if (s_Data->Ready)
		{
			RV_CORE_INFO("Environment IBL ready ({0} roughness levels, {1}x{1} BRDF table)",
						 kRoughnessLevels, kBRDFSize);
		}
		else
		{
			RV_CORE_ERROR("Environment IBL unavailable; reflections fall back to the "
						  "box-filtered mip chain");
		}
	}

	void EnvironmentIBL::BeginFrame()
	{
		if (s_Data)
			s_Data->SetCursor = 0;
	}

	void EnvironmentIBL::Shutdown()
	{
		s_Data.reset();
	}

	bool EnvironmentIBL::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	Ref<RHITexture> EnvironmentIBL::GetBRDF()
	{
		return s_Data ? s_Data->BRDF : nullptr;
	}

	Ref<RHISampler> EnvironmentIBL::GetBRDFSampler()
	{
		return s_Data ? s_Data->BRDFSampler : nullptr;
	}

	void EnvironmentIBL::ClearCache()
	{
		if (s_Data)
			s_Data->Prefiltered.clear();
	}

	Ref<RHITexture> EnvironmentIBL::GetPrefiltered(const Ref<RHITexture>& source)
	{
		if (!s_Data || !source)
			return source;

		const auto it = s_Data->Prefiltered.find(source.get());
		return it != s_Data->Prefiltered.end() && it->second ? it->second : source;
	}

	void EnvironmentIBL::Invalidate(const Ref<RHITexture>& source)
	{
		if (s_Data && source)
			s_Data->Stale.insert(source.get());
	}

	bool EnvironmentIBL::PrefilterInto(RHICommandList& cmd, const Ref<RHITexture>& source,
									   const Ref<RHITexture>& destination, uint32_t slot)
	{
		if (!s_Data || !s_Data->Ready || !source || !destination)
			return false;

		if (!EnsurePipeline(s_Data->Shader, s_Data->Pipeline, "EnvironmentIBL.prefilter"))
			return false;

		// The destination's levels, not the source's. A slice is allocated with
		// however many roughness levels the array carries, and filling fewer
		// would leave the roughest ones holding whatever the previous occupant
		// of that slot left behind.
		const uint32_t levels = destination->GetDesc().MipLevels;

		cmd.PushDebugGroup("Prefilter probe");
		Convolve(cmd, s_Data->Pipeline, source, destination,
				 slot * CubeFaces::kFaceCount, levels);
		cmd.PopDebugGroup();

		return true;
	}

	bool EnvironmentIBL::IrradianceInto(RHICommandList& cmd, const Ref<RHITexture>& source,
										const Ref<RHITexture>& destination, uint32_t slot)
	{
		if (!s_Data || !s_Data->Ready || !source || !destination)
			return false;

		if (!EnsurePipeline(s_Data->IrradianceShader, s_Data->IrradiancePipeline,
							"EnvironmentIBL.irradiance"))
			return false;

		// One level. Irradiance has no roughness to sweep -- it is the whole
		// hemisphere already, and a mip chain over it would be a blur of a blur.
		cmd.PushDebugGroup("Convolve probe irradiance");
		Convolve(cmd, s_Data->IrradiancePipeline, source, destination,
				 slot * CubeFaces::kFaceCount, 1);
		cmd.PopDebugGroup();

		return true;
	}

	Ref<RHITexture> EnvironmentIBL::Prefilter(RHICommandList& cmd, const Ref<RHITexture>& source)
	{
		if (!s_Data || !s_Data->Ready || !source)
			return source;

		const bool stale = s_Data->Stale.erase(source.get()) != 0;

		Ref<RHITexture> existing;
		if (const auto it = s_Data->Prefiltered.find(source.get()); it != s_Data->Prefiltered.end())
		{
			// Known to be unfilterable, and that does not change with time.
			if (!it->second)
				return source;

			if (!stale)
				return it->second;

			existing = it->second;
		}

		const uint32_t base = source->GetWidth();
		const uint32_t levels = LevelsFor(base);
		if (levels == 0)
		{
			// Genuinely too small to filter. Reported rather than passed over
			// in silence: falling back to the box-filtered chain is the same
			// thing as not having image-based specular at all, and the whole
			// point of this module knowing whether it is ready was to stop
			// that happening quietly.
			RV_CORE_WARN("Environment of {0} px faces is too small to prefilter; "
						 "its reflections use the box-filtered chain", base);
			s_Data->Prefiltered[source.get()] = nullptr;
			return source;
		}

		TextureDesc desc;
		desc.Width = base;
		desc.Height = base;
		desc.Layers = CubeFaces::kFaceCount;
		desc.Type = TextureType::TextureCube;
		desc.Format = Format::R16G16B16A16_SFLOAT;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;
		desc.MipLevels = levels;
		desc.DebugName = "ibl.prefiltered";

		// Refilled rather than reallocated when a probe re-captures: a new cube
		// every six frames would be a leak with extra steps.
		Ref<RHITexture> destination = existing ? existing : s_Data->Device->CreateTexture(desc);
		if (!destination)
		{
			s_Data->Prefiltered[source.get()] = nullptr;
			return source;
		}

		if (!EnsurePipeline(s_Data->Shader, s_Data->Pipeline, "EnvironmentIBL.prefilter"))
		{
			s_Data->Prefiltered[source.get()] = nullptr;
			return source;
		}

		cmd.PushDebugGroup("Prefilter environment");
		Convolve(cmd, s_Data->Pipeline, source, destination, 0, levels);
		cmd.PopDebugGroup();

		s_Data->Prefiltered[source.get()] = destination;

		// Only the first time. A realtime probe refilters every six frames and
		// would otherwise fill the log with it.
		if (!existing)
		{
			RV_CORE_INFO("Prefiltered an environment map ({0} levels from {1} px faces)",
						 levels, base);
		}

		return destination;
	}
}
