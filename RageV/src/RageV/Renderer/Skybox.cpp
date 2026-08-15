#include <rvpch.h>
#include "Skybox.h"
#include "Renderer.h"
#include "TextureLoader.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		struct SkyParams
		{
			Mat4 InvViewRotationProjection{ 1.0f };
			Vec4 Horizon{ 0.0f };   // rgb, a = intensity
			Vec4 Zenith{ 0.0f };    // rgb, a = mode
			Vec4 Ground{ 0.0f };
		};

		// 112 bytes, inside the 128 every implementation guarantees. Worth
		// keeping in mind before adding a field: the next one does not fit.
		static_assert(sizeof(SkyParams) == 112, "Sky push constants must stay under 128 bytes");

		// What the sky needs for motion vectors, and the reason it is a
		// uniform buffer rather than four more push constants.
		//
		// The block above is 112 of the guaranteed 128 bytes. A previous
		// view-projection is 64 more and the jitter pair 16, so there is
		// nowhere to put them -- which is why the sky reported no motion for
		// as long as it did. Small and separate rather than moving the whole
		// of SkyParams: the existing fields are fine where they are, and the
		// smaller change is the one whose effect on everything else is
		// obviously nothing.
		struct SkyMotion
		{
			// Direction to clip, last frame, rotation only. A sky is
			// infinitely far away, so a direction is all it has -- and
			// projecting one with w = 0 gives the same answer whether or not
			// the matrix carries a translation.
			Mat4 PreviousViewRotationProjection{ 1.0f };

			// xy = this frame's sub-pixel offset in NDC, zw = last frame's,
			// exactly as the scene block carries them. Both projections above
			// have one folded in and the velocity is meant to be free of it.
			Vec4 Jitter{ 0.0f };
		};

		struct SkyboxData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader> Shader;
			Ref<RHIPipeline> Pipeline;
			Ref<RHISampler> Sampler;

			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
		// Sample count, which has to equal the target's. A pipeline whose
		// rasterizationSamples disagrees with the attachment it draws into is
		// undefined behaviour rather than an error, so it travels with the
		// formats and gets compared with them.
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
			bool PipelineDirty = true;

			// One set per draw, pooled per frame in flight, for the same reason
			// the post chain pools its own: a descriptor set that is already
			// bound must not be rewritten, and a frame can hold more than one
			// viewport.
			std::vector<std::vector<Ref<RHIResourceSet>>> Sets;
			uint32_t SetCursor = 0;

			// One motion buffer per set, allocated alongside them, because a
			// draw reads its uniform block when the GPU runs it rather than
			// when it was recorded -- two viewports sharing one buffer would
			// have the second overwrite what the first is about to use. The
			// same reason Renderer3D keeps a scene block per scene.
			std::vector<std::vector<Ref<RHIBuffer>>> MotionBuffers;

			// Direction-to-clip as the last Draw left it, and the jitter that
			// was folded into it.
			//
			// From the previous *Draw*, not the previous frame: a probe
			// capture draws the sky six times with six different cameras, and
			// differencing against one of those is the motion between two
			// faces of a cube rather than between two frames. The scene pass
			// is the last caller in a frame, so what the scene pass reads is
			// what the scene pass wrote -- the same property Renderer3D
			// relies on, and the same one that breaks if anything starts
			// drawing a sky after the scene.
			Mat4 PreviousDirectionToClip{ 1.0f };
			Vec2 PreviousJitter{ 0.0f, 0.0f };

			// The gradient, baked into a cube so it can be reflected. Rebuilt
			// only when one of the three colours changes, which in the editor
			// is while a colour picker is open and never otherwise.
			Ref<RHITexture> GradientCube;
			Ref<RHITexture> GradientIrradiance;
			Vec3 GradientHorizon{ -1.0f };
			Vec3 GradientZenith{ -1.0f };
			Vec3 GradientGround{ -1.0f };

			bool Ready = false;
		};

		// Small on purpose. This is three colours and a horizon: a reflection
		// of it has nothing in it that 32 texels a side cannot say, and the
		// cost of being wrong about that is a slightly soft reflection of a
		// gradient.
		constexpr uint32_t kGradientCubeSize = 32;

		// Shared with sky.rvshader. Both have to agree, or a mirrored surface
		// shows a different sky from the one behind it.
		Vec3 GradientAt(const Vec3& direction, const SceneEnvironment& environment)
		{
			const float height = Math::Clamp(std::fabs(direction.y), 0.0f, 1.0f);
			const float t = std::pow(height, 0.45f);

			return direction.y >= 0.0f
				 ? Math::Mix(environment.SkyHorizon, environment.SkyZenith, t)
				 : Math::Mix(environment.SkyHorizon, environment.SkyGround, t);
		}

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
		s_Data->MotionBuffers.resize(device.GetFramesInFlight());
		s_Data->Ready = s_Data->Shader != nullptr;

		if (s_Data->Ready)
			RV_CORE_INFO("Skybox ready (gradient, cubemap)");
		else
			RV_CORE_ERROR("Skybox unavailable; scenes will show the clear colour");
	}

	void Skybox::Shutdown()
	{
		s_Data.reset();
	}

	void Skybox::SetTargetFormats(Format color, Format depth, uint32_t samples,
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

	void Skybox::BeginFrame()
	{
		if (s_Data)
			s_Data->SetCursor = 0;
	}

	bool Skybox::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	Ref<RHITexture> Skybox::ResolveEnvironment(const SceneEnvironment& environment,
											   const Ref<RHITexture>& cubemap)
	{
		if (!s_Data || !s_Data->Device)
			return nullptr;

		if (environment.Sky == SkyType::Cubemap && cubemap)
			return cubemap;

		// A flat background reflects nothing, which is the honest answer: there
		// is no sky there to reflect.
		if (environment.Sky == SkyType::Color)
			return TextureLoader::BlackCube(*s_Data->Device);

		const bool stale = !s_Data->GradientCube ||
						   s_Data->GradientHorizon != environment.SkyHorizon ||
						   s_Data->GradientZenith != environment.SkyZenith ||
						   s_Data->GradientGround != environment.SkyGround;

		if (stale)
		{
			CubeFaces faces;
			faces.Size = kGradientCubeSize;
			faces.Pixels.resize((size_t)CubeFaces::kFaceCount * kGradientCubeSize * kGradientCubeSize * 4);

			const float inverse = 1.0f / (float)kGradientCubeSize;

			for (uint32_t face = 0; face < CubeFaces::kFaceCount; face++)
			{
				float* output = faces.Face(face);
				for (uint32_t y = 0; y < kGradientCubeSize; y++)
				{
					for (uint32_t x = 0; x < kGradientCubeSize; x++)
					{
						const Vec3 direction =
							CubeFaceDirection(face, ((float)x + 0.5f) * inverse,
													((float)y + 0.5f) * inverse);
						const Vec3 colour = GradientAt(direction, environment);

						float* texel = output + ((size_t)y * kGradientCubeSize + x) * 4;
						texel[0] = colour.r;
						texel[1] = colour.g;
						texel[2] = colour.b;
						texel[3] = 1.0f;
					}
				}
			}

			s_Data->GradientCube = TextureLoader::CreateCube(*s_Data->Device, faces, "sky.gradient");

			// Convolved from the same 32-pixel faces. A gradient has nothing in
			// it above the lowest frequencies, so a coarse source is not a
			// compromise here -- it is the whole signal.
			const CubeFaces irradiance = IrradianceFromCube(faces, 16);
			s_Data->GradientIrradiance =
				TextureLoader::CreateCube(*s_Data->Device, irradiance, "sky.gradient.irradiance");

			s_Data->GradientHorizon = environment.SkyHorizon;
			s_Data->GradientZenith = environment.SkyZenith;
			s_Data->GradientGround = environment.SkyGround;
		}

		return s_Data->GradientCube ? s_Data->GradientCube
									: TextureLoader::BlackCube(*s_Data->Device);
	}

	Ref<RHITexture> Skybox::ResolveIrradiance(const SceneEnvironment& environment,
											  const Ref<RHITexture>& cubemap,
											  const Ref<RHITexture>& irradiance)
	{
		if (!s_Data || !s_Data->Device)
			return nullptr;

		if (environment.Sky == SkyType::Cubemap && irradiance)
			return irradiance;

		// A flat background contributes no light. The ambient colour is still
		// added on top of this, so a scene with no sky is not black.
		if (environment.Sky == SkyType::Color)
			return TextureLoader::BlackCube(*s_Data->Device);

		// A cubemap sky that has its cube but not its irradiance. Draw is
		// putting that cube on screen, so falling through to the gradient
		// below would light the scene from SkyHorizon / SkyZenith / SkyGround
		// -- fields a scene using an environment map has no reason to have set
		// to anything meaningful. The result was ambient light of a plausible
		// colour with no relationship to the visible sky, and nothing said so.
		//
		// TextureLoader now fails a cube whose irradiance will not build, so
		// this should be unreachable; it is handled rather than asserted
		// because "unreachable" has been wrong here before.
		if (environment.Sky == SkyType::Cubemap && cubemap)
		{
			RV_CORE_WARN("The sky cube has no irradiance, so the scene has no diffuse "
						 "ambient from it. The gradient's would be the wrong sky.");
			return TextureLoader::BlackCube(*s_Data->Device);
		}

		// Builds the gradient cube if it is stale, which also builds this.
		ResolveEnvironment(environment, nullptr);

		return s_Data->GradientIrradiance ? s_Data->GradientIrradiance
										  : TextureLoader::BlackCube(*s_Data->Device);
	}

	Mat4 Skybox::BuildDirectionMatrix(const Mat4& projection,
										   const Mat4& cameraTransform,
										   float rotation)
	{
		// Translation dropped: a sky is infinitely far away, so moving the
		// camera must not move it. Keeping only the rotation of the view matrix
		// is what expresses that.
		const Mat4 view = Mat4(Mat3(Math::Inverse(cameraTransform)));

		// Applied to the result rather than to the view, so it turns the sky
		// and not the camera. The two differ by a sign, and confusing them is
		// the reason this function exists to be tested.
		const Mat4 spin = Math::Rotate(Mat4(1.0f), rotation, Vec3(0.0f, 1.0f, 0.0f));

		return spin * Math::Inverse(projection * view);
	}

	namespace
	{
		// The other direction: a world direction to clip space.
		//
		// The exact inverse of BuildDirectionMatrix, built forwards rather
		// than inverted, because inverting a matrix to undo an inversion
		// accumulates error for no reason. Undoing the spin rather than
		// applying it is the sign that makes these two a pair -- see the note
		// in BuildDirectionMatrix about which of the sky and the camera each
		// rotation belongs to.
		Mat4 BuildClipMatrix(const Mat4& projection, const Mat4& cameraTransform,
							 float rotation)
		{
			const Mat4 view = Mat4(Mat3(Math::Inverse(cameraTransform)));
			const Mat4 unspin = Math::Rotate(Mat4(1.0f), -rotation, Vec3(0.0f, 1.0f, 0.0f));

			return (projection * view) * unspin;
		}
	}

	void Skybox::Draw(const Camera& camera, const Mat4& cameraTransform,
					  const SceneEnvironment& environment, const Ref<RHITexture>& cubemap,
					  const Vec2& jitter)
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
			desc.Samples = s_Data->TargetSamples;
			if (s_Data->TargetVelocity != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetVelocity);
			if (s_Data->TargetNormal != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetNormal);
			desc.DepthFormat = s_Data->TargetDepth;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->PipelineDirty = false;
		}

		if (!s_Data->Pipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& sets = s_Data->Sets[frame];
		auto& buffers = s_Data->MotionBuffers[frame];

		while (s_Data->SetCursor >= sets.size())
			sets.push_back(s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0));
		while (s_Data->SetCursor >= buffers.size())
		{
			BufferDesc desc;
			desc.Size = sizeof(SkyMotion);
			desc.Usage = BufferUsage::Uniform;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = "Skybox.motion";
			buffers.push_back(s_Data->Device->CreateBuffer(desc));
		}

		const uint32_t slot = s_Data->SetCursor++;

		Ref<RHIResourceSet>& set = sets[slot];
		if (!set)
			set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

		const Ref<RHIBuffer>& motionBuffer = buffers[slot];
		if (!motionBuffer)
			return;

		// Always written, even in gradient mode: the shader declares the cube
		// sampler unconditionally, and a binding the layout has but the set does
		// not is a validation error, not a harmless omission.
		const Ref<RHITexture> bound = mode == SkyType::Cubemap
									? cubemap
									: TextureLoader::BlackCube(*s_Data->Device);
		if (!bound)
			return;

		// Where the sky was last time this ran, before this frame's overwrites
		// it. The sky has no motion of its own -- it is infinitely far away --
		// but it sweeps across the screen when the camera turns, and a
		// temporal filter told otherwise reprojects it onto itself and smears
		// it. ENGINE-NOTES 7r.
		SkyMotion motion;
		motion.PreviousViewRotationProjection = s_Data->PreviousDirectionToClip;
		motion.Jitter = Vec4(jitter.x, jitter.y,
							 s_Data->PreviousJitter.x, s_Data->PreviousJitter.y);

		s_Data->PreviousDirectionToClip =
			BuildClipMatrix(camera.GetProjection(), cameraTransform, environment.SkyRotation);
		s_Data->PreviousJitter = jitter;

		motionBuffer->Upload(&motion, sizeof(motion));

		set->SetTexture(0, bound, s_Data->Sampler);
		set->SetUniformBuffer(1, motionBuffer, 0, sizeof(SkyMotion));
		set->Commit();

		SkyParams params;
		params.InvViewRotationProjection =
			BuildDirectionMatrix(camera.GetProjection(), cameraTransform, environment.SkyRotation);
		params.Horizon = Vec4(environment.SkyHorizon, environment.SkyIntensity);
		params.Zenith = Vec4(environment.SkyZenith, (float)mode);
		params.Ground = Vec4(environment.SkyGround, 0.0f);

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->BindResourceSet(0, set);
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd->Draw(3);
	}
}
