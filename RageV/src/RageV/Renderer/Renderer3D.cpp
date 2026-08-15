#include <rvpch.h>
#include "Renderer3D.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "TextureLoader.h"
#include "ShadowMap.h"
#include "EnvironmentIBL.h"
#include "LightGrid.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Mirrors GpuLight in pbr.rvshader, std430.
		//
		// There is no cap on how many of these a frame may carry. There was --
		// eight, because the scene's uniform block declared arrays of eight and
		// a uniform block must declare a length. A storage buffer does not.
		struct GpuLight
		{
			Vec4 Position;    // xyz, w = 1 positional / 0 directional
			Vec4 Direction;   // xyz forward axis
			Vec4 Color;       // rgb, a = intensity
			Vec4 Params;      // range, cos(inner), cos(outer)
			Vec4 Shadow;      // kind, slot, far, texel scale
		};
		static_assert(sizeof(GpuLight) == 80, "Must match GpuLight in pbr.rvshader");

		// Mirrors the std140 SceneData block in pbr.rvshader.
		struct SceneUniforms
		{
			Mat4 ViewProjection;
			Vec4 CameraPosition;
			// rgb = ambient colour, a = ambient intensity
			Vec4 Ambient;
			// x = environment intensity, y = its highest mip, zw = cos and sin
			// of the sky's rotation
			Vec4 Environment;
			// x = the environment's mip-0 face size, in texels.
			Vec4 EnvironmentSize;

			// xyz = tiles across, tiles down, depth slices. w = how many
			// directional lights sit at the front of the light buffer.
			Vec4 ClusterGrid;
			// x near, y far, zw the scale and bias that map a view depth to a
			// slice.
			Vec4 ClusterDepth;

			// World space straight to shadow lookup coordinates, per cascade.
			Mat4 CascadeLookup[4];
			// Far view-space distance of each cascade, for selection.
			Vec4 CascadeSplits;
			// World size of one texel in each, for normal-offset bias.
			Vec4 CascadeTexel;
			// The camera's forward axis: cascade selection needs a view depth
			// and the shader has no view matrix, only a view-projection.
			Vec4 CameraForward;
			// x = cascades rendered (0 = no shadows), y = normal offset scale,
			// z = one cascade texel in lookup coordinates, w unused
			Vec4 ShadowParams;

			Mat4 SpotLookup[4];

			int32_t   LightCount;
			int32_t   _padding[3];

			// Last frame's ViewProjection, for motion vectors. Appended so
			// every offset above is unchanged -- this struct is mirrored by
			// hand in include/scene_vertex.glsl, and the two disagreeing is a
			// wrong picture rather than a failed build. ENGINE-NOTES 7r.
			Mat4 PreviousViewProjection;

			// xy = this frame's sub-pixel offset in NDC, zw = last frame's.
			//
			// Both, because both projections above carry one and the velocity
			// is the difference of the two. Subtracting only the current one
			// would leave last frame's offset in every motion vector, which is
			// half a pixel of velocity on a scene where nothing moved.
			Vec4 Jitter;
		};

		// Where a batch starts in the instance buffer. The model matrix used to
		// be here, one push per object; it is per instance now, and this is all
		// that is left.
		struct ObjectPushConstants
		{
			int32_t BaseInstance;
		};
		static_assert(sizeof(ObjectPushConstants) == 4, "Push constant block must stay within 128 bytes");

		// The skinned depth pass needs one more: where this caster's bones sit.
		//
		// Two structs rather than one with an unused member, because an unused
		// push constant is optimised out of the shader and the reflected range
		// shrinks with it -- writing eight bytes into a four-byte range is a
		// validation error rather than a harmless extra.
		struct SkinnedShadowPushConstants
		{
			int32_t BaseInstance;
			int32_t BoneBase;
		};
		static_assert(sizeof(SkinnedShadowPushConstants) == 8, "Must match shadow_depth_skinned.rvshader");

		// Mirrors InstanceData in pbr.rvshader, std430.
		struct InstanceData
		{
			Mat4 Model;

			// The same matrix on the previous frame. The difference between
			// where a vertex projects under the two is the motion vector, and
			// it belongs per instance for the same reason Model does: two
			// objects moving differently still batch into one instanced draw.
			//
			// Equal to Model for anything that did not move, which is most of
			// a scene, and produces exactly zero velocity there rather than
			// something small and wrong.
			Mat4 PreviousModel;
			// transpose(inverse(mat3(Model))), as a mat4 because std430 pads a
			// mat3 to the same size anyway and a mat4 has no surprises in it.
			// Computed once per instance rather than once per vertex, which is
			// where it used to happen.
			Mat4 NormalMatrix;
			Vec4 BaseColor;
			Vec4 EmissiveColor;
			// metallic, roughness, occlusion, normal scale
			Vec4 Surface;
			// x = where this instance's bones start in the bone buffer, zero
			// for anything the skinned pipeline does not draw.
			// y = which cube of the reflection-probe arrays this object
			//     reflects. Slot 0 is the sky, so zero is a real answer and
			//     not a missing one.
			//
			// Per instance, and that is the point: the probe rides along with
			// the model matrix, so two objects choosing different probes still
			// batch into one instanced draw. Selecting per draw instead would
			// mean the probe had to be part of the sort key, and a run would
			// split every time the answer changed.
			Vec4 Indices{ 0.0f };
		};
		static_assert(sizeof(InstanceData) == 256,
					  "Must match InstanceData in include/scene_vertex.glsl");

		// One submitted mesh, held until EndScene can sort them.
		//
		// Drawing immediately is what made every mesh its own draw call. The
		// order objects arrive in is the registry's, which is neither grouped
		// by mesh nor sorted by depth, so nothing can be batched without first
		// having all of them.
		struct PendingDraw
		{
			// Sort key: the bound state a draw needs. Meshes first because
			// changing vertex buffers is the more expensive of the two, and
			// materials within a mesh so a run is contiguous in both.
			const Mesh* MeshKey = nullptr;
			uint64_t MaterialKey = 0;
			// First in the sort key: skinned and static are different
			// pipelines, and a run has to be one or the other.
			bool Skinned = false;

			Ref<Mesh> MeshRef;
			Ref<Material> MaterialRef;
			InstanceData Instance;
			// Distance from the eye, for ordering batches front to back.
			float ViewDepth = 0.0f;
		};

		// A maximal span of Pending sharing one pipeline, mesh and material --
		// exactly what becomes one instanced draw. Ordering these rather than
		// the draws inside them is what lets depth sorting and batching
		// coexist.
		struct DrawRun
		{
			size_t Begin = 0;
			size_t End = 0;
			// The nearest member, which is the one whose depth writes would
			// occlude the most.
			float Nearest = 0.0f;
		};

		// The depth pass has no material, so its only key is the mesh.
		struct PendingShadowDraw
		{
			const Mesh* MeshKey = nullptr;
			Ref<Mesh> MeshRef;
			// Light view-projection times model, already multiplied out.
			Mat4 LightMVP{ 1.0f };
			bool Skinned = false;
			// Where this caster's bones start. -1 when it has none.
			int32_t BoneBase = -1;
		};

		struct Renderer3DData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			// The same lighting through a wider vertex. Its own pipeline
			// because the vertex layout differs, and its own shader because the
			// vertex stage does; everything below that is a shared include.
			Ref<RHIShader>   SkinnedShader;
			Ref<RHIPipeline> SkinnedPipeline;
			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
		// Sample count, which has to equal the target's. A pipeline whose
		// rasterizationSamples disagrees with the attachment it draws into is
		// undefined behaviour rather than an error, so it travels with the
		// formats and gets compared with them.
			// The previous view-projection and its jitter used to live here,
			// one pair for the process. They are per frame chain and now live
			// with the history they reproject into -- CameraMotion, reached
			// through Renderer::GetCameraMotion(). See BeginScene.
			uint32_t TargetSamples = 1;

		// Where the scene writes its motion vectors, or Undefined for a pass
		// that has no velocity attachment bound.
		//
		// One shape for every pass that writes the scene target -- the scene
		// pass and the overlay both bind {colour, velocity}. 7q paid for the
		// alternative: pipelines built for one target shape being bound into a
		// pass with another is undefined behaviour rather than an error.
			Format TargetVelocity = Format::Undefined;
			Format TargetNormal = Format::Undefined;
			bool   PipelineDirty = true;
			bool   Wireframe = false;

			// One per scene *within* a frame, not one per frame in flight.
			//
			// A draw reads the scene block when the GPU runs it, not when it is
			// recorded, so two scenes in one frame -- the editor viewport and
			// the game viewport -- need separate blocks. Sharing one meant the
			// second BeginScene overwrote the view-projection the first
			// viewport's draws were about to use.
			struct SceneSlot
			{
				Ref<RHIBuffer>      Buffer;
				Ref<RHIResourceSet> Set;
				// The batch's per-instance array. Grows to the largest scene
				// this slot has drawn and stays there, so a steady scene stops
				// allocating after its first frame.
				Ref<RHIBuffer>      Instances;
				uint32_t            InstanceCapacity = 0;
				// Every light in this scene, however many that is.
				Ref<RHIBuffer>      Lights;
				uint32_t            LightCapacity = 0;
				// The cluster grid: a range per cell, and the indices those
				// ranges point into.
				Ref<RHIBuffer>      Cells;
				uint32_t            CellCapacity = 0;
				Ref<RHIBuffer>      CellIndices;
				uint32_t            CellIndexCapacity = 0;
				// Every skinned instance's bones, back to back. One buffer for
				// the scene rather than one per character: forty characters
				// would otherwise want forty bindings.
				Ref<RHIBuffer>      Bones;
				uint32_t            BoneCapacity = 0;
				// The skinned pipeline's layout declares one binding the static
				// one does not -- the bones -- so it needs a set of its own.
				// Writing a binding a shader never declared is the hazard
				// recorded in HANDOFF section 5, and the validation layer says
				// so immediately.
				Ref<RHIResourceSet> SkinnedSet;
			};

			// [frame in flight][scene within the frame]
			std::vector<std::vector<SceneSlot>> SceneSlots;
			uint32_t SceneCursor = 0;

			// The same idea for the depth pass, which needs one per shadow
			// render rather than one per scene: a frame opens a shadow pass per
			// cascade, per spot light and per face of every point light's cube,
			// and each reads its own instances when the GPU gets to it.
			struct ShadowSlot
			{
				Ref<RHIBuffer>      Instances;
				Ref<RHIResourceSet> Set;
				uint32_t            InstanceCapacity = 0;
				// The depth pass needs the same bones the lit pass used, or a
				// character casts the shadow of its bind pose.
				Ref<RHIBuffer>      Bones;
				uint32_t            BoneCapacity = 0;
				Ref<RHIResourceSet> SkinnedSet;
			};

			std::vector<std::vector<ShadowSlot>> ShadowSlots;
			uint32_t ShadowCursor = 0;

			// The slot BeginScene took, so EndScene reaches the same one
			// without recomputing an index from the cursor -- which is off by
			// one the moment BeginScene returns early.
			SceneSlot* ActiveScene = nullptr;

			// Built in BeginScene, uploaded there too.
			std::vector<GpuLight> LightScratch;
			// Original light indices, directional first. The shadow assignment
			// is keyed on the original order and has to be undone through this.
			std::vector<uint32_t> LightOrder;
			LightList Ordered;
			LightGrid Grid;

			// Accumulated between BeginScene and EndScene, then sorted.
			std::vector<PendingDraw> Pending;
			std::vector<InstanceData> InstanceScratch;
			// Kept between frames so the front-to-back reorder allocates
			// nothing on a stable scene.
			std::vector<DrawRun> Runs;
			std::vector<PendingDraw> SortScratch;

			// The depth pass carries only a matrix per caster.
			std::vector<PendingShadowDraw> ShadowPending;
			std::vector<Mat4> ShadowScratch;

			// Bone matrices for this scene and this shadow pass. Appended to as
			// skinned meshes are submitted; each draw remembers where its own
			// run began.
			std::vector<Mat4> BoneScratch;
			std::vector<Mat4> ShadowBoneScratch;

			Ref<Material> DefaultMaterial;

			// The depth-only pipeline. Its own shader and its own pipeline
			// object: no colour attachment, so it cannot share the lit one.
			Ref<RHIShader>   ShadowShader;
			Ref<RHIPipeline> ShadowPipeline;
			Ref<RHIShader>   ShadowSkinnedShader;
			Ref<RHIPipeline> ShadowSkinnedPipeline;
			Format ShadowDepth = Format::D32_SFLOAT;
			Mat4 ShadowViewProjection{ 1.0f };
			bool ShadowActive = false;

			// Mip-filtered and clamped, unlike the material sampler: roughness
			// selects a level here, so a sampler pinned to level 0 would make
			// every surface a mirror.
			Ref<RHISampler> EnvironmentSampler;

			SceneUniforms Scene{};
			bool SceneActive = false;

			unsigned int DrawCalls = 0;
			unsigned int Triangles = 0;
			unsigned int Culled = 0;

			bool Ready = false;
		};

		std::unique_ptr<Renderer3DData> s_Data;

		Renderer3DData::SceneSlot& AcquireSceneSlot()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& slots = s_Data->SceneSlots[frame];

			while (s_Data->SceneCursor >= slots.size())
			{
				Renderer3DData::SceneSlot slot;

				BufferDesc desc;
				desc.Size = sizeof(SceneUniforms);
				desc.Usage = BufferUsage::Uniform;
				desc.Memory = MemoryDomain::HostVisible;
				desc.DebugName = "Renderer3D.scene." + std::to_string(frame) + "." +
								 std::to_string(slots.size());
				slot.Buffer = s_Data->Device->CreateBuffer(desc);

				slots.push_back(std::move(slot));
			}

			Renderer3DData::SceneSlot& slot = slots[s_Data->SceneCursor++];

			// Lazily, because a set needs a pipeline and the pipeline is built
			// from target formats that Init does not know.
			if (!slot.Set)
				slot.Set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

			return slot;
		}

		Renderer3DData::ShadowSlot& AcquireShadowSlot()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& slots = s_Data->ShadowSlots[frame];

			while (s_Data->ShadowCursor >= slots.size())
				slots.push_back(Renderer3DData::ShadowSlot{});

			Renderer3DData::ShadowSlot& slot = slots[s_Data->ShadowCursor++];

			if (!slot.Set)
				slot.Set = s_Data->Device->CreateResourceSet(s_Data->ShadowPipeline, 0);

			return slot;
		}

		// Grows `buffer` to hold at least `count` elements of `stride`, in
		// powers of two so a scene that creeps upwards does not reallocate
		// every frame. Returns false if the device would not give one.
		bool EnsureInstanceBuffer(Ref<RHIBuffer>& buffer, uint32_t& capacity,
								  uint32_t count, uint32_t stride, const char* name)
		{
			if (buffer && capacity >= count)
				return true;

			uint32_t target = capacity > 0 ? capacity : 64;
			while (target < count)
				target *= 2;

			BufferDesc desc;
			desc.Size = (uint64_t)target * stride;
			desc.Usage = BufferUsage::Storage;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = name;

			// A new buffer rather than a resize: the old one may still be
			// bound to a command buffer this frame, and the deletion queue is
			// what makes releasing it safe.
			Ref<RHIBuffer> grown = s_Data->Device->CreateBuffer(desc);
			if (!grown)
				return false;

			buffer = grown;
			capacity = target;
			return true;
		}
	}

	void Renderer3D::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<Renderer3DData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();
		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/pbr.rvshader");
			return;
		}
		s_Data->Shader = device.CreateShader(*compiled);

		// Slots are created on demand; most frames need one.
		s_Data->SceneSlots.resize(device.GetFramesInFlight());
		s_Data->ShadowSlots.resize(device.GetFramesInFlight());

		s_Data->DefaultMaterial = Material::CreateDefault(device);

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/pbr_skinned.rvshader"))
			s_Data->SkinnedShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/pbr_skinned.rvshader");

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth.rvshader"))
			s_Data->ShadowShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/shadow_depth.rvshader");

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth_skinned.rvshader"))
			s_Data->ShadowSkinnedShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/shadow_depth_skinned.rvshader");

		SamplerDesc environment;
		environment.WrapU = WrapMode::ClampToEdge;
		environment.WrapV = WrapMode::ClampToEdge;
		environment.WrapW = WrapMode::ClampToEdge;
		environment.MaxLod = 16.0f;
		s_Data->EnvironmentSampler = device.CreateSampler(environment);

		// Both shaders, not just the lit one. The shadow pass failing on its own
		// used to leave this announcing readiness while nothing cast anything.
		s_Data->Ready = s_Data->Shader != nullptr && s_Data->ShadowShader != nullptr &&
						s_Data->DefaultMaterial != nullptr;

		if (s_Data->Ready)
			RV_CORE_INFO("Renderer3D ready (Cook-Torrance PBR, shadow casting)");
		else
			RV_CORE_ERROR("Renderer3D incomplete; meshes or their shadows will not draw");
	}

	void Renderer3D::Shutdown()
	{
		Mesh::ClearCache();
		TextureLoader::ClearCache();
		// After s_Data's default material, which holds a reference to it.
		s_Data.reset();
		Material::ReleaseShared();
	}

	Ref<Material> Renderer3D::GetDefaultMaterial()
	{
		return s_Data ? s_Data->DefaultMaterial : nullptr;
	}

	void Renderer3D::SetTargetFormats(Format color, Format depth, uint32_t samples,
									   Format velocity, Format normal)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth &&
			s_Data->TargetSamples == samples &&
			s_Data->TargetVelocity == velocity && s_Data->TargetNormal == normal && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetSamples = samples;
		s_Data->TargetVelocity = velocity;
		s_Data->TargetNormal = normal;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void Renderer3D::SetWireframe(bool enabled)
	{
		if (!s_Data || s_Data->Wireframe == enabled)
			return;
		s_Data->Wireframe = enabled;
		s_Data->PipelineDirty = true;
	}

	void Renderer3D::EnsurePipeline()
	{
		if (!s_Data->PipelineDirty || !s_Data->Shader)
			return;

		GraphicsPipelineDesc desc;
		desc.Name = "Renderer3D.pbr";
		desc.Shader = s_Data->Shader;
		desc.Topology = PrimitiveTopology::TriangleList;
		// Primitives are generated counter-clockwise when viewed from outside.
		desc.Rasterizer.Cull = s_Data->Wireframe ? CullMode::None : CullMode::Back;
		desc.Rasterizer.Front = FrontFace::CounterClockwise;
		desc.Rasterizer.Polygon = s_Data->Wireframe ? PolygonMode::Line : PolygonMode::Fill;
		desc.Blend = BlendPreset::Opaque;
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = true;
		desc.ColorFormats = { s_Data->TargetColor };
		desc.Samples = s_Data->TargetSamples;
		if (s_Data->TargetVelocity != Format::Undefined)
			desc.ColorFormats.push_back(s_Data->TargetVelocity);
		if (s_Data->TargetNormal != Format::Undefined)
			desc.ColorFormats.push_back(s_Data->TargetNormal);
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);

		// The same description with the other shader. Its vertex layout is
		// reflected from that shader, so the wider vertex needs nothing stated
		// here -- and both pipelines share every raster and depth setting,
		// which is what stops a skinned mesh being subtly differently lit.
		if (s_Data->SkinnedShader)
		{
			desc.Name = "Renderer3D.pbr.skinned";
			desc.Shader = s_Data->SkinnedShader;
			s_Data->SkinnedPipeline = s_Data->Device->CreatePipeline(desc);
		}

		s_Data->PipelineDirty = false;

		// Resource sets are tied to a pipeline layout, so they go with it and
		// are recreated on demand.
		for (auto& frame : s_Data->SceneSlots)
		{
			for (auto& slot : frame)
			{
				slot.Set.reset();
				slot.SkinnedSet.reset();
			}
		}
	}

	void Renderer3D::BeginFrame()
	{
		if (!s_Data)
			return;

		// The GPU has finished with this frame's slots, so they are reusable.
		s_Data->SceneCursor = 0;
		s_Data->ShadowCursor = 0;
		// Accumulated across every scene drawn this frame rather than reset per
		// scene, or the statistics panel would only ever show the last viewport.
		s_Data->DrawCalls = 0;
		s_Data->Triangles = 0;
		s_Data->Culled = 0;
	}

	void Renderer3D::BeginScene(const Camera& camera, const Mat4& cameraTransform,
								const LightList& lights, const SceneEnvironment& environment,
								const RenderSettings& render,
								const Ref<RHITexture>& environmentMap,
								const Ref<RHITexture>& irradianceMap,
								const Vec2& jitter)
	{
		if (!s_Data)
			return;

		s_Data->Scene = {};

		const Mat4 viewProjection = camera.GetProjection() * Math::Inverse(cameraTransform);

		// What *this frame chain* drew with last time, which is the only camera
		// a velocity here can mean anything against: the history about to be
		// reprojected is that camera's image.
		//
		// **This used to be one matrix for the whole process**, written by
		// every BeginScene and read by the next, justified by "the scene pass
		// is the last caller in a frame". That is true of the runtime and false
		// of the editor, which draws the viewport and the game view in one
		// frame from two cameras: the game view then differenced itself against
		// the *editor* camera and TAA fetched its history from wherever that
		// gap pointed. The result was a faint second copy of the whole scene
		// over the game view that slid about as the editor camera moved -- an
		// image answering to an input that has nothing to do with it.
		// ENGINE-NOTES 7u.
		//
		// Null for anything with no history to reproject: a probe capture's six
		// faces, a shadow cascade, a chain with temporal filtering off. Those
		// difference against themselves, which is zero velocity -- the same
		// value the velocity attachment is cleared to.
		CameraMotion* motion = Renderer::GetCameraMotion();

		s_Data->Scene.PreviousViewProjection = motion ? motion->ViewProjection : viewProjection;
		const Vec2 previousJitter = motion ? motion->Jitter : jitter;
		s_Data->Scene.Jitter = Vec4(jitter.x, jitter.y, previousJitter.x, previousJitter.y);

		s_Data->Scene.ViewProjection = viewProjection;

		// Recorded after reading, and only for a real chain. What this frame
		// draws with is what the next frame of *this* chain reprojects from.
		if (motion)
		{
			motion->ViewProjection = viewProjection;
			motion->Jitter = jitter;
		}
		s_Data->Scene.CameraPosition = Vec4(Vec3(cameraTransform[3]), 1.0f);
		s_Data->Scene.Ambient = Vec4(environment.AmbientColor, environment.AmbientIntensity);

		// Every light, with no cap. The shader reads them from a storage buffer
		// whose length is decided here rather than declared in a block.
		//
		// Reordered so the directional lights come first. They have no position
		// and reach every cell, so binning them would put a copy of each in all
		// 3456 cells; instead the shader reads the first N unconditionally and
		// takes the rest from its own cell.
		const int lightCount = (int)lights.size();

		s_Data->LightOrder.clear();
		s_Data->LightOrder.reserve(lights.size());
		for (uint32_t i = 0; i < (uint32_t)lights.size(); i++)
		{
			if (lights[i].Type == Light::LightType::Directional)
				s_Data->LightOrder.push_back(i);
		}

		const uint32_t directionalCount = (uint32_t)s_Data->LightOrder.size();

		for (uint32_t i = 0; i < (uint32_t)lights.size(); i++)
		{
			if (lights[i].Type != Light::LightType::Directional)
				s_Data->LightOrder.push_back(i);
		}

		s_Data->Ordered.clear();
		s_Data->Ordered.reserve(lights.size());
		for (uint32_t index : s_Data->LightOrder)
			s_Data->Ordered.push_back(lights[index]);

		s_Data->LightScratch.clear();
		s_Data->LightScratch.reserve(lights.size());

		for (const LightRenderData& light : s_Data->Ordered)
		{
			const bool directional = light.Type == Light::LightType::Directional;

			GpuLight entry{};
			// w == 0 tells the shader distance attenuation does not apply.
			entry.Position = Vec4(light.Position, directional ? 0.0f : 1.0f);
			entry.Direction = Vec4(light.Direction, 0.0f);
			entry.Color = Vec4(light.Color, light.Intensity);

			// Cones are compared as cosines in the shader, so convert once here
			// rather than per fragment. Equal angles disable the cone test.
			const float inner = light.Type == Light::LightType::Spot
							  ? std::cos(Math::Radians(light.InnerCone)) : 1.0f;
			const float outer = light.Type == Light::LightType::Spot
							  ? std::cos(Math::Radians(light.OuterCone)) : 1.0f;

			entry.Params = { std::max(light.Range, 0.0001f), inner, outer, 0.0f };
			entry.Shadow = Vec4(0.0f);

			s_Data->LightScratch.push_back(entry);
		}
		s_Data->Scene.LightCount = lightCount;

		// Zero intensity is how "nothing to reflect" is expressed: the sampler
		// still has to be bound, because the shader declares it either way, but
		// the term it feeds multiplies out.
		const float mips = environmentMap ? (float)environmentMap->GetDesc().MipLevels : 1.0f;
		s_Data->Scene.Environment = {
			environmentMap ? environment.SkyIntensity : 0.0f,
			Math::Max(mips - 1.0f, 0.0f),
			std::cos(environment.SkyRotation),
			std::sin(environment.SkyRotation),
		};

		// Sent, not derived. A prefiltered cube stops at its roughness levels,
		// so exp2(highest mip) is a sixteenth of its real face size -- which
		// quietly turned the reflection's anti-aliasing term off.
		s_Data->Scene.EnvironmentSize = {
			environmentMap ? (float)environmentMap->GetWidth() : 1.0f, 0.0f, 0.0f, 0.0f,
		};

		// Cascades come from ShadowMap rather than being passed in: this runs
		// once per viewport, and the cascades belong to the frame.
		s_Data->Scene.CameraForward =
			Vec4(Math::Normalize(Vec3(cameraTransform * Vec4(0, 0, -1, 0))), 0.0f);
		s_Data->Scene.ShadowParams = Vec4(0.0f, 0.0f, 0.0f, -1.0f);

		const uint32_t cascadeCount = ShadowMap::HasCascades() ? ShadowMap::GetCascadeCount() : 0;
		if (cascadeCount > 0)
		{
			const ShadowCascade* cascades = ShadowMap::GetCascades();
			const uint32_t resolution = Math::Max(ShadowMap::GetResolution(), 1u);

			for (uint32_t i = 0; i < cascadeCount; i++)
			{
				s_Data->Scene.CascadeLookup[i] = cascades[i].LookupMatrix;
				s_Data->Scene.CascadeSplits[i] = cascades[i].SplitDepth;
				s_Data->Scene.CascadeTexel[i] = cascades[i].TexelWorldSize;
			}

			// The last cascade's split repeated, so a fragment past the end
			// selects the coarsest map rather than reading an uninitialised
			// split and picking arbitrarily.
			for (uint32_t i = cascadeCount; i < ShadowMap::kMaxCascades; i++)
			{
				s_Data->Scene.CascadeLookup[i] = cascades[cascadeCount - 1].LookupMatrix;
				s_Data->Scene.CascadeSplits[i] = cascades[cascadeCount - 1].SplitDepth;
				s_Data->Scene.CascadeTexel[i] = cascades[cascadeCount - 1].TexelWorldSize;
			}

			s_Data->Scene.ShadowParams = {
				(float)cascadeCount,
				render.ShadowNormalOffset,
				1.0f / (float)resolution,
				0.0f,
			};
		}
		else
		{
			s_Data->Scene.ShadowParams.y = render.ShadowNormalOffset;
		}

		// Which map each light got, decided when the shadows were rendered.
		//
		// Only the first few can have one: there are four spot maps and four
		// point cubes however many lights a scene has. Past that a light lights
		// and does not cast, which is the budget rather than the cap that used
		// to sit here.
		// Indexed by the light's *original* position, not its position after the
		// reorder above -- ShadowMap assigned slots while walking the scene, and
		// asking it about the wrong light gives a light somebody else's map.
		for (uint32_t slot = 0; slot < (uint32_t)s_Data->LightOrder.size(); slot++)
		{
			const uint32_t original = s_Data->LightOrder[slot];
			if (original >= ShadowMap::kMaxLights)
				continue;

			const LocalShadow& assigned = ShadowMap::GetAssignment(original);

			s_Data->LightScratch[slot].Shadow = {
				(float)(uint32_t)assigned.Type,
				(float)Math::Max(assigned.Slot, 0),
				assigned.FarClip,
				assigned.TexelScale,
			};

			if (assigned.Type == LocalShadow::Kind::Spot && assigned.Slot >= 0)
				s_Data->Scene.SpotLookup[assigned.Slot] = assigned.LookupMatrix;
		}

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		Renderer3DData::SceneSlot& slot = AcquireSceneSlot();
		s_Data->ActiveScene = &slot;

		// The light buffer. Always at least one element: a zero-length storage
		// buffer is not a binding, and a scene with no lights at all still has
		// to bind something the layout is happy with.
		const uint32_t lightSlots = std::max<uint32_t>((uint32_t)s_Data->LightScratch.size(), 1u);
		if (!EnsureInstanceBuffer(slot.Lights, slot.LightCapacity, lightSlots,
								  sizeof(GpuLight), "Renderer3D.lights"))
		{
			return;
		}

		if (!s_Data->LightScratch.empty())
		{
			slot.Lights->Upload(s_Data->LightScratch.data(),
								s_Data->LightScratch.size() * sizeof(GpuLight));
		}

		// The cluster grid. Built here rather than in the scene, because it is
		// keyed to the camera this pass is drawing with -- the editor's two
		// viewports need one each, for the same reason their shadow cascades do.
		s_Data->Grid.Build(camera, cameraTransform, s_Data->Ordered, directionalCount);

		const auto& cells = s_Data->Grid.Cells();
		const auto& cellIndices = s_Data->Grid.Indices();
		const uint32_t indexSlots = std::max<uint32_t>((uint32_t)cellIndices.size(), 1u);

		if (!EnsureInstanceBuffer(slot.Cells, slot.CellCapacity, (uint32_t)cells.size(),
								  sizeof(LightGrid::Cell), "Renderer3D.cells") ||
			!EnsureInstanceBuffer(slot.CellIndices, slot.CellIndexCapacity, indexSlots,
								  sizeof(uint32_t), "Renderer3D.cellIndices"))
		{
			return;
		}

		slot.Cells->Upload(cells.data(), cells.size() * sizeof(LightGrid::Cell));
		if (!cellIndices.empty())
			slot.CellIndices->Upload(cellIndices.data(), cellIndices.size() * sizeof(uint32_t));

		float nearPlane = 0.1f, farPlane = 1000.0f;
		LightGrid::DepthRangeOf(camera.GetProjection(), nearPlane, farPlane);

		s_Data->Scene.ClusterGrid = {
			(float)LightGrid::kTilesX, (float)LightGrid::kTilesY,
			(float)LightGrid::kSlices, (float)directionalCount,
		};
		s_Data->Scene.ClusterDepth = {
			nearPlane, farPlane,
			LightGrid::SliceScale(nearPlane, farPlane),
			LightGrid::SliceBias(nearPlane, farPlane),
		};

		// Written after the grid, because the grid decides the two vectors
		// above and the block is uploaded once.
		slot.Buffer->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		// Both sets get every write. Two sets rather than one because their
		// layouts differ by a single binding, and one loop rather than two
		// copies because the day they drift is the day a skinned mesh is lit
		// from a different environment than the mesh beside it.
		if (s_Data->SkinnedPipeline && !slot.SkinnedSet)
			slot.SkinnedSet = s_Data->Device->CreateResourceSet(s_Data->SkinnedPipeline, 0);

		Ref<RHIResourceSet> targets[] = { slot.Set, slot.SkinnedSet };

		for (const Ref<RHIResourceSet>& sceneSet : targets)
		{
		if (!sceneSet)
			continue;

		sceneSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
		sceneSet->SetStorageBuffer(8, slot.Lights, 0, (uint64_t)lightSlots * sizeof(GpuLight));
		sceneSet->SetStorageBuffer(9, slot.Cells, 0, cells.size() * sizeof(LightGrid::Cell));
		sceneSet->SetStorageBuffer(10, slot.CellIndices, 0,
								   (uint64_t)indexSlots * sizeof(uint32_t));
		// Never left unwritten. A binding the layout declares and the set does
		// not fill is a validation error rather than a harmless omission, which
		// is the same lesson the tonemap pass learned about its bloom input.
		//
		// Both are cube *arrays* now -- the probe arrays, whose slot 0 is the
		// sky -- so the stand-in has to be one too. A plain cube here is a
		// different descriptor type, which is a validation error rather than a
		// dark reflection.
		sceneSet->SetTexture(1, environmentMap ? environmentMap
											   : TextureLoader::BlackCubeArray(*s_Data->Device),
							 s_Data->EnvironmentSampler);
		sceneSet->SetTexture(5, irradianceMap ? irradianceMap
											  : TextureLoader::BlackCubeArray(*s_Data->Device),
							 s_Data->EnvironmentSampler);

		// The BRDF table. Never null in practice, but the binding has to be
		// filled either way, and a white 1x1 reads as "reflect everything"
		// rather than as nothing.
		if (const Ref<RHITexture> brdf = EnvironmentIBL::GetBRDF())
			sceneSet->SetTexture(6, brdf, EnvironmentIBL::GetBRDFSampler());
		else
			sceneSet->SetTexture(6, TextureLoader::White(*s_Data->Device),
								 s_Data->EnvironmentSampler);

		// All four, always. A comparison sampler the layout declares and the
		// set does not fill is a validation error; a 1x1 depth of 1.0 is the
		// harmless answer, because under LessOrEqual every comparison against
		// the far plane passes and the surface reads as lit.
		{
			const Ref<RHISampler> shadowSampler = ShadowMap::GetSampler();
			const Ref<RHITexture> empty = ShadowMap::GetEmptyTexture();

			for (uint32_t i = 0; i < ShadowMap::kMaxCascades; i++)
			{
				Ref<RHITexture> cascade = i < cascadeCount ? ShadowMap::GetCascadeTexture(i)
														  : nullptr;
				sceneSet->SetTexture(2, cascade ? cascade : empty, shadowSampler, i);
			}

			const Ref<RHITexture> emptyCube = ShadowMap::GetEmptyCube();
			for (uint32_t i = 0; i < ShadowMap::kMaxLocal; i++)
			{
				const Ref<RHITexture> spot = ShadowMap::GetSpotTexture(i);
				sceneSet->SetTexture(3, spot ? spot : empty, shadowSampler, i);

				const Ref<RHITexture> point = ShadowMap::GetPointTexture(i);
				sceneSet->SetTexture(4, point ? point : emptyCube, shadowSampler, i);
			}
		}
		}   // both sets

		// Not committed and not bound yet.
		//
		// The instance buffer is part of this same set, and its contents are
		// not known until every mesh has been submitted -- so the commit waits
		// for EndScene. Committing here and again there would be rewriting a
		// descriptor set that is already bound to a command buffer, which is
		// the hazard recorded in HANDOFF §5.
		s_Data->Pending.clear();
		s_Data->BoneScratch.clear();
		s_Data->SceneActive = true;
	}

	void Renderer3D::EndScene()
	{
		if (!s_Data || !s_Data->SceneActive)
			return;

		s_Data->SceneActive = false;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd || s_Data->Pending.empty() || !s_Data->Pipeline)
			return;

		if (!s_Data->ActiveScene)
			return;

		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;

		// Orders draws front to back without breaking a single batch.
		//
		// Early-z only skips shading a pixel once something nearer has written
		// depth, so the order draws arrive in decides how much fragment work is
		// wasted. Measured on 200 full-screen slabs: **0.32 ms nearest-first
		// against 33.9 ms furthest-first**, a factor of 105. That is the ceiling,
		// and it is what makes this worth doing.
		//
		// **Two levels, and the inner one is where the win actually is.**
		//
		// Instances *within* a run share a mesh and a material by definition, so
		// reordering them changes nothing about the draw -- same batch, same
		// instance count, same state -- and buys the whole of the early-z effect.
		// This is the level that matters: the slab scene is 200 objects sharing
		// one cube and one material, so it is a *single* batch, and reordering
		// batches alone left it at 33.5 ms against 33.9. Sorting inside the batch
		// is what takes it to 0.32.
		//
		// Runs are then ordered by their nearest member, which helps when a scene
		// has many distinct meshes rather than many copies of one.
		//
		// What is deliberately *not* done is a global sort by depth. It orders
		// perfectly and dissolves the grouping instancing depends on, and on a
		// realistic scene that trade loses: 1500 spread-out meshes measured
		// 0.567 ms globally depth-sorted against 0.548 ms in the grouped order,
		// because the draw calls lost cost more than the overdraw saved.
		auto SortBatchesFrontToBack = [&]()
		{
			if (!EngineConfig::Get().DepthSortOpaque)
				return;

			auto& pending = s_Data->Pending;
			if (pending.size() < 2)
				return;

			s_Data->Runs.clear();

			for (size_t begin = 0; begin < pending.size();)
			{
				size_t end = begin + 1;
				while (end < pending.size() &&
					   pending[end].Skinned == pending[begin].Skinned &&
					   pending[end].MeshKey == pending[begin].MeshKey &&
					   pending[end].MaterialKey == pending[begin].MaterialKey)
				{
					end++;
				}

				// Inside the batch. Free, and the level the measurement says
				// carries the effect.
				std::sort(pending.begin() + begin, pending.begin() + end,
						  [](const PendingDraw& a, const PendingDraw& b)
						  { return a.ViewDepth < b.ViewDepth; });

				s_Data->Runs.push_back({ begin, end, pending[begin].ViewDepth });
				begin = end;
			}

			if (s_Data->Runs.size() < 2)
				return;

			// Skinned last whatever their depth: the two pipelines must stay
			// separated, which is the property the grouping sort guarantees.
			std::stable_sort(s_Data->Runs.begin(), s_Data->Runs.end(),
							 [&](const DrawRun& a, const DrawRun& b)
							 {
								 const bool skinnedA = pending[a.Begin].Skinned;
								 const bool skinnedB = pending[b.Begin].Skinned;
								 if (skinnedA != skinnedB)
									 return !skinnedA;
								 return a.Nearest < b.Nearest;
							 });

			s_Data->SortScratch.clear();
			s_Data->SortScratch.reserve(pending.size());
			for (const DrawRun& run : s_Data->Runs)
			{
				for (size_t i = run.Begin; i < run.End; i++)
					s_Data->SortScratch.push_back(std::move(pending[i]));
			}

			pending.swap(s_Data->SortScratch);
		};

		// Grouped, not merely sorted. Objects arrive in registry order, which
		// is neither, so runs of identical state only exist after this.
		std::sort(s_Data->Pending.begin(), s_Data->Pending.end(),
				  [](const PendingDraw& a, const PendingDraw& b)
				  {
					  // Pipeline first. Static and skinned are different
					  // pipelines and different vertex layouts, so a run has to
					  // be entirely one or the other -- sorting them together
					  // would put a pipeline switch in the middle of a batch.
					  if (a.Skinned != b.Skinned)
						  return !a.Skinned;
					  if (a.MeshKey != b.MeshKey)
						  return a.MeshKey < b.MeshKey;
					  return a.MaterialKey < b.MaterialKey;
				  });

		SortBatchesFrontToBack();

		const uint32_t count = (uint32_t)s_Data->Pending.size();

		if (!EnsureInstanceBuffer(slot.Instances, slot.InstanceCapacity, count,
								  sizeof(InstanceData), "Renderer3D.instances"))
		{
			return;
		}

		s_Data->InstanceScratch.clear();
		s_Data->InstanceScratch.reserve(count);
		for (const PendingDraw& draw : s_Data->Pending)
			s_Data->InstanceScratch.push_back(draw.Instance);

		slot.Instances->Upload(s_Data->InstanceScratch.data(),
							   (uint64_t)count * sizeof(InstanceData));

		slot.Set->SetStorageBuffer(7, slot.Instances, 0,
								   (uint64_t)count * sizeof(InstanceData));

		// Always bound, even with nothing skinned in the scene: the layout
		// declares the binding whether or not this frame uses it, and a
		// declared binding left unwritten is a validation error rather than an
		// unread one. One identity is the smallest honest filler.
		const uint32_t boneCount = Math::Max((uint32_t)s_Data->BoneScratch.size(), 1u);
		if (!EnsureInstanceBuffer(slot.Bones, slot.BoneCapacity, boneCount,
								  sizeof(Mat4), "Renderer3D.bones"))
		{
			return;
		}

		if (s_Data->BoneScratch.empty())
		{
			const Mat4 identity(1.0f);
			slot.Bones->Upload(&identity, sizeof(identity));
		}
		else
		{
			slot.Bones->Upload(s_Data->BoneScratch.data(),
							   s_Data->BoneScratch.size() * sizeof(Mat4));
		}

		// The instance buffer to both, the bones only to the set whose layout
		// declares them.
		if (slot.SkinnedSet)
		{
			slot.SkinnedSet->SetStorageBuffer(7, slot.Instances, 0,
											  (uint64_t)count * sizeof(InstanceData));
			slot.SkinnedSet->SetStorageBuffer(11, slot.Bones, 0,
											  (uint64_t)boneCount * sizeof(Mat4));
			slot.SkinnedSet->Commit();
		}

		slot.Set->Commit();

		// Bound per run rather than once, because the run decides which of the
		// two pipelines draws it.
		const Mesh* boundPipelineFor = nullptr;
		bool boundSkinned = false;
		bool anyPipelineBound = false;

		// One draw per run of identical mesh and bound material state.
		uint32_t start = 0;
		while (start < count)
		{
			uint32_t end = start + 1;
			while (end < count &&
				   s_Data->Pending[end].MeshKey == s_Data->Pending[start].MeshKey &&
				   s_Data->Pending[end].MaterialKey == s_Data->Pending[start].MaterialKey)
			{
				end++;
			}

			const PendingDraw& first = s_Data->Pending[start];

			// A skinned run needs the skinned pipeline. Without one it is
			// skipped rather than drawn by the static one: the vertex layouts
			// differ, and the static pipeline would read joint indices as
			// texture coordinates and scatter the mesh across the world.
			const Ref<RHIPipeline>& pipeline = first.Skinned ? s_Data->SkinnedPipeline
															 : s_Data->Pipeline;
			if (!pipeline)
			{
				start = end;
				continue;
			}

			const Ref<RHIResourceSet>& sceneSet = first.Skinned ? slot.SkinnedSet : slot.Set;
			if (!sceneSet)
			{
				start = end;
				continue;
			}

			if (!anyPipelineBound || boundSkinned != first.Skinned)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, sceneSet);
				boundSkinned = first.Skinned;
				anyPipelineBound = true;
			}

			// Any material in the run would do: the key is exactly the state
			// this binds, so they are interchangeable by construction.
			if (first.MaterialRef)
				first.MaterialRef->Bind(*cmd, pipeline, 1);

			ObjectPushConstants object;
			object.BaseInstance = (int32_t)start;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

			cmd->BindVertexBuffer(0, first.MeshRef->GetVertexBuffer());
			cmd->BindIndexBuffer(first.MeshRef->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(first.MeshRef->GetIndexCount(), end - start);

			s_Data->DrawCalls++;
			s_Data->Triangles += (first.MeshRef->GetIndexCount() / 3) * (end - start);

			start = end;
		}

		s_Data->Pending.clear();
	}

	void Renderer3D::BeginShadow(const Mat4& viewProjection)
	{
		if (!s_Data || !s_Data->ShadowShader)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		if (!s_Data->ShadowPipeline)
		{
			GraphicsPipelineDesc desc;
			desc.Name = "Renderer3D.shadow";
			desc.Shader = s_Data->ShadowShader;
			desc.Topology = PrimitiveTopology::TriangleList;

			// Stated rather than reflected. The shader reads only the position,
			// so reflection would produce a tightly packed 12-byte stride and
			// walk through the mesh buffer reading normals as positions.
			VertexBinding binding;
			binding.Binding = 0;
			binding.Stride = sizeof(MeshVertex);
			desc.VertexInput.Bindings = { binding };
			desc.VertexInput.Attributes = {
				{ 0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, Position) },
			};

			// Nothing culled.
			//
			// Culling front faces is the classic way to avoid acne -- the depth
			// written is then the *back* of each caster, a bias that scales with
			// the geometry instead of with a guess. It also moves every shadow
			// away from its caster by that same thickness, which on a sphere is
			// a whole diameter: the small spheres in the sample scene had their
			// shadows sitting most of a radius away from them, which reads as
			// the shadow belonging to something else.
			//
			// Culling nothing records the surface nearest the light, keeps
			// contact where it belongs, and lets single-sided geometry cast at
			// all. Acne is then entirely the biases' problem, which is what the
			// normal offset was written for.
			desc.Rasterizer.Cull = CullMode::None;
			desc.Rasterizer.Front = FrontFace::CounterClockwise;

			// Slope-scaled on top, for surfaces nearly edge-on to the light,
			// where one texel of shadow map covers a lot of depth.
			//
			// Small, because these were tuned alongside front-face culling and
			// its free thickness. Every unit of bias here is a unit the shadow
			// moves away from whatever cast it, and on an object the size of a
			// few texels the shadow ends up looking like it belongs to
			// something else standing nearby.
			desc.Rasterizer.DepthBiasEnable = true;
			desc.Rasterizer.DepthBiasConstant = 0.6f;
			desc.Rasterizer.DepthBiasSlope = 1.4f;

			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = true;
			desc.ColorFormats.clear();
			desc.DepthFormat = s_Data->ShadowDepth;

			s_Data->ShadowPipeline = s_Data->Device->CreatePipeline(desc);

			// The skinned depth pipeline. Its vertex layout is reflected from
			// its own shader -- which declares the normal and texture
			// coordinate it does not read, precisely so the stride matches the
			// buffer the lit skinned pass reads. Stating a tighter one here
			// would walk through the mesh reading joints as positions.
			if (s_Data->ShadowSkinnedShader)
			{
				GraphicsPipelineDesc skinned = desc;
				skinned.Name = "Renderer3D.shadow.skinned";
				skinned.Shader = s_Data->ShadowSkinnedShader;
				skinned.VertexInput = {};   // reflected, unlike the static one
				s_Data->ShadowSkinnedPipeline = s_Data->Device->CreatePipeline(skinned);
			}
		}

		if (!s_Data->ShadowPipeline)
			return;

		s_Data->ShadowViewProjection = viewProjection;
		s_Data->ShadowPending.clear();
		s_Data->ShadowBoneScratch.clear();
		cmd->BindPipeline(s_Data->ShadowPipeline);
		s_Data->ShadowActive = true;
	}

	void Renderer3D::DrawMeshShadow(const Ref<Mesh>& mesh, const Mat4& transform)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		PendingShadowDraw draw;
		draw.MeshKey = mesh.get();
		draw.MeshRef = mesh;
		draw.LightMVP = s_Data->ShadowViewProjection * transform;

		s_Data->ShadowPending.push_back(std::move(draw));
	}

	void Renderer3D::DrawSkinnedMeshShadow(const Ref<Mesh>& mesh, const Mat4& transform,
										   const std::vector<Mat4>& bones)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		const int32_t base = (int32_t)s_Data->ShadowBoneScratch.size();

		if (bones.empty())
			s_Data->ShadowBoneScratch.emplace_back(1.0f);
		else
		{
			s_Data->ShadowBoneScratch.insert(s_Data->ShadowBoneScratch.end(),
											 bones.begin(), bones.end());
		}

		PendingShadowDraw draw;
		draw.MeshKey = mesh.get();
		draw.MeshRef = mesh;
		draw.LightMVP = s_Data->ShadowViewProjection * transform;
		draw.Skinned = true;
		draw.BoneBase = base;

		s_Data->ShadowPending.push_back(std::move(draw));
	}

	void Renderer3D::EndShadow()
	{
		if (!s_Data || !s_Data->ShadowActive)
			return;

		s_Data->ShadowActive = false;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd || s_Data->ShadowPending.empty() || !s_Data->ShadowPipeline)
		{
			s_Data->ShadowPending.clear();
			return;
		}

		// The depth pass is where batching pays most: a frame opens one of
		// these per cascade, per casting spot light and per face of every point
		// light's cube, and every one of them walked the whole scene issuing a
		// draw per caster.
		std::sort(s_Data->ShadowPending.begin(), s_Data->ShadowPending.end(),
				  [](const PendingShadowDraw& a, const PendingShadowDraw& b)
				  {
					  // Static first, then skinned, for the same reason the lit
					  // pass does it: two pipelines, two vertex layouts.
					  if (a.Skinned != b.Skinned)
						  return !a.Skinned;
					  if (a.MeshKey != b.MeshKey)
						  return a.MeshKey < b.MeshKey;
					  // A skinned run is one caster at a time: each has its own
					  // pose, and the bone base is pushed rather than carried
					  // per instance in this pass.
					  return a.BoneBase < b.BoneBase;
				  });

		Renderer3DData::ShadowSlot& slot = AcquireShadowSlot();
		const uint32_t count = (uint32_t)s_Data->ShadowPending.size();

		if (!EnsureInstanceBuffer(slot.Instances, slot.InstanceCapacity, count,
								  sizeof(Mat4), "Renderer3D.shadowInstances"))
		{
			s_Data->ShadowPending.clear();
			return;
		}

		s_Data->ShadowScratch.clear();
		s_Data->ShadowScratch.reserve(count);
		for (const PendingShadowDraw& draw : s_Data->ShadowPending)
			s_Data->ShadowScratch.push_back(draw.LightMVP);

		slot.Instances->Upload(s_Data->ShadowScratch.data(),
							   (uint64_t)count * sizeof(Mat4));

		slot.Set->SetStorageBuffer(0, slot.Instances, 0, (uint64_t)count * sizeof(Mat4));
		slot.Set->Commit();

		// The skinned depth pass has a second set, because its layout declares
		// a bone buffer the static one does not. Built only when something in
		// this pass is actually skinned.
		const bool anySkinned = s_Data->ShadowPending.back().Skinned;
		if (anySkinned && s_Data->ShadowSkinnedPipeline)
		{
			const uint32_t boneCount =
				Math::Max((uint32_t)s_Data->ShadowBoneScratch.size(), 1u);

			if (EnsureInstanceBuffer(slot.Bones, slot.BoneCapacity, boneCount,
									 sizeof(Mat4), "Renderer3D.shadowBones"))
			{
				if (s_Data->ShadowBoneScratch.empty())
				{
					const Mat4 identity(1.0f);
					slot.Bones->Upload(&identity, sizeof(identity));
				}
				else
				{
					slot.Bones->Upload(s_Data->ShadowBoneScratch.data(),
									   s_Data->ShadowBoneScratch.size() * sizeof(Mat4));
				}

				if (!slot.SkinnedSet)
				{
					slot.SkinnedSet =
						s_Data->Device->CreateResourceSet(s_Data->ShadowSkinnedPipeline, 0);
				}

				slot.SkinnedSet->SetStorageBuffer(0, slot.Instances, 0,
												  (uint64_t)count * sizeof(Mat4));
				slot.SkinnedSet->SetStorageBuffer(1, slot.Bones, 0,
												  (uint64_t)boneCount * sizeof(Mat4));
				slot.SkinnedSet->Commit();
			}
		}

		bool boundSkinned = false;
		bool anyBound = false;

		uint32_t start = 0;
		while (start < count)
		{
			uint32_t end = start + 1;
			while (end < count &&
				   s_Data->ShadowPending[end].Skinned == s_Data->ShadowPending[start].Skinned &&
				   s_Data->ShadowPending[end].MeshKey == s_Data->ShadowPending[start].MeshKey &&
				   // A skinned run is one caster: the bone base is a push
				   // constant here, so two characters cannot share a draw.
				   !s_Data->ShadowPending[start].Skinned)
			{
				end++;
			}

			const PendingShadowDraw& first = s_Data->ShadowPending[start];
			const Ref<Mesh>& mesh = first.MeshRef;

			const Ref<RHIPipeline>& pipeline = first.Skinned ? s_Data->ShadowSkinnedPipeline
															 : s_Data->ShadowPipeline;
			const Ref<RHIResourceSet>& set = first.Skinned ? slot.SkinnedSet : slot.Set;

			// A skinned caster with no skinned pipeline is skipped rather than
			// drawn by the static one: it would cast the shadow of a mesh
			// scattered across the world, which is worse than casting none.
			if (!pipeline || !set)
			{
				start = end;
				continue;
			}

			if (!anyBound || boundSkinned != first.Skinned)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, set);
				boundSkinned = first.Skinned;
				anyBound = true;
			}

			if (first.Skinned)
			{
				SkinnedShadowPushConstants object;
				object.BaseInstance = (int32_t)start;
				object.BoneBase = first.BoneBase < 0 ? 0 : first.BoneBase;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}
			else
			{
				ObjectPushConstants object;
				object.BaseInstance = (int32_t)start;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}

			cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
			cmd->BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(mesh->GetIndexCount(), end - start);

			s_Data->DrawCalls++;

			start = end;
		}

		s_Data->ShadowPending.clear();
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const Mat4& transform,
							  const Ref<Material>& material, const MaterialParams& params,
							  uint32_t probe,
							   const Mat4* previousTransform)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey();
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;

		draw.Instance.Model = transform;
		draw.Instance.PreviousModel = previousTransform ? *previousTransform : transform;
		// Once per object rather than once per vertex, which is where the
		// shader was doing it -- an inverse and a transpose of a 3x3 for every
		// vertex of every mesh, all producing the same matrix.
		draw.Instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		draw.Instance.BaseColor = params.BaseColor;
		draw.Instance.EmissiveColor = params.EmissiveColor;
		draw.Instance.Surface = { params.Metallic, params.Roughness,
								  params.Occlusion, params.NormalScale };
		// No bones, and the probe the scene picked for this object.
		draw.Instance.Indices = { 0.0f, (float)probe, 0.0f, 0.0f };

		{
			const Vec3 eye = Vec3(s_Data->Scene.CameraPosition);
			const Vec3 centre = Vec3(transform[3]);
			draw.ViewDepth = Math::Length(centre - eye);
		}

		// Recorded, not drawn. EndScene sorts these and issues one draw per run
		// of identical state; drawing here is what made the count equal the
		// object count.
		s_Data->Pending.push_back(std::move(draw));
	}

	void Renderer3D::DrawSkinnedMesh(const Ref<Mesh>& mesh, const Mat4& transform,
									 const Ref<Material>& material, const MaterialParams& params,
									 const std::vector<Mat4>& bones, uint32_t probe,
							   const Mat4* previousTransform)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		// A skinned mesh with no pose is drawn by the skinned pipeline anyway:
		// its vertex layout is the wider one, and the static pipeline would
		// read joints as texture coordinates. An empty bone run leaves every
		// weighted matrix zero, so one identity is supplied instead and the
		// mesh appears in its bind pose.
		const uint32_t base = (uint32_t)s_Data->BoneScratch.size();

		if (bones.empty())
			s_Data->BoneScratch.emplace_back(1.0f);
		else
			s_Data->BoneScratch.insert(s_Data->BoneScratch.end(), bones.begin(), bones.end());

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey();
		draw.Skinned = true;
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;

		draw.Instance.Model = transform;
		draw.Instance.PreviousModel = previousTransform ? *previousTransform : transform;
		draw.Instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		draw.Instance.BaseColor = params.BaseColor;
		draw.Instance.EmissiveColor = params.EmissiveColor;
		draw.Instance.Surface = { params.Metallic, params.Roughness,
								  params.Occlusion, params.NormalScale };
		draw.Instance.Indices = { (float)base, (float)probe, 0.0f, 0.0f };

		s_Data->Pending.push_back(std::move(draw));
	}

	unsigned int Renderer3D::GetCulledCount() { return s_Data ? s_Data->Culled : 0; }
	void Renderer3D::CountCulled() { if (s_Data) s_Data->Culled++; }

	bool Renderer3D::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	unsigned int Renderer3D::GetMaxCellLoad() { return s_Data ? s_Data->Grid.MaxCellLoad() : 0; }
	unsigned int Renderer3D::GetLightCount() { return s_Data ? (unsigned int)s_Data->LightScratch.size() : 0; }

	unsigned int Renderer3D::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }
	unsigned int Renderer3D::GetTriangleCount() { return s_Data ? s_Data->Triangles : 0; }
}
