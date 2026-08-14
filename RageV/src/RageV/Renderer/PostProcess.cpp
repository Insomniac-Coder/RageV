#include <rvpch.h>
#include "PostProcess.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include <array>
#include <map>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Push-constant block. One shape for every pass, so the dispatch path
		// does not need to know which shader it is feeding -- unused fields
		// cost four bytes each and save a branch per pass.
		struct PostParams
		{
			Vec2 TexelSize{ 0.0f };
			float A = 0.0f;
			float B = 0.0f;
			float C = 0.0f;

			// Whether sampling has to flip vertically. Filled by Dispatch, never
			// by a caller, because it is a property of the backend and not of
			// the effect. See the note there.
			float FlipY = 0.0f;
		};

		const char* ShaderPath(int shader)
		{
			switch (shader)
			{
				case 0: return "assets/shaders/bloom_prefilter.rvshader";
				case 1: return "assets/shaders/bloom_downsample.rvshader";
				case 2: return "assets/shaders/bloom_upsample.rvshader";
				case 3: return "assets/shaders/tonemap.rvshader";
				case 4: return "assets/shaders/fxaa.rvshader";
				case 5: return "assets/shaders/blit.rvshader";
				case 6: return "assets/shaders/smaa_edges.rvshader";
				case 7: return "assets/shaders/smaa_weights.rvshader";
				case 8: return "assets/shaders/smaa_blend.rvshader";
				default: return "assets/shaders/ssaa_resolve.rvshader";
			}
		}

		struct PostData
		{
			RHIDevice* Device = nullptr;

			// One per Shader::Count. Not spelled with the enum because that is
			// private to PostProcess and this struct is not.
			std::array<Ref<RHIShader>, 10> Shaders;

			// Keyed by shader and output format: a pipeline bakes the format it
			// renders into, and this chain writes an HDR one then an LDR one.
			std::map<std::pair<int, Format>, Ref<RHIPipeline>> Pipelines;

			Ref<RHISampler> Sampler;
			Ref<RHISampler> PointSampler;

			// One set per draw, not one reused: a descriptor set that is
			// already bound must not be rewritten, and the bloom chain binds a
			// different source a dozen times in a frame. Pooled per frame in
			// flight and reset by BeginFrame.
			std::vector<std::vector<Ref<RHIResourceSet>>> Sets;
			uint32_t SetCursor = 0;

			// Stands in for the bloom texture when bloom is off. The tonemap
			// shader declares two samplers unconditionally, so the binding has
			// to be filled with something -- and black adds nothing.
			Ref<RHITexture> Black;

			bool Ready = false;
		};

		std::unique_ptr<PostData> s_Data;
	}

	void PostProcess::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<PostData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		bool ok = true;
		for (int i = 0; i < (int)Shader::Count; i++)
		{
			auto compiled = ShaderCompiler::CompileFromFile(ShaderPath(i));
			if (!compiled)
			{
				RV_CORE_ERROR("PostProcess: failed to compile {0}", ShaderPath(i));
				ok = false;
				continue;
			}
			s_Data->Shaders[i] = device.CreateShader(*compiled);
		}

		SamplerDesc sampler;
		// Clamped, because every one of these filters reads outside the image
		// at its edges and wrapping there produces a bright rim.
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.WrapW = WrapMode::ClampToEdge;
		sampler.MaxLod = 0.0f;
		s_Data->Sampler = device.CreateSampler(sampler);

		// SMAA's edge and weight maps hold classifications rather than colours.
		// Filtering them produces values that are not any of the things they
		// encode -- half an edge flag is not "half an edge", it is nothing.
		sampler.MinFilter = FilterMode::Nearest;
		sampler.MagFilter = FilterMode::Nearest;
		sampler.Mipmap = MipmapMode::Nearest;
		s_Data->PointSampler = device.CreateSampler(sampler);

		{
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = Format::R8G8B8A8_UNORM;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = "PostProcess.black";
			s_Data->Black = device.CreateTexture(desc);
			const uint32_t black = 0xff000000;
			s_Data->Black->Upload(&black, sizeof(black));
		}

		s_Data->Sets.resize(device.GetFramesInFlight());
		s_Data->Ready = ok;

		// Conditional, because `ok` is false when any of the six shaders failed
		// and the errors above scroll past. Announcing readiness anyway is how
		// a broken post chain looks like a working one that draws nothing.
		if (ok)
			RV_CORE_INFO("PostProcess ready (bloom, ACES tonemap, FXAA, SMAA, SSAA)");
		else
			RV_CORE_ERROR("PostProcess unavailable; the frame will not be tone mapped");
	}

	void PostProcess::Shutdown()
	{
		s_Data.reset();
	}

	void PostProcess::BeginFrame()
	{
		if (s_Data)
			s_Data->SetCursor = 0;
	}

	bool PostProcess::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void PostProcess::Dispatch(RHICommandList& cmd, Shader shader, Format outputFormat,
							   const Ref<RHITexture>& first, const Ref<RHITexture>& second,
							   const void* params, uint32_t paramSize,
							   Sampling firstSampling, Sampling secondSampling)
	{
		if (!s_Data || !s_Data->Ready || !first)
			return;

		const int index = (int)shader;
		const auto key = std::make_pair(index, outputFormat);

		auto it = s_Data->Pipelines.find(key);
		if (it == s_Data->Pipelines.end())
		{
			GraphicsPipelineDesc desc;
			desc.Name = "PostProcess";
			desc.Shader = s_Data->Shaders[index];
			desc.Topology = PrimitiveTopology::TriangleList;
			desc.Rasterizer.Cull = CullMode::None;

			// The upsample accumulates onto what the level already holds, which
			// is what lets the chain use one target per level instead of two.
			desc.Blend = shader == Shader::Upsample ? BlendPreset::Additive
													: BlendPreset::Opaque;

			// A fullscreen pass has nothing to test against and nothing worth
			// writing. Declaring no depth also frees the caller from having to
			// give these passes a depth attachment.
			desc.DepthStencil.DepthTestEnable = false;
			desc.DepthStencil.DepthWriteEnable = false;

			desc.ColorFormats = { outputFormat };
			desc.DepthFormat = Format::Undefined;

			it = s_Data->Pipelines.emplace(key, s_Data->Device->CreatePipeline(desc)).first;
		}

		const Ref<RHIPipeline>& pipeline = it->second;
		if (!pipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& sets = s_Data->Sets[frame];

		if (s_Data->SetCursor >= sets.size())
			sets.push_back(s_Data->Device->CreateResourceSet(pipeline, 0));

		// A set built against one pipeline layout cannot be bound to another.
		// The layouts here are identical in shape, but the handle is not, so
		// the set is rebuilt when the pipeline it was made for is not this one.
		Ref<RHIResourceSet>& set = sets[s_Data->SetCursor++];
		if (!set)
			set = s_Data->Device->CreateResourceSet(pipeline, 0);

		const auto samplerFor = [](Sampling sampling)
		{
			return sampling == Sampling::Point ? s_Data->PointSampler : s_Data->Sampler;
		};

		set->SetTexture(0, first, samplerFor(firstSampling));

		// Only when the shader actually declares a second binding. Writing one
		// it does not have is not a harmless extra -- the layout has a single
		// binding, so the write is out of range and the driver takes it badly.
		if (second)
			set->SetTexture(1, second, samplerFor(secondSampling));

		set->Commit();

		cmd.BindPipeline(pipeline);
		cmd.BindResourceSet(0, set);

		// A fullscreen pass reads one render target and writes another, and the
		// two backends do not store a rendered image the same way round.
		//
		// Vulkan's first framebuffer row is the top of the image; OpenGL's is
		// the bottom. The RHI hides that everywhere else by giving Vulkan a
		// negative-height viewport, so geometry lands in the same place on both
		// -- but that fixes where a fragment *writes*, not where a texture
		// coordinate *reads*. A fragment at the top of the destination samples
		// v = 1, which is the last row of the source: the bottom on Vulkan and
		// the top on OpenGL. So Vulkan flips and OpenGL does not.
		//
		// An even number of passes hides it, which is why this survived: with
		// anti-aliasing on, the scene goes through tonemap and FXAA and comes
		// out the right way up. The bloom chain has an odd number, and its
		// contribution was being added upside down -- visible only once
		// something in the scene was bright enough to bleed, as a set of blobs
		// mirrored about the middle of the image.
		PostParams local{};
		if (params && paramSize >= sizeof(PostParams))
		{
			memcpy(&local, params, sizeof(PostParams));
			local.FlipY = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
			cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(PostParams), &local);
		}
		else if (params && paramSize > 0)
		{
			cmd.PushConstants(ShaderStage::Fragment, 0, paramSize, params);
		}

		// Three vertices, no buffer: the vertex shader builds the triangle from
		// gl_VertexIndex.
		cmd.Draw(3);
	}

	void PostProcess::Prefilter(RHICommandList& cmd, const Ref<RHITexture>& source,
								uint32_t width, uint32_t height, Format outputFormat,
								float threshold, float knee, float clamp)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u) };
		params.A = threshold;
		params.B = knee;
		params.C = Math::Max(clamp, threshold);
		Dispatch(cmd, Shader::Prefilter, outputFormat, source, nullptr, &params, sizeof(params));
	}

	void PostProcess::Downsample(RHICommandList& cmd, const Ref<RHITexture>& source,
								 uint32_t sourceWidth, uint32_t sourceHeight, Format outputFormat)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(sourceWidth, 1u),
							 1.0f / (float)Math::Max(sourceHeight, 1u) };
		Dispatch(cmd, Shader::Downsample, outputFormat, source, nullptr, &params, sizeof(params));
	}

	void PostProcess::Upsample(RHICommandList& cmd, const Ref<RHITexture>& source,
							   uint32_t sourceWidth, uint32_t sourceHeight,
							   Format outputFormat, float radius)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(sourceWidth, 1u),
							 1.0f / (float)Math::Max(sourceHeight, 1u) };
		params.A = radius;
		Dispatch(cmd, Shader::Upsample, outputFormat, source, nullptr, &params, sizeof(params));
	}

	void PostProcess::Tonemap(RHICommandList& cmd, const Ref<RHITexture>& scene,
							  const Ref<RHITexture>& bloom, Format outputFormat,
							  float exposure, float bloomIntensity)
	{
		PostParams params;
		params.TexelSize = { exposure, bloomIntensity };
		// Never null: the shader declares the binding whether or not bloom ran.
		Dispatch(cmd, Shader::Tonemap, outputFormat, scene,
				 bloom ? bloom : s_Data->Black, &params, sizeof(params));
	}

	void PostProcess::FXAA(RHICommandList& cmd, const Ref<RHITexture>& source,
						   uint32_t width, uint32_t height, Format outputFormat,
						   float contrastThreshold, float relativeThreshold)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u) };
		params.A = contrastThreshold;
		params.B = relativeThreshold;
		Dispatch(cmd, Shader::FXAA, outputFormat, source, nullptr, &params, sizeof(params));
	}

	void PostProcess::SmaaEdges(RHICommandList& cmd, const Ref<RHITexture>& source,
								uint32_t width, uint32_t height, Format outputFormat,
								float threshold, float localContrast)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u) };
		params.A = threshold;
		params.B = localContrast;
		// Point: every tap is a whole number of texels away, so filtering
		// would only add rounding.
		Dispatch(cmd, Shader::SmaaEdges, outputFormat, source, nullptr, &params, sizeof(params),
				 Sampling::Point);
	}

	void PostProcess::SmaaWeights(RHICommandList& cmd, const Ref<RHITexture>& edges,
								  uint32_t width, uint32_t height, Format outputFormat)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u) };
		Dispatch(cmd, Shader::SmaaWeights, outputFormat, edges, nullptr, &params, sizeof(params),
				 Sampling::Point);
	}

	void PostProcess::SmaaBlend(RHICommandList& cmd, const Ref<RHITexture>& source,
								const Ref<RHITexture>& weights,
								uint32_t width, uint32_t height, Format outputFormat)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u) };
		// The colour is read *between* texels -- that offset tap is where the
		// blending actually happens, and the filter is what performs it. The
		// weights beside it are read exactly.
		Dispatch(cmd, Shader::SmaaBlend, outputFormat, source, weights, &params, sizeof(params),
				 Sampling::Linear, Sampling::Point);
	}

	void PostProcess::SsaaResolve(RHICommandList& cmd, const Ref<RHITexture>& source,
								  uint32_t sourceWidth, uint32_t sourceHeight,
								  Format outputFormat, int factor)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(sourceWidth, 1u),
							 1.0f / (float)Math::Max(sourceHeight, 1u) };
		params.A = (float)factor;
		// Point, because every tap is aimed at a source pixel centre. A linear
		// sampler would give the same answer for an even factor and quietly
		// widen the footprint for an odd one.
		Dispatch(cmd, Shader::SsaaResolve, outputFormat, source, nullptr, &params, sizeof(params),
				 Sampling::Point);
	}

	void PostProcess::Blit(RHICommandList& cmd, const Ref<RHITexture>& source, Format outputFormat)
	{
		PostParams params;
		Dispatch(cmd, Shader::Blit, outputFormat, source, nullptr, &params, sizeof(params));
	}
}
