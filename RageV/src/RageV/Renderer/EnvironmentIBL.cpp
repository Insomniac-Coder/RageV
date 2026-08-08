#include <rvpch.h>
#include "EnvironmentIBL.h"
#include "Renderer.h"
#include "Cubemap.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include <map>

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
			glm::vec4 Axis{ 0.0f };
			glm::vec4 Right{ 0.0f };
			glm::vec4 Up{ 0.0f };
			// x = roughness, y = source face size, z = sample count
			glm::vec4 Settings{ 0.0f };
		};

		// CubeFaceDirection(face, u, v) = normalize(Axis + Right * s + Up * t)
		// with s = 2u-1 and t = 2v-1. Kept beside the table it mirrors.
		struct FaceBasis { glm::vec3 Axis, Right, Up; };

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

			Ref<RHITexture> BRDF;
			Ref<RHISampler> BRDFSampler;

			// One scratch target per roughness level, since each is a different
			// size and a render target may not be resized while a command
			// buffer holds it.
			std::array<Ref<RHIRenderTarget>, EnvironmentIBL::kRoughnessLevels> Scratch;
			uint32_t ScratchBase = 0;

			// Descriptor sets, one per face per level, so none is rewritten
			// while still bound.
			std::vector<Ref<RHIResourceSet>> Sets;

			// Keyed on the source texture: a scene keeps one environment and
			// rebuilding it every frame would be thirty-six renders a frame.
			std::map<const RHITexture*, Ref<RHITexture>> Prefiltered;

			bool Ready = false;
		};

		std::unique_ptr<IBLData> s_Data;

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

	void EnvironmentIBL::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<IBLData>();
		s_Data->Device = &device;

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/prefilter.rvshader"))
			s_Data->Shader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("EnvironmentIBL: failed to compile assets/shaders/prefilter.rvshader");

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

	Ref<RHITexture> EnvironmentIBL::Prefilter(RHICommandList& cmd, const Ref<RHITexture>& source)
	{
		if (!s_Data || !s_Data->Ready || !source)
			return source;

		if (const auto it = s_Data->Prefiltered.find(source.get()); it != s_Data->Prefiltered.end())
			return it->second ? it->second : source;

		const uint32_t base = source->GetWidth();
		if (base < (1u << kRoughnessLevels))
		{
			// Too small to have a level per roughness step. The gradient sky's
			// 32-pixel cube lands here, and its box-filtered chain is a fine
			// answer for something with no detail in it.
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
		desc.MipLevels = kRoughnessLevels;
		desc.DebugName = "ibl.prefiltered";

		Ref<RHITexture> destination = s_Data->Device->CreateTexture(desc);
		if (!destination)
		{
			s_Data->Prefiltered[source.get()] = nullptr;
			return source;
		}

		// Scratch targets, one per level. Rebuilt when the base size changes,
		// which is once per project in practice.
		if (s_Data->ScratchBase != base)
		{
			for (auto& target : s_Data->Scratch)
				target.reset();
			s_Data->ScratchBase = base;
		}

		if (!s_Data->Pipeline)
		{
			GraphicsPipelineDesc pipeline;
			pipeline.Name = "EnvironmentIBL.prefilter";
			pipeline.Shader = s_Data->Shader;
			pipeline.Topology = PrimitiveTopology::TriangleList;
			pipeline.Rasterizer.Cull = CullMode::None;
			pipeline.DepthStencil.DepthTestEnable = false;
			pipeline.DepthStencil.DepthWriteEnable = false;
			pipeline.Blend = BlendPreset::Opaque;
			pipeline.ColorFormats = { Format::R16G16B16A16_SFLOAT };
			pipeline.DepthFormat = Format::Undefined;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(pipeline);
		}

		if (!s_Data->Pipeline)
		{
			s_Data->Prefiltered[source.get()] = nullptr;
			return source;
		}

		cmd.PushDebugGroup("Prefilter environment");

		uint32_t setCursor = 0;

		for (uint32_t level = 0; level < kRoughnessLevels; level++)
		{
			const uint32_t size = glm::max(base >> level, 1u);

			if (!s_Data->Scratch[level])
			{
				RenderTargetDesc target;
				target.Width = size;
				target.Height = size;
				target.ColorAttachments = { { Format::R16G16B16A16_SFLOAT } };
				target.HasDepth = false;
				target.DebugName = "ibl.prefilter" + std::to_string(level);

				s_Data->Scratch[level] = s_Data->Device->CreateRenderTarget(target);
			}

			if (!s_Data->Scratch[level])
				continue;

			const float roughness = (float)level / (float)(kRoughnessLevels - 1);
			const Ref<RHITexture> rendered = s_Data->Scratch[level]->GetColorTexture(0);

			for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			{
				RenderPassBeginInfo begin;
				begin.Target = s_Data->Scratch[level].get();
				begin.ClearColor = true;
				begin.UseDepth = false;

				cmd.BeginRenderPass(begin);

				if (setCursor >= s_Data->Sets.size())
					s_Data->Sets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0));

				Ref<RHIResourceSet>& set = s_Data->Sets[setCursor++];
				if (!set)
					set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

				set->SetTexture(0, source, s_Data->Sampler);
				set->Commit();

				PrefilterParams params;
				params.Axis = glm::vec4(kBasis[face].Axis, 0.0f);
				params.Right = glm::vec4(kBasis[face].Right, 0.0f);
				params.Up = glm::vec4(kBasis[face].Up, 0.0f);
				params.Settings = { roughness, (float)base, (float)kPrefilterSamples, 0.0f };

				cmd.BindPipeline(s_Data->Pipeline);
				cmd.BindResourceSet(0, set);
				cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
				cmd.Draw(3);

				cmd.EndRenderPass();

				// Into this level's mip of the destination. The copy is where
				// the two backends' row order is reconciled, exactly as it is
				// for a reflection probe's faces.
				cmd.CopyToTextureLayer(rendered, destination, face, level);
			}
		}

		cmd.PopDebugGroup();

		s_Data->Prefiltered[source.get()] = destination;
		RV_CORE_INFO("Prefiltered an environment map ({0} levels from {1} px faces)",
					 kRoughnessLevels, base);

		return destination;
	}
}
