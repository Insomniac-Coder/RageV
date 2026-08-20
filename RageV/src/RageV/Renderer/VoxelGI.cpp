#include <rvpch.h>
#include "VoxelGI.h"
#include "Renderer.h"
#include "ShadowMap.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Math/Math.h"
#include <algorithm>

namespace RageV
{
	using namespace RHI;

	namespace
	{
		// Mirrors VoxelInstance in voxelize.rvshader, std430.
		struct GpuVoxelInstance
		{
			Mat4 Model;
			Vec4 BaseColor;
			Vec4 EmissiveColor;
		};
		static_assert(sizeof(GpuVoxelInstance) == 96, "Must match VoxelInstance in voxelize.rvshader");

		// Mirrors VoxelParams in voxelize.rvshader, std140: one block for every
		// cascade, the draw's push constant picking which.
		struct GpuVoxelParams
		{
			// xyz = minimum corner, w = voxel size, per cascade
			Vec4 Cascade[VoxelGI::kMaxCascades];
			// x = N, y = cascades, zw unused
			Vec4 Grid;
		};
		static_assert(sizeof(GpuVoxelParams) == 80, "Must match VoxelParams in voxelize.rvshader");

		// Mirrors ObjectData in voxelize.rvshader.
		struct VoxelPushConstants
		{
			int32_t BaseInstance = 0;
			int32_t Axis = 0;
			int32_t Cascade = 0;
			int32_t _pad = 0;
		};

		// Mirrors GatherParams in voxelgi_gather.rvshader, std140.
		struct GpuGatherParams
		{
			Vec4 Texel;
			Vec4 Clip;
			Vec4 CameraRow0, CameraRow1, CameraRow2, CameraPosition;
			Vec4 Grid;
			Vec4 Cascade[VoxelGI::kMaxCascades];
		};
		static_assert(sizeof(GpuGatherParams) == 176, "Must match GatherParams in voxelgi_gather.rvshader");

		// Mirrors InjectParams in voxel_inject.rvshader, std140.
		struct GpuInjectParams
		{
			Vec4 Grid;
			Vec4 Trace;
			Vec4 Cascade[VoxelGI::kMaxCascades];
			Mat4 CascadeLookup[ShadowMap::kMaxCascades];
			Vec4 CascadeTexel;
			Vec4 ShadowTexel;
		};
		static_assert(sizeof(GpuInjectParams) == 384, "Must match InjectParams in voxel_inject.rvshader");

		// Mirrors Params in voxel_mip.rvshader.
		struct MipPushConstants
		{
			int32_t SourceIsotropic = 0;
			int32_t FaceHeight = 0;
			int32_t _pad0 = 0, _pad1 = 0;
		};

		// Mirrors VoxelLight in voxel_inject.rvshader, std430.
		struct GpuVoxelLight
		{
			Vec4 Position;
			Vec4 Direction;
			Vec4 Color;
			Vec4 Params;
		};
		static_assert(sizeof(GpuVoxelLight) == 64, "Must match VoxelLight in voxel_inject.rvshader");

		struct PendingCaster
		{
			const Mesh* MeshKey = nullptr;
			uint64_t MaterialKey = 0;
			Ref<Mesh> MeshRef;
			Ref<Material> MaterialRef;
			GpuVoxelInstance Instance;
		};

		// One frame in flight's worth of what the GPU may still be reading.
		struct FrameSlot
		{
			Ref<RHIBuffer> Instances;
			uint32_t InstanceCapacity = 0;
			Ref<RHIBuffer> VoxelParams;
			Ref<RHIResourceSet> VoxelizeSet;

			Ref<RHIBuffer> InjectParams;
			Ref<RHIBuffer> Lights;
			uint32_t LightCapacity = 0;
			// One per radiance image: the set binds "the other one" as last
			// frame's chain, so it differs by which is current.
			Ref<RHIResourceSet> InjectSet[2];

			// The gather runs once per graph, and the editor builds two.
			struct GatherSlot
			{
				Ref<RHIBuffer> Params;
				Ref<RHIResourceSet> Set;
				const RHIPipeline* Pipeline = nullptr;
			};
			std::vector<GatherSlot> Gathers;
			size_t GatherCursor = 0;
		};

		struct VoxelData
		{
			RHIDevice* Device = nullptr;
			bool Ready = false;

			Ref<RHIShader> VoxelizeShader, ClearShader, InjectShader, MipShader, GatherShader;
			Ref<RHIPipeline> VoxelizePipeline;
			Ref<RHIComputePipeline> ClearPipeline, InjectPipeline, MipPipeline;
			// Keyed by output format, as PostProcess keys its passes.
			std::unordered_map<uint32_t, Ref<RHIPipeline>> GatherPipelines;

