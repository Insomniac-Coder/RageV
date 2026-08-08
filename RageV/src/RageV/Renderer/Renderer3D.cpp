#include <rvpch.h>
#include "Renderer3D.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "TextureLoader.h"
#include "ShadowMap.h"
#include <glm/gtc/constants.hpp>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		constexpr unsigned int kMaxLights = 8;   // must match mesh.rvshader

		// Mirrors the std140 SceneData block in pbr.rvshader.
		struct SceneUniforms
		{
			glm::mat4 ViewProjection;
			glm::vec4 CameraPosition;
			// rgb = ambient colour, a = ambient intensity
			glm::vec4 Ambient;
			// xyz = position, w = 0 for directional
			glm::vec4 LightPositions[kMaxLights];
			// xyz = forward axis, for directional travel and spot cones
			glm::vec4 LightDirections[kMaxLights];
			// rgb = colour, a = intensity
			glm::vec4 LightColors[kMaxLights];
			// x = range, y = cos(inner cone), z = cos(outer cone)
			glm::vec4 LightParams[kMaxLights];
			// x = environment intensity, y = its highest mip, zw = cos and sin
			// of the sky's rotation
			glm::vec4 Environment;

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

			// Per light: x = kind of map, y = slot, z = far, w = the world size
			// of one of its texels per unit of distance.
			glm::vec4 LightShadow[kMaxLights];
			glm::mat4 SpotLookup[4];

			int32_t   LightCount;
			int32_t   _padding[3];
		};

		// Just the model matrix now: base colour moved into the material, which
		// is where every other surface parameter lives.
		struct ObjectPushConstants
		{
			glm::mat4 Model;
		};
		static_assert(sizeof(ObjectPushConstants) == 64, "Push constant block must stay within 128 bytes");

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
			};

			// [frame in flight][scene within the frame]
			std::vector<std::vector<SceneSlot>> SceneSlots;
			uint32_t SceneCursor = 0;

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

		RV_CORE_INFO("Renderer3D ready (Cook-Torrance PBR)");
	}

	void Renderer3D::Shutdown()
	{
		Mesh::ClearCache();
		TextureLoader::ClearCache();
		s_Data.reset();
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
		// Accumulated across every scene drawn this frame rather than reset per
		// scene, or the statistics panel would only ever show the last viewport.
		s_Data->DrawCalls = 0;
		s_Data->Triangles = 0;
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

		const int lightCount = (int)std::min<size_t>(lights.size(), kMaxLights);
		for (int i = 0; i < lightCount; i++)
		{
			const LightRenderData& light = lights[i];
			const bool directional = light.Type == Light::LightType::Directional;

			// w == 0 tells the shader distance attenuation does not apply.
			s_Data->Scene.LightPositions[i] = glm::vec4(light.Position, directional ? 0.0f : 1.0f);
			s_Data->Scene.LightDirections[i] = glm::vec4(light.Direction, 0.0f);
			s_Data->Scene.LightColors[i] = glm::vec4(light.Color, light.Intensity);

			// Cones are compared as cosines in the shader, so convert once here
			// rather than per fragment. Equal angles disable the cone test.
			const float inner = light.Type == Light::LightType::Spot
							  ? std::cos(glm::radians(light.InnerCone)) : 1.0f;
			const float outer = light.Type == Light::LightType::Spot
							  ? std::cos(glm::radians(light.OuterCone)) : 1.0f;

			s_Data->Scene.LightParams[i] = { std::max(light.Range, 0.0001f), inner, outer, 0.0f };
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
		for (int i = 0; i < lightCount; i++)
		{
			const LocalShadow& assigned = ShadowMap::GetAssignment((uint32_t)i);

			s_Data->Scene.LightShadow[i] = {
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
		slot.Buffer->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		auto& sceneSet = slot.Set;
		sceneSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
		// Never left unwritten. A binding the layout declares and the set does
		// not fill is a validation error rather than a harmless omission, which
		// is the same lesson the tonemap pass learned about its bloom input.
		sceneSet->SetTexture(1, environmentMap ? environmentMap
											   : TextureLoader::BlackCube(*s_Data->Device),
							 s_Data->EnvironmentSampler);
		sceneSet->SetTexture(5, irradianceMap ? irradianceMap
											  : TextureLoader::BlackCube(*s_Data->Device),
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

		sceneSet->Commit();

		// Bound once for the whole scene; only push constants change per draw.
		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, sceneSet);
		s_Data->SceneActive = true;
	}

	void Renderer3D::EndScene()
	{
		if (s_Data)
			s_Data->SceneActive = false;
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
		cmd->BindPipeline(s_Data->ShadowPipeline);
		s_Data->ShadowActive = true;
	}

	void Renderer3D::DrawMeshShadow(const Ref<Mesh>& mesh, const glm::mat4& transform)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		ObjectPushConstants object;
		object.Model = s_Data->ShadowViewProjection * transform;

		cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
		cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
		cmd->BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
		cmd->DrawIndexed(mesh->GetIndexCount());

		s_Data->DrawCalls++;
	}

	void Renderer3D::EndShadow()
	{
		if (s_Data)
			s_Data->ShadowActive = false;
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
							  const Ref<Material>& material)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (effective)
			effective->Bind(*cmd, s_Data->Pipeline, 1);

		ObjectPushConstants object;
		object.Model = transform;

		cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
		cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
		cmd->BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
		cmd->DrawIndexed(mesh->GetIndexCount());

		s_Data->DrawCalls++;
		s_Data->Triangles += mesh->GetIndexCount() / 3;
	}

	unsigned int Renderer3D::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }
	unsigned int Renderer3D::GetTriangleCount() { return s_Data ? s_Data->Triangles : 0; }
}
