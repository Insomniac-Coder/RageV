#include <rvpch.h>
#include "RayShadows.h"
#include "Mesh.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

namespace RageV
{
	using namespace RHI;

	namespace
	{
		// One skinned caster's per-frame resources (ENGINE-NOTES 7an): the
		// buffer the compute pass poses into, the structure refit from it,
		// and the set that binds the three buffers to the pass. Reused by
		// index across frames; a slot whose mesh changed recreates that
		// frame's buffer and structure, and the first build after that is a
		// full one because a refit needs the previous shape to have been
		// this mesh's.
		struct SkinnedCaster
		{
			// [frame in flight]
			std::vector<Ref<RHIBuffer>> Posed;
			std::vector<Ref<RHIAccelerationStructure>> Structures;
			std::vector<Ref<RHIResourceSet>> Sets;
			std::vector<const Mesh*> BuiltFor;
		};

		// What Build has to pose before it builds: which caster, with which
		// mesh, from which bones.
		struct PendingSkin
		{
			uint32_t Caster = 0;
			Ref<Mesh> MeshRef;
			uint32_t BoneBase = 0;
		};

		struct SkinParams
		{
			uint32_t VertexCount = 0;
			uint32_t BoneBase = 0;
		};

		struct RayShadowsData
		{
			RHIDevice* Device = nullptr;
			bool Available = false;

			// [frame in flight]
			std::vector<Ref<RHIAccelerationStructure>> Structures;
			uint32_t Capacity = 0;
			Ref<RHIAccelerationStructure> Empty;

			std::vector<AccelerationInstance> Instances;
			// Parallel to Instances: what each one is, for shading a hit (7ao).
			std::vector<RayCaster> Records;
			bool Active = false;
			bool BuiltThisFrame = false;
			uint32_t BuiltCount = 0;

			// The posing pass. Absent -- and every skinned caster traces at
			// its bind pose -- when the shader did not compile or the device
			// has no compute, which a device with ray queries always has.
			Ref<RHIShader> SkinShader;
			Ref<RHIComputePipeline> SkinPipeline;
			std::vector<SkinnedCaster> Casters;
			uint32_t CasterCursor = 0;
			std::vector<PendingSkin> Skins;
			std::vector<Mat4> BoneScratch;
			// [frame in flight]
			std::vector<Ref<RHIBuffer>> Bones;
			std::vector<uint32_t> BoneCapacity;
			uint32_t SkinnedThisFrame = 0;
		};

		std::unique_ptr<RayShadowsData> s_Data;

		constexpr uint32_t kInitialCapacity = 256;

		// Grows the per-frame structures to hold at least `count` instances,
		// in powers of two, so a scene that creeps upwards does not reallocate
		// every frame. The old ones are released through the deletion queue.
		void EnsureCapacity(uint32_t count)
		{
			if (s_Data->Capacity >= count && !s_Data->Structures.empty())
				return;

			uint32_t target = s_Data->Capacity > 0 ? s_Data->Capacity : kInitialCapacity;
			while (target < count)
				target *= 2;

			const uint32_t frames = s_Data->Device->GetFramesInFlight();
			s_Data->Structures.assign(frames, nullptr);
			for (uint32_t i = 0; i < frames; i++)
				s_Data->Structures[i] = s_Data->Device->CreateTopLevelAS(target);
			s_Data->Capacity = target;
		}

		// A host-visible storage buffer of at least `count` matrices for this
		// frame's bones, grown in powers of two like every per-frame pool.
		bool EnsureBones(uint32_t frame, uint32_t count)
		{
			if (s_Data->Bones[frame] && s_Data->BoneCapacity[frame] >= count)
				return true;

			uint32_t target = s_Data->BoneCapacity[frame] > 0 ? s_Data->BoneCapacity[frame] : 256;
			while (target < count)
				target *= 2;

			BufferDesc desc;
			desc.Size = (uint64_t)target * sizeof(Mat4);
			desc.Usage = BufferUsage::Storage;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = "RayShadows.bones";
			Ref<RHIBuffer> grown = s_Data->Device->CreateBuffer(desc);
			if (!grown)
				return false;

			s_Data->Bones[frame] = grown;
			s_Data->BoneCapacity[frame] = target;
			return true;
		}