			// The grids. Cascades side by side along X (7bc). Radiance is
			// level 0 alone; Faces is the directional chain above it -- six
			// faces stacked along Y, half the side, MipLevels - 1 levels.
			uint32_t Resolution = 0;
			uint32_t CascadeCount = 0;
			uint32_t MipLevels = 0;
			Ref<RHITexture> Albedo, Normal, Emissive;
			Ref<RHITexture> Radiance[2];
			Ref<RHITexture> Faces[2];
			uint32_t Current = 0;   // which Radiance the last update wrote
			Ref<RHIRenderTarget> Dummy;

			Ref<RHISampler> RadianceSampler;
			Ref<RHISampler> PointSampler;

			// Per-level sets for the mip pass, per radiance image; their
			// bindings never change, so they are built once.
			std::vector<Ref<RHIResourceSet>> MipSets[2];
			Ref<RHIResourceSet> ClearSet;

			std::vector<FrameSlot> Frames;

			// This frame's fit.
			Vec3 Origins[VoxelGI::kMaxCascades];
			float Sizes[VoxelGI::kMaxCascades] = {};
			bool HasGrid = false;

			std::vector<PendingCaster> Pending;
			std::vector<GpuVoxelInstance> InstanceScratch;
			std::vector<GpuVoxelLight> LightScratch;
			unsigned int CasterCount = 0;
			unsigned int DrawCalls = 0;
		};

		std::unique_ptr<VoxelData> s_Data;

		uint32_t NearestResolution(int requested)
		{
			if (requested <= 48) return 32;
			if (requested <= 96) return 64;
			return 128;
		}

		// Grows a host-visible buffer to `count` elements of `stride` bytes,
		// in steps, so a scene that fluctuates does not reallocate every
		// frame. The same shape Renderer3D's EnsureInstanceBuffer has.
		bool EnsureBuffer(Ref<RHIBuffer>& buffer, uint32_t& capacity, uint32_t count,
						  uint32_t stride, BufferUsage usage, const char* name)
		{
			if (buffer && capacity >= count)
				return true;
			uint32_t newCapacity = Math::Max(capacity, 64u);
			while (newCapacity < count)
				newCapacity *= 2;
			BufferDesc desc;
			desc.Size = (uint64_t)newCapacity * stride;
			desc.Usage = usage;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = name;
			buffer = s_Data->Device->CreateBuffer(desc);
			capacity = buffer ? newCapacity : 0;
			return buffer != nullptr;
		}

		Ref<RHIBuffer> MakeUniform(uint64_t size, const char* name)
		{
			BufferDesc desc;
			desc.Size = size;
			desc.Usage = BufferUsage::Uniform;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = name;
			return s_Data->Device->CreateBuffer(desc);
		}

		Ref<RHITexture> MakeGrid(const char* name, Format format, uint32_t mips,
								 uint32_t width, uint32_t height, uint32_t depth)
		{
			TextureDesc desc;
			desc.Type = TextureType::Texture3D;
			desc.Width = width;
			desc.Height = height;
			desc.Depth = depth;
			desc.MipLevels = mips;
			desc.Format = format;
			desc.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			desc.DebugName = name;
			return s_Data->Device->CreateTexture(desc);
		}

