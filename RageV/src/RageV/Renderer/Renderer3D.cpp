#include <rvpch.h>
#include "Renderer3D.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "TextureLoader.h"
#include "ShadowMap.h"
#include "EnvironmentIBL.h"
#include <glm/gtc/constants.hpp>

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
			glm::vec4 Position;    // xyz, w = 1 positional / 0 directional
			glm::vec4 Direction;   // xyz forward axis
			glm::vec4 Color;       // rgb, a = intensity
			glm::vec4 Params;      // range, cos(inner), cos(outer)
			glm::vec4 Shadow;      // kind, slot, far, texel scale
		};
		static_assert(sizeof(GpuLight) == 80, "Must match GpuLight in pbr.rvshader");

		// Mirrors the std140 SceneData block in pbr.rvshader.
		struct SceneUniforms
		{
			glm::mat4 ViewProjection;
			glm::vec4 CameraPosition;
			// rgb = ambient colour, a = ambient intensity
			glm::vec4 Ambient;
			// x = environment intensity, y = its highest mip, zw = cos and sin
			// of the sky's rotation
			glm::vec4 Environment;
			// x = the environment's mip-0 face size, in texels.
			glm::vec4 EnvironmentSize;

			// World space straight to shadow lookup coordinates, per cascade.
			glm::mat4 CascadeLookup[4];
			// Far view-space distance of each cascade, for selection.
			glm::vec4 CascadeSplits;
			// World size of one texel in each, for normal-offset bias.
			glm::vec4 CascadeTexel;
			// The camera's forward axis: cascade selection needs a view depth
			// and the shader has no view matrix, only a view-projection.
			glm::vec4 CameraForward;
			// x = cascades rendered (0 = no shadows), y = normal offset scale,
			// z = one cascade texel in lookup coordinates, w unused
			glm::vec4 ShadowParams;

			glm::mat4 SpotLookup[4];

			int32_t   LightCount;
			int32_t   _padding[3];
		};

		// Where a batch starts in the instance buffer. The model matrix used to
		// be here, one push per object; it is per instance now, and this is all
		// that is left.
		struct ObjectPushConstants
		{
			int32_t BaseInstance;
		};
		static_assert(sizeof(ObjectPushConstants) == 4, "Push constant block must stay within 128 bytes");

		// Mirrors InstanceData in pbr.rvshader, std430.
		struct InstanceData
		{
			glm::mat4 Model;
			// transpose(inverse(mat3(Model))), as a mat4 because std430 pads a
			// mat3 to the same size anyway and a mat4 has no surprises in it.
			// Computed once per instance rather than once per vertex, which is
			// where it used to happen.
			glm::mat4 NormalMatrix;
			glm::vec4 BaseColor;
			glm::vec4 EmissiveColor;
			// metallic, roughness, occlusion, normal scale
			glm::vec4 Surface;
		};
		static_assert(sizeof(InstanceData) == 176, "Must match InstanceData in pbr.rvshader");

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

			Ref<Mesh> MeshRef;
			Ref<Material> MaterialRef;
			InstanceData Instance;
		};

		// The depth pass has no material, so its only key is the mesh.
		struct PendingShadowDraw
		{
			const Mesh* MeshKey = nullptr;
			Ref<Mesh> MeshRef;
			// Light view-projection times model, already multiplied out.
			glm::mat4 LightMVP{ 1.0f };
		};

		struct Renderer3DData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
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
			};

			std::vector<std::vector<ShadowSlot>> ShadowSlots;
			uint32_t ShadowCursor = 0;

			// The slot BeginScene took, so EndScene reaches the same one
			// without recomputing an index from the cursor -- which is off by
			// one the moment BeginScene returns early.
			SceneSlot* ActiveScene = nullptr;

			// Built in BeginScene, uploaded there too.
			std::vector<GpuLight> LightScratch;

			// Accumulated between BeginScene and EndScene, then sorted.
			std::vector<PendingDraw> Pending;
			std::vector<InstanceData> InstanceScratch;

			// The depth pass carries only a matrix per caster.
			std::vector<PendingShadowDraw> ShadowPending;
			std::vector<glm::mat4> ShadowScratch;

			Ref<Material> DefaultMaterial;

			// The depth-only pipeline. Its own shader and its own pipeline
			// object: no colour attachment, so it cannot share the lit one.
			Ref<RHIShader>   ShadowShader;
			Ref<RHIPipeline> ShadowPipeline;
			Format ShadowDepth = Format::D32_SFLOAT;
			glm::mat4 ShadowViewProjection{ 1.0f };
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

			if (s_Data->SceneCursor >= slots.size())
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

			if (s_Data->ShadowCursor >= slots.size())
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

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth.rvshader"))
			s_Data->ShadowShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/shadow_depth.rvshader");

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

	void Renderer3D::SetTargetFormats(Format color, Format depth)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
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
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
		s_Data->PipelineDirty = false;

		// Resource sets are tied to a pipeline layout, so they go with it and
		// are recreated on demand.
		for (auto& frame : s_Data->SceneSlots)
		{
			for (auto& slot : frame)
				slot.Set.reset();
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

	void Renderer3D::BeginScene(const Camera& camera, const glm::mat4& cameraTransform,
								const LightList& lights, const SceneEnvironment& environment,
								const Ref<RHITexture>& environmentMap,
								const Ref<RHITexture>& irradianceMap)
	{
		if (!s_Data)
			return;

		s_Data->Scene = {};
		s_Data->Scene.ViewProjection = camera.GetProjection() * glm::inverse(cameraTransform);
		s_Data->Scene.CameraPosition = glm::vec4(glm::vec3(cameraTransform[3]), 1.0f);
		s_Data->Scene.Ambient = glm::vec4(environment.AmbientColor, environment.AmbientIntensity);

		// Every light, with no cap. The shader reads them from a storage buffer
		// whose length is decided here rather than declared in a block.
		const int lightCount = (int)lights.size();
		s_Data->LightScratch.clear();
		s_Data->LightScratch.reserve(lights.size());

		for (const LightRenderData& light : lights)
		{
			const bool directional = light.Type == Light::LightType::Directional;

			GpuLight entry{};
			// w == 0 tells the shader distance attenuation does not apply.
			entry.Position = glm::vec4(light.Position, directional ? 0.0f : 1.0f);
			entry.Direction = glm::vec4(light.Direction, 0.0f);
			entry.Color = glm::vec4(light.Color, light.Intensity);

			// Cones are compared as cosines in the shader, so convert once here
			// rather than per fragment. Equal angles disable the cone test.
			const float inner = light.Type == Light::LightType::Spot
							  ? std::cos(glm::radians(light.InnerCone)) : 1.0f;
			const float outer = light.Type == Light::LightType::Spot
							  ? std::cos(glm::radians(light.OuterCone)) : 1.0f;

			entry.Params = { std::max(light.Range, 0.0001f), inner, outer, 0.0f };
			entry.Shadow = glm::vec4(0.0f);

			s_Data->LightScratch.push_back(entry);
		}
		s_Data->Scene.LightCount = lightCount;

		// Zero intensity is how "nothing to reflect" is expressed: the sampler
		// still has to be bound, because the shader declares it either way, but
		// the term it feeds multiplies out.
		const float mips = environmentMap ? (float)environmentMap->GetDesc().MipLevels : 1.0f;
		s_Data->Scene.Environment = {
			environmentMap ? environment.SkyIntensity : 0.0f,
			glm::max(mips - 1.0f, 0.0f),
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
			glm::vec4(glm::normalize(glm::vec3(cameraTransform * glm::vec4(0, 0, -1, 0))), 0.0f);
		s_Data->Scene.ShadowParams = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);

		const uint32_t cascadeCount = ShadowMap::HasCascades() ? ShadowMap::GetCascadeCount() : 0;
		if (cascadeCount > 0)
		{
			const ShadowCascade* cascades = ShadowMap::GetCascades();
			const uint32_t resolution = glm::max(ShadowMap::GetResolution(), 1u);

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
				environment.ShadowNormalOffset,
				1.0f / (float)resolution,
				0.0f,
			};
		}
		else
		{
			s_Data->Scene.ShadowParams.y = environment.ShadowNormalOffset;
		}

		// Which map each light got, decided when the shadows were rendered.
		//
		// Only the first few can have one: there are four spot maps and four
		// point cubes however many lights a scene has. Past that a light lights
		// and does not cast, which is the budget rather than the cap that used
		// to sit here.
		const int shadowed = glm::min(lightCount, (int)ShadowMap::kMaxLights);
		for (int i = 0; i < shadowed; i++)
		{
			const LocalShadow& assigned = ShadowMap::GetAssignment((uint32_t)i);

			s_Data->LightScratch[i].Shadow = {
				(float)(uint32_t)assigned.Type,
				(float)glm::max(assigned.Slot, 0),
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
		slot.Buffer->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		// The light buffer. Always at least one element: a zero-length storage
		// buffer is not a binding, and a scene with no lights at all still has
		// to bind something the layout is happy with.
		const uint32_t lightSlots = glm::max<uint32_t>((uint32_t)s_Data->LightScratch.size(), 1u);
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

		auto& sceneSet = slot.Set;
		sceneSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
		sceneSet->SetStorageBuffer(8, slot.Lights, 0, (uint64_t)lightSlots * sizeof(GpuLight));
		// Never left unwritten. A binding the layout declares and the set does
		// not fill is a validation error rather than a harmless omission, which
		// is the same lesson the tonemap pass learned about its bloom input.
		sceneSet->SetTexture(1, environmentMap ? environmentMap
											   : TextureLoader::BlackCube(*s_Data->Device),
							 s_Data->EnvironmentSampler);
		sceneSet->SetTexture(5, irradianceMap ? irradianceMap
											  : TextureLoader::BlackCube(*s_Data->Device),
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

		// Not committed and not bound yet.
		//
		// The instance buffer is part of this same set, and its contents are
		// not known until every mesh has been submitted -- so the commit waits
		// for EndScene. Committing here and again there would be rewriting a
		// descriptor set that is already bound to a command buffer, which is
		// the hazard recorded in HANDOFF §5.
		s_Data->Pending.clear();
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

		// Grouped, not merely sorted. Objects arrive in registry order, which
		// is neither, so runs of identical state only exist after this.
		std::sort(s_Data->Pending.begin(), s_Data->Pending.end(),
				  [](const PendingDraw& a, const PendingDraw& b)
				  {
					  if (a.MeshKey != b.MeshKey)
						  return a.MeshKey < b.MeshKey;
					  return a.MaterialKey < b.MaterialKey;
				  });

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
		slot.Set->Commit();

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, slot.Set);

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

			// Any material in the run would do: the key is exactly the state
			// this binds, so they are interchangeable by construction.
			if (first.MaterialRef)
				first.MaterialRef->Bind(*cmd, s_Data->Pipeline, 1);

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

	void Renderer3D::BeginShadow(const glm::mat4& viewProjection)
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
		}

		if (!s_Data->ShadowPipeline)
			return;

		s_Data->ShadowViewProjection = viewProjection;
		s_Data->ShadowPending.clear();
		cmd->BindPipeline(s_Data->ShadowPipeline);
		s_Data->ShadowActive = true;
	}

	void Renderer3D::DrawMeshShadow(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		PendingShadowDraw draw;
		draw.MeshKey = mesh.get();
		draw.MeshRef = mesh;
		draw.LightMVP = s_Data->ShadowViewProjection * transform;

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
					  return a.MeshKey < b.MeshKey;
				  });

		Renderer3DData::ShadowSlot& slot = AcquireShadowSlot();
		const uint32_t count = (uint32_t)s_Data->ShadowPending.size();

		if (!EnsureInstanceBuffer(slot.Instances, slot.InstanceCapacity, count,
								  sizeof(glm::mat4), "Renderer3D.shadowInstances"))
		{
			s_Data->ShadowPending.clear();
			return;
		}

		s_Data->ShadowScratch.clear();
		s_Data->ShadowScratch.reserve(count);
		for (const PendingShadowDraw& draw : s_Data->ShadowPending)
			s_Data->ShadowScratch.push_back(draw.LightMVP);

		slot.Instances->Upload(s_Data->ShadowScratch.data(),
							   (uint64_t)count * sizeof(glm::mat4));

		slot.Set->SetStorageBuffer(0, slot.Instances, 0, (uint64_t)count * sizeof(glm::mat4));
		slot.Set->Commit();

		cmd->BindResourceSet(0, slot.Set);

		uint32_t start = 0;
		while (start < count)
		{
			uint32_t end = start + 1;
			while (end < count &&
				   s_Data->ShadowPending[end].MeshKey == s_Data->ShadowPending[start].MeshKey)
			{
				end++;
			}

			const Ref<Mesh>& mesh = s_Data->ShadowPending[start].MeshRef;

			ObjectPushConstants object;
			object.BaseInstance = (int32_t)start;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

			cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
			cmd->BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(mesh->GetIndexCount(), end - start);

			s_Data->DrawCalls++;

			start = end;
		}

		s_Data->ShadowPending.clear();
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
							  const Ref<Material>& material)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		const MaterialParams& params = effective->GetParams();

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey();
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;

		draw.Instance.Model = transform;
		// Once per object rather than once per vertex, which is where the
		// shader was doing it -- an inverse and a transpose of a 3x3 for every
		// vertex of every mesh, all producing the same matrix.
		draw.Instance.NormalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(transform))));
		draw.Instance.BaseColor = params.BaseColor;
		draw.Instance.EmissiveColor = params.EmissiveColor;
		draw.Instance.Surface = { params.Metallic, params.Roughness,
								  params.Occlusion, params.NormalScale };

		// Recorded, not drawn. EndScene sorts these and issues one draw per run
		// of identical state; drawing here is what made the count equal the
		// object count.
		s_Data->Pending.push_back(std::move(draw));
	}

	unsigned int Renderer3D::GetCulledCount() { return s_Data ? s_Data->Culled : 0; }
	void Renderer3D::CountCulled() { if (s_Data) s_Data->Culled++; }

	bool Renderer3D::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	unsigned int Renderer3D::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }
	unsigned int Renderer3D::GetTriangleCount() { return s_Data ? s_Data->Triangles : 0; }
}