		// This frame's posed buffer and dynamic structure for one caster,
		// made or remade for the mesh it is about to pose.
		bool EnsureCaster(SkinnedCaster& caster, uint32_t frame, const Ref<Mesh>& mesh)
		{
			const uint32_t frames = s_Data->Device->GetFramesInFlight();
			if (caster.Posed.size() != frames)
			{
				caster.Posed.assign(frames, nullptr);
				caster.Structures.assign(frames, nullptr);
				caster.Sets.assign(frames, nullptr);
				caster.BuiltFor.assign(frames, nullptr);
			}

			if (caster.BuiltFor[frame] == mesh.get() && caster.Posed[frame] && caster.Structures[frame])
				return true;

			BufferDesc posedDesc;
			posedDesc.Size = (uint64_t)Math::Max(mesh->GetVertexCount(), 1u) * sizeof(Vec4);
			posedDesc.Usage = BufferUsage::Storage | BufferUsage::AccelerationStructureInput;
			posedDesc.Memory = MemoryDomain::DeviceLocal;
			posedDesc.DebugName = mesh->GetVertexBuffer()->GetDesc().DebugName + ".posed";
			Ref<RHIBuffer> posed = s_Data->Device->CreateBuffer(posedDesc);
			if (!posed)
				return false;

			AccelerationGeometryDesc geometry;
			geometry.Vertices = posed;
			geometry.VertexStride = sizeof(Vec4);
			geometry.VertexCount = mesh->GetVertexCount();
			geometry.Indices = mesh->GetIndexBuffer();
			geometry.Type = IndexType::UInt32;
			geometry.IndexCount = mesh->GetIndexCount();
			geometry.Dynamic = true;
			geometry.DebugName = mesh->GetVertexBuffer()->GetDesc().DebugName + ".posed.blas";
			Ref<RHIAccelerationStructure> structure = s_Data->Device->CreateBottomLevelAS(geometry);
			if (!structure)
				return false;

			if (!caster.Sets[frame])
				caster.Sets[frame] = s_Data->Device->CreateResourceSet(s_Data->SkinPipeline, 0);
			if (!caster.Sets[frame])
				return false;

			caster.Posed[frame] = posed;
			caster.Structures[frame] = structure;
			caster.BuiltFor[frame] = mesh.get();
			return true;
		}
	}

	void RayShadows::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<RayShadowsData>();
		s_Data->Device = &device;
		s_Data->Available = device.GetCaps().SupportsRayQuery;
		if (!s_Data->Available)
			return;

		EnsureCapacity(kInitialCapacity);

		// The stand-in: built once with nothing in it, so a set that must name
		// a structure on a frame that traced nothing has one to name, and
		// every ray into it misses -- "lit", which is what a frame with no
		// shadow information means everywhere else in this renderer.
		s_Data->Empty = device.CreateTopLevelAS(1);
		if (s_Data->Empty)
		{
			device.ExecuteImmediate([&](RHICommandList& cmd)
			{
				cmd.BuildTopLevelAS(s_Data->Empty, nullptr, 0);
			});
		}

		// The posing pass (7an). A device that traces has compute; the guard
		// is for the shader, which is a file and can be missing.
		const uint32_t frames = device.GetFramesInFlight();
		s_Data->Bones.assign(frames, nullptr);
		s_Data->BoneCapacity.assign(frames, 0);
		if (device.GetCaps().SupportsCompute)
		{
			ShaderCompiler::Init();
			if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/skin_positions.rvshader"))
			{
				s_Data->SkinShader = device.CreateShader(*compiled);
				if (s_Data->SkinShader)
				{
					ComputePipelineDesc desc;
					desc.Name = "RayShadows.skin";
					desc.Shader = s_Data->SkinShader;
					s_Data->SkinPipeline = device.CreateComputePipeline(desc);
				}
			}
		}
		if (!s_Data->SkinPipeline)
		{
			RV_CORE_WARN("Ray-traced shadows: the skinning pass did not build; skinned casters "
						 "trace at their bind pose");
		}