		// (Re)allocates the grids for a resolution and cascade count, and the
		// sets whose bindings are the grids themselves.
		bool EnsureGrids(uint32_t resolution, uint32_t cascades)
		{
			if (s_Data->Albedo && s_Data->Resolution == resolution && s_Data->CascadeCount == cascades)
				return true;

			s_Data->Resolution = resolution;
			s_Data->CascadeCount = cascades;
			// Stated rather than derived from the width, so the coarsest level
			// is one texel per cascade and not a fraction of one (7bc).
			s_Data->MipLevels = 1 + (uint32_t)Math::Floor(Math::Log2((float)resolution));

			const uint32_t n = resolution;
			const uint32_t width = n * cascades;
			s_Data->Albedo = MakeGrid("VoxelGI.albedo", Format::R8G8B8A8_UNORM, 1, width, n, n);
			s_Data->Normal = MakeGrid("VoxelGI.normal", Format::R8G8B8A8_UNORM, 1, width, n, n);
			s_Data->Emissive = MakeGrid("VoxelGI.emissive", Format::R16G16B16A16_SFLOAT, 1, width, n, n);
			s_Data->Radiance[0] = MakeGrid("VoxelGI.radiance0", Format::R16G16B16A16_SFLOAT, 1, width, n, n);
			s_Data->Radiance[1] = MakeGrid("VoxelGI.radiance1", Format::R16G16B16A16_SFLOAT, 1, width, n, n);
			// The face chain: level 1 and up of the lit grid, per direction
			// (voxel_mip.rvshader): half the side, six faces along Y, with the
			// levels the isotropic chain would have had above its first.
			const uint32_t faceLevels = Math::Max(s_Data->MipLevels - 1, 1u);
			s_Data->Faces[0] = MakeGrid("VoxelGI.faces0", Format::R16G16B16A16_SFLOAT, faceLevels,
										width / 2, (n / 2) * 6, n / 2);
			s_Data->Faces[1] = MakeGrid("VoxelGI.faces1", Format::R16G16B16A16_SFLOAT, faceLevels,
										width / 2, (n / 2) * 6, n / 2);
			if (!s_Data->Albedo || !s_Data->Normal || !s_Data->Emissive
				|| !s_Data->Radiance[0] || !s_Data->Radiance[1]
				|| !s_Data->Faces[0] || !s_Data->Faces[1])
			{
				RV_CORE_ERROR("VoxelGI: could not allocate the grids at {0}^3 x {1}", resolution, cascades);
				s_Data->Albedo.reset();
				return false;
			}

			// The dummy target the voxeliser draws into: N x N, the cheapest
			// colour format, no depth. Its contents are never read.
			RenderTargetDesc target;
			target.Width = resolution;
			target.Height = resolution;
			target.ColorAttachments = { AttachmentDesc{ Format::R8_UNORM, LoadOp::DontCare, StoreOp::Store } };
			target.HasDepth = false;
			target.DebugName = "VoxelGI.dummy";
			s_Data->Dummy = s_Data->Device->CreateRenderTarget(target);

			s_Data->ClearSet = s_Data->Device->CreateResourceSet(s_Data->ClearPipeline, 0);
			s_Data->ClearSet->SetStorageImage(0, s_Data->Albedo, 0);

			// One set per level of the face chain: level 0 of it reads the
			// isotropic level 0, every level after reads the face level below.
			for (uint32_t r = 0; r < 2; r++)
			{
				s_Data->MipSets[r].clear();
				for (uint32_t level = 0; level < faceLevels; level++)
				{
					Ref<RHIResourceSet> set = s_Data->Device->CreateResourceSet(s_Data->MipPipeline, 0);
					if (level == 0)
						set->SetStorageImage(0, s_Data->Radiance[r], 0);
					else
						set->SetStorageImage(0, s_Data->Faces[r], level - 1);
					set->SetStorageImage(1, s_Data->Faces[r], level);
					s_Data->MipSets[r].push_back(set);
				}
			}

			// The per-frame sets bind the grids too, so they are rebuilt.
			for (FrameSlot& frame : s_Data->Frames)
			{
				frame.VoxelizeSet.reset();
				frame.InjectSet[0].reset();
				frame.InjectSet[1].reset();
			}

			RV_CORE_INFO("VoxelGI: grids at {0}^3 x {1} cascades, {2} levels",
						 resolution, cascades, s_Data->MipLevels);
			return true;
		}
	}

	void VoxelGI::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<VoxelData>();
		s_Data->Device = &device;

		if (!device.GetCaps().SupportsCompute || !device.GetCaps().SupportsFragmentStores)
		{
			RV_CORE_WARN("VoxelGI: the device lacks compute or fragment-stage image stores; "
						 "voxel global illumination is unavailable and the screen-space form stays");
			return;
		}

		ShaderCompiler::Init();

		auto compile = [&](const char* path) -> Ref<RHIShader>
		{
			auto compiled = ShaderCompiler::CompileFromFile(path);
			if (!compiled)
			{
				RV_CORE_ERROR("VoxelGI: failed to compile {0}", path);
				return nullptr;
			}
			return device.CreateShader(*compiled);
		};

		s_Data->VoxelizeShader = compile("assets/shaders/voxelize.rvshader");
		s_Data->ClearShader = compile("assets/shaders/voxel_clear.rvshader");
		s_Data->InjectShader = compile("assets/shaders/voxel_inject.rvshader");
		s_Data->MipShader = compile("assets/shaders/voxel_mip.rvshader");
		s_Data->GatherShader = compile("assets/shaders/voxelgi_gather.rvshader");
		if (!s_Data->VoxelizeShader || !s_Data->ClearShader || !s_Data->InjectShader
			|| !s_Data->MipShader || !s_Data->GatherShader)
		{
			return;
		}

