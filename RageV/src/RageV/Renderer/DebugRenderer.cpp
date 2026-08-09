#include <rvpch.h>
#include "DebugRenderer.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Renderer/Frustum.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Two vertices each. Generous: the whole point is that turning the
		// overlay on should never be the reason something stops being drawn,
		// and 32k lines is about 1.8 MB of vertices.
		constexpr uint32_t kMaxLines = 32768;
		constexpr uint32_t kMaxVertices = kMaxLines * 2;

		// Circles are drawn as line loops. 24 reads as round at any size a
		// collider is inspected at, and the cost is per segment rather than per
		// pixel.
		constexpr int kCircleSegments = 24;

		struct VertexData
		{
			Vec3 Position;
			Vec4 Color;
		};
		static_assert(sizeof(VertexData) == 28, "Must match the vertex stride reflected from debug.rvshader");

		struct SceneUniforms
		{
			Mat4 ViewProjection;
		};

		struct DebugRendererData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			Format TargetColor = Format::B8G8R8A8_UNORM;
			Format TargetDepth = Format::D24_UNORM_S8_UINT;
			bool   PipelineDirty = true;

			// Per batch, not per frame, for the reason recorded on Renderer2D:
			// a draw reads its buffer when the GPU runs it, and the editor ends
			// this renderer's scene twice in a frame once there are two
			// viewports.
			struct Batch
			{
				Ref<RHIBuffer>      Vertices;
				Ref<RHIBuffer>      Scene;
				Ref<RHIResourceSet> SceneSet;
			};

			std::vector<std::vector<Batch>> Batches;
			uint32_t BatchCursor = 0;

			std::vector<VertexData> Vertices;
			uint32_t LineCount = 0;

			SceneUniforms Scene{};
			bool InScene = false;

			// The volume the current scene can see, and how many shapes fell
			// outside it. Built in BeginScene from the same matrix the shader
			// gets, so what is culled and what is drawn cannot disagree.
			Frustum View;
			uint32_t CulledCount = 0;

			// False when the shader did not compile. Asked by the test
			// suite, so a silent failure fails a build rather than a picture.
			bool Ready = false;
		};

		std::unique_ptr<DebugRendererData> s_Data;

		DebugRendererData::Batch& AcquireBatch()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& batches = s_Data->Batches[frame];

			if (s_Data->BatchCursor >= batches.size())
			{
				const std::string index = std::to_string(frame) + "." + std::to_string(batches.size());

				DebugRendererData::Batch batch;

				BufferDesc vertexDesc;
				vertexDesc.Size = (uint64_t)kMaxVertices * sizeof(VertexData);
				vertexDesc.Usage = BufferUsage::Vertex;
				vertexDesc.Memory = MemoryDomain::HostVisible;
				vertexDesc.DebugName = "DebugRenderer.vertices." + index;
				batch.Vertices = s_Data->Device->CreateBuffer(vertexDesc);

				BufferDesc sceneDesc;
				sceneDesc.Size = sizeof(SceneUniforms);
				sceneDesc.Usage = BufferUsage::Uniform;
				sceneDesc.Memory = MemoryDomain::HostVisible;
				sceneDesc.DebugName = "DebugRenderer.scene." + index;
				batch.Scene = s_Data->Device->CreateBuffer(sceneDesc);

				batches.push_back(std::move(batch));
			}

			DebugRendererData::Batch& batch = batches[s_Data->BatchCursor++];

			if (!batch.SceneSet)
				batch.SceneSet = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

			return batch;
		}

		// The two axes spanning the plane whose normal is `axis`, so one circle
		// routine serves all three orientations.
		void CircleBasis(int axis, Vec3& u, Vec3& v)
		{
			switch (axis)
			{
				case 0:  u = { 0.0f, 1.0f, 0.0f }; v = { 0.0f, 0.0f, 1.0f }; break;  // x
				case 1:  u = { 1.0f, 0.0f, 0.0f }; v = { 0.0f, 0.0f, 1.0f }; break;  // y
				default: u = { 1.0f, 0.0f, 0.0f }; v = { 0.0f, 1.0f, 0.0f }; break;  // z
			}
		}

		// A closed circle in the plane of u and v, centred on `centre`.
		void AddCircle(const Vec3& centre, const Vec3& u, const Vec3& v,
					   float radius, const Vec4& color)
		{
			Vec3 previous = centre + u * radius;

			for (int i = 1; i <= kCircleSegments; i++)
			{
				const float angle = (float)i / (float)kCircleSegments * Math::TwoPi;
				const Vec3 point = centre + (u * std::cos(angle) + v * std::sin(angle)) * radius;

				DebugRenderer::DrawLine(previous, point, color);
				previous = point;
			}
		}

		// Half a circle, from u through v and back to -u. The caps of a capsule.
		void AddArc(const Vec3& centre, const Vec3& u, const Vec3& v,
					float radius, const Vec4& color)
		{
			const int segments = kCircleSegments / 2;
			Vec3 previous = centre + u * radius;

			for (int i = 1; i <= segments; i++)
			{
				const float angle = (float)i / (float)segments * Math::Pi;
				const Vec3 point = centre + (u * std::cos(angle) + v * std::sin(angle)) * radius;

				DebugRenderer::DrawLine(previous, point, color);
				previous = point;
			}
		}
	}

	void DebugRenderer::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<DebugRendererData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();
		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/debug.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("DebugRenderer: failed to compile assets/shaders/debug.rvshader");
			return;
		}
		s_Data->Shader = device.CreateShader(*compiled);
		s_Data->Ready = s_Data->Shader != nullptr;

		s_Data->Batches.resize(device.GetFramesInFlight());
		s_Data->Vertices.resize(kMaxVertices);

		if (s_Data->Ready)
			RV_CORE_INFO("DebugRenderer ready ({0} lines per batch)", kMaxLines);
		else
			RV_CORE_ERROR("DebugRenderer incomplete; overlays will not draw");
	}

	bool DebugRenderer::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void DebugRenderer::Shutdown()
	{
		s_Data.reset();
	}

	void DebugRenderer::SetTargetFormats(Format color, Format depth)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void DebugRenderer::EnsurePipeline()
	{
		if (!s_Data->PipelineDirty || !s_Data->Shader)
			return;

		GraphicsPipelineDesc desc;
		desc.Name = "DebugRenderer.lines";
		desc.Shader = s_Data->Shader;
		desc.Topology = PrimitiveTopology::LineList;
		desc.Rasterizer.Cull = CullMode::None;
		desc.Blend = BlendPreset::AlphaBlend;

		// Tested but not written.
		//
		// Tested, so a collider behind a wall is behind the wall -- an overlay
		// that ignores depth turns a scene of any size into a thicket and stops
		// telling you where anything is. Not written, so the lines cannot
		// occlude each other or anything drawn after them: they are an
		// annotation on the image, not part of it.
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = false;

		desc.ColorFormats = { s_Data->TargetColor };
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
		s_Data->PipelineDirty = false;

		// Resource sets belong to a pipeline layout, so they go with it.
		for (auto& frame : s_Data->Batches)
			frame.clear();
	}

	void DebugRenderer::BeginFrame()
	{
		if (!s_Data)
			return;

		s_Data->BatchCursor = 0;
	}

	void DebugRenderer::BeginScene(const Camera& camera, const Mat4& cameraTransform)
	{
		if (!s_Data)
			return;

		s_Data->LineCount = 0;
		s_Data->CulledCount = 0;
		s_Data->InScene = true;
		s_Data->Scene.ViewProjection = camera.GetProjection() * Math::Inverse(cameraTransform);
		s_Data->View.Build(s_Data->Scene.ViewProjection);
	}

	bool DebugRenderer::Visible(const Vec3& centre, float radius)
	{
		if (!s_Data || !s_Data->InScene)
			return true;

		// A box around the sphere rather than a plane-distance test: it is
		// conservative in the safe direction -- it can keep something that is
		// off screen, and never drops something that is on it.
		if (s_Data->View.Intersects(centre, Vec3(radius)))
			return true;

		s_Data->CulledCount++;
		return false;
	}

	uint32_t DebugRenderer::GetCulledCount()
	{
		return s_Data ? s_Data->CulledCount : 0;
	}

	void DebugRenderer::EndScene()
	{
		if (!s_Data)
			return;

		Flush();
		s_Data->InScene = false;
		s_Data->LineCount = 0;
	}

	void DebugRenderer::Flush()
	{
		if (s_Data->LineCount == 0)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		DebugRendererData::Batch& batch = AcquireBatch();

		const uint32_t vertexCount = s_Data->LineCount * 2;
		batch.Vertices->Upload(s_Data->Vertices.data(), (uint64_t)vertexCount * sizeof(VertexData));
		batch.Scene->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		batch.SceneSet->SetUniformBuffer(0, batch.Scene, 0, sizeof(SceneUniforms));
		batch.SceneSet->Commit();

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, batch.SceneSet);
		cmd->BindVertexBuffer(0, batch.Vertices);
		// No index buffer: a line list shares no vertices between lines, so an
		// index per vertex would be a strictly larger way to say the same thing.
		cmd->Draw(vertexCount);
	}

	uint32_t DebugRenderer::GetLineCount()
	{
		return s_Data ? s_Data->LineCount : 0;
	}

	// -------------------------------------------------------------------------
	// Primitives
	// -------------------------------------------------------------------------
	void DebugRenderer::DrawLine(const Vec3& from, const Vec3& to, const Vec4& color)
	{
		// Outside a scene rather than inside one: silently dropping is right
		// here, because debug draw is called from wherever a question is being
		// asked and must never be the thing that breaks a frame.
		if (!s_Data || !s_Data->InScene || s_Data->LineCount >= kMaxLines)
			return;

		VertexData* vertex = &s_Data->Vertices[s_Data->LineCount * 2];
		vertex[0].Position = from;
		vertex[0].Color = color;
		vertex[1].Position = to;
		vertex[1].Color = color;

		s_Data->LineCount++;
	}

	void DebugRenderer::DrawBox(const Mat4& transform, const Vec3& halfExtents,
								const Vec4& color)
	{
		// The sphere that contains the box however it is turned. Scale is not
		// assumed to be absent from the matrix even though these callers do not
		// put it there -- the longest basis vector covers it either way.
		const float scale = std::max({ Math::Length(Vec3(transform[0])),
									   Math::Length(Vec3(transform[1])),
									   Math::Length(Vec3(transform[2])) });

		if (!Visible(Vec3(transform[3]), Math::Length(halfExtents) * scale))
			return;

		// The eight corners, transformed once each, rather than twelve edges
		// transformed at both ends.
		Vec3 corner[8];
		for (int i = 0; i < 8; i++)
		{
			const Vec3 sign{
				(i & 1) ? 1.0f : -1.0f,
				(i & 2) ? 1.0f : -1.0f,
				(i & 4) ? 1.0f : -1.0f,
			};
			corner[i] = Vec3(transform * Vec4(sign * halfExtents, 1.0f));
		}

		// Two opposing faces, then the four struts between them. Corners differ
		// in exactly one bit iff they share an edge.
		static constexpr int kEdges[12][2] = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },   // -z face
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },   // +z face
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // between them
		};

		for (const auto& edge : kEdges)
			DrawLine(corner[edge[0]], corner[edge[1]], color);
	}

	void DebugRenderer::DrawSphere(const Mat4& transform, float radius, const Vec4& color)
	{
		const Vec3 centre = Vec3(transform[3]);

		if (!Visible(centre, radius))
			return;

		for (int axis = 0; axis < 3; axis++)
		{
			Vec3 u, v;
			CircleBasis(axis, u, v);

			// Rotated with the entity so the rings track its orientation, which
			// is the only way a sphere collider shows that it is rotating.
			AddCircle(centre,
					  Vec3(transform * Vec4(u, 0.0f)),
					  Vec3(transform * Vec4(v, 0.0f)),
					  radius, color);
		}
	}

	void DebugRenderer::DrawCapsule(const Mat4& transform, float radius, float halfHeight,
									const Vec4& color)
	{
		const Vec3 centre = Vec3(transform[3]);
		const Vec3 right = Vec3(transform * Vec4(1.0f, 0.0f, 0.0f, 0.0f));
		const Vec3 up = Vec3(transform * Vec4(0.0f, 1.0f, 0.0f, 0.0f));
		const Vec3 forward = Vec3(transform * Vec4(0.0f, 0.0f, 1.0f, 0.0f));

		if (!Visible(centre, halfHeight + radius))
			return;

		// A capsule is Y-up, matching ColliderComponent::Height and Jolt.
		const Vec3 top = centre + up * halfHeight;
		const Vec3 bottom = centre - up * halfHeight;

		// The rings where the caps meet the cylinder.
		AddCircle(top, right, forward, radius, color);
		AddCircle(bottom, right, forward, radius, color);

		// Four struts, so the cylinder reads as a cylinder from any angle.
		for (int i = 0; i < 4; i++)
		{
			const float angle = (float)i / 4.0f * Math::TwoPi;
			const Vec3 offset = (right * std::cos(angle) + forward * std::sin(angle)) * radius;
			DrawLine(bottom + offset, top + offset, color);
		}

		// Two arcs per cap, at right angles: enough to read as a dome without
		// drawing a hemisphere's worth of lines.
		AddArc(top, right, up, radius, color);
		AddArc(top, forward, up, radius, color);
		AddArc(bottom, right, -up, radius, color);
		AddArc(bottom, forward, -up, radius, color);
	}
}
