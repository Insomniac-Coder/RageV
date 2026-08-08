#include <rvpch.h>
#include "Skybox.h"
#include "Renderer.h"
#include "TextureLoader.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include <glm/gtc/matrix_transform.hpp>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		struct SkyParams
		{
			glm::mat4 InvViewRotationProjection{ 1.0f };
			glm::vec4 Horizon{ 0.0f };   // rgb, a = intensity
			glm::vec4 Zenith{ 0.0f };    // rgb, a = mode
			glm::vec4 Ground{ 0.0f };
		};

		// 112 bytes, inside the 128 every implementation guarantees. Worth
		// keeping in mind before adding a field: the next one does not fit.
		static_assert(sizeof(SkyParams) == 112, "Sky push constants must stay under 128 bytes");

		struct SkyboxData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader> Shader;
			Ref<RHIPipeline> Pipeline;
			Ref<RHISampler> Sampler;

			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
			bool PipelineDirty = true;

			// One set per draw, pooled per frame in flight, for the same reason
			// the post chain pools its own: a descriptor set that is already
			// bound must not be rewritten, and a frame can hold more than one
			// viewport.
			std::vector<std::vector<Ref<RHIResourceSet>>> Sets;
			uint32_t SetCursor = 0;

			bool Ready = false;
		};

		std::unique_ptr<SkyboxData> s_Data;
	}

	void Skybox::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<SkyboxData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/sky.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("Skybox: failed to compile assets/shaders/sky.rvshader");
			return;
		}

		s_Data->Shader = device.CreateShader(*compiled);

		SamplerDesc sampler;
		// Clamped on all three axes. Cube sampling never actually leaves the
		// cube, but the wrap mode still decides what happens along a face edge
		// on hardware without seamless filtering.
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.WrapW = WrapMode::ClampToEdge;
		s_Data->Sampler = device.CreateSampler(sampler);

		s_Data->Sets.resize(device.GetFramesInFlight());
		s_Data->Ready = s_Data->Shader != nullptr;

		RV_CORE_INFO("Skybox ready (gradient, cubemap)");
	}

	void Skybox::Shutdown()
	{
		s_Data.reset();
	}

	void Skybox::SetTargetFormats(Format color, Format depth)
	{
		if (!s_Data)
			return;

		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void Skybox::BeginFrame()
	{
		if (s_Data)
			s_Data->SetCursor = 0;
	}

	bool Skybox::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	glm::mat4 Skybox::BuildDirectionMatrix(const glm::mat4& projection,
										   const glm::mat4& cameraTransform,
										   float rotation)
	{
		// Translation dropped: a sky is infinitely far away, so moving the
		// camera must not move it. Keeping only the rotation of the view matrix
		// is what expresses that.
		const glm::mat4 view = glm::mat4(glm::mat3(glm::inverse(cameraTransform)));

		// Applied to the result rather than to the view, so it turns the sky
		// and not the camera. The two differ by a sign, and confusing them is
		// the reason this function exists to be tested.
		const glm::mat4 spin = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 1.0f, 0.0f));

		return spin * glm::inverse(projection * view);
	}

	void Skybox::Draw(const Camera& camera, const glm::mat4& cameraTransform,
					  const SceneEnvironment& environment, const Ref<RHITexture>& cubemap)
	{
		if (!s_Data || !s_Data->Ready || environment.Sky == SkyType::Color)
			return;

		// A cubemap sky with no cubemap would draw black over the whole frame,
		// which looks like a bug in the renderer rather than a missing asset.
		// Falling back to the gradient keeps the scene readable while the
		// handle is unset or the file is missing.
		SkyType mode = environment.Sky;
		if (mode == SkyType::Cubemap && !cubemap)
			mode = SkyType::Gradient;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		if (s_Data->PipelineDirty || !s_Data->Pipeline)
		{
			GraphicsPipelineDesc desc;
			desc.Name = "Skybox";
			desc.Shader = s_Data->Shader;
			desc.Topology = PrimitiveTopology::TriangleList;
			desc.Rasterizer.Cull = CullMode::None;

			// Tested but not written. The sky must lose to every surface in the
			// scene, and it must not put its own far-plane depth into the buffer
			// where a later pass would read it as geometry.
			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = false;
			// LessOrEqual, not Less: the sky sits exactly on the cleared value,
			// so a strict comparison rejects every pixel and the sky never
			// appears at all.
			desc.DepthStencil.DepthCompare = CompareOp::LessOrEqual;

			desc.ColorFormats = { s_Data->TargetColor };
			desc.DepthFormat = s_Data->TargetDepth;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->PipelineDirty = false;
		}

		if (!s_Data->Pipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& sets = s_Data->Sets[frame];

		if (s_Data->SetCursor >= sets.size())
			sets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0));

		Ref<RHIResourceSet>& set = sets[s_Data->SetCursor++];
		if (!set)
			set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

		// Always written, even in gradient mode: the shader declares the cube
		// sampler unconditionally, and a binding the layout has but the set does
		// not is a validation error, not a harmless omission.
		const Ref<RHITexture> bound = mode == SkyType::Cubemap
									? cubemap
									: TextureLoader::BlackCube(*s_Data->Device);
		if (!bound)
			return;

		set->SetTexture(0, bound, s_Data->Sampler);
		set->Commit();

		SkyParams params;
		params.InvViewRotationProjection =
			BuildDirectionMatrix(camera.GetProjection(), cameraTransform, environment.SkyRotation);
		params.Horizon = glm::vec4(environment.SkyHorizon, environment.SkyIntensity);
		params.Zenith = glm::vec4(environment.SkyZenith, (float)mode);
		params.Ground = glm::vec4(environment.SkyGround, 0.0f);

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, set);
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd->Draw(3);
	}
}
