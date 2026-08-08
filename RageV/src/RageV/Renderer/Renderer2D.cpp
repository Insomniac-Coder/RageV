#include <rvpch.h>
#include "Renderer2D.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		constexpr unsigned int kMaxQuads = 20000;
		constexpr unsigned int kMaxVertices = kMaxQuads * 4;
		constexpr unsigned int kMaxIndices = kMaxQuads * 6;
		constexpr unsigned int kMaxTextureSlots = 32;   // must match quad.rvshader
		constexpr unsigned int kMaxLights = 8;          // must match quad.rvshader

		constexpr glm::vec4 kQuadCorners[4] = {
			{ -0.5f, -0.5f, 0.0f, 1.0f }, {  0.5f, -0.5f, 0.0f, 1.0f },
			{  0.5f,  0.5f, 0.0f, 1.0f }, { -0.5f,  0.5f, 0.0f, 1.0f },
		};
		constexpr glm::vec2 kQuadTexCoords[4] = {
			{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
		};

		struct VertexData
		{
			glm::vec3 Position;
			glm::vec3 Normal;
			glm::vec4 Color;
			glm::vec2 TexCoord;
			float     TextureIndex;
			float     TilingFactor;
		};
		static_assert(sizeof(VertexData) == 56, "Must match the vertex stride reflected from quad.rvshader");

		// Mirrors the std140 SceneData block in quad.rvshader.
		struct SceneUniforms
		{
			glm::mat4 ViewProjection;
			glm::vec4 CameraPosition;
			glm::vec4 LightPositions[kMaxLights];
			glm::vec4 LightColors[kMaxLights];
			int32_t   LightCount;
			int32_t   _padding[3];
		};

		// The quad's object-space normal rotated into world space. The
		// inverse-transpose is only needed under non-uniform scale or shear;
		// for the rigid transforms quads use, the renormalised upper 3x3 is
		// equivalent and far cheaper.
		glm::vec3 WorldSpaceNormal(const glm::mat4& transform)
		{
			return glm::normalize(glm::mat3(transform) * glm::vec3(0.0f, 0.0f, -1.0f));
		}

		struct Renderer2DData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			Format TargetColor = Format::B8G8R8A8_UNORM;
			Format TargetDepth = Format::D24_UNORM_S8_UINT;
			bool   PipelineDirty = true;

			// One per frame in flight. The vertex stream and the scene uniforms
			// are rewritten every frame, so a single instance would be
			// overwritten by the next frame while the GPU was still reading it
			// for the previous one. This is the same class of hazard
			// synchronization validation flags, and it only bites under load.
			std::vector<Ref<RHIBuffer>>      VertexBuffers;
			std::vector<Ref<RHIBuffer>>      SceneBuffers;
			std::vector<Ref<RHIResourceSet>> SceneSets;
			std::vector<Ref<RHIResourceSet>> TextureSets;

			Ref<RHIBuffer>  IndexBuffer;
			Ref<RHITexture> WhiteTexture;
			Ref<RHISampler> Sampler;

			// CPU-side staging for the current batch, copied into the frame's
			// vertex buffer at flush.
			std::vector<VertexData> Vertices;
			VertexData* VertexCursor = nullptr;

			std::vector<Ref<RHITexture>> TextureSlots;
			unsigned int NextTextureSlot = 1;   // 0 is the white texture

			unsigned int QuadCount = 0;
			unsigned int IndexCount = 0;
			unsigned int DrawCalls = 0;

			SceneUniforms Scene{};
		};

		std::unique_ptr<Renderer2DData> s_Data;
	}

	void Renderer2D::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<Renderer2DData>();
		s_Data->Device = &device;

		const uint32_t frames = device.GetFramesInFlight();

		ShaderCompiler::Init();
		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/quad.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("Renderer2D: failed to compile assets/shaders/quad.rvshader");
			return;
		}
		s_Data->Shader = device.CreateShader(*compiled);

		// Index data never changes, so it lives in device-local memory and is
		// uploaded once through the staging path.
		{
			std::vector<uint32_t> indices(kMaxIndices);
			uint32_t offset = 0;
			for (unsigned int i = 0; i < kMaxIndices; i += 6)
			{
				indices[i + 0] = offset + 0;
				indices[i + 1] = offset + 1;
				indices[i + 2] = offset + 2;
				indices[i + 3] = offset + 2;
				indices[i + 4] = offset + 3;
				indices[i + 5] = offset + 0;
				offset += 4;
			}

			BufferDesc desc;
			desc.Size = indices.size() * sizeof(uint32_t);
			desc.Usage = BufferUsage::Index;
			desc.Memory = MemoryDomain::DeviceLocal;
			desc.DebugName = "Renderer2D.indices";
			s_Data->IndexBuffer = device.CreateBuffer(desc);
			s_Data->IndexBuffer->Upload(indices.data(), desc.Size);
		}

		s_Data->VertexBuffers.resize(frames);
		s_Data->SceneBuffers.resize(frames);
		for (uint32_t i = 0; i < frames; i++)
		{
			BufferDesc vertexDesc;
			vertexDesc.Size = (uint64_t)kMaxVertices * sizeof(VertexData);
			vertexDesc.Usage = BufferUsage::Vertex;
			vertexDesc.Memory = MemoryDomain::HostVisible;
			vertexDesc.DebugName = "Renderer2D.vertices." + std::to_string(i);
			s_Data->VertexBuffers[i] = device.CreateBuffer(vertexDesc);

			BufferDesc sceneDesc;
			sceneDesc.Size = sizeof(SceneUniforms);
			sceneDesc.Usage = BufferUsage::Uniform;
			sceneDesc.Memory = MemoryDomain::HostVisible;
			sceneDesc.DebugName = "Renderer2D.scene." + std::to_string(i);
			s_Data->SceneBuffers[i] = device.CreateBuffer(sceneDesc);
		}

		{
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = Format::R8G8B8A8_UNORM;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = "Renderer2D.white";
			s_Data->WhiteTexture = device.CreateTexture(desc);
			const uint32_t white = 0xffffffff;
			s_Data->WhiteTexture->Upload(&white, sizeof(white));
		}

		SamplerDesc samplerDesc;
		samplerDesc.MaxLod = 0.0f;   // no mips on the batch textures yet
		s_Data->Sampler = device.CreateSampler(samplerDesc);

		s_Data->Vertices.resize(kMaxVertices);
		s_Data->VertexCursor = s_Data->Vertices.data();

		s_Data->TextureSlots.assign(kMaxTextureSlots, nullptr);
		s_Data->TextureSlots[0] = s_Data->WhiteTexture;

		RV_CORE_INFO("Renderer2D ready ({0} frames in flight, {1} quads per batch)", frames, kMaxQuads);
	}

	void Renderer2D::Shutdown()
	{
		// Resources must be released before the device outlives them.
		s_Data.reset();
		ShaderCompiler::Shutdown();
	}

	void Renderer2D::SetTargetFormats(Format color, Format depth)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void Renderer2D::EnsurePipeline()
	{
		if (!s_Data->PipelineDirty || !s_Data->Shader)
			return;

		GraphicsPipelineDesc desc;
		desc.Name = "Renderer2D.quad";
		desc.Shader = s_Data->Shader;
		desc.Topology = PrimitiveTopology::TriangleList;
		// Quads are drawn from both sides; the scene has no winding convention.
		desc.Rasterizer.Cull = CullMode::None;
		desc.Blend = BlendPreset::AlphaBlend;
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = true;
		desc.ColorFormats = { s_Data->TargetColor };
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
		s_Data->PipelineDirty = false;

		// Resource sets are tied to a pipeline layout, so they are rebuilt with
		// the pipeline.
		const uint32_t frames = s_Data->Device->GetFramesInFlight();
		s_Data->SceneSets.clear();
		s_Data->TextureSets.clear();
		for (uint32_t i = 0; i < frames; i++)
		{
			s_Data->SceneSets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0));
			s_Data->TextureSets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 1));
		}
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		if (!s_Data)
			return;

		s_Data->DrawCalls = 0;
		ResetScene();

		s_Data->Scene = {};
		s_Data->Scene.ViewProjection = camera.GetViewProjectionMatrix();
		s_Data->Scene.LightCount = 0;
	}

	void Renderer2D::BeginScene(const Cameranew& camera, const glm::mat4& transform, const LightData& lightData)
	{
		if (!s_Data)
			return;

		s_Data->DrawCalls = 0;
		ResetScene();

		s_Data->Scene = {};
		s_Data->Scene.ViewProjection = camera.GetProjection() * glm::inverse(transform);
		s_Data->Scene.CameraPosition = glm::vec4(glm::vec3(transform[3]), 1.0f);

		const int lightCount = (int)std::min<size_t>(lightData.size(), kMaxLights);
		for (int i = 0; i < lightCount; i++)
		{
			s_Data->Scene.LightPositions[i] = glm::vec4(std::get<0>(lightData[i]), 1.0f);
			s_Data->Scene.LightColors[i] = glm::vec4(std::get<1>(lightData[i]), 1.0f);
		}
		s_Data->Scene.LightCount = lightCount;
	}

	void Renderer2D::ResetScene()
	{
		s_Data->QuadCount = 0;
		s_Data->IndexCount = 0;
		s_Data->VertexCursor = s_Data->Vertices.data();

		for (unsigned int i = 1; i < s_Data->NextTextureSlot; i++)
			s_Data->TextureSlots[i].reset();
		s_Data->NextTextureSlot = 1;
	}

	void Renderer2D::EndScene()
	{
		if (!s_Data || s_Data->QuadCount == 0)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();

		// Each frame slot has its own vertex and uniform storage, so writing
		// here cannot race the GPU reading the previous frame's batch.
		auto& vertexBuffer = s_Data->VertexBuffers[frame];
		vertexBuffer->Upload(s_Data->Vertices.data(), (uint64_t)s_Data->QuadCount * 4 * sizeof(VertexData));

		auto& sceneBuffer = s_Data->SceneBuffers[frame];
		sceneBuffer->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		auto& sceneSet = s_Data->SceneSets[frame];
		sceneSet->SetUniformBuffer(0, sceneBuffer, 0, sizeof(SceneUniforms));
		sceneSet->Commit();

		// Every element of the sampler array has to be written even though only
		// the occupied slots are read: the shader indexes it dynamically, so
		// validation treats all of them as potentially accessed.
		auto& textureSet = s_Data->TextureSets[frame];
		for (unsigned int slot = 0; slot < kMaxTextureSlots; slot++)
		{
			const auto& texture = slot < s_Data->NextTextureSlot && s_Data->TextureSlots[slot]
								? s_Data->TextureSlots[slot]
								: s_Data->WhiteTexture;
			textureSet->SetTexture(0, texture, s_Data->Sampler, slot);
		}
		textureSet->Commit();

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, sceneSet);
		cmd->BindResourceSet(1, textureSet);
		cmd->BindVertexBuffer(0, vertexBuffer);
		cmd->BindIndexBuffer(s_Data->IndexBuffer, IndexType::UInt32);
		cmd->DrawIndexed(s_Data->IndexCount);

		s_Data->DrawCalls++;
	}

	void Renderer2D::FlushAndReset()
	{
		EndScene();
		ResetScene();
	}

	unsigned int Renderer2D::ResolveTextureSlot(const Ref<RHITexture>& texture)
	{
		for (unsigned int i = 1; i < s_Data->NextTextureSlot; i++)
		{
			if (s_Data->TextureSlots[i] == texture)
				return i;
		}

		if (s_Data->NextTextureSlot >= kMaxTextureSlots)
			FlushAndReset();

		const unsigned int slot = s_Data->NextTextureSlot;
		s_Data->TextureSlots[slot] = texture;
		s_Data->NextTextureSlot++;
		return slot;
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color)
	{
		if (!s_Data)
			return;

		if (s_Data->QuadCount >= kMaxQuads)
			FlushAndReset();

		// Hoisted out of the vertex loop: this used to run a 4x4 inverse and a
		// transpose per vertex to produce the same vector four times.
		const glm::vec3 normal = WorldSpaceNormal(transform);

		VertexData* vertex = s_Data->VertexCursor;
		for (int i = 0; i < 4; i++, vertex++)
		{
			vertex->Position = glm::vec3(transform * kQuadCorners[i]);
			vertex->Normal = normal;
			vertex->Color = color;
			vertex->TexCoord = kQuadTexCoords[i];
			vertex->TextureIndex = 0.0f;
			vertex->TilingFactor = 1.0f;
		}
		s_Data->VertexCursor = vertex;

		s_Data->QuadCount++;
		s_Data->IndexCount += 6;
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<RHITexture>& texture, float tilingfactor)
	{
		if (!s_Data)
			return;

		if (s_Data->QuadCount >= kMaxQuads)
			FlushAndReset();

		const float slot = (float)ResolveTextureSlot(texture);
		const glm::vec3 normal = WorldSpaceNormal(transform);

		VertexData* vertex = s_Data->VertexCursor;
		for (int i = 0; i < 4; i++, vertex++)
		{
			vertex->Position = glm::vec3(transform * kQuadCorners[i]);
			vertex->Normal = normal;
			vertex->Color = glm::vec4(1.0f);
			vertex->TexCoord = kQuadTexCoords[i];
			vertex->TextureIndex = slot;
			vertex->TilingFactor = tilingfactor;
		}
		s_Data->VertexCursor = vertex;

		s_Data->QuadCount++;
		s_Data->IndexCount += 6;
	}

	unsigned int Renderer2D::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }
	unsigned int Renderer2D::GetVerticesCount() { return s_Data ? s_Data->QuadCount * 4 : 0; }
	unsigned int Renderer2D::GetIndiciesCount() { return s_Data ? s_Data->IndexCount : 0; }
	unsigned int Renderer2D::GetQuadCount()     { return s_Data ? s_Data->QuadCount : 0; }
}
