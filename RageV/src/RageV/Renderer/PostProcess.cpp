#include <cstddef>
#include <rvpch.h>
#include "PostProcess.h"
#include "Renderer.h"
#include "RayCounters.h"
// The stand-in cube the fog binds when a scene has no sky to take its colour
// from (WR-3). A declared binding with nothing in it is undefined on both
// backends, so "no sky" has to be a black cube rather than a null.
#include "TextureLoader.h"
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

		// The tonemap wants more than the shared block carries, and it is the
		// only pass that does. Laid out as PostParams *followed by* its own
		// fields rather than as a separate struct, so Dispatch can still find
		// FlipY where it has always been and nothing else has to change.
		// ENGINE-NOTES 7w.
		struct TonemapParams
		{
			PostParams Base;

			float Aberration = 0.0f;
			float Vignette = 0.0f;
			float VignetteSmoothness = 0.5f;
			float Grain = 0.0f;
			float GrainSize = 1.0f;
			float Frame = 0.0f;

			// Whether to take the exposure from the buffer at binding 3
			// instead of from Base. Zero keeps the manual value exactly.
			float AutoExposure = 0.0f;
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
				case 9: return "assets/shaders/ssaa_resolve.rvshader";
				case 10: return "assets/shaders/taa_resolve.rvshader";
				case 11: return "assets/shaders/dof_prepass.rvshader";
				case 12: return "assets/shaders/dof_gather.rvshader";
				case 13: return "assets/shaders/dof_composite.rvshader";
				case 14: return "assets/shaders/motionblur_pack.rvshader";
				case 15: return "assets/shaders/motionblur_tilemax.rvshader";
				case 16: return "assets/shaders/motionblur_neighbormax.rvshader";
				case 17: return "assets/shaders/motionblur_gather.rvshader";
				case 18: return "assets/shaders/ssao_compute.rvshader";
				case 19: return "assets/shaders/ssao_blur.rvshader";
				case 20: return "assets/shaders/ssao_apply.rvshader";
				case 21: return "assets/shaders/ssr_trace.rvshader";
				case 22: return "assets/shaders/ssr_resolve.rvshader";
				case 23: return "assets/shaders/ssr_hiz.rvshader";
				case 24: return "assets/shaders/rtao_compute.rvshader";
				case 25: return "assets/shaders/ssgi_compute.rvshader";
				case 26: return "assets/shaders/ssgi_blur.rvshader";
				case 27: return "assets/shaders/gi_denoise.rvshader";
				case 29: return "assets/shaders/importance_tiles.rvshader";
				case 30: return "assets/shaders/tile_reduce.rvshader";
				case 31: return "assets/shaders/tile_budget.rvshader";
				case 32: return "assets/shaders/water_backdrop.rvshader";
				case 33: return "assets/shaders/debug_view.rvshader";
				default: return "assets/shaders/fog.rvshader";
			}
		}

		// One pooled descriptor set, and the pipeline it was built for.
		struct PooledSet
		{
			Ref<RHIResourceSet> Set;
			// Compared, never dereferenced. Raw because the pipeline is owned
			// by the pipeline map and outlives the pool.
			const RHIPipeline*  Pipeline = nullptr;
		};

		struct PostData
		{
			RHIDevice* Device = nullptr;

			// One per Shader::Count. Not spelled with the enum because that is
			// private to PostProcess and this struct is not -- so the number is
			// asserted against it in Init instead, where the enum is in scope.
			std::array<Ref<RHIShader>, 34> Shaders;

			// Keyed by shader and output format: a pipeline bakes the format it
			// renders into, and this chain writes an HDR one then an LDR one.
			// The second format is Undefined for every pass but the one that
			// writes two attachments, so those keys are unchanged.
			std::map<std::tuple<int, Format, Format>, Ref<RHIPipeline>> Pipelines;

			Ref<RHISampler> Sampler;
			Ref<RHISampler> PointSampler;

			// One set per draw, not one reused: a descriptor set that is
			// already bound must not be rewritten, and the bloom chain binds a
			// different source a dozen times in a frame. Pooled per frame in
			// flight and reset by BeginFrame.
			//
			// **Paired with the pipeline it was built for**, and that is not
			// bookkeeping -- it is the whole correctness of the pool. A set
			// knows its pipeline's binding layout, and the shaders here do not
			// all have the same one: most declare a single texture, tone
			// mapping and SMAA's blend declare two, the temporal resolve
			// declares three. Slot 4 of the pool is whichever dispatch happens
			// to be fourth, and *that changes when the anti-aliasing mode
			// changes*, because the modes contribute different numbers of
			// passes.
			//
			// Reused across a layout change, a set silently drops every
			// binding the old layout did not have -- so tone mapping loses its
			// bloom texture and samples whatever is still bound in its place.
			// The symptom is a dark scene with enormous bloom, and it appears
			// only when somebody switches mode with the application running,
			// which is why every check here missed it: they each start in one
			// mode and exit.
			std::vector<std::vector<PooledSet>> Sets;
			uint32_t SetCursor = 0;

			// Stands in for the bloom texture when bloom is off. The tonemap
			// shader declares two samplers unconditionally, so the binding has
			// to be filled with something -- and black adds nothing.
			Ref<RHITexture> Black;

			// And for the grading LUT, which the tonemap shader also declares
			// unconditionally. A 2-cube identity rather than a black one: if
			// the shader's size guard were ever wrong, sampling this grades
			// nothing, where sampling black would turn the frame black -- so
			// the stand-in fails in the direction that stays debuggable.
			Ref<RHITexture> IdentityLut;
			Ref<RHIBuffer> UnitExposure;

			bool Ready = false;
		};

		std::unique_ptr<PostData> s_Data;
	}

	void PostProcess::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<PostData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		static_assert((int)Shader::Count <= 34,
					  "PostData::Shaders is too small; grow it with the enum");

		bool ok = true;
		for (int i = 0; i < (int)Shader::Count; i++)
		{
			// The ray-traced occlusion pass declares an acceleration
			// structure, which a backend without ray queries can neither
			// cross-compile nor bind: left null there, and its entry point
			// declines. Nothing asks for it on such a device.
			if (i == (int)Shader::RtaoCompute && !device.GetCaps().SupportsRayQuery)
				continue;

			// The temporal resolve counts the pixels that reused history
			// into the ray counters (WR-16 S0) -- subgroup ops and a
			// fragment atomic -- only where the counters exist, which is
			// the ray-query path. The OpenGL compile is the shader as it was.
			std::vector<std::string> defines;
			if (i == (int)Shader::TaaResolve && device.GetCaps().SupportsRayQuery)
				defines.push_back("RV_RAY_COUNTERS");

			auto compiled = ShaderCompiler::CompileFromFile(ShaderPath(i), defines);
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

		{
			TextureDesc desc;
			desc.Type = TextureType::Texture3D;
			desc.Width = desc.Height = desc.Depth = 2;
			desc.Format = Format::R16G16B16A16_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = "PostProcess.identityLut";
			s_Data->IdentityLut = device.CreateTexture(desc);

			// The eight corners of the colour cube, red fastest -- the same
			// order a `.cube` uses. Written as halves by hand rather than
			// through the asset layer, because PostProcess must come up before
			// any project is open.
			constexpr uint16_t kZero = 0x0000;   // 0.0
			constexpr uint16_t kOne  = 0x3C00;   // 1.0
			const uint16_t identity[8 * 4] = {
				kZero, kZero, kZero, kOne,   kOne,  kZero, kZero, kOne,
				kZero, kOne,  kZero, kOne,   kOne,  kOne,  kZero, kOne,
				kZero, kZero, kOne,  kOne,   kOne,  kZero, kOne,  kOne,
				kZero, kOne,  kOne,  kOne,   kOne,  kOne,  kOne,  kOne,
			};
			s_Data->IdentityLut->UploadLayer(identity, sizeof(identity), 0);
		}

		// What fills the exposure binding when auto exposure is off, for the
		// same reason the identity volume fills the LUT binding: the tonemap
		// shader declares it unconditionally, and a declared binding with
		// nothing bound is undefined behaviour rather than a helpful zero.
		//
		// It holds an exposure of 1, which the shader never reads -- it
		// branches on the auto-exposure flag rather than multiplying by this,
		// so that "off" stays exact rather than merely arithmetically
		// harmless. The value is here so that a bug in that branch produces
		// the unmodified picture instead of a black one.
		{
			struct { float AdaptedLog, Exposure, MeasuredLog, Pad; } unit
				{ 0.0f, 1.0f, 0.0f, 0.0f };

			BufferDesc desc;
			desc.Size = sizeof(unit);
			desc.Usage = BufferUsage::Storage | BufferUsage::TransferDst;
			desc.Memory = MemoryDomain::DeviceLocal;
			desc.DebugName = "PostProcess.unitexposure";
			s_Data->UnitExposure = device.CreateBuffer(desc);

			if (s_Data->UnitExposure)
				s_Data->UnitExposure->Upload(&unit, sizeof(unit));
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
							   Sampling firstSampling, Sampling secondSampling,
							   const Ref<RHITexture>& third, Sampling thirdSampling,
							   const Ref<RHITexture>& fourth, Sampling fourthSampling,
							   const Ref<RHIBuffer>& storage,
							   const Ref<RHIAccelerationStructure>& structure,
							   Format secondOutputFormat,
							   const Ref<RHIBuffer>& counters)
	{
		if (!s_Data || !s_Data->Ready || !first)
			return;
		if (!s_Data->Shaders[(int)shader])
			return;

		const int index = (int)shader;
		const auto key = std::make_tuple(index, outputFormat, secondOutputFormat);

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
			// **A second attachment, for a pass that has to remember more than
			// a colour.** The GI denoiser writes its sample counter and
			// luminance moments here; every other pass leaves this Undefined
			// and gets exactly the single-attachment pipeline it had.
			if (secondOutputFormat != Format::Undefined)
				desc.ColorFormats.push_back(secondOutputFormat);
			desc.DepthFormat = Format::Undefined;

			it = s_Data->Pipelines.emplace(key, s_Data->Device->CreatePipeline(desc)).first;
		}

		const Ref<RHIPipeline>& pipeline = it->second;
		if (!pipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& sets = s_Data->Sets[frame];

		while (s_Data->SetCursor >= sets.size())
			sets.push_back({});

		// A set built against one pipeline layout cannot serve another, and
		// the layouts here are *not* all the same shape -- see PooledSet.
		// Rebuilt when the slot holds a set made for a different pipeline,
		// which is what happens on the frame the anti-aliasing mode changes.
		//
		// Safe to replace here: the pool is per frame in flight, and the
		// cursor is reset by BeginFrame, which runs after the fence for this
		// frame index has been waited on. Nothing in flight still refers to
		// the set being dropped.
		PooledSet& slot = sets[s_Data->SetCursor++];
		if (!slot.Set || slot.Pipeline != pipeline.get())
		{
			slot.Set = s_Data->Device->CreateResourceSet(pipeline, 0);
			slot.Pipeline = pipeline.get();
		}

		const Ref<RHIResourceSet>& set = slot.Set;

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

		if (third)
			set->SetTexture(2, third, samplerFor(thirdSampling));

		// Slot 3 is a texture *or* the exposure buffer, and never both: the
		// layout is the shader's, so the only thing that could go wrong is one
		// shader declaring both, and none does. SSGI's gather is the one pass
		// that wants a fourth image (ENGINE-NOTES 7ay).
		if (fourth)
			set->SetTexture(3, fourth, samplerFor(fourthSampling));

		// Auto exposure's answer, and never null for a shader that declares it
		// -- the caller passes a unit buffer when the feature is off, for the
		// same reason the identity LUT fills binding 2: a declared binding with
		// nothing bound is undefined behaviour, not a zero.
		if (storage)
			set->SetStorageBuffer(3, storage);

		// The frame's acceleration structure, for the one pass that traces.
		if (structure)
			set->SetAccelerationStructure(4, structure);

		// The counters, for the passes that count and the one that draws
		// them (WR-16 S0). Only when the caller passed one, which it does
		// only for a shader that declares the binding.
		if (counters)
			set->SetStorageBuffer(RayCounters::kPostBinding, counters);

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
		// The whole blob is forwarded, not just the shared header: the tonemap
		// passes a longer one. FlipY is patched where PostParams puts it,
		// which is why anything larger has to *begin* with a PostParams.
		//
		// 128 bytes because that is the push-constant size Vulkan guarantees;
		// a pass wanting more than that needs a uniform buffer rather than a
		// bigger array here, and clamping says so at the point it would break.
		alignas(16) uint8_t local[128];
		if (params && paramSize >= sizeof(PostParams))
		{
			const uint32_t size = paramSize < sizeof(local) ? paramSize
															: (uint32_t)sizeof(local);
			memcpy(local, params, size);

			const float flip = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
			memcpy(local + offsetof(PostParams, FlipY), &flip, sizeof(float));

			cmd.PushConstants(ShaderStage::Fragment, 0, size, local);
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
							  float exposure, float bloomIntensity,
							  const Ref<RHITexture>& lut, uint32_t lutSize,
							  float lutStrength, const LensParams& lens)
	{
		if (!s_Data)
			return;

		// A LUT that failed to load, or none at all, grades nothing -- but the
		// binding is still filled, because a declared sampler with nothing
		// bound is undefined behaviour rather than a black texture. The
		// identity volume is what fills it, and the size of 0 is what stops
		// the shader sampling it.
		const bool grading = lut && lutSize >= 2;

		TonemapParams params;
		params.Base.TexelSize = { exposure, bloomIntensity };
		params.Base.A = grading ? (float)lutSize : 0.0f;
		params.Base.B = lutStrength;

		params.Aberration = lens.Aberration;
		params.Vignette = lens.Vignette;
		params.VignetteSmoothness = lens.VignetteSmoothness;
		params.Grain = lens.Grain;
		params.GrainSize = lens.GrainSize;

		// The frame number as a float, and it stays exact: a float carries
		// integers to 2^24, which at 60 Hz is seventy-seven hours before the
		// grain pattern would start repeating a neighbouring frame's. Well
		// past the point where anything else about a session is still true.
		params.Frame = (float)(Renderer::GetFrameCount() & 0xFFFFFFu);

		// A flag rather than a multiply by one. `x * 1.0f` happens to be exact
		// in IEEE, but the rule this codebase follows is to branch past an
		// effect that is off rather than to reason about whether computing it
		// is harmless -- and here the branch is also what keeps the shader
		// from reading a buffer that holds nothing meaningful. ENGINE-NOTES 7y.
		params.AutoExposure = lens.Exposure ? 1.0f : 0.0f;

		// Never null: the shader declares every extra binding whether or not
		// bloom ran, whether or not anything is being graded, and whether or
		// not the exposure was metered.
		Dispatch(cmd, Shader::Tonemap, outputFormat, scene,
				 bloom ? bloom : s_Data->Black, &params, sizeof(params),
				 Sampling::Linear, Sampling::Linear,
				 grading ? lut : s_Data->IdentityLut, Sampling::Linear,
				 nullptr, Sampling::Linear,   // no fourth image; binding 3 is the buffer
				 lens.Exposure ? lens.Exposure : s_Data->UnitExposure);
	}

	// --- depth of field. ENGINE-NOTES 7z ------------------------------------

	namespace
	{
		// PostParams followed by the lens, the same way TonemapParams extends
		// it -- so Dispatch still finds FlipY where it has always been.
		struct DofPrepassParams
		{
			PostParams Base;
			float FocusDistance = 5.0f;
			float FocalLength = 0.05f;
			float FNumber = 2.8f;
			float FrameHeight = 1080.0f;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float MaxRadius = 24.0f;
		};

		struct DofGatherParams
		{
			PostParams Base;
			float TexelX = 0.0f;
			float TexelY = 0.0f;
			float MaxRadius = 24.0f;
		};

		struct DofCompositeParams
		{
			PostParams Base;
			float SharpBelow = 1.0f;
		};
	}

	void PostProcess::DofPrepass(RHICommandList& cmd, const Ref<RHITexture>& scene,
								 const Ref<RHITexture>& depth, uint32_t frameHeight,
								 Format outputFormat, const FocusParams& focus)
	{
		if (!s_Data || !scene || !depth)
			return;

		DofPrepassParams params;
		// Clamped off the focal length, because the thin-lens denominator is
		// `d - f`: focusing *at* the focal length is focusing at infinity, and
		// the expression diverges rather than doing something interesting.
		params.FocusDistance = Math::Max(focus.FocusDistance, focus.FocalLength * 1.01f);
		params.FocalLength = focus.FocalLength;
		params.FNumber = Math::Max(focus.FNumber, 0.5f);
		params.FrameHeight = (float)frameHeight;
		params.NearClip = focus.NearClip;
		params.FarClip = focus.FarClip;
		params.MaxRadius = focus.MaxRadius;

		// Depth is read with the point sampler. Filtering it would average a
		// near distance with a far one and produce a distance nothing in the
		// scene is at -- which reads as a halo around every silhouette.
		Dispatch(cmd, Shader::DofPrepass, outputFormat, scene, depth,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point);
	}

	void PostProcess::DofGather(RHICommandList& cmd, const Ref<RHITexture>& source,
								uint32_t width, uint32_t height,
								Format outputFormat, float maxRadius)
	{
		if (!s_Data || !source || width == 0 || height == 0)
			return;

		DofGatherParams params;
		params.TexelX = 1.0f / (float)width;
		params.TexelY = 1.0f / (float)height;
		// In the half-resolution pixels the taps are placed in.
		params.MaxRadius = maxRadius * 0.5f;

		Dispatch(cmd, Shader::DofGather, outputFormat, source, nullptr,
				 &params, sizeof(params));
	}

	void PostProcess::DofComposite(RHICommandList& cmd, const Ref<RHITexture>& scene,
								   const Ref<RHITexture>& blurred, Format outputFormat)
	{
		if (!s_Data || !scene || !blurred)
			return;

		DofCompositeParams params;
		// One pixel. A blur smaller than the thing it is blurring is not
		// visible, and taking the half-resolution image for it would be
		// trading a sharp pixel for a soft one and calling it a lens.
		params.SharpBelow = 1.0f;

		Dispatch(cmd, Shader::DofComposite, outputFormat, scene, blurred,
				 &params, sizeof(params));
	}

	// --- motion blur (9.5) -- ENGINE-NOTES 7ab -------------------------------

	namespace
	{
		struct MotionBlurPackParams
		{
			PostParams Base;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
		};

		struct MotionBlurTileParams
		{
			PostParams Base;
			float TileSize = 20.0f;
		};

		struct MotionBlurGatherParams
		{
			PostParams Base;
			float Shutter = 0.5f;
			float MaxRadius = 20.0f;
		};
	}

	void PostProcess::MotionBlurPack(RHICommandList& cmd, const Ref<RHITexture>& velocity,
									 const Ref<RHITexture>& depth,
									 uint32_t width, uint32_t height,
									 float nearClip, float farClip, Format outputFormat)
	{
		if (!s_Data || !velocity || !depth)
			return;

		MotionBlurPackParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.NearClip = nearClip;
		params.FarClip = farClip;

		// Point sampling on both: a velocity halfway between two surfaces
		// moving apart is a direction nothing travelled in, and a filtered
		// depth is a surface that does not exist.
		Dispatch(cmd, Shader::MotionBlurPack, outputFormat, velocity, depth,
				 &params, sizeof(params), Sampling::Point, Sampling::Point);
	}

	void PostProcess::MotionBlurTileMax(RHICommandList& cmd, const Ref<RHITexture>& packed,
										uint32_t sourceWidth, uint32_t sourceHeight,
										float tileSize, Format outputFormat)
	{
		if (!s_Data || !packed)
			return;

		MotionBlurTileParams params;
		// The *source's* texel: the shader walks source pixels around each
		// tile's centre.
		params.Base.TexelSize = { 1.0f / (float)Math::Max(sourceWidth, 1u),
								  1.0f / (float)Math::Max(sourceHeight, 1u) };
		params.TileSize = tileSize;

		Dispatch(cmd, Shader::MotionBlurTileMax, outputFormat, packed, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::MotionBlurNeighborMax(RHICommandList& cmd, const Ref<RHITexture>& tiles,
											uint32_t tileWidth, uint32_t tileHeight,
											Format outputFormat)
	{
		if (!s_Data || !tiles)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(tileWidth, 1u),
							 1.0f / (float)Math::Max(tileHeight, 1u) };

		Dispatch(cmd, Shader::MotionBlurNeighborMax, outputFormat, tiles, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::MotionBlurGather(RHICommandList& cmd, const Ref<RHITexture>& scene,
									   const Ref<RHITexture>& packed, const Ref<RHITexture>& tiles,
									   uint32_t width, uint32_t height,
									   float shutter, float maxRadius, Format outputFormat)
	{
		if (!s_Data || !scene || !packed || !tiles)
			return;

		MotionBlurGatherParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.Shutter = Math::Clamp(shutter, 0.0f, 1.0f);
		params.MaxRadius = Math::Max(maxRadius, 1.0f);

		// The scene is filtered -- its taps land between texels on purpose --
		// while velocity and the tile maxima are classifications, point
		// sampled for SMAA's reason.
		Dispatch(cmd, Shader::MotionBlurGather, outputFormat, scene, packed,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 tiles, Sampling::Point);
	}

	// --- the view reconstruction both SSAO and SSR carry -- ENGINE-NOTES 7ae -

	namespace
	{
		// The rows of the view matrix's rotation, as the shaders take them.
		// Column-major storage: row i is element [c][i] across columns c.
		// Only the rotation, so w is zero.
		void ViewRows(const Mat4& v, Vec4& row0, Vec4& row1, Vec4& row2)
		{
			row0 = Vec4(v[0][0], v[1][0], v[2][0], 0.0f);
			row1 = Vec4(v[0][1], v[1][1], v[2][1], 0.0f);
			row2 = Vec4(v[0][2], v[1][2], v[2][2], 0.0f);
		}
	}

	// --- SSAO (9.6) -- ENGINE-NOTES 7ac --------------------------------------

	namespace
	{
		struct SsaoComputeParams
		{
			PostParams Base;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvP0 = 1.0f;
			float InvP1 = 1.0f;
			float Radius = 0.5f;
			// The push-constant block is std430-ish: vec4s land on 16-byte
			// boundaries. Base is 24 bytes and the five floats above bring the
			// offset to 44; the shader pads to 48 and so must this, or the rows
			// land four bytes early and the normal rotates by garbage.
			float _pad0 = 0.0f;
			Vec4 ViewRow0{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec4 ViewRow1{ 0.0f, 1.0f, 0.0f, 0.0f };
			Vec4 ViewRow2{ 0.0f, 0.0f, 1.0f, 0.0f };
		};
		static_assert(offsetof(SsaoComputeParams, ViewRow0) == 48,
					  "ViewRow0 must sit at the 16-byte boundary the shader pads to");

		// The ray-traced twin's block (7ao): the same head, then the camera
		// rather than the view -- a ray is cast in world space, so the pass
		// carries the frame back out through the camera's rotation and
		// position instead of bringing the normal in through the view's.
		// 112 bytes: inside the 128 every device guarantees.
		struct RtaoComputeParams
		{
			PostParams Base;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvP0 = 1.0f;
			float InvP1 = 1.0f;
			float Radius = 0.5f;
			// Rays a pixel. Was a compile-time constant in the shader; it is
			// the dial between AoDetail::Full and AoDetail::VeryHigh, and it
			// fits in the pad that was already here, so the 128-byte layout
			// below is unchanged.
			float Taps = 4.0f;
			Vec4 CameraRow0{ 1.0f, 0.0f, 0.0f, 0.0f };   // rows of the camera-to-world rotation
			Vec4 CameraRow1{ 0.0f, 1.0f, 0.0f, 0.0f };
			Vec4 CameraRow2{ 0.0f, 0.0f, 1.0f, 0.0f };
			Vec4 CameraPosition{ 0.0f, 0.0f, 0.0f, 0.0f };
			// The frame's TAA jitter in NDC, xy. 128 bytes with it, which is
			// exactly the push-constant size every Vulkan device guarantees.
			Vec4 Jitter{ 0.0f, 0.0f, 0.0f, 0.0f };
		};
		static_assert(offsetof(RtaoComputeParams, CameraRow0) == 48 && sizeof(RtaoComputeParams) == 128,
					  "RtaoComputeParams must match rtao_compute.rvshader");
	}

	void PostProcess::RtaoCompute(RHICommandList& cmd, const Ref<RHITexture>& depth,
								  const Ref<RHITexture>& surface,
								  const Ref<RHIAccelerationStructure>& structure,
								  const Ref<RHITexture>& budget,
								  uint32_t width, uint32_t height,
								  const ViewReconstruction& view, float radius,
								  uint32_t taps,
								  Format outputFormat, float frame)
	{
		if (!s_Data || !depth || !surface || !structure)
			return;

		RtaoComputeParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.Radius = Math::Max(radius, 0.01f);
		// Free slot on this shader; the spiral's per-frame advance.
		params.Base.A = frame;
		// Clamped to at least one: a zero would divide by zero in the
		// shader's average.
		params.Taps = (float)Math::Max(taps, 1u);
		// The budget's presence as a number rather than a null texture, so
		// the shader's binding count stays fixed.
		params.Base.C = budget ? 1.0f : 0.0f;

		// The camera transform is the view's inverse; the rotation part of
		// an inverse is the transpose, so the camera's rows are the view's
		// columns, and the position falls out of the full inverse.
		const Mat4 camera = Math::Inverse(view.View);
		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0], 0.0f);
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], 0.0f);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], 0.0f);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], 0.0f);
		params.Jitter = Vec4(view.JitterX, view.JitterY, 0.0f, 0.0f);

		Dispatch(cmd, Shader::RtaoCompute, outputFormat, depth, surface,
				 &params, sizeof(params), Sampling::Point, Sampling::Point,
				 budget ? budget : s_Data->Black, Sampling::Point,
				 nullptr, Sampling::Point,
				 nullptr, structure, Format::Undefined,
				 // The taps are counted (WR-16 S0). The shader declares the
				 // binding unconditionally, and it only compiles where the
				 // counters exist -- the same condition, ray query.
				 RayCounters::Buffer());
	}

	void PostProcess::SsaoCompute(RHICommandList& cmd, const Ref<RHITexture>& depth,
								  const Ref<RHITexture>& surface,
								  uint32_t width, uint32_t height,
								  const ViewReconstruction& view, float radius,
								  Format outputFormat)
	{
		if (!s_Data || !depth || !surface)
			return;

		SsaoComputeParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.Radius = Math::Max(radius, 0.01f);
		ViewRows(view.View, params.ViewRow0, params.ViewRow1, params.ViewRow2);

		// Both point sampled: a filtered depth is a surface that does not
		// exist, and a filtered normal is a direction nothing faces.
		Dispatch(cmd, Shader::SsaoCompute, outputFormat, depth, surface,
				 &params, sizeof(params), Sampling::Point, Sampling::Point);
	}

	void PostProcess::SsgiCompute(RHICommandList& cmd, const Ref<RHITexture>& depth,
								  const Ref<RHITexture>& surface,
								  const Ref<RHITexture>& scene,
								  const Ref<RHITexture>& contributed,
								  uint32_t width, uint32_t height,
								  const ViewReconstruction& view, float radius,
								  float taps, Format outputFormat)
	{
		if (!s_Data || !depth || !surface || !scene || !contributed)
			return;

		SsaoComputeParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.Radius = Math::Max(radius, 0.01f);
		// The shared header's spare slot, read by the gather as a tap count
		// (ENGINE-NOTES 7az). Clamped so no profile can ask for a loop that
		// does not end.
		params.Base.A = Math::Clamp(taps, 1.0f, 64.0f);
		ViewRows(view.View, params.ViewRow0, params.ViewRow1, params.ViewRow2);

		// Depth and normal point sampled for SSAO's reasons; the lit image
		// linear, because a bounce is low frequency and a gather of point
		// samples off a half-resolution grid aliases into sparkle.
		// The indirect light already in the lit image, linear for the same
		// reason the image is: the two are subtracted tap for tap and a
		// point-sampled minuend against a filtered subtrahend leaves an edge
		// where neither belongs (ENGINE-NOTES 7ay).
		Dispatch(cmd, Shader::SsgiCompute, outputFormat, depth, surface,
				 &params, sizeof(params), Sampling::Point, Sampling::Point,
				 scene, Sampling::Linear, contributed, Sampling::Linear);
	}

	void PostProcess::SsgiBlur(RHICommandList& cmd, const Ref<RHITexture>& source,
							   uint32_t width, uint32_t height,
							   float directionX, float directionY, Format outputFormat)
	{
		if (!s_Data || !source)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
							 1.0f / (float)Math::Max(height, 1u) };
		params.A = directionX;
		params.B = directionY;

		Dispatch(cmd, Shader::SsgiBlur, outputFormat, source, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::ImportanceTiles(RHICommandList& cmd,
									  const Ref<RHITexture>& surface,
									  const Ref<RHITexture>& depth,
									  const Ref<RHITexture>& velocity,
									  uint32_t tileWidth, uint32_t tileHeight,
									  uint32_t tileSize,
									  uint32_t screenWidth, uint32_t screenHeight,
									  float nearClip, float farClip,
									  Format outputFormat)
	{
		if (!s_Data || !surface || !depth || !velocity)
			return;

		struct ImportanceParams
		{
			PostParams Base;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float ScreenX = 0.0f;
			float ScreenY = 0.0f;
		};

		ImportanceParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(tileWidth, 1u),
								  1.0f / (float)Math::Max(tileHeight, 1u) };
		params.Base.A = (float)Math::Max(tileSize, 1u);
				params.NearClip = nearClip;
		params.FarClip = farClip;
		params.ScreenX = 1.0f / (float)Math::Max(screenWidth, 1u);
		params.ScreenY = 1.0f / (float)Math::Max(screenHeight, 1u);

		Dispatch(cmd, Shader::ImportanceTiles, outputFormat, surface, depth,
			 &params, sizeof(params), Sampling::Point, Sampling::Point,
			 velocity, Sampling::Point);
	}

	void PostProcess::TileReduce(RHICommandList& cmd, const Ref<RHITexture>& source,
								 uint32_t outWidth, uint32_t outHeight,
								 Format outputFormat)
	{
		if (!s_Data || !source)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(outWidth, 1u),
							 1.0f / (float)Math::Max(outHeight, 1u) };

		Dispatch(cmd, Shader::TileReduce, outputFormat, source, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::TileBudget(RHICommandList& cmd, const Ref<RHITexture>& tiles,
								 const Ref<RHITexture>& mean,
								 const Ref<RHITexture>& history,
								 uint32_t tileWidth, uint32_t tileHeight,
								 float aoAverage, float giAverage,
								 float minFactor, float maxFactor,
								 float hysteresis, float aoCeiling, float giCeiling,
								 Format outputFormat)
	{
		if (!s_Data || !tiles || !mean)
			return;

		struct BudgetParams
		{
			PostParams Base;
			float MinFactor = 0.25f;
			float MaxFactor = 4.0f;
			float Hysteresis = 1.0f;
			float AoCeiling = 16.0f;
			float GiCeiling = 16.0f;
		};

		BudgetParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(tileWidth, 1u),
								  1.0f / (float)Math::Max(tileHeight, 1u) };
		params.Base.A = aoAverage;
		params.Base.B = giAverage;
		params.Base.C = history ? 1.0f : 0.0f;
		params.MinFactor = minFactor;
		params.MaxFactor = maxFactor;
		params.Hysteresis = hysteresis;
		params.AoCeiling = aoCeiling;
		params.GiCeiling = giCeiling;

		Dispatch(cmd, Shader::TileBudget, outputFormat, tiles, mean,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 history ? history : s_Data->Black, Sampling::Point);
	}



	void PostProcess::SsaoBlur(RHICommandList& cmd, const Ref<RHITexture>& source,
							   uint32_t width, uint32_t height,
							   float directionX, float directionY, Format outputFormat)
	{
		if (!s_Data || !source)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
							 1.0f / (float)Math::Max(height, 1u) };
		params.A = directionX;
		params.B = directionY;

		Dispatch(cmd, Shader::SsaoBlur, outputFormat, source, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	namespace
	{
		// 128 bytes exactly -- the push-constant size every Vulkan device
		// guarantees, and the budget RtaoComputeParams already spends to the
		// byte. The fog's three scalars ride in the shared header's A, B and C
		// rather than being appended, which is what keeps it inside that.
		struct FogParams
		{
			PostParams Base;                              // A density, B falloff, C max
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvP0 = 1.0f;
			float InvP1 = 1.0f;

			// **These two sit here to align the vec4s, not for tidiness.**
			//
			// A vec4 in a push-constant block is 16-byte aligned, and the
			// header plus the four clip scalars ends at 40 -- so the shader
			// would pad to 48 while this struct would not, and every field
			// after it would be read eight bytes out. It renders as fog the
			// wrong colour, because Color lands on part of CameraPosition.
			// RtaoComputeParams never hits this: its six scalars end at 48
			// already. Two floats of real payload close the gap instead of
			// padding, and the block still ends at exactly 128.
			// **And the second of the two is real payload now.** It was
			// padding to close the alignment gap described above; the fog
			// floor fits it exactly, so the block still ends at 128 and no
			// offset moves.
			float StartDistance = 0.0f;
			float Floor = -1.0e9f;

			// **The w lanes carry WR-3, and that is what kept it out of a
			// uniform buffer.** The sky the fog takes its colour from is
			// seven vec4s of gradient state if it is evaluated, and this
			// block has none of them spare -- it ends at exactly 128. Sampled
			// from the sky cube instead, it is four scalars, and four scalars
			// is precisely what the rotation rows were already uploading as
			// zeros. See fog.rvshader for which lane is which.
			Vec4 CameraRow0{ 1.0f, 0.0f, 0.0f, 0.0f };   // w: SkyIntensity
			Vec4 CameraRow1{ 0.0f, 1.0f, 0.0f, 0.0f };   // w: SkyRotation
			Vec4 CameraRow2{ 0.0f, 0.0f, 1.0f, 0.0f };   // w: SkyAffect
			Vec4 CameraPosition{ 0.0f, 0.0f, 0.0f, 0.0f };  // w: SkyOcclusion
			Vec4 Color{ 0.55f, 0.60f, 0.68f, 0.0f };      // w: the reference height
		};
		static_assert(offsetof(FogParams, CameraRow0) % 16 == 0,
					  "the camera rows must be 16-byte aligned or the shader reads them shifted");
		static_assert(sizeof(FogParams) <= 128, "fog push constants must fit the guaranteed 128 bytes");
	}

	void PostProcess::Fog(RHICommandList& cmd, const Ref<RHITexture>& scene,
						  const Ref<RHITexture>& depth, const FogSettings& fog,
						  const FogView& view, Format outputFormat,
						  const Ref<RHITexture>& skyCube)
	{
		if (!s_Data || !scene || !depth)
			return;

		FogParams params;
		params.Base.A = Math::Max(fog.Density, 0.0f);
		params.Base.B = Math::Max(fog.HeightFalloff, 0.0f);
		params.Base.C = Math::Clamp(fog.MaxOpacity, 0.0f, 1.0f);
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.Color = Vec4(fog.Color.x, fog.Color.y, fog.Color.z, fog.Height);
		params.StartDistance = Math::Max(fog.StartDistance, 0.0f);
		params.Floor = fog.Floor;

		// The camera transform is the view's inverse; the rotation part of an
		// inverse is the transpose, so the camera's rows are the view's
		// columns, and the position falls out of the full inverse.
		const Mat4 camera = Math::Inverse(view.View);

		// **WR-3 rides the w lanes**, which were zeros. A sky the pass cannot
		// read is a sky it must not blend toward, so a missing cube forces the
		// dials off here rather than being discovered as black fog at the
		// far plane -- the caller passes null for a scene whose sky is a flat
		// colour, which is exactly the case with no sky to take a colour from.
		const float skyAffect = skyCube ? Math::Clamp(fog.SkyAffect, 0.0f, 1.0f) : 0.0f;
		const float skyOcclusion = skyCube ? Math::Clamp(fog.SkyOcclusion, 0.0f, 1.0f) : 0.0f;

		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0],
								 Math::Max(fog.SkyIntensity, 0.0f));
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], fog.SkyRotation);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], skyAffect);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], skyOcclusion);

		// **Depth is point sampled.** A linear fetch across a silhouette
		// averages two depths that describe nothing, and the fog would read a
		// point floating between the near object and the far one.
		//
		// The sky cube is bound whether or not the dials are on: the shader
		// declares the binding unconditionally, and a declared binding with
		// nothing in it is undefined behaviour on both backends rather than a
		// helpful zero -- the same rule the identity LUT and the unit exposure
		// buffer follow. Filtered, because the gradient cube is 32 texels a
		// face and a point sample of it would show its own texels as facets.
		Dispatch(cmd, Shader::Fog, outputFormat, scene, depth,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 skyCube ? skyCube : TextureLoader::BlackCube(*s_Data->Device),
				 Sampling::Linear);
	}

	void PostProcess::WaterBackdrop(RHICommandList& cmd, const Ref<RHITexture>& scene,
									const Ref<RHITexture>& depth,
									float nearClip, float farClip,
									Format outputFormat, Format depthOutputFormat)
	{
		if (!s_Data || !scene || !depth)
			return;

		PostParams params;
		params.A = nearClip;
		params.B = farClip;

		// Point-sampled depth for the fog pass's reason: a filtered read
		// across a silhouette is a depth between two surfaces, and the water
		// would measure its thickness to a point that is not there.
		Dispatch(cmd, Shader::WaterBackdropCopy, outputFormat, scene, depth,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 nullptr, Sampling::Linear, nullptr, Sampling::Linear,
				 nullptr, nullptr, depthOutputFormat);
	}

	void PostProcess::SsaoApply(RHICommandList& cmd, const Ref<RHITexture>& scene,
								const Ref<RHITexture>& occlusion,
								const Ref<RHITexture>& depth,
								uint32_t occlusionWidth, uint32_t occlusionHeight,
								float nearClip, float farClip,
								float intensity, Format outputFormat)
	{
		if (!s_Data || !scene || !occlusion)
			return;

		PostParams params;
		params.A = Math::Max(intensity, 0.0f);
		params.B = nearClip;
		params.C = farClip;
		// **The occlusion buffer's texel size, not the frame's.** The upsample
		// walks the four half-resolution texels around each pixel, so it needs
		// the grid it is walking.
		params.TexelSize = { 1.0f / (float)Math::Max(occlusionWidth, 1u),
							 1.0f / (float)Math::Max(occlusionHeight, 1u) };

		// The occlusion is read at exact texel centres, so a linear filter
		// returns each texel whole; the weighting is the shader's own.
		Dispatch(cmd, Shader::SsaoApply, outputFormat, scene, occlusion,
				 &params, sizeof(params), Sampling::Linear, Sampling::Linear,
				 depth, Sampling::Point);
	}

	// --- SSR (9.7) -- ENGINE-NOTES 7ad ---------------------------------------

	namespace
	{
		struct SsrTraceParams
		{
			PostParams Base;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvP0 = 1.0f;
			float InvP1 = 1.0f;
			float MaxDistance = 20.0f;
			float Thickness = 0.5f;
			// The push-constant block is std430-ish: vec4s land on 16-byte
			// boundaries. Base is 24 bytes and the six floats above bring the
			// offset to 48, which is aligned -- and the static_assert below is
			// what keeps that true when somebody adds a seventh.
			Vec4 ViewRow0{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec4 ViewRow1{ 0.0f, 1.0f, 0.0f, 0.0f };
			Vec4 ViewRow2{ 0.0f, 0.0f, 1.0f, 0.0f };

			// Where a mirror ray stops being the answer: below the first the
			// trace is taken whole, above the second the probe's blur is what
			// many jittered rays would converge to anyway.
			//
			// **Sent rather than hardcoded, because the ray-traced path has
			// the same window and the two must not drift.** They did: this
			// shader held 0.55 to 0.9 while Renderer::GetReflectionGloss said
			// 0.25 to 0.6, so a surface at roughness 0.66 was entirely the
			// probe's on Vulkan and 77% a screen-space trace on OpenGL --
			// which, being screen space, came and went with the camera. The
			// showroom's walls are 0.66. ENGINE-NOTES 7cj.
			//
			// After the vec4s on purpose: ViewRow0 has to stay on a 16-byte
			// boundary and two floats in front of it would have moved it.
			float GlossBegin = 0.25f;
			float GlossEnd = 0.60f;
		};
		static_assert(offsetof(SsrTraceParams, ViewRow0) % 16 == 0,
					  "ViewRow0 must sit on a 16-byte boundary for the shader's vec4");
	}

	namespace
	{
		struct SsrHiZParams
		{
			PostParams Base;   // A = level 0 width, B = level 0 height, C = levels
			float FirstLevel = 0.0f;
			float LastLevel = 0.0f;
			float SourceLevel = -1.0f;
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
		};

		SsrHiZParams HiZParams(uint32_t traceWidth, uint32_t traceHeight,
							   float nearClip, float farClip)
		{
			uint32_t atlasWidth = 1, atlasHeight = 1;
			PostProcess::SsrHiZSize(traceWidth, traceHeight, atlasWidth, atlasHeight);

			SsrHiZParams params;
			params.Base.TexelSize = { 1.0f / (float)Math::Max(atlasWidth, 1u),
									  1.0f / (float)Math::Max(atlasHeight, 1u) };
			params.Base.A = (float)traceWidth;
			params.Base.B = (float)traceHeight;
			params.Base.C = (float)PostProcess::kSsrHiZLevels;
			params.NearClip = nearClip;
			params.FarClip = farClip;
			return params;
		}
	}

	void PostProcess::SsrHiZSize(uint32_t traceWidth, uint32_t traceHeight,
								 uint32_t& atlasWidth, uint32_t& atlasHeight)
	{
		// Mirrors HiZAtlasSize in include/hiz_atlas.glsl: level 0 plus a
		// half-width column for every level after it.
		atlasWidth = traceWidth + (traceWidth >> 1);
		atlasHeight = traceHeight;
	}

	void PostProcess::SsrHiZFine(RHICommandList& cmd, const Ref<RHITexture>& depth,
								 uint32_t traceWidth, uint32_t traceHeight,
								 float nearClip, float farClip, Format outputFormat)
	{
		if (!s_Data || !depth)
			return;

		SsrHiZParams params = HiZParams(traceWidth, traceHeight, nearClip, farClip);
		params.FirstLevel = 0.0f;
		params.LastLevel = (float)(kSsrHiZFineLevels - 1);
		params.SourceLevel = -1.0f;

		// Point: a filtered depth is a surface that does not exist.
		Dispatch(cmd, Shader::SsrHiZ, outputFormat, depth, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::SsrHiZCoarse(RHICommandList& cmd, const Ref<RHITexture>& fine,
								   uint32_t traceWidth, uint32_t traceHeight,
								   float farClip, Format outputFormat)
	{
		if (!s_Data || !fine)
			return;

		SsrHiZParams params = HiZParams(traceWidth, traceHeight, 0.05f, farClip);
		params.FirstLevel = (float)kSsrHiZFineLevels;
		params.LastLevel = (float)(kSsrHiZLevels - 1);
		params.SourceLevel = (float)(kSsrHiZFineLevels - 1);

		Dispatch(cmd, Shader::SsrHiZ, outputFormat, fine, nullptr,
				 &params, sizeof(params), Sampling::Point);
	}

	void PostProcess::SsrTrace(RHICommandList& cmd, const Ref<RHITexture>& hiZFine,
							   const Ref<RHITexture>& hiZCoarse,
							   const Ref<RHITexture>& surface,
							   uint32_t width, uint32_t height,
							   const SsrParams& ssr, Format outputFormat)
	{
		if (!s_Data || !hiZFine || !hiZCoarse || !surface)
			return;

		SsrTraceParams params;
		params.Base.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
								  1.0f / (float)Math::Max(height, 1u) };
		params.Base.A = (float)kSsrHiZLevels;
		params.Base.B = (float)kSsrHiZFineLevels;
		params.NearClip = ssr.View.NearClip;
		params.FarClip = ssr.View.FarClip;
		params.InvP0 = ssr.View.InvProjection0;
		params.InvP1 = ssr.View.InvProjection1;
		params.MaxDistance = Math::Max(ssr.MaxDistance, 0.1f);
		params.Thickness = Math::Max(ssr.Thickness, 0.01f);
		ViewRows(ssr.View.View, params.ViewRow0, params.ViewRow1, params.ViewRow2);

		// The same window the ray-traced reflections use, from the same place.
		const Vec2 gloss = Renderer::GetReflectionGloss();
		params.GlossBegin = gloss.x;
		params.GlossEnd = gloss.y;

		// All point sampled: the atlases are fetched by texel, and a filtered
		// normal is a direction nothing faces.
		Dispatch(cmd, Shader::SsrTrace, outputFormat, hiZFine, surface,
				 &params, sizeof(params), Sampling::Point, Sampling::Point,
				 hiZCoarse, Sampling::Point);
	}

	void PostProcess::SsrResolve(RHICommandList& cmd, const Ref<RHITexture>& scene,
								 const Ref<RHITexture>& trace, const Ref<RHITexture>& surface,
								 uint32_t width, uint32_t height, Format outputFormat)
	{
		if (!s_Data || !scene || !trace || !surface)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
							 1.0f / (float)Math::Max(height, 1u) };

		// The trace is half resolution and *point* sampled: the shader
		// upsamples it by hand, resolving each of the four nearest texels at
		// its own hit and blending the radiances -- a filtered read of hit
		// coordinates lands somewhere no ray went (see the shader, and
		// ENGINE-NOTES 7af). The surface is point sampled for the same reason
		// as in the trace.
		Dispatch(cmd, Shader::SsrResolve, outputFormat, scene, trace,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 surface, Sampling::Point);
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

	void PostProcess::TemporalResolve(RHICommandList& cmd, const Ref<RHITexture>& current,
									  const Ref<RHITexture>& history,
									  const Ref<RHITexture>& velocity,
									  uint32_t width, uint32_t height, Format outputFormat,
									  float feedback, bool hasHistory,
									  const Ref<RHITexture>& moments, Format momentsFormat)
	{
		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
							 1.0f / (float)Math::Max(height, 1u) };
		// Clamped short of 1, which would be a filter that never accepts a new
		// frame: the image would freeze on whatever it first accumulated and
		// look, from the outside, exactly like the resolve having stopped.
		params.A = Math::Clamp(feedback, 0.0f, 0.98f);
		params.B = hasHistory ? 1.0f : 0.0f;
		// Whether last frame's moments are there to be read. The first frame
		// after a resize has a pair but no history in it, and the shader must
		// start its count from one rather than read a count out of garbage.
		params.C = (moments && momentsFormat != Format::Undefined && hasHistory) ? 1.0f : 0.0f;

		// The neighbourhood is read at exact texel offsets, so the current
		// frame is sampled point -- a filtered read of a 3x3 box would blur
		// the box before the clip is computed from it, which widens it and
		// lets more ghosting through.
		//
		// The history is linear, and that one is not a preference: it is
		// sampled at a reprojected position that lands between texels
		// whenever anything moves by a fraction of a pixel, which is always.
		//
		// Velocity is point for the same reason SMAA's maps are: it is a
		// measurement per pixel, and the average of two pixels' motion is the
		// motion of nothing.
		Dispatch(cmd, Shader::TaaResolve, outputFormat, current, history,
				 &params, sizeof(params), Sampling::Point, Sampling::Linear,
				 velocity, Sampling::Point,
				 // Binding 3 is filled whether or not there are moments to
				 // read, for GiDenoise's reason: a declared binding with
				 // nothing bound is undefined behaviour, and params.C is what
				 // says the black is not data.
				 moments ? moments : s_Data->Black, Sampling::Point,
				 nullptr, nullptr, momentsFormat,
				 // The validity lane's count (WR-16 S0): declared by the
				 // shader under RV_RAY_COUNTERS, which Init defines exactly
				 // where RayCounters is available, so the two agree.
				 RayCounters::IsAvailable() ? RayCounters::Buffer() : nullptr);
	}

	void PostProcess::GiDenoise(RHICommandList& cmd, const Ref<RHITexture>& current,
								const Ref<RHITexture>& history, const Ref<RHITexture>& velocity,
								uint32_t width, uint32_t height,
								float feedback, bool hasHistory, Format outputFormat,
								const Ref<RHITexture>& moments, Format momentsFormat)
	{
		if (!s_Data || !current)
			return;

		PostParams params;
		params.TexelSize = { 1.0f / (float)Math::Max(width, 1u),
							 1.0f / (float)Math::Max(height, 1u) };
		// Clamped short of 1 for TemporalResolve's reason: a filter that never
		// accepts a new frame freezes on what it first accumulated, and from
		// the outside that looks exactly like the pass having stopped running.
		params.A = Math::Clamp(feedback, 0.0f, 0.98f);
		params.B = (hasHistory && history && velocity) ? 1.0f : 0.0f;

		// Whether last frame's moments are there to be read. Separate from B:
		// the colour history and the moments are allocated together, but the
		// first frame after a resize has neither and the shader must fall back
		// to its spatial estimate rather than divide by a count of zero.
		params.C = (moments && momentsFormat != Format::Undefined
					&& params.B > 0.5f) ? 1.0f : 0.0f;

		Dispatch(cmd, Shader::GiDenoise, outputFormat, current, history,
				 &params, sizeof(params), Sampling::Point, Sampling::Linear,
				 velocity, Sampling::Point,
				 // **Binding 3 is filled whether or not there are moments to
				 // read.** The shader declares it unconditionally, and a
				 // declared binding with nothing bound is undefined behaviour
				 // rather than a zero -- the same reason the identity LUT fills
				 // binding 2 and the unit buffer fills the exposure slot. Left
				 // null it faulted the device outright, and on a frame with no
				 // history, which is every frame in a mode that never
				 // accumulates. `params.C` is what tells the shader the black is
				 // not data.
				 moments ? moments : s_Data->Black, Sampling::Point,
				 nullptr, nullptr, momentsFormat);
	}

	void PostProcess::Blit(RHICommandList& cmd, const Ref<RHITexture>& source, Format outputFormat)
	{
		PostParams params;
		Dispatch(cmd, Shader::Blit, outputFormat, source, nullptr, &params, sizeof(params));
	}

	void PostProcess::DebugView(RHICommandList& cmd, const Ref<RHITexture>& frame,
								const Ref<RHITexture>& aux, const Ref<RHIBuffer>& counts,
								int mode, float scale, Format outputFormat)
	{
		if (!s_Data || !frame || !counts)
			return;

		PostParams params;
		params.A = (float)mode;
		params.B = Math::Max(scale, 1.0e-6f);
		params.C = aux ? 1.0f : 0.0f;
		// The frame linear, the auxiliary point: a validity flag and a tile
		// map are both things a filtered read would invent values between.
		Dispatch(cmd, Shader::DebugView, outputFormat, frame, aux ? aux : s_Data->Black,
				 &params, sizeof(params), Sampling::Linear, Sampling::Point,
				 nullptr, Sampling::Point, nullptr, Sampling::Point,
				 nullptr, nullptr, Format::Undefined, counts);
	}
}
