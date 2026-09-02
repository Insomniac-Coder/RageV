#include <rvpch.h>
#include "LightGlow.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

namespace RageV
{
	using namespace RHI;

	namespace
	{
		// Mirrors GlowScene in light_glow.rvshader, std140.
		struct GlowUniforms
		{
			Mat4 ViewProjection{ 1.0f };
			Vec4 CameraRight{ 1.0f, 0.0f, 0.0f, 0.0f };
			Vec4 CameraUp{ 0.0f, 1.0f, 0.0f, 0.0f };
			Vec4 CameraPosition{ 0.0f, 0.0f, 0.0f, 0.0f };   // w: radians per pixel
			Vec4 Sizes{ 4.0f, 24.0f, 1.0f, 0.0f };           // glow px, flare px, intensity, flare share
			Vec4 Params{ 6.0f, 0.25f, 0.0f, 0.0f };          // rays, side glow
		};
		static_assert(sizeof(GlowUniforms) == 64 + 5 * 16, "Must match light_glow.rvshader");

		// **How much a fixture seen from outside its cone still glows.** A
		// street lamp's housing is shielded, but its lens is visible from the
		// side -- the reference photographs show the whole row as points from
		// the headland, which is far outside every lamp's 85-degree cone --
		// so the floor is a quarter rather than nothing.
		constexpr float kSideGlow = 0.25f;

		struct LightGlowData
		{
			RHIDevice* Device = nullptr;
			Ref<RHIShader> Shader;
			Ref<RHIPipeline> Pipeline;

			Format TargetColor = Format::B8G8R8A8_UNORM;
			Format TargetDepth = Format::D24_UNORM_S8_UINT;
			Format TargetVelocity = Format::Undefined;
			Format TargetNormal = Format::Undefined;
			Format TargetIndirect = Format::Undefined;
			uint32_t TargetSamples = 1;
			bool PipelineDirty = true;

			// One per frame in flight, and one per scene drawn in a frame:
			// the editor draws its viewport and the game view from two
			// cameras in one frame, and a uniform buffer rewritten between
			// the two records would hand the first draw the second camera.
			struct Batch
			{
				Ref<RHIBuffer> Uniforms;
				Ref<RHIResourceSet> Set;
			};
			std::vector<std::vector<Batch>> Batches;
			uint32_t BatchCursor = 0;
			uint32_t CursorFrame = ~0u;

			GlowUniforms Scene{};
			bool Orthographic = false;
			uint32_t Width = 0;
			uint32_t Height = 0;
			LightGlowSettings Settings;
		};

		std::unique_ptr<LightGlowData> s_Data;

		void EnsurePipeline()
		{
			if (!s_Data->PipelineDirty || !s_Data->Shader)
				return;

			GraphicsPipelineDesc desc;
			desc.Name = "LightGlow";
			desc.Shader = s_Data->Shader;
			desc.Topology = PrimitiveTopology::TriangleList;
			// A quad that always faces the viewer has no back to cull.
			desc.Rasterizer.Cull = CullMode::None;
			// Additive on every attachment: the colour sums into the scene,
			// and the other three are written as zero, which adds nothing.
			desc.Blend = BlendPreset::Additive;
			// Tested so geometry in front of a lamp hides its glow; not
			// written so a glow never occludes anything drawn after it.
			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = false;

			desc.ColorFormats = { s_Data->TargetColor };
			if (s_Data->TargetVelocity != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetVelocity);
			if (s_Data->TargetNormal != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetNormal);
			if (s_Data->TargetIndirect != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetIndirect);
			desc.DepthFormat = s_Data->TargetDepth;
			desc.Samples = s_Data->TargetSamples;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->PipelineDirty = false;

			// Sets belong to a pipeline layout, so they go with it.
			for (auto& frame : s_Data->Batches)
				frame.clear();
		}

		LightGlowData::Batch& AcquireBatch()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			if (frame != s_Data->CursorFrame)
			{
				s_Data->CursorFrame = frame;
				s_Data->BatchCursor = 0;
			}

			while (s_Data->Batches.size() <= frame)
				s_Data->Batches.push_back({});
			auto& batches = s_Data->Batches[frame];

			while (s_Data->BatchCursor >= batches.size())
			{
				LightGlowData::Batch batch;
				BufferDesc uniformDesc;
				uniformDesc.Size = sizeof(GlowUniforms);
				uniformDesc.Usage = BufferUsage::Uniform;
				uniformDesc.Memory = MemoryDomain::HostVisible;
				uniformDesc.DebugName = "LightGlow.scene." + std::to_string(frame) + "."
									  + std::to_string(batches.size());
				batch.Uniforms = s_Data->Device->CreateBuffer(uniformDesc);
				batches.push_back(std::move(batch));
			}