		RV_CORE_INFO("Ray-traced shadows available: {0} instances per frame to start", kInitialCapacity);
	}

	void RayShadows::Shutdown()
	{
		s_Data.reset();
	}

	bool RayShadows::IsAvailable()
	{
		return s_Data && s_Data->Available;
	}

	void RayShadows::BeginFrame()
	{
		if (!s_Data)
			return;
		s_Data->Active = false;
		s_Data->BuiltThisFrame = false;
		s_Data->SkinnedThisFrame = 0;
		ClearInstances();
	}

	void RayShadows::ClearInstances()
	{
		if (!s_Data)
			return;
		s_Data->Instances.clear();
		s_Data->Records.clear();
		s_Data->Skins.clear();
		s_Data->BoneScratch.clear();
		s_Data->CasterCursor = 0;
	}

	void RayShadows::AddInstance(const Ref<Mesh>& mesh, const Mat4& world, const std::vector<Mat4>* bones,
								 const Ref<Material>& material, const MaterialParams& params,
								 uint64_t owner, bool isStatic)
	{
		if (!s_Data || !s_Data->Available || !mesh)
			return;

		AccelerationInstance instance;
		memcpy(instance.Transform, &world[0][0], sizeof(instance.Transform));
		instance.CustomIndex = (uint32_t)s_Data->Instances.size();

		// **Only a cutout asks traversal to stop and think.** Everything else
		// keeps the hardware's own commit, so a scene without cutouts pays
		// nothing for the loop that tests them.
		instance.ForceNoOpaque = material && material->GetBlendMode() == BlendMode::Masked;

		// **One of two worlds** (kMaskStatic / kMaskMoving, 7cx): what lets
		// the bake's solve see only what it bakes, and a static pixel's
		// subtractive shadow ray see only what can move.
		instance.Mask = isStatic ? kMaskStatic : kMaskMoving;

		RayCaster record;
		record.MeshRef = mesh;
		record.MaterialRef = material;
		record.Params = params;
		record.Owner = owner;
		record.Static = isStatic;

		// A posed skinned caster: this frame's structure for a caster slot,
		// refit in Build from the vertices the compute pass writes. Falls
		// through to the mesh's own structure when there is no pass to pose
		// with, so the shadow is present and stated rather than absent.
		if (bones && !bones->empty() && mesh->IsSkinned() && s_Data->SkinPipeline
			&& mesh->GetVertexCount() > 0 && mesh->GetIndexCount() >= 3)
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			const uint32_t index = s_Data->CasterCursor++;
			while (index >= s_Data->Casters.size())
				s_Data->Casters.push_back({});

			SkinnedCaster& caster = s_Data->Casters[index];
			if (EnsureCaster(caster, frame, mesh))
			{
				PendingSkin skin;
				skin.Caster = index;
				skin.MeshRef = mesh;
				skin.BoneBase = (uint32_t)s_Data->BoneScratch.size();
				s_Data->BoneScratch.insert(s_Data->BoneScratch.end(), bones->begin(), bones->end());
				s_Data->Skins.push_back(std::move(skin));

				instance.Blas = caster.Structures[frame];
				record.Posed = caster.Posed[frame];
				s_Data->Instances.push_back(std::move(instance));
				s_Data->Records.push_back(std::move(record));
				return;
			}
		}

		// A mesh the device could not build a structure for is left out
		// rather than failing the frame; the mesh says so once, at build.
		const Ref<RHIAccelerationStructure>& blas = mesh->GetAccelerationStructure(*s_Data->Device);
		if (!blas)
			return;

		instance.Blas = blas;
		s_Data->Instances.push_back(std::move(instance));
		s_Data->Records.push_back(std::move(record));
	}

	const std::vector<RayCaster>& RayShadows::GetCasters()
	{
		static const std::vector<RayCaster> none;
		return s_Data ? s_Data->Records : none;
	}

	void RayShadows::Build(RHICommandList& cmd)
	{
		if (!s_Data || !s_Data->Available)
			return;
		if (s_Data->BuiltThisFrame)
		{
			// The structure belongs to the frame, not to the view: an earlier
			// view built it, and this one has since rebuilt the record list
			// that a hit's custom index is resolved through. The two must
			// describe the same world, or a hit carries an index into a list
			// that no longer holds what the structure says it holds -- which
			// is a shader reading whatever is at that address (7bp).
			//
			// Everything the list is built from is view-independent by
			// construction, so this cannot fire. It is checked because when it
			// did, the symptom was a lost device forty seconds later with
			// nothing to tie it to.
			if ((uint32_t)s_Data->Instances.size() != s_Data->BuiltCount)
			{
				static bool reported = false;
				if (!reported)
				{
					reported = true;
					RV_CORE_ERROR("[RayShadows] the structure was built with {0} instances "
								  "and this view's record list has {1}. A traced hit resolves "
								  "its custom index through the list, so the two must match; "
								  "something view-dependent has got into the list",
								  s_Data->BuiltCount, s_Data->Instances.size());
				}
			}
			return;
		}

		const uint32_t frame = s_Data->Device->GetFrameIndex();

		// Pose, then refit, then build: every skinned caster's positions
		// written by one dispatch each, one barrier per posed buffer between
		// the write and the build that reads it, every dynamic structure
		// refit (its own barrier orders the top-level build after it), and
		// then the frame's structure. All before the first render pass.
		if (!s_Data->Skins.empty() && EnsureBones(frame, (uint32_t)s_Data->BoneScratch.size()))
		{
			s_Data->Bones[frame]->Upload(s_Data->BoneScratch.data(),
										 s_Data->BoneScratch.size() * sizeof(Mat4));

			cmd.BindComputePipeline(s_Data->SkinPipeline);
			for (const PendingSkin& skin : s_Data->Skins)
			{
				SkinnedCaster& caster = s_Data->Casters[skin.Caster];
				const Ref<RHIResourceSet>& set = caster.Sets[frame];
				set->SetStorageBuffer(0, skin.MeshRef->GetVertexBuffer());
				set->SetStorageBuffer(1, s_Data->Bones[frame]);
				set->SetStorageBuffer(2, caster.Posed[frame]);
				set->Commit();

				SkinParams params;
				params.VertexCount = skin.MeshRef->GetVertexCount();
				params.BoneBase = skin.BoneBase;

				cmd.BindResourceSet(0, set);
				cmd.PushConstants(ShaderStage::Compute, 0, sizeof(params), &params);
				cmd.Dispatch(s_Data->SkinPipeline->GroupsFor(params.VertexCount));
			}

			for (const PendingSkin& skin : s_Data->Skins)
			{
				cmd.BufferBarrier(s_Data->Casters[skin.Caster].Posed[frame],
								  BufferSync::ComputeWrite, BufferSync::AccelerationBuild);
			}
			for (const PendingSkin& skin : s_Data->Skins)
				cmd.BuildBottomLevelAS(s_Data->Casters[skin.Caster].Structures[frame]);

			s_Data->SkinnedThisFrame = (uint32_t)s_Data->Skins.size();
		}

		const uint32_t count = (uint32_t)s_Data->Instances.size();
		EnsureCapacity(Math::Max(count, 1u));

		if (frame >= s_Data->Structures.size() || !s_Data->Structures[frame])
			return;

		cmd.BuildTopLevelAS(s_Data->Structures[frame], s_Data->Instances.data(), count);
		s_Data->Active = true;
		s_Data->BuiltThisFrame = true;
		s_Data->BuiltCount = count;
	}

	bool RayShadows::IsActive()
	{
		return s_Data && s_Data->Active;
	}

	const Ref<RHIAccelerationStructure>& RayShadows::GetStructure()
	{
		static const Ref<RHIAccelerationStructure> none;
		if (!s_Data || !s_Data->Available)
			return none;
		if (s_Data->Active)
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			if (frame < s_Data->Structures.size() && s_Data->Structures[frame])
				return s_Data->Structures[frame];
		}
		return s_Data->Empty;
	}

	uint32_t RayShadows::GetInstanceCount()
	{
		return s_Data ? (uint32_t)s_Data->Instances.size() : 0u;
	}

	uint32_t RayShadows::GetSkinnedCount()
	{
		return s_Data ? s_Data->SkinnedThisFrame : 0u;
	}
}
