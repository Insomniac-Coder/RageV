#include <rvpch.h>
#include "Renderer3D.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "TextureLoader.h"
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
			// xyz = position, w = 0 for directional
			glm::vec4 LightPositions[kMaxLights];
			// xyz = forward axis, for directional travel and spot cones
			glm::vec4 LightDirections[kMaxLights];
			// rgb = colour, a = intensity
			glm::vec4 LightColors[kMaxLights];
			// x = range, y = cos(inner cone), z = cos(outer cone)
			glm::vec4 LightParams[kMaxLights];
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

			// Per frame in flight: the scene block is rewritten every frame, so
			// a single buffer would be overwritten while the GPU still read it.
			std::vector<Ref<RHIBuffer>>      SceneBuffers;
			std::vector<Ref<RHIResourceSet>> SceneSets;
			Ref<Material> DefaultMaterial;

			SceneUniforms Scene{};
			bool SceneActive = false;

			unsigned int DrawCalls = 0;
			unsigned int Triangles = 0;
		};

		std::unique_ptr<Renderer3DData> s_Data;
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

		const uint32_t frames = device.GetFramesInFlight();
		s_Data->SceneBuffers.resize(frames);
		for (uint32_t i = 0; i < frames; i++)
		{
			BufferDesc desc;
			desc.Size = sizeof(SceneUniforms);
			desc.Usage = BufferUsage::Uniform;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = "Renderer3D.scene." + std::to_string(i);
			s_Data->SceneBuffers[i] = device.CreateBuffer(desc);
		}

		s_Data->DefaultMaterial = Material::CreateDefault(device);

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

		const uint32_t frames = s_Data->Device->GetFramesInFlight();
		s_Data->SceneSets.clear();
		for (uint32_t i = 0; i < frames; i++)
			s_Data->SceneSets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0));
	}

	void Renderer3D::BeginScene(const Cameranew& camera, const glm::mat4& cameraTransform,
								const LightList& lights)
	{
		if (!s_Data)
			return;

		s_Data->DrawCalls = 0;
		s_Data->Triangles = 0;

		s_Data->Scene = {};
		s_Data->Scene.ViewProjection = camera.GetProjection() * glm::inverse(cameraTransform);
		s_Data->Scene.CameraPosition = glm::vec4(glm::vec3(cameraTransform[3]), 1.0f);

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

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		s_Data->SceneBuffers[frame]->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		auto& sceneSet = s_Data->SceneSets[frame];
		sceneSet->SetUniformBuffer(0, s_Data->SceneBuffers[frame], 0, sizeof(SceneUniforms));
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
