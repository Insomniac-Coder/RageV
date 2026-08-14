#include <rvpch.h>
#include <map>
#include "UIRenderer.h"
#include "Renderer.h"
#include "RageV/UI/TextLayout.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// A HUD is hundreds of quads; a screen of text is a few thousand. This
		// is generous, and overflowing it flushes rather than dropping.
		constexpr uint32_t kMaxQuads = 10000;
		constexpr uint32_t kMaxVertices = kMaxQuads * 4;
		constexpr uint32_t kMaxIndices = kMaxQuads * 6;
		constexpr uint32_t kMaxTextureSlots = 32;   // must match ui.rvshader

		constexpr float kModePlain = 0.0f;
		constexpr float kModeField = 1.0f;

		// Three components, not two, and the screen layer writes z = 0.
		//
		// Text in the world needs a real position, and giving it its own vertex
		// type would mean a second pipeline layout and a second copy of the
		// fragment stage. The two layers already differ in exactly two things
		// -- the matrix and the depth state -- and this keeps it to two.
		struct UIVertex
		{
			Vec3  Position;
			Vec2  TexCoord;
			Vec4  Color;
			float TextureIndex;
			Vec2  Mode;         // x: plain or field, y: the field's range in atlas pixels
		};
		static_assert(sizeof(UIVertex) == 48,
					  "Must match the vertex stride reflected from ui.rvshader");

		struct UIData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;

			// Two pipelines, one shader. They differ in exactly two things --
			// whether the depth test runs, and what they draw into -- and
			// nothing else about the two layers is allowed to diverge.
			Ref<RHIPipeline> Pipeline;
			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::Undefined;
			bool   PipelineDirty = true;

			Ref<RHIPipeline> WorldPipeline;
			Format WorldColor = Format::R16G16B16A16_SFLOAT;
			Format WorldDepth = Format::D32_SFLOAT;
			// The world layer draws *inside the scene pass*, so it takes the
			// scene's sample count -- while the screen-space layer above stays
			// at one, because it draws into the finished image. Two layers,
			// two pipelines, two counts, and conflating them is what made a
			// four-sample scene bind a one-sample pipeline and crash.
			uint32_t WorldSamples = 1;
			Format WorldVelocity = Format::Undefined;
			Ref<RHIShader> WorldShader;
			bool   WorldPipelineDirty = true;

			// One world pipeline per sample count, rather than one rebuilt
			// whenever the count changes.
			//
			// The scene target is multisampled under MSAA and a probe face is
			// never multisampled, so a frame that captures a probe asks for
			// both -- and rebuilding flushes the batch buffers, which is fine
			// once at startup and catastrophic in the middle of a frame that
			// has already recorded into them. That is not a hypothetical: it
			// was an out-of-range subscript on the first frame of any scene
			// containing world text and a reflection probe. ENGINE-NOTES 7q.
			std::map<uint32_t, Ref<RHIPipeline>> WorldPipelines;

			// One set of storage per batch, for the reason Renderer2D documents:
			// a draw is recorded against the buffer it was bound to, so anything
			// that flushes twice in a frame needs separate storage each time.
			struct Batch
			{
				Ref<RHIBuffer>      Vertices;
				Ref<RHIResourceSet> TextureSet;
			};

			std::vector<std::vector<Batch>> Batches;   // [frame in flight][batch]
			uint32_t BatchCursor = 0;

			Ref<RHIBuffer>  IndexBuffer;
			Ref<RHITexture> WhiteTexture;
			Ref<RHISampler> Sampler;

			std::vector<UIVertex> Vertices;
			uint32_t QuadCount = 0;

			std::vector<Ref<RHITexture>> TextureSlots;
			uint32_t NextTextureSlot = 1;   // 0 is the white texture

			Mat4 Projection{ 1.0f };
			bool InLayer = false;

			// Which pipeline the flush should bind. Set by Begin/BeginWorld,
			// read by Flush, so a quad cannot be recorded against one layer and
			// drawn with the other's depth state.
			bool WorldLayer = false;

			uint32_t DrawCalls = 0;
			uint32_t QuadsThisFrame = 0;

			bool Ready = false;
		};

		std::unique_ptr<UIData> s_Data;

		// Takes the pipeline the set will be built against. Both layers share
		// one pool of batches, and a resource set belongs to a pipeline
		// *layout* -- which is identical here, since it is one shader -- but
		// passing it keeps that an argument rather than an assumption.
		UIData::Batch& AcquireBatch(const Ref<RHIPipeline>& pipeline)
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& batches = s_Data->Batches[frame];

			// A loop, not an `if`, and the difference is a crash.
			//
			// Creating a pipeline clears this pool, because a resource set
			// belongs to a pipeline layout. With one pipeline that only ever
			// happened before the first draw, so growing by one per acquire was
			// enough. With two, the *second* pipeline is built partway through
			// a frame in which the cursor has already advanced -- and then a
			// single push leaves the pool shorter than the cursor, and the
			// index below runs off the end.
			while (s_Data->BatchCursor >= batches.size())
			{
				UIData::Batch batch;

				BufferDesc desc;
				desc.Size = (uint64_t)kMaxVertices * sizeof(UIVertex);
				desc.Usage = BufferUsage::Vertex;
				desc.Memory = MemoryDomain::HostVisible;
				desc.DebugName = "UIRenderer.vertices." + std::to_string(frame) + "." +
								 std::to_string(batches.size());
				batch.Vertices = s_Data->Device->CreateBuffer(desc);

				batches.push_back(std::move(batch));
			}

			UIData::Batch& batch = batches[s_Data->BatchCursor++];

			if (!batch.TextureSet)
				batch.TextureSet = s_Data->Device->CreateResourceSet(pipeline, 0);

			return batch;
		}

		void EnsurePipeline()
		{
			if (!s_Data->PipelineDirty || !s_Data->Shader)
				return;

			GraphicsPipelineDesc desc;
			desc.Name = "UIRenderer";
			desc.Shader = s_Data->Shader;
			desc.Topology = PrimitiveTopology::TriangleList;
			desc.Rasterizer.Cull = CullMode::None;
			desc.Blend = BlendPreset::AlphaBlend;

			// No depth at all. UI layering is a sort order somebody authored,
			// resolved by the order quads are submitted -- the painter's
			// algorithm. A depth buffer would make the layering depend on
			// numbers nobody set.
			desc.DepthStencil.DepthTestEnable = false;
			desc.DepthStencil.DepthWriteEnable = false;

			desc.ColorFormats = { s_Data->TargetColor };
			desc.DepthFormat = Format::Undefined;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->PipelineDirty = false;

			// Sets belong to a pipeline layout, so they go with the pipeline.
			for (auto& frame : s_Data->Batches)
				frame.clear();
		}

		void EnsureWorldPipeline()
		{
			if (!s_Data->Shader)
				return;

			// Already built for this sample count: nothing to do, and in
			// particular nothing to clear.
			const auto cached = s_Data->WorldPipelines.find(s_Data->WorldSamples);
			if (cached != s_Data->WorldPipelines.end())
			{
				s_Data->WorldPipeline = cached->second;
				s_Data->WorldPipelineDirty = false;
				return;
			}

			GraphicsPipelineDesc desc;
			desc.Name = "UIRenderer (world text)";
			desc.Shader = s_Data->WorldShader ? s_Data->WorldShader : s_Data->Shader;
			desc.Topology = PrimitiveTopology::TriangleList;

			// Two-sided, so a label keeps reading from behind rather than
			// disappearing. A billboard never presents its back, but text laid
			// flat in the world does, and vanishing is the worse answer.
			desc.Rasterizer.Cull = CullMode::None;
			desc.Blend = BlendPreset::AlphaBlend;

			// **Tested, not written**, which is the whole difference from the
			// screen layer. Testing is what lets geometry occlude a nameplate
			// -- the point of drawing text in the world at all. Writing would
			// have each glyph quad occlude the transparent things behind it,
			// including the rest of its own string where the quads overlap.
			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = false;

			desc.ColorFormats = { s_Data->WorldColor };
			if (s_Data->WorldVelocity != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->WorldVelocity);
			desc.Samples = s_Data->WorldSamples;
			desc.DepthFormat = s_Data->WorldDepth;

			s_Data->WorldPipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->WorldPipelines[s_Data->WorldSamples] = s_Data->WorldPipeline;
			s_Data->WorldPipelineDirty = false;

			for (auto& frame : s_Data->Batches)
				frame.clear();
		}

		uint32_t ResolveTextureSlot(const Ref<RHITexture>& texture);
		void Flush();

		// Four vertices, one quad, in submission order. The index buffer pairs
		// them as 0-1-2, 2-3-0, so this is top-left, top-right, bottom-right,
		// bottom-left.
		// The general case: four positions, in whatever space the current
		// layer's matrix expects. Both layers come through here, so batching,
		// slot assignment and the flush-on-full rule cannot drift apart
		// between them.
		void PushQuadCorners(const Vec3 corners[4], const Vec4& color,
							 const Ref<RHITexture>& texture,
							 float u0, float v0, float u1, float v1,
							 float mode, float pxRange)
		{
			if (!s_Data->InLayer)
				return;

			if (s_Data->QuadCount >= kMaxQuads)
				Flush();

			const uint32_t slot = ResolveTextureSlot(texture);

			UIVertex* vertex = s_Data->Vertices.data() + (size_t)s_Data->QuadCount * 4;

			const Vec2 uvs[4] = {
				{ u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 },
			};

			for (int i = 0; i < 4; i++)
			{
				vertex[i].Position = corners[i];
				vertex[i].TexCoord = uvs[i];
				vertex[i].Color = color;
				vertex[i].TextureIndex = (float)slot;
				vertex[i].Mode = Vec2(mode, pxRange);
			}

			s_Data->QuadCount++;
			s_Data->QuadsThisFrame++;
		}

		void PushQuad(const UIRect& rect, const Vec4& color,
					  const Ref<RHITexture>& texture, float u0, float v0, float u1, float v1,
					  float mode, float pxRange)
		{
			// z = 0: the screen layer's matrix is orthographic over -1..1, so
			// every quad lands in the middle of that range and the depth test
			// is off anyway.
			const Vec3 corners[4] = {
				{ rect.X,       rect.Y,        0.0f },
				{ rect.Right(), rect.Y,        0.0f },
				{ rect.Right(), rect.Bottom(), 0.0f },
				{ rect.X,       rect.Bottom(), 0.0f },
			};

			PushQuadCorners(corners, color, texture, u0, v0, u1, v1, mode, pxRange);
		}

		uint32_t ResolveTextureSlot(const Ref<RHITexture>& texture)
		{
			if (!texture)
				return 0;   // the white texture

			for (uint32_t i = 1; i < s_Data->NextTextureSlot; i++)
			{
				if (s_Data->TextureSlots[i] == texture)
					return i;
			}

			if (s_Data->NextTextureSlot >= kMaxTextureSlots)
			{
				// Out of slots. Flushing frees them all, which costs a draw
				// call and keeps the picture correct -- the alternative is
				// drawing the wrong texture, silently.
				Flush();
			}

			const uint32_t slot = s_Data->NextTextureSlot++;
			s_Data->TextureSlots[slot] = texture;
			return slot;
		}

		void Flush()
		{
			if (s_Data->QuadCount == 0)
				return;

			RHICommandList* cmd = Renderer::GetCommandList();
			if (!cmd)
			{
				s_Data->QuadCount = 0;
				return;
			}

			if (s_Data->WorldLayer)
				EnsureWorldPipeline();
			else
				EnsurePipeline();

			const Ref<RHIPipeline>& pipeline =
				s_Data->WorldLayer ? s_Data->WorldPipeline : s_Data->Pipeline;

			if (!pipeline)
			{
				s_Data->QuadCount = 0;
				return;
			}

			UIData::Batch& batch = AcquireBatch(pipeline);

			batch.Vertices->Upload(s_Data->Vertices.data(),
								   (uint64_t)s_Data->QuadCount * 4 * sizeof(UIVertex));

			// Every element written, even the unused ones: the shader indexes
			// the array dynamically, so validation treats all of them as live.
			for (uint32_t slot = 0; slot < kMaxTextureSlots; slot++)
			{
				const Ref<RHITexture>& texture =
					slot < s_Data->NextTextureSlot && s_Data->TextureSlots[slot]
						? s_Data->TextureSlots[slot]
						: s_Data->WhiteTexture;
				batch.TextureSet->SetTexture(0, texture, s_Data->Sampler, slot);
			}
			batch.TextureSet->Commit();

			cmd->BindPipeline(pipeline);
			cmd->BindResourceSet(0, batch.TextureSet);
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(Mat4), &s_Data->Projection);
			cmd->BindVertexBuffer(0, batch.Vertices);
			cmd->BindIndexBuffer(s_Data->IndexBuffer, IndexType::UInt32);
			cmd->DrawIndexed(s_Data->QuadCount * 6);

			s_Data->DrawCalls++;
			s_Data->QuadCount = 0;

			// Slots are per draw call, so a flush releases them.
			s_Data->NextTextureSlot = 1;
		}
	}

	Mat4 UIRenderer::BuildProjection(uint32_t width, uint32_t height)
	{
		const float w = (float)Math::Max((int)width, 1);
		const float h = (float)Math::Max((int)height, 1);

		// **There is no backend difference here**, and that is worth stating
		// because it looks as though there should be.
		//
		// Vulkan's framebuffer origin is the top left and OpenGL's is the
		// bottom left, so the obvious move is to branch on the backend. This
		// was written that way first, and it was wrong in *both* directions --
		// the HUD came out mirrored along the bottom edge on whichever API the
		// branch happened to favour.
		//
		// The reason is that this pass does not draw into a raw framebuffer. It
		// draws into the *finished* image, after tone mapping and
		// anti-aliasing, and the post chain has already normalised the
		// orientation: it samples with a flip on Vulkan (`FlipY` in
		// PostProcess and tonemap.rvshader) precisely so that what comes out
		// the far end is the same on both. By the time the UI runs, it is.
		//
		// Orthographic maps `bottom` to clip -1 and `top` to clip +1, so "UI
		// y = 0 is the top of what the viewer sees" is bottom = h, top = 0.
		// A check asserts it on each backend rather than trusting this note.
		return Math::Orthographic(0.0f, w, h, 0.0f, -1.0f, 1.0f);
	}

	void UIRenderer::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<UIData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/ui.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("UIRenderer: failed to compile assets/shaders/ui.rvshader");
			return;
		}

		s_Data->Shader = device.CreateShader(*compiled);

		// The world layer's twin, which differs only by a velocity output.
		if (auto world = ShaderCompiler::CompileFromFile("assets/shaders/ui_world.rvshader"))
			s_Data->WorldShader = device.CreateShader(*world);
		else
			RV_CORE_ERROR("UIRenderer: failed to compile assets/shaders/ui_world.rvshader");
		s_Data->Ready = s_Data->Shader != nullptr;

		{
			std::vector<uint32_t> indices(kMaxIndices);
			uint32_t offset = 0;
			for (uint32_t i = 0; i < kMaxIndices; i += 6)
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
			desc.DebugName = "UIRenderer.indices";
			s_Data->IndexBuffer = device.CreateBuffer(desc);
			s_Data->IndexBuffer->Upload(indices.data(), desc.Size);
		}

		{
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = Format::R8G8B8A8_UNORM;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = "UIRenderer.white";
			s_Data->WhiteTexture = device.CreateTexture(desc);
			const uint32_t white = 0xffffffff;
			s_Data->WhiteTexture->Upload(&white, sizeof(white));
		}

		SamplerDesc sampler;
		// Linear, and clamped. A glyph sits against its neighbours in the atlas
		// with two pixels of padding, and wrapping would let the right edge of
		// one letter bleed into the left edge of another.
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.MaxLod = 0.0f;
		s_Data->Sampler = device.CreateSampler(sampler);

		s_Data->Batches.resize(device.GetFramesInFlight());
		s_Data->Vertices.resize(kMaxVertices);
		s_Data->TextureSlots.assign(kMaxTextureSlots, nullptr);
		s_Data->TextureSlots[0] = s_Data->WhiteTexture;

		if (s_Data->Ready)
			RV_CORE_INFO("UIRenderer ready ({0} quads per batch)", kMaxQuads);
		else
			RV_CORE_ERROR("UIRenderer unavailable; no game UI will draw");
	}

	void UIRenderer::Shutdown()
	{
		s_Data.reset();
	}

	bool UIRenderer::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void UIRenderer::SetTargetFormats(Format color, Format depth)
	{
		(void)depth;   // the pass runs without one; see EnsurePipeline

		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->PipelineDirty = true;
	}

	void UIRenderer::BeginFrame()
	{
		if (!s_Data)
			return;

		s_Data->BatchCursor = 0;
		s_Data->DrawCalls = 0;
		s_Data->QuadsThisFrame = 0;
	}

	void UIRenderer::Begin(uint32_t width, uint32_t height)
	{
		if (!s_Data || !s_Data->Ready)
			return;

		s_Data->Projection = BuildProjection(width, height);
		s_Data->QuadCount = 0;
		s_Data->NextTextureSlot = 1;
		s_Data->InLayer = true;
	}

	void UIRenderer::End()
	{
		if (!s_Data || !s_Data->InLayer)
			return;

		Flush();
		s_Data->InLayer = false;
		s_Data->WorldLayer = false;
	}

	void UIRenderer::DrawRect(const UIRect& rect, const Vec4& color)
	{
		if (!s_Data || !s_Data->Ready || color.w <= 0.0f)
			return;

		PushQuad(rect, color, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, kModePlain, 0.0f);
	}

	void UIRenderer::DrawImage(const UIRect& rect, const Ref<RHITexture>& texture,
							   const Vec4& tint, const UIRect& uv)
	{
		if (!s_Data || !s_Data->Ready || tint.w <= 0.0f)
			return;

		PushQuad(rect, tint, texture, uv.X, uv.Y, uv.Right(), uv.Bottom(),
				 kModePlain, 0.0f);
	}

	void UIRenderer::DrawText(const std::string& text, const Font& font,
							  const Ref<RHITexture>& atlas, const Vec2& position,
							  const UI::TextStyle& style, const Vec4& color)
	{
		if (!s_Data || !s_Data->Ready || !atlas || color.w <= 0.0f || style.Size <= 0.0f)
			return;

		// Layout is somebody else's job, and on purpose: this is the same call
		// world-space text will make, with a different matrix afterwards.
		const UI::TextLayout layout = UI::Build(text, font, style);

		for (const UI::PlacedGlyph& glyph : layout.Glyphs)
		{
			const UIRect rect{ position.x + glyph.X, position.y + glyph.Y,
							   glyph.Width, glyph.Height };
			PushQuad(rect, color, atlas, glyph.U0, glyph.V0, glyph.U1, glyph.V1,
					 kModeField, font.PxRange);
		}
	}

	void UIRenderer::DrawText(const std::string& text, const Font& font,
							  const Ref<RHITexture>& atlas, const Vec2& position,
							  float size, const Vec4& color)
	{
		UI::TextStyle style;
		style.Size = size;
		DrawText(text, font, atlas, position, style, color);
	}

	void UIRenderer::BeginWorld(const Mat4& viewProjection)
	{
		if (!s_Data || !s_Data->Ready)
			return;

		s_Data->Projection = viewProjection;
		s_Data->QuadCount = 0;
		s_Data->NextTextureSlot = 1;
		s_Data->InLayer = true;
		s_Data->WorldLayer = true;
	}

	void UIRenderer::SetWorldTargetFormats(Format color, Format depth, uint32_t samples,
										   Format velocity)
	{
		if (!s_Data)
			return;

		// A format change invalidates every cached pipeline. A *sample count*
		// change does not -- it selects among them, which is the whole point
		// of the cache.
		if (s_Data->WorldColor != color || s_Data->WorldDepth != depth ||
			s_Data->WorldVelocity != velocity)
		{
			s_Data->WorldColor = color;
			s_Data->WorldDepth = depth;
			s_Data->WorldVelocity = velocity;
			s_Data->WorldPipelines.clear();
			s_Data->WorldPipelineDirty = true;
		}

		if (s_Data->WorldSamples != samples)
		{
			s_Data->WorldSamples = samples;
			s_Data->WorldPipelineDirty = true;
		}
	}

	void UIRenderer::DrawWorldText(const std::string& text, const Font& font,
								   const Ref<RHITexture>& atlas, const Vec3& origin,
								   const Vec3& right, const Vec3& up,
								   const UI::TextStyle& style, const Vec4& color)
	{
		if (!s_Data || !s_Data->Ready || !atlas || color.w <= 0.0f || style.Size <= 0.0f)
			return;

		// **The same call the screen layer makes.** Layout is in its own units
		// and knows nothing about where the text will end up, which is what
		// makes this three lines rather than a second layout engine.
		const UI::TextLayout layout = UI::Build(text, font, style);

		for (const UI::PlacedGlyph& glyph : layout.Glyphs)
		{
			// Layout's y runs *down* the page, and `up` runs up the world, so
			// the vertical term is subtracted. Getting this backwards renders
			// every string upside down and mirrored about its own baseline --
			// which reads as a font problem and is not one.
			const Vec3 topLeft = origin + right * glyph.X - up * glyph.Y;
			const Vec3 dx = right * glyph.Width;
			const Vec3 dy = up * glyph.Height;

			const Vec3 corners[4] = {
				topLeft,
				topLeft + dx,
				topLeft + dx - dy,
				topLeft - dy,
			};

			PushQuadCorners(corners, color, atlas, glyph.U0, glyph.V0, glyph.U1, glyph.V1,
							kModeField, font.PxRange);
		}
	}

	void UIRenderer::DrawWorldSprite(const Vec3& center, const Vec3& right, const Vec3& up,
									 const Ref<RHITexture>& texture, const Vec4& tint,
									 const UIRect& uv)
	{
		if (!s_Data || !s_Data->Ready || !texture || tint.w <= 0.0f)
			return;

		// Wound the way the glyph quads are -- top left, top right, bottom
		// right, bottom left -- because both go through PushQuadCorners, and
		// the V coordinates below assume that order. `up` is subtracted for
		// the lower corners for the same reason it is there: texture V runs
		// down the image and `up` runs up the world.
		const Vec3 corners[4] = {
			center - right + up,
			center + right + up,
			center + right - up,
			center - right - up,
		};

		PushQuadCorners(corners, tint, texture, uv.X, uv.Y, uv.Right(), uv.Bottom(),
						kModePlain, 0.0f);
	}

	uint32_t UIRenderer::GetDrawCallCount()
	{
		return s_Data ? s_Data->DrawCalls : 0;
	}

	uint32_t UIRenderer::GetQuadCount()
	{
		return s_Data ? s_Data->QuadsThisFrame : 0;
	}
}
