#include <rvpch.h>
#include "ParticleRenderer.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Scene/Components.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Particles/ParticleSystem.h"
#include <algorithm>
#include <iterator>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Per draw, which is per emitter. Matches the simulation's pool cap:
		// a bigger pool would silently draw a truncation of itself.
		constexpr uint32_t kMaxInstancesPerDraw = 16384;

		// 48 bytes, and the std430 layout of three vec4s -- the same struct
		// the shader declares, and the same one the GPU simulation will keep
		// on its own buffers.
		struct InstanceData
		{
			Vec4 PositionSize;   // xyz world position, w size
			Vec4 Color;
			Vec4 Params;         // x rotation, yzw unused
		};
		static_assert(sizeof(InstanceData) == 48, "Must match particle.rvshader's std430 struct");

		struct SceneUniforms
		{
			Mat4 ViewProjection;
			Vec4 CameraRight;
			Vec4 CameraUp;
		};

		struct DrawPush
		{
			int32_t BaseInstance;
			int32_t Flat;
		};
		static_assert(sizeof(DrawPush) == 8, "Must match particle.rvshader's push block");

		// One emitter's submission, held until EndScene so the draws can be
		// depth-sorted against each other before anything is uploaded.
		struct PendingDraw
		{
			std::vector<InstanceData> Instances;
			// Set when a compute pass wrote the instances instead. The vector
			// above is empty in that case and `GpuCount` is the pool size.
			Ref<RHIBuffer> GpuInstances;
			uint32_t GpuCount = 0;
			Ref<RHITexture> Texture;
			ParticleBlend Blend = ParticleBlend::Alpha;
			bool Flat = false;
			float Depth = 0.0f;   // emitter origin along the view direction
		};

		struct ParticleRendererData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader> Shader;
			Ref<RHIPipeline> AlphaPipeline;
			Ref<RHIPipeline> AdditivePipeline;

			// Weighted blending: a second shader writing two attachments, and
			// the fullscreen composite that turns them back into a picture.
			Ref<RHIShader> WeightedShader;
			Ref<RHIPipeline> WeightedPipeline;
			Ref<RHIShader> ResolveShader;
			Ref<RHIPipeline> ResolvePipeline;
			// Held back by EndScene for the transparent pass.
			std::vector<PendingDraw> Weighted;

			Ref<RHITexture> WhiteTexture;
			Ref<RHISampler> Sampler;
			Format TargetColor = Format::B8G8R8A8_UNORM;
			Format TargetDepth = Format::D24_UNORM_S8_UINT;
		// Sample count, which has to equal the target's. A pipeline whose
		// rasterizationSamples disagrees with the attachment it draws into is
		// undefined behaviour rather than an error, so it travels with the
		// formats and gets compared with them.
			uint32_t TargetSamples = 1;
			bool PipelineDirty = true;

			// Per batch, not per frame: one batch is one draw's buffers, and
			// the editor renders more than one scene per frame.
			struct Batch
			{
				Ref<RHIBuffer> Instances;
				Ref<RHIBuffer> Scene;
				Ref<RHIResourceSet> Set;
				// The resolve binds two textures and nothing else, so it needs
				// a set built against its own pipeline's layout.
				Ref<RHIResourceSet> ResolveSet;
			};

			std::vector<std::vector<Batch>> Batches;
			uint32_t BatchCursor = 0;

			std::vector<PendingDraw> Pending;
			uint32_t ParticleCount = 0;

			SceneUniforms Scene{};
			Vec3 CameraPosition{ 0.0f };
			Vec3 CameraForward{ 0.0f, 0.0f, -1.0f };
			bool InScene = false;

			bool Ready = false;
		};

		std::unique_ptr<ParticleRendererData> s_Data;

		ParticleRendererData::Batch& AcquireBatch(const Ref<RHIPipeline>& pipeline)
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& batches = s_Data->Batches[frame];

			// `while`, not `if`, and this is not defensive padding.
			//
			// The pool is cleared whenever the pipelines are rebuilt, and a
			// rebuild can land in the middle of a frame -- MSAA changes the
			// sample count in BuildFrame, which runs *after* a reflection probe
			// has already captured six faces and advanced this cursor. Growing
			// by one and then indexing the cursor reads past the end, which is
			// a segfault on the first frame of any scene with both a probe and
			// a particle emitter. Found by MSAA; present since the pool was
			// written. ENGINE-NOTES 7q.
			while (s_Data->BatchCursor >= batches.size())
			{
				const std::string index =
					std::to_string(frame) + "." + std::to_string(batches.size());

				ParticleRendererData::Batch batch;

				BufferDesc instanceDesc;
				instanceDesc.Size = (uint64_t)kMaxInstancesPerDraw * sizeof(InstanceData);
				instanceDesc.Usage = BufferUsage::Storage;
				instanceDesc.Memory = MemoryDomain::HostVisible;
				instanceDesc.DebugName = "ParticleRenderer.instances." + index;
				batch.Instances = s_Data->Device->CreateBuffer(instanceDesc);

				BufferDesc sceneDesc;
				sceneDesc.Size = sizeof(SceneUniforms);
				sceneDesc.Usage = BufferUsage::Uniform;
				sceneDesc.Memory = MemoryDomain::HostVisible;
				sceneDesc.DebugName = "ParticleRenderer.scene." + index;
				batch.Scene = s_Data->Device->CreateBuffer(sceneDesc);

				batches.push_back(std::move(batch));
			}

			ParticleRendererData::Batch& batch = batches[s_Data->BatchCursor++];

			// Sets belong to a pipeline layout; both pipelines here share one
			// shader and therefore one layout, so either pipeline works.
			if (!batch.Set)
				batch.Set = s_Data->Device->CreateResourceSet(pipeline, 0);

			return batch;
		}
	}

	void ParticleRenderer::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<ParticleRendererData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();
		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/particle.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("ParticleRenderer: failed to compile assets/shaders/particle.rvshader");
			return;
		}
		s_Data->Shader = device.CreateShader(*compiled);
		s_Data->Ready = s_Data->Shader != nullptr;

		// Optional: an engine whose weighted shader failed to compile still
		// draws alpha and additive, and says so once rather than per frame.
		if (auto weighted = ShaderCompiler::CompileFromFile("assets/shaders/particle_weighted.rvshader"))
			s_Data->WeightedShader = device.CreateShader(*weighted);
		if (auto resolve = ShaderCompiler::CompileFromFile("assets/shaders/oit_resolve.rvshader"))
			s_Data->ResolveShader = device.CreateShader(*resolve);

		if (!s_Data->WeightedShader || !s_Data->ResolveShader)
			RV_CORE_WARN("Weighted-blended transparency is unavailable; emitters "
						 "asking for it will draw as ordinary alpha");

		// The textureless fallback, same as Renderer2D's: a particle without
		// a sprite is a coloured quad, not an error.
		TextureDesc white;
		white.Width = 1;
		white.Height = 1;
		white.Format = Format::R8G8B8A8_UNORM;
		white.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		white.DebugName = "ParticleRenderer.white";
		s_Data->WhiteTexture = device.CreateTexture(white);
		const uint32_t whitePixel = 0xFFFFFFFFu;
		s_Data->WhiteTexture->Upload(&whitePixel, sizeof(whitePixel));

		SamplerDesc samplerDesc;
		s_Data->Sampler = device.CreateSampler(samplerDesc);

		s_Data->Batches.resize(device.GetFramesInFlight());

		if (s_Data->Ready)
			RV_CORE_INFO("ParticleRenderer ready ({0} particles per emitter draw)",
						 kMaxInstancesPerDraw);
		else
			RV_CORE_ERROR("ParticleRenderer incomplete; particles will not draw");
	}

	bool ParticleRenderer::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void ParticleRenderer::Shutdown()
	{
		s_Data.reset();
	}

	void ParticleRenderer::SetTargetFormats(Format color, Format depth, uint32_t samples)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth &&
			s_Data->TargetSamples == samples
			&& s_Data->AlphaPipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetSamples = samples;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void ParticleRenderer::EnsurePipelines()
	{
		if (!s_Data->PipelineDirty || !s_Data->Shader)
			return;

		GraphicsPipelineDesc desc;
		desc.Name = "ParticleRenderer.alpha";
		desc.Shader = s_Data->Shader;
		desc.Topology = PrimitiveTopology::TriangleList;
		// Quads that always face the viewer have no back to cull; flat ones
		// deliberately stay visible from behind, because a 2D game's camera
		// may sit on either side of its plane.
		desc.Rasterizer.Cull = CullMode::None;
		desc.Blend = BlendPreset::AlphaBlend;

		// Tested so the scene occludes its particles; not written so
		// particles never occlude each other -- a blended pixel in the depth
		// buffer would cut holes in everything drawn behind it.
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = false;

		desc.ColorFormats = { s_Data->TargetColor };
		desc.Samples = s_Data->TargetSamples;
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->AlphaPipeline = s_Data->Device->CreatePipeline(desc);

		desc.Name = "ParticleRenderer.additive";
		desc.Blend = BlendPreset::Additive;
		s_Data->AdditivePipeline = s_Data->Device->CreatePipeline(desc);

		// --- weighted blending -------------------------------------------
		// Two attachments with opposite equations, which is the reason
		// per-attachment blend state exists at all.
		if (s_Data->WeightedShader)
		{
			GraphicsPipelineDesc weighted;
			weighted.Name = "ParticleRenderer.weighted";
			weighted.Shader = s_Data->WeightedShader;
			weighted.Topology = PrimitiveTopology::TriangleList;
			weighted.Rasterizer.Cull = CullMode::None;
			weighted.DepthStencil.DepthTestEnable = true;
			weighted.DepthStencil.DepthWriteEnable = false;
			weighted.ColorFormats = { Format::R16G16B16A16_SFLOAT, Format::R8_UNORM };
			weighted.Samples = s_Data->TargetSamples;
			weighted.BlendPerAttachment = { BlendPreset::WeightedAccumulate,
											BlendPreset::WeightedRevealage };
			weighted.DepthFormat = s_Data->TargetDepth;
			s_Data->WeightedPipeline = s_Data->Device->CreatePipeline(weighted);
		}

		if (s_Data->ResolveShader)
		{
			GraphicsPipelineDesc resolve;
			resolve.Name = "ParticleRenderer.oitResolve";
			resolve.Shader = s_Data->ResolveShader;
			resolve.Topology = PrimitiveTopology::TriangleList;
			resolve.Rasterizer.Cull = CullMode::None;
			// The composite covers the screen and has nothing to test.
			resolve.DepthStencil.DepthTestEnable = false;
			resolve.DepthStencil.DepthWriteEnable = false;
			resolve.Blend = BlendPreset::AlphaBlend;
			resolve.ColorFormats = { s_Data->TargetColor };
			resolve.Samples = s_Data->TargetSamples;
			resolve.DepthFormat = Format::Undefined;
			s_Data->ResolvePipeline = s_Data->Device->CreatePipeline(resolve);
		}

		s_Data->PipelineDirty = false;

		// Resource sets belong to a pipeline layout, so they go with it.
		for (auto& frame : s_Data->Batches)
			frame.clear();
	}

	void ParticleRenderer::BeginFrame()
	{
		if (!s_Data)
			return;

		s_Data->BatchCursor = 0;
	}

	void ParticleRenderer::BeginScene(const Camera& camera, const Mat4& cameraTransform)
	{
		if (!s_Data)
			return;

		s_Data->Pending.clear();
		s_Data->ParticleCount = 0;
		s_Data->InScene = true;

		s_Data->Scene.ViewProjection = camera.GetProjection() * Math::Inverse(cameraTransform);
		s_Data->Scene.CameraRight = Vec4(Math::Normalize(Vec3(cameraTransform[0])), 0.0f);
		s_Data->Scene.CameraUp = Vec4(Math::Normalize(Vec3(cameraTransform[1])), 0.0f);
		s_Data->CameraPosition = Vec3(cameraTransform[3]);
		s_Data->CameraForward = Math::Normalize(Vec3(cameraTransform * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
	}

	void ParticleRenderer::DrawEmitter(const ParticleEmitterComponent& emitter, const Mat4& world)
	{
		if (!s_Data || !s_Data->InScene || emitter.Pool.empty())
			return;

		PendingDraw draw;
		draw.Blend = emitter.Blend;
		draw.Flat = emitter.Facing == ParticleFacing::Flat;
		draw.Depth = Math::Dot(Vec3(world[3]) - s_Data->CameraPosition, s_Data->CameraForward);

		draw.Texture = emitter.Texture.IsValid()
					 ? Assets::Manager::GetTexture(emitter.Texture)
					 : nullptr;
		if (!draw.Texture)
			draw.Texture = s_Data->WhiteTexture;

		const bool local = emitter.Space == ParticleSpace::Local;

		// Local pools scale with their emitter; the longest basis axis is
		// what a non-uniform scale does to a round thing.
		const float scale = local
			? Math::Max(Math::Max(Math::Length(Vec3(world[0])), Math::Length(Vec3(world[1]))),
						Math::Length(Vec3(world[2])))
			: 1.0f;

		const size_t count = Math::Min(emitter.Pool.size(), (size_t)kMaxInstancesPerDraw);
		draw.Instances.reserve(count);

		// Fetched once for the emitter, not once per particle: the lookup is a
		// hash and there can be sixteen thousand particles. Null means the
		// handle was never set, which is the ordinary case -- the two-point
		// ramps below still decide that channel.
		const Curve::Baked* sizeCurve = Assets::Manager::GetBakedCurve(emitter.SizeCurve);
		const Curve::Baked* colorCurve = Assets::Manager::GetBakedCurve(emitter.ColorGradient);
		const Curve::Baked* alphaCurve = Assets::Manager::GetBakedCurve(emitter.AlphaCurve);

		for (size_t i = 0; i < count; i++)
		{
			const Particle& particle = emitter.Pool[i];
			const float t = Math::Clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f);

			const Vec3 position = local
								? Vec3(world * Vec4(particle.Position, 1.0f))
								: particle.Position;

			const Particles::Appearance appearance =
				Particles::Evaluate(emitter, t, sizeCurve, colorCurve, alphaCurve);

			const float size = Math::Max(0.0f, appearance.Size * scale);
			Vec4 color = appearance.Color;

			// Additive sums; the alpha channel would be ignored by the blend,
			// so fading has to happen in the payload. Folding alpha into the
			// colour makes ColorEnd's fade-out mean the same thing in both
			// modes.
			if (emitter.Blend == ParticleBlend::Additive)
				color = Vec4(Vec3(color) * color.a, 1.0f);

			InstanceData& instance = draw.Instances.emplace_back();
			instance.PositionSize = Vec4(position, size);
			instance.Color = color;
			instance.Params = Vec4(particle.Rotation, 0.0f, 0.0f, 0.0f);
		}

		// Alpha needs its particles far-to-near; additive cannot tell.
		if (emitter.Blend == ParticleBlend::Alpha)
		{
			const Vec3 eye = s_Data->CameraPosition;
			const Vec3 forward = s_Data->CameraForward;
			std::sort(draw.Instances.begin(), draw.Instances.end(),
				[&](const InstanceData& a, const InstanceData& b)
				{
					return Math::Dot(Vec3(a.PositionSize) - eye, forward)
						 > Math::Dot(Vec3(b.PositionSize) - eye, forward);
				});
		}

		s_Data->ParticleCount += (uint32_t)draw.Instances.size();
		s_Data->Pending.push_back(std::move(draw));
	}

	void ParticleRenderer::DrawEmitterGpu(const ParticleEmitterComponent& emitter,
										  const Mat4& world,
										  const Ref<RHIBuffer>& instances, uint32_t count)
	{
		if (!s_Data || !s_Data->InScene || !instances || count == 0)
			return;

		PendingDraw draw;
		draw.GpuInstances = instances;
		draw.GpuCount = count;
		draw.Blend = emitter.Blend;
		draw.Flat = emitter.Facing == ParticleFacing::Flat;
		draw.Depth = Math::Dot(Vec3(world[3]) - s_Data->CameraPosition, s_Data->CameraForward);

		draw.Texture = emitter.Texture.IsValid()
					 ? Assets::Manager::GetTexture(emitter.Texture)
					 : nullptr;
		if (!draw.Texture)
			draw.Texture = s_Data->WhiteTexture;

		// No per-particle sort. Sorting would mean reading the pool back, and
		// a readback is the one thing the GPU path exists to avoid -- so an
		// alpha-blended GPU emitter blends in pool order. Emitters are still
		// sorted against each other in Flush, which is what covers the common
		// case of two effects overlapping.
		s_Data->ParticleCount += count;
		s_Data->Pending.push_back(std::move(draw));
	}

	void ParticleRenderer::EndScene()
	{
		if (!s_Data)
			return;

		// Weighted draws belong to a later pass writing different attachments,
		// so they are moved aside rather than drawn. Everything else goes now.
		s_Data->Weighted.clear();

		const bool canWeight = s_Data->WeightedPipeline != nullptr;

		auto split = std::stable_partition(s_Data->Pending.begin(), s_Data->Pending.end(),
			[&](const PendingDraw& draw)
			{
				return !(canWeight && draw.Blend == ParticleBlend::WeightedBlended);
			});

		s_Data->Weighted.assign(std::make_move_iterator(split),
								std::make_move_iterator(s_Data->Pending.end()));
		s_Data->Pending.erase(split, s_Data->Pending.end());

		Flush();
		s_Data->Pending.clear();
		s_Data->InScene = false;
	}

	bool ParticleRenderer::HasWeighted()
	{
		return s_Data && !s_Data->Weighted.empty();
	}

	void ParticleRenderer::FlushWeighted()
	{
		if (!s_Data || s_Data->Weighted.empty())
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		EnsurePipelines();
		if (!s_Data->WeightedPipeline)
			return;

		// No sort of any kind here, between emitters or within them. That is
		// the entire point: accumulation is a sum and revealage a product, and
		// neither cares what order it was fed.
		for (const PendingDraw& draw : s_Data->Weighted)
		{
			ParticleRendererData::Batch& batch = AcquireBatch(s_Data->WeightedPipeline);

			const bool onGpu = draw.GpuInstances != nullptr;
			const uint32_t instanceCount = onGpu ? draw.GpuCount
												 : (uint32_t)draw.Instances.size();
			const uint64_t bytes = (uint64_t)instanceCount * sizeof(InstanceData);

			if (!onGpu)
				batch.Instances->Upload(draw.Instances.data(), bytes);

			batch.Scene->Upload(&s_Data->Scene, sizeof(SceneUniforms));

			batch.Set->SetUniformBuffer(0, batch.Scene, 0, sizeof(SceneUniforms));
			batch.Set->SetStorageBuffer(1, onGpu ? draw.GpuInstances : batch.Instances,
										0, bytes);
			batch.Set->SetTexture(2, draw.Texture, s_Data->Sampler);
			batch.Set->Commit();

			cmd->BindPipeline(s_Data->WeightedPipeline);
			cmd->BindResourceSet(0, batch.Set);

			DrawPush push;
			push.BaseInstance = 0;
			push.Flat = draw.Flat ? 1 : 0;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(push), &push);

			cmd->Draw(6, instanceCount);
		}
	}

	void ParticleRenderer::ResolveWeighted(const Ref<RHITexture>& accumulate,
										   const Ref<RHITexture>& revealage)
	{
		if (!s_Data || !accumulate || !revealage)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		EnsurePipelines();
		if (!s_Data->ResolvePipeline)
			return;

		// A batch for its descriptor set; the fullscreen triangle needs no
		// buffers of its own, and the vertex shader builds it from the index.
		ParticleRendererData::Batch& batch = AcquireBatch(s_Data->ResolvePipeline);

		if (!batch.ResolveSet)
			batch.ResolveSet = s_Data->Device->CreateResourceSet(s_Data->ResolvePipeline, 0);

		batch.ResolveSet->SetTexture(0, accumulate, s_Data->Sampler);
		batch.ResolveSet->SetTexture(1, revealage, s_Data->Sampler);
		batch.ResolveSet->Commit();

		cmd->BindPipeline(s_Data->ResolvePipeline);
		cmd->BindResourceSet(0, batch.ResolveSet);

		// Which way up the accumulation targets were stored. Every fullscreen
		// pass in the engine owes this; PostProcess::Dispatch has the reason.
		const float flipY = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(float), &flipY);

		cmd->Draw(3);

		// Consumed. A frame that draws nothing weighted must not composite
		// what the last one left.
		s_Data->Weighted.clear();
	}

	void ParticleRenderer::Flush()
	{
		if (s_Data->Pending.empty())
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		EnsurePipelines();
		if (!s_Data->AlphaPipeline || !s_Data->AdditivePipeline)
			return;

		// Emitters far to near, so an alpha emitter blends over what is
		// behind it -- including other emitters.
		std::sort(s_Data->Pending.begin(), s_Data->Pending.end(),
				  [](const PendingDraw& a, const PendingDraw& b) { return a.Depth > b.Depth; });

		for (const PendingDraw& draw : s_Data->Pending)
		{
			const Ref<RHIPipeline>& pipeline = draw.Blend == ParticleBlend::Additive
											 ? s_Data->AdditivePipeline
											 : s_Data->AlphaPipeline;

			ParticleRendererData::Batch& batch = AcquireBatch(pipeline);

			// A GPU emitter's instances are already on the device; the batch
			// contributes only its scene uniforms and its descriptor set, and
			// its own instance buffer goes unused for this draw.
			const bool onGpu = draw.GpuInstances != nullptr;

			const uint32_t instanceCount = onGpu ? draw.GpuCount
												 : (uint32_t)draw.Instances.size();
			const uint64_t bytes = (uint64_t)instanceCount * sizeof(InstanceData);

			if (!onGpu)
				batch.Instances->Upload(draw.Instances.data(), bytes);

			batch.Scene->Upload(&s_Data->Scene, sizeof(SceneUniforms));

			batch.Set->SetUniformBuffer(0, batch.Scene, 0, sizeof(SceneUniforms));
			batch.Set->SetStorageBuffer(1, onGpu ? draw.GpuInstances : batch.Instances,
										0, bytes);
			batch.Set->SetTexture(2, draw.Texture, s_Data->Sampler);
			batch.Set->Commit();

			cmd->BindPipeline(pipeline);
			cmd->BindResourceSet(0, batch.Set);

			// Each draw's instances start at its own buffer's origin, so the
			// base is always zero -- but it travels as a push constant anyway,
			// because that is the one base-instance mechanism that means the
			// same thing on both backends.
			DrawPush push;
			push.BaseInstance = 0;
			push.Flat = draw.Flat ? 1 : 0;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(push), &push);

			// Six corner vertices from gl_VertexIndex, no vertex buffer.
			cmd->Draw(6, instanceCount);
		}
	}

	uint32_t ParticleRenderer::GetParticleCount()
	{
		return s_Data ? s_Data->ParticleCount : 0;
	}
}
