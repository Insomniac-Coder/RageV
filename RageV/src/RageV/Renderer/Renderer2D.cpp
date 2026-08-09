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

		constexpr Vec4 kQuadCorners[4] = {
			{ -0.5f, -0.5f, 0.0f, 1.0f }, {  0.5f, -0.5f, 0.0f, 1.0f },
			{  0.5f,  0.5f, 0.0f, 1.0f }, { -0.5f,  0.5f, 0.0f, 1.0f },
		};
		constexpr Vec2 kQuadTexCoords[4] = {
			{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
		};

		struct VertexData
		{
			Vec3 Position;
			Vec3 Normal;
			Vec4 Color;
			Vec2 TexCoord;
			float     TextureIndex;
			float     TilingFactor;
		};
		static_assert(sizeof(VertexData) == 56, "Must match the vertex stride reflected from quad.rvshader");

		// Mirrors the std140 SceneData block in quad.rvshader.
		struct SceneUniforms
		{
			Mat4 ViewProjection;
			Vec4 CameraPosition;
			Vec4 LightPositions[kMaxLights];
			Vec4 LightColors[kMaxLights];
			int32_t   LightCount;
			int32_t   _padding[3];
		};

		// The quad's object-space normal rotated into world space. The
		// inverse-transpose is only needed under non-uniform scale or shear;
		// for the rigid transforms quads use, the renormalised upper 3x3 is
		// equivalent and far cheaper.
		Vec3 WorldSpaceNormal(const Mat4& transform)
		{
			return Math::Normalize(Mat3(transform) * Vec3(0.0f, 0.0f, -1.0f));
		}

		struct Renderer2DData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			Format TargetColor = Format::B8G8R8A8_UNORM;
			Format TargetDepth = Format::D24_UNORM_S8_UINT;
			bool   PipelineDirty = true;
			bool   Wireframe = false;

			// One set of storage per *batch*, not per frame in flight.
			//
			// Per-frame alone was not enough. A batch's draw is recorded against
			// whatever buffer it was bound to, and the data has to still be
			// there when the GPU runs it -- so anything that ends a scene more
			// than once in a frame needs separate storage each time. Two things
			// do: a scene that overflows kMaxQuads and flushes mid-way, and
			// drawing the same scene into two viewports. The first was already
			// a live bug; it just needed 20000 quads to show itself.
			struct Batch
			{
				Ref<RHIBuffer>      Vertices;
				Ref<RHIBuffer>      Scene;
				Ref<RHIResourceSet> SceneSet;
				Ref<RHIResourceSet> TextureSet;
			};

			// [frame in flight][batch within the frame]
			std::vector<std::vector<Batch>> Batches;
			uint32_t BatchCursor = 0;

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

			// False when the shader did not compile. Asked by the test
			// suite, so a silent failure fails a build rather than a picture.
			bool Ready = false;
		};

		std::unique_ptr<Renderer2DData> s_Data;

		// The batch storage for the current scene. Grown on demand and kept, so a
		// scene that needs three batches allocates them once and reuses them every
		// frame after.
		Renderer2DData::Batch& AcquireBatch()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& batches = s_Data->Batches[frame];

			if (s_Data->BatchCursor >= batches.size())
			{
				const std::string index = std::to_string(frame) + "." + std::to_string(batches.size());

				Renderer2DData::Batch batch;

				BufferDesc vertexDesc;
				vertexDesc.Size = (uint64_t)kMaxVertices * sizeof(VertexData);
				vertexDesc.Usage = BufferUsage::Vertex;
				vertexDesc.Memory = MemoryDomain::HostVisible;
				vertexDesc.DebugName = "Renderer2D.vertices." + index;
				batch.Vertices = s_Data->Device->CreateBuffer(vertexDesc);

				BufferDesc sceneDesc;
				sceneDesc.Size = sizeof(SceneUniforms);
				sceneDesc.Usage = BufferUsage::Uniform;
				sceneDesc.Memory = MemoryDomain::HostVisible;
				sceneDesc.DebugName = "Renderer2D.scene." + index;
				batch.Scene = s_Data->Device->CreateBuffer(sceneDesc);

				batches.push_back(std::move(batch));
			}

			Renderer2DData::Batch& batch = batches[s_Data->BatchCursor++];

			// Created lazily rather than in Init: sets need a pipeline, and the
			// pipeline is built from the target formats, which Init does not know.
			if (!batch.SceneSet)
				batch.SceneSet = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);
			if (!batch.TextureSet)
				batch.TextureSet = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 1);

			return batch;
		}
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
		s_Data->Ready = s_Data->Shader != nullptr;

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

		// Batches are created on demand; most frames need one.
		s_Data->Batches.resize(frames);

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

		if (s_Data->Ready)
			RV_CORE_INFO("Renderer2D ready ({0} frames in flight, {1} quads per batch)", frames, kMaxQuads);
		else
			RV_CORE_ERROR("Renderer2D incomplete; quads will not draw");
	}

	bool Renderer2D::IsReady()
	{
		return s_Data && s_Data->Ready;
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

	void Renderer2D::SetWireframe(bool enabled)
	{
		if (!s_Data || s_Data->Wireframe == enabled)
			return;

		s_Data->Wireframe = enabled;
		// Polygon mode is baked into the pipeline on Vulkan, so this rebuilds
		// rather than toggling state.
		s_Data->PipelineDirty = true;
	}

	bool Renderer2D::IsWireframe()
	{
		return s_Data && s_Data->Wireframe;
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
		desc.Rasterizer.Polygon = s_Data->Wireframe ? PolygonMode::Line : PolygonMode::Fill;
		desc.Blend = BlendPreset::AlphaBlend;
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = true;
		desc.ColorFormats = { s_Data->TargetColor };
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
		s_Data->PipelineDirty = false;

		// Resource sets are tied to a pipeline layout, so every batch's sets are
		// discarded with the pipeline and recreated on demand.
		for (auto& frame : s_Data->Batches)
			frame.clear();
	}

	void Renderer2D::BeginFrame()
	{
		if (!s_Data)
			return;

		// The GPU has finished with this frame's slot, so its batches are free
		// to reuse.
		s_Data->BatchCursor = 0;
		s_Data->DrawCalls = 0;
	}

	void Renderer2D::BeginScene(const Camera& camera, const Mat4& transform, const LightList& lights)
	{
		if (!s_Data)
			return;

		ResetScene();

		s_Data->Scene = {};
		s_Data->Scene.ViewProjection = camera.GetProjection() * Math::Inverse(transform);
		s_Data->Scene.CameraPosition = Vec4(Vec3(transform[3]), 1.0f);

		const int lightCount = (int)std::min<size_t>(lights.size(), kMaxLights);
		for (int i = 0; i < lightCount; i++)
		{
			// The quad shader has no attenuation model, so intensity is folded
			// into the colour rather than carried separately.
			s_Data->Scene.LightPositions[i] = Vec4(lights[i].Position, 1.0f);
			s_Data->Scene.LightColors[i] = Vec4(lights[i].Color * lights[i].Intensity, 1.0f);
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

		// Its own storage, so this draw's data survives until the GPU runs it
		// even if another batch is recorded before the frame ends.
		Renderer2DData::Batch& batch = AcquireBatch();

		batch.Vertices->Upload(s_Data->Vertices.data(),
							   (uint64_t)s_Data->QuadCount * 4 * sizeof(VertexData));
		batch.Scene->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		auto& sceneSet = batch.SceneSet;
		sceneSet->SetUniformBuffer(0, batch.Scene, 0, sizeof(SceneUniforms));
		sceneSet->Commit();

		// Every element of the sampler array has to be written even though only
		// the occupied slots are read: the shader indexes it dynamically, so
		// validation treats all of them as potentially accessed.
		auto& textureSet = batch.TextureSet;
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
		cmd->BindVertexBuffer(0, batch.Vertices);
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

	void Renderer2D::DrawQuad(const Mat4& transform, const Vec4& color)
	{
		if (!s_Data)
			return;

		if (s_Data->QuadCount >= kMaxQuads)
			FlushAndReset();

		// Hoisted out of the vertex loop: this used to run a 4x4 inverse and a
		// transpose per vertex to produce the same vector four times.
		const Vec3 normal = WorldSpaceNormal(transform);

		VertexData* vertex = s_Data->VertexCursor;
		for (int i = 0; i < 4; i++, vertex++)
		{
			vertex->Position = Vec3(transform * kQuadCorners[i]);
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

	void Renderer2D::DrawQuad(const Mat4& transform, const Ref<RHITexture>& texture, float tilingfactor)
	{
		if (!s_Data)
			return;

		if (s_Data->QuadCount >= kMaxQuads)
			FlushAndReset();

		const float slot = (float)ResolveTextureSlot(texture);
		const Vec3 normal = WorldSpaceNormal(transform);

		VertexData* vertex = s_Data->VertexCursor;
		for (int i = 0; i < 4; i++, vertex++)
		{
			vertex->Position = Vec3(transform * kQuadCorners[i]);
			vertex->Normal = normal;
			vertex->Color = Vec4(1.0f);
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