			LightGlowData::Batch& batch = batches[s_Data->BatchCursor++];
			if (!batch.Set)
				batch.Set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);
			return batch;
		}
	}

	void LightGlow::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<LightGlowData>();
		s_Data->Device = &device;

		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/light_glow.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("LightGlow: failed to compile assets/shaders/light_glow.rvshader; "
						  "lights will not glow");
			return;
		}
		s_Data->Shader = device.CreateShader(*compiled);
	}

	void LightGlow::Shutdown()
	{
		s_Data.reset();
	}

	void LightGlow::SetTargetFormats(Format color, Format depth, uint32_t samples,
									 Format velocity, Format normal, Format indirect)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth
			&& s_Data->TargetSamples == samples && s_Data->TargetVelocity == velocity
			&& s_Data->TargetNormal == normal && s_Data->TargetIndirect == indirect
			&& s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetDepth = depth;
		s_Data->TargetSamples = samples;
		s_Data->TargetVelocity = velocity;
		s_Data->TargetNormal = normal;
		s_Data->TargetIndirect = indirect;
		s_Data->PipelineDirty = true;
	}

	void LightGlow::BeginScene(const Mat4& viewProjection, const Mat4& cameraTransform,
							   const Mat4& projection)
	{
		if (!s_Data)
			return;

		s_Data->Scene.ViewProjection = viewProjection;
		s_Data->Scene.CameraRight = Vec4(Math::Normalize(Vec3(cameraTransform[0])), 0.0f);
		s_Data->Scene.CameraUp = Vec4(Math::Normalize(Vec3(cameraTransform[1])), 0.0f);
		s_Data->Scene.CameraPosition = Vec4(Vec3(cameraTransform[3]), 0.0f);

		// A perspective projection's [1][1] is 1/tan(fov/2), so the vertical
		// field of view is 2/[1][1] and a pixel subtends that over the
		// target's height. An orthographic one has no angle per pixel at all,
		// and nothing sensible to draw: its [3][3] is one.
		s_Data->Orthographic = projection[3][3] == 1.0f || projection[1][1] == 0.0f;
		const float fovTangentTimesTwo = s_Data->Orthographic ? 0.0f : 2.0f / projection[1][1];
		s_Data->Scene.CameraPosition.w =
			s_Data->Height > 0 ? fovTangentTimesTwo / (float)s_Data->Height : 0.0f;
	}

	void LightGlow::SetViewport(uint32_t width, uint32_t height)
	{
		if (!s_Data)
			return;
		s_Data->Width = width;
		s_Data->Height = height;
	}

	void LightGlow::SetSettings(const LightGlowSettings& settings)
	{
		if (!s_Data)
			return;
		s_Data->Settings = settings;
	}

	void LightGlow::Draw(RHICommandList& cmd, const Ref<RHIBuffer>& lights, uint32_t lightCount)
	{
		if (!s_Data || !s_Data->Shader || !lights || lightCount == 0)
			return;
		if (!s_Data->Settings.Enabled || s_Data->Width == 0 || s_Data->Height == 0
			|| s_Data->Orthographic)
			return;

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		GlowUniforms uniforms = s_Data->Scene;
		// The angle per pixel depends on the height BeginScene may not have
		// had yet; the projection's term was stored through it, so redo the
		// division against the viewport that is actually bound.
		uniforms.CameraPosition.w = uniforms.CameraPosition.w * (float)Math::Max(1u, 1u);
		uniforms.Sizes = Vec4(Math::Max(s_Data->Settings.GlowPixels, 1.0f),
							  Math::Max(s_Data->Settings.FlarePixels, 2.0f),
							  Math::Max(s_Data->Settings.Intensity, 0.0f),
							  Math::Clamp(s_Data->Settings.FlareShare, 0.0f, 1.0f));
		uniforms.Params = Vec4(Math::Max(std::floor(s_Data->Settings.FlareRays), 0.0f),
							   kSideGlow, 0.0f, 0.0f);

		LightGlowData::Batch& batch = AcquireBatch();
		batch.Uniforms->Upload(&uniforms, sizeof(uniforms));
		batch.Set->SetUniformBuffer(0, batch.Uniforms, 0, sizeof(GlowUniforms));
		batch.Set->SetStorageBuffer(1, lights, 0, (uint64_t)lightCount * 80u);
		batch.Set->Commit();

		cmd.BindPipeline(s_Data->Pipeline);
		cmd.BindResourceSet(0, batch.Set);
		cmd.Draw(6, lightCount);
	}
}