		{
			GraphicsPipelineDesc desc;
			desc.Name = "VoxelGI.voxelize";
			desc.Shader = s_Data->VoxelizeShader;
			desc.Topology = PrimitiveTopology::TriangleList;
			// Stated rather than reflected, for the shadow pipeline's reason:
			// the stride is the mesh's, whatever the shader reads.
			VertexBinding binding;
			binding.Binding = 0;
			binding.Stride = sizeof(MeshVertex);
			desc.VertexInput.Bindings = { binding };
			desc.VertexInput.Attributes = {
				{ 0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, Position) },
				{ 1, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, Normal) },
				{ 2, 0, Format::R32G32_SFLOAT,    offsetof(MeshVertex, TexCoord) },
			};
			// Nothing culled and no depth: every triangle of every surface,
			// whichever way it faces, lands in the grid.
			desc.Rasterizer.Cull = CullMode::None;
			desc.Rasterizer.Front = FrontFace::CounterClockwise;
			desc.DepthStencil.DepthTestEnable = false;
			desc.DepthStencil.DepthWriteEnable = false;
			desc.ColorFormats = { Format::R8_UNORM };
			desc.DepthFormat = Format::Undefined;
			s_Data->VoxelizePipeline = device.CreatePipeline(desc);
		}

		auto computePipeline = [&](const char* name, const Ref<RHIShader>& shader)
		{
			ComputePipelineDesc desc;
			desc.Name = name;
			desc.Shader = shader;
			return device.CreateComputePipeline(desc);
		};
		s_Data->ClearPipeline = computePipeline("VoxelGI.clear", s_Data->ClearShader);
		s_Data->InjectPipeline = computePipeline("VoxelGI.inject", s_Data->InjectShader);
		s_Data->MipPipeline = computePipeline("VoxelGI.mip", s_Data->MipShader);
		if (!s_Data->VoxelizePipeline || !s_Data->ClearPipeline || !s_Data->InjectPipeline
			|| !s_Data->MipPipeline)
		{
			RV_CORE_ERROR("VoxelGI: a pipeline failed to build");
			return;
		}

		// The lit chain's sampler: trilinear, clamped, the whole chain.
		SamplerDesc sampler;
		sampler.MinFilter = FilterMode::Linear;
		sampler.MagFilter = FilterMode::Linear;
		sampler.Mipmap = MipmapMode::Linear;
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.WrapW = WrapMode::ClampToEdge;
		sampler.MaxLod = 16.0f;
		s_Data->RadianceSampler = device.CreateSampler(sampler);

		// Depth and the surface attachment, point sampled, as every
		// reconstruction pass samples them.
		sampler.MinFilter = FilterMode::Nearest;
		sampler.MagFilter = FilterMode::Nearest;
		sampler.Mipmap = MipmapMode::Nearest;
		sampler.MaxLod = 0.0f;
		s_Data->PointSampler = device.CreateSampler(sampler);

		s_Data->Frames.resize(device.GetFramesInFlight());
		s_Data->Ready = s_Data->RadianceSampler && s_Data->PointSampler;
		if (s_Data->Ready)
			RV_CORE_INFO("Voxel global illumination ready");
	}

	void VoxelGI::Shutdown()
	{
		s_Data.reset();
	}

	bool VoxelGI::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void VoxelGI::BeginFrame()
	{
		if (!s_Data || s_Data->Frames.empty())
			return;
		FrameSlot& frame = s_Data->Frames[s_Data->Device->GetFrameIndex()];
		frame.GatherCursor = 0;
	}

	void VoxelGI::Invalidate()
	{
		if (s_Data)
			s_Data->HasGrid = false;
	}

	bool VoxelGI::HasGrid()
	{
		return s_Data && s_Data->HasGrid;
	}

	unsigned int VoxelGI::GetCasterCount() { return s_Data ? s_Data->CasterCount : 0; }
	unsigned int VoxelGI::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }

	void VoxelGI::Submit(const Ref<Mesh>& mesh, const Mat4& world,
						 const Ref<Material>& material, const MaterialParams& params)
	{
		if (!s_Data || !mesh || mesh->IsSkinned())
			return;

		PendingCaster caster;
		caster.MeshKey = mesh.get();
		caster.MeshRef = mesh;
		caster.MaterialRef = material;
		// The bound key, always: the voxeliser binds materials on every path.
		caster.MaterialKey = material ? material->GetBatchKey(false) : 0;
		caster.Instance.Model = world;
		caster.Instance.BaseColor = params.BaseColor;
		caster.Instance.EmissiveColor = params.EmissiveColor;
		s_Data->Pending.push_back(std::move(caster));
	}

	void VoxelGI::Update(RHICommandList& cmd, const VoxelGiSettings& settings,
						 const Vec3& cameraPosition, const LightList& lights,
						 const DrawCasters& draw)
	{
		s_Data->HasGrid = false;
		if (!IsReady() || !draw)
			return;

		const uint32_t resolution = NearestResolution(settings.Resolution);
		const uint32_t cascades = (uint32_t)Math::Clamp(settings.Cascades, 1, (int)kMaxCascades);
		const float voxelSize = Math::Clamp(settings.VoxelSize, 0.05f, 4.0f);
		if (!EnsureGrids(resolution, cascades))
			return;

		// --- the fit: each cascade centred on the camera, its origin snapped
		// to eight of its own voxels so a still scene under a moving camera
		// lands in the same cells and the first levels of the chain hold
		// still (7bc).
		for (uint32_t k = 0; k < cascades; k++)
		{
			const float size = voxelSize * (float)(1u << k);
			const float extent = size * (float)resolution;
			const float snap = size * 8.0f;
			Vec3 origin = cameraPosition - Vec3(extent * 0.5f);
			origin = Vec3(Math::Floor(origin.x / snap) * snap,
						  Math::Floor(origin.y / snap) * snap,
						  Math::Floor(origin.z / snap) * snap);
			s_Data->Origins[k] = origin;
			s_Data->Sizes[k] = size;
		}
		for (uint32_t k = cascades; k < kMaxCascades; k++)
		{
			s_Data->Origins[k] = s_Data->Origins[cascades - 1];
			s_Data->Sizes[k] = s_Data->Sizes[cascades - 1];
		}

		const uint32_t frameIndex = s_Data->Device->GetFrameIndex();
		FrameSlot& frame = s_Data->Frames[frameIndex];
		const uint32_t previous = s_Data->Current;
		const uint32_t current = 1 - previous;

		// --- the walk, into Pending
		s_Data->Pending.clear();
		{
			const float outer = s_Data->Sizes[cascades - 1] * (float)resolution;
			const Vec3 min = s_Data->Origins[cascades - 1];
			draw(min, min + Vec3(outer));
		}
		s_Data->CasterCount = (unsigned int)s_Data->Pending.size();
		s_Data->DrawCalls = 0;

		// --- clear the occupancy
		// Last frame's gather and injection read these; this frame's writes
		// wait for them (the reverse half BufferBarrier's note insists on).
		cmd.TextureBarrier(s_Data->Albedo, TextureSync::ShaderRead, TextureSync::ComputeWrite);
		cmd.TextureBarrier(s_Data->Radiance[current], TextureSync::ShaderRead, TextureSync::ComputeWrite);
		cmd.TextureBarrier(s_Data->Faces[current], TextureSync::ShaderRead, TextureSync::ComputeWrite);
		cmd.TextureBarrier(s_Data->Radiance[previous], TextureSync::ComputeWrite, TextureSync::ShaderRead);
		cmd.TextureBarrier(s_Data->Faces[previous], TextureSync::ComputeWrite, TextureSync::ShaderRead);

		cmd.PushDebugGroup("Voxel GI");
		{
			const uint32_t width = resolution * cascades;
			s_Data->ClearSet->Commit();
			cmd.BindComputePipeline(s_Data->ClearPipeline);
			cmd.BindResourceSet(0, s_Data->ClearSet);
			cmd.Dispatch((width + 7) / 8, (resolution + 7) / 8, (resolution + 7) / 8);
			cmd.TextureBarrier(s_Data->Albedo, TextureSync::ComputeWrite, TextureSync::FragmentWrite);
			cmd.TextureBarrier(s_Data->Normal, TextureSync::ShaderRead, TextureSync::FragmentWrite);
			cmd.TextureBarrier(s_Data->Emissive, TextureSync::ShaderRead, TextureSync::FragmentWrite);
		}

		// --- voxelise
		if (!s_Data->Pending.empty())
		{
			auto& pending = s_Data->Pending;
			// Grouped by material, then mesh: the material bind is the more
			// expensive state here, and a run shares both.
			std::sort(pending.begin(), pending.end(),
					  [](const PendingCaster& a, const PendingCaster& b)
					  {
						  if (a.MaterialKey != b.MaterialKey)
							  return a.MaterialKey < b.MaterialKey;
						  return a.MeshKey < b.MeshKey;
					  });

			const uint32_t count = (uint32_t)pending.size();
			if (!EnsureBuffer(frame.Instances, frame.InstanceCapacity, count,
							  sizeof(GpuVoxelInstance), BufferUsage::Storage, "VoxelGI.instances"))
			{
				cmd.PopDebugGroup();
				return;
			}
			s_Data->InstanceScratch.clear();
			s_Data->InstanceScratch.reserve(count);
			for (const PendingCaster& caster : pending)
				s_Data->InstanceScratch.push_back(caster.Instance);
			frame.Instances->Upload(s_Data->InstanceScratch.data(), (uint64_t)count * sizeof(GpuVoxelInstance));

			if (!frame.VoxelParams)
				frame.VoxelParams = MakeUniform(sizeof(GpuVoxelParams), "VoxelGI.params");
			GpuVoxelParams params{};
			for (uint32_t k = 0; k < kMaxCascades; k++)
				params.Cascade[k] = Vec4(s_Data->Origins[k], s_Data->Sizes[k]);
			params.Grid = Vec4((float)resolution, (float)cascades, 0.0f, 0.0f);
			frame.VoxelParams->Upload(&params, sizeof(params));

			if (!frame.VoxelizeSet)
				frame.VoxelizeSet = s_Data->Device->CreateResourceSet(s_Data->VoxelizePipeline, 0);
			frame.VoxelizeSet->SetUniformBuffer(0, frame.VoxelParams, 0, sizeof(GpuVoxelParams));
			frame.VoxelizeSet->SetStorageBuffer(1, frame.Instances, 0, (uint64_t)count * sizeof(GpuVoxelInstance));
			frame.VoxelizeSet->SetStorageImage(2, s_Data->Albedo, 0);
			frame.VoxelizeSet->SetStorageImage(3, s_Data->Normal, 0);
			frame.VoxelizeSet->SetStorageImage(4, s_Data->Emissive, 0);
			frame.VoxelizeSet->Commit();

			RenderPassBeginInfo begin;
			begin.Target = s_Data->Dummy.get();
			begin.ClearColor = false;
			begin.ClearDepth = false;
			begin.UseDepth = false;
			cmd.PushDebugGroup("Voxelise");
			cmd.BeginRenderPass(begin);

			cmd.BindPipeline(s_Data->VoxelizePipeline);
			cmd.BindResourceSet(0, frame.VoxelizeSet);

			// Every cascade, every axis, every run. The material and the
			// mesh are bound once per run and the draw repeated per cascade
			// and axis inside it, so the expensive state changes are the
			// fewest.
			uint32_t start = 0;
			while (start < count)
			{
				uint32_t end = start + 1;
				while (end < count && pending[end].MaterialKey == pending[start].MaterialKey
					   && pending[end].MeshKey == pending[start].MeshKey)
				{
					end++;
				}

				const PendingCaster& first = pending[start];

				// No material, no draw. This used to bind set 1 only where
				// there was one and then draw either way, which is a draw whose
				// pipeline statically uses a set that was never bound -- the
				// validation error `all sets 0 to 1 are not compatible`. It
				// stayed hidden because the voxel form's checks shoot at frame
				// 60, by which time every material has resolved; a caster whose
				// material is still loading only exists in the first frames.
				// Skipping is also the right answer on its own terms: a surface
				// whose albedo is not known yet cannot be voxelised, and a
				// frame later it will be.
				if (!first.MaterialRef)
				{
					start = end;
					continue;
				}
				first.MaterialRef->Bind(cmd, s_Data->VoxelizePipeline, 1);

				cmd.BindVertexBuffer(0, first.MeshRef->GetVertexBuffer());
				cmd.BindIndexBuffer(first.MeshRef->GetIndexBuffer(), IndexType::UInt32);

				for (uint32_t k = 0; k < cascades; k++)
				{
					for (int axis = 0; axis < 3; axis++)
					{
						VoxelPushConstants push;
						push.BaseInstance = (int32_t)start;
						push.Axis = axis;
						push.Cascade = (int32_t)k;
						cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(push), &push);
						cmd.DrawIndexed(first.MeshRef->GetIndexCount(), end - start);
						s_Data->DrawCalls++;
					}
				}
				start = end;
			}

			cmd.EndRenderPass();
			cmd.PopDebugGroup();
		}

		// --- inject
		cmd.TextureBarrier(s_Data->Albedo, TextureSync::FragmentWrite, TextureSync::ShaderRead);
		cmd.TextureBarrier(s_Data->Normal, TextureSync::FragmentWrite, TextureSync::ShaderRead);
		cmd.TextureBarrier(s_Data->Emissive, TextureSync::FragmentWrite, TextureSync::ShaderRead);
		{
			// The lights, as the lit shader has them, plus which one the
			// cascades belong to.
			s_Data->LightScratch.clear();
			const int shadowLight = ShadowMap::GetLightIndex();
			const bool haveCascades = ShadowMap::HasCascades() && ShadowMap::GetCascadeCount() > 0;
			for (size_t i = 0; i < lights.size(); i++)
			{
				const LightRenderData& light = lights[i];
				GpuVoxelLight entry{};
				const bool positional = light.Type != Light::LightType::Directional;
				entry.Position = Vec4(light.Position, positional ? 1.0f : 0.0f);
				entry.Direction = Vec4(light.Direction, 0.0f);
				entry.Color = Vec4(light.Color, light.Intensity);
				const float cosInner = Math::Cos(Math::Radians(light.InnerCone));
				const float cosOuter = Math::Cos(Math::Radians(light.OuterCone));
				const bool cascaded = !positional && haveCascades && (int)i == shadowLight
								   && light.CastShadows;
				entry.Params = Vec4(light.Range,
									light.Type == Light::LightType::Spot ? cosInner : 1.0f,
									light.Type == Light::LightType::Spot ? cosOuter : 1.0f,
									cascaded ? 1.0f : 0.0f);
				s_Data->LightScratch.push_back(entry);
			}
			const uint32_t lightCount = Math::Max((uint32_t)s_Data->LightScratch.size(), 1u);
			if (!EnsureBuffer(frame.Lights, frame.LightCapacity, lightCount,
							  sizeof(GpuVoxelLight), BufferUsage::Storage, "VoxelGI.lights"))
			{
				cmd.PopDebugGroup();
				return;
			}
			if (s_Data->LightScratch.empty())
			{
				const GpuVoxelLight none{};
				frame.Lights->Upload(&none, sizeof(none));
			}
			else
			{
				frame.Lights->Upload(s_Data->LightScratch.data(),
									 s_Data->LightScratch.size() * sizeof(GpuVoxelLight));
			}

			if (!frame.InjectParams)
				frame.InjectParams = MakeUniform(sizeof(GpuInjectParams), "VoxelGI.inject");
			GpuInjectParams inject{};
			const float maxMip = (float)Math::Max((int)s_Data->MipLevels - 2, 0);
			const float maxDistance = s_Data->Sizes[cascades - 1] * (float)resolution;
			const uint32_t cascadeMaps = haveCascades ? ShadowMap::GetCascadeCount() : 0u;
			inject.Grid = Vec4((float)resolution, (float)cascades,
							   (float)Math::Clamp(settings.Bounces, 1, 2),
							   (float)s_Data->LightScratch.size());
			inject.Trace = Vec4(maxMip, maxDistance, (float)cascadeMaps, settings.ShadowNormalOffset);
			for (uint32_t k = 0; k < kMaxCascades; k++)
				inject.Cascade[k] = Vec4(s_Data->Origins[k], s_Data->Sizes[k]);
			if (const ShadowCascade* shadowCascades = ShadowMap::GetCascades())
			{
				for (uint32_t c = 0; c < ShadowMap::kMaxCascades; c++)
				{
					inject.CascadeLookup[c] = shadowCascades[c].LookupMatrix;
					inject.CascadeTexel[(int)c] = shadowCascades[c].TexelWorldSize;
				}
			}
			inject.ShadowTexel = Vec4(1.0f / (float)Math::Max(ShadowMap::GetResolution(), 1u));
			frame.InjectParams->Upload(&inject, sizeof(inject));

			Ref<RHIResourceSet>& set = frame.InjectSet[current];
			if (!set)
				set = s_Data->Device->CreateResourceSet(s_Data->InjectPipeline, 0);
			set->SetStorageImage(0, s_Data->Albedo, 0);
			set->SetStorageImage(1, s_Data->Normal, 0);
			set->SetStorageImage(2, s_Data->Emissive, 0);
			set->SetStorageImage(3, s_Data->Radiance[current], 0);
			set->SetTexture(4, s_Data->Radiance[previous], s_Data->RadianceSampler);
			set->SetTexture(8, s_Data->Faces[previous], s_Data->RadianceSampler);
			set->SetUniformBuffer(5, frame.InjectParams, 0, sizeof(GpuInjectParams));
			set->SetStorageBuffer(6, frame.Lights, 0, (uint64_t)lightCount * sizeof(GpuVoxelLight));
			for (uint32_t c = 0; c < ShadowMap::kMaxCascades; c++)
			{
				Ref<RHITexture> map = c < cascadeMaps ? ShadowMap::GetCascadeTexture(c) : nullptr;
				set->SetTexture(7, map ? map : ShadowMap::GetEmptyTexture(), ShadowMap::GetSampler(), c);
			}
			set->Commit();

			const uint32_t width = resolution * cascades;
			cmd.PushDebugGroup("Inject");
			cmd.BindComputePipeline(s_Data->InjectPipeline);
			cmd.BindResourceSet(0, set);
			cmd.Dispatch((width + 7) / 8, (resolution + 7) / 8, (resolution + 7) / 8);
			cmd.PopDebugGroup();
		}

		// --- the directional mip chain (voxel_mip.rvshader)
		{
			cmd.PushDebugGroup("Mip");
			cmd.TextureBarrier(s_Data->Radiance[current], TextureSync::ComputeWrite, TextureSync::ShaderRead);
			uint32_t width = (resolution * cascades) / 2, faceHeight = resolution / 2, depth = resolution / 2;
			const uint32_t faceLevels = (uint32_t)s_Data->MipSets[current].size();
			cmd.BindComputePipeline(s_Data->MipPipeline);
			for (uint32_t level = 0; level < faceLevels; level++)
			{
				if (level > 0)
					cmd.TextureBarrier(s_Data->Faces[current], TextureSync::ComputeWrite, TextureSync::ShaderRead);
				Ref<RHIResourceSet>& set = s_Data->MipSets[current][level];
				set->Commit();
				cmd.BindResourceSet(0, set);
				MipPushConstants push;
				push.SourceIsotropic = level == 0 ? 1 : 0;
				push.FaceHeight = (int32_t)Math::Max(faceHeight, 1u);
				cmd.PushConstants(ShaderStage::Compute, 0, sizeof(push), &push);
				const uint32_t height = Math::Max(faceHeight, 1u) * 6;
				cmd.Dispatch((Math::Max(width, 1u) + 3) / 4, (height + 3) / 4, (Math::Max(depth, 1u) + 3) / 4);
				width = Math::Max(width / 2u, 1u);
				faceHeight = Math::Max(faceHeight / 2u, 1u);
				depth = Math::Max(depth / 2u, 1u);
			}
			cmd.TextureBarrier(s_Data->Faces[current], TextureSync::ComputeWrite, TextureSync::ShaderRead);
			cmd.PopDebugGroup();
		}
		cmd.PopDebugGroup();

		s_Data->Current = current;
		s_Data->HasGrid = true;
	}

	void VoxelGI::Gather(RHICommandList& cmd, const Ref<RHITexture>& depth,
						 const Ref<RHITexture>& surface, uint32_t width, uint32_t height,
						 const PostProcess::ViewReconstruction& view, Format outputFormat)
	{
		if (!IsReady() || !s_Data->HasGrid || !depth || !surface)
			return;

		auto it = s_Data->GatherPipelines.find((uint32_t)outputFormat);
		if (it == s_Data->GatherPipelines.end())
		{
			GraphicsPipelineDesc desc;
			desc.Name = "VoxelGI.gather";
			desc.Shader = s_Data->GatherShader;
			desc.Topology = PrimitiveTopology::TriangleList;
			desc.Rasterizer.Cull = CullMode::None;
			desc.Blend = BlendPreset::Opaque;
			desc.DepthStencil.DepthTestEnable = false;
			desc.DepthStencil.DepthWriteEnable = false;
			desc.ColorFormats = { outputFormat };
			desc.DepthFormat = Format::Undefined;
			it = s_Data->GatherPipelines.emplace((uint32_t)outputFormat, s_Data->Device->CreatePipeline(desc)).first;
		}
		const Ref<RHIPipeline>& pipeline = it->second;
		if (!pipeline)
			return;

		FrameSlot& frame = s_Data->Frames[s_Data->Device->GetFrameIndex()];
		while (frame.GatherCursor >= frame.Gathers.size())
			frame.Gathers.push_back({});
		FrameSlot::GatherSlot& slot = frame.Gathers[frame.GatherCursor++];
		if (!slot.Params)
			slot.Params = MakeUniform(sizeof(GpuGatherParams), "VoxelGI.gather");
		if (!slot.Set || slot.Pipeline != pipeline.get())
		{
			slot.Set = s_Data->Device->CreateResourceSet(pipeline, 0);
			slot.Pipeline = pipeline.get();
		}

		GpuGatherParams params{};
		const float flip = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		params.Texel = Vec4(1.0f / (float)Math::Max(width, 1u), 1.0f / (float)Math::Max(height, 1u), flip, 0.0f);
		params.Clip = Vec4(view.NearClip, view.FarClip, view.InvProjection0, view.InvProjection1);
		const Mat4 camera = Math::Inverse(view.View);
		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0], 0.0f);
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], 0.0f);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], 0.0f);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], 0.0f);
		const uint32_t cascades = s_Data->CascadeCount;
		params.Grid = Vec4((float)s_Data->Resolution, (float)cascades,
						   (float)Math::Max((int)s_Data->MipLevels - 2, 0),
						   s_Data->Sizes[cascades - 1] * (float)s_Data->Resolution);
		for (uint32_t k = 0; k < kMaxCascades; k++)
			params.Cascade[k] = Vec4(s_Data->Origins[k], s_Data->Sizes[k]);
		slot.Params->Upload(&params, sizeof(params));

		slot.Set->SetTexture(0, depth, s_Data->PointSampler);
		slot.Set->SetTexture(1, surface, s_Data->PointSampler);
		slot.Set->SetTexture(2, s_Data->Radiance[s_Data->Current], s_Data->RadianceSampler);
		slot.Set->SetTexture(4, s_Data->Faces[s_Data->Current], s_Data->RadianceSampler);
		slot.Set->SetUniformBuffer(3, slot.Params, 0, sizeof(GpuGatherParams));
		slot.Set->Commit();

		cmd.BindPipeline(pipeline);
		cmd.BindResourceSet(0, slot.Set);
		cmd.Draw(3);
	}
}
