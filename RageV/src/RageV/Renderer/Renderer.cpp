#include <rvpch.h>
#include "Renderer.h"
#include "Renderer2D.h"
#include "Renderer3D.h"
#include "DebugRenderer.h"
#include "ParticleRenderer.h"
#include "RageV/Particles/GpuParticles.h"
#include "Skybox.h"
#include "ViewportGrid.h"
#include "UIRenderer.h"
#include "ShadowMap.h"
#include "EnvironmentIBL.h"
#include "ProbeArray.h"
#include "PostProcess.h"
#include "AutoExposure.h"

namespace RageV
{
	namespace
	{
		RHI::RHIDevice*      s_Device = nullptr;
		RHI::RHICommandList* s_CommandList = nullptr;

		// Frames drawn since the main loop started, and the sub-pixel offset
		// the current one is being drawn with. See the header for why the
		// count is not process-wide.
		uint64_t s_FrameCount = 0;
		Vec2     s_Jitter{ 0.0f, 0.0f };

		// Whose history the scene being drawn will be reprojected into. Null
		// outside a scene pass, which is most of the frame.
		CameraMotion* s_CameraMotion = nullptr;

		// Last frame's reflection trace for the scene being drawn. Null
		// outside a scene pass, like the two above.
		const Renderer::ScreenReflections* s_ScreenReflections = nullptr;
	}

	void Renderer::Init(RHI::RHIDevice& device)
	{
		s_Device = &device;
		Renderer2D::Init(device);
		Renderer3D::Init(device);
		DebugRenderer::Init(device);
		ParticleRenderer::Init(device);
		// After the renderer: an emitter that cannot simulate on the GPU still
		// draws, and Init logs which of the two it got.
		Particles::Gpu::Init(device);
		Skybox::Init(device);
		ViewportGrid::Init(device);
		UIRenderer::Init(device);
		ShadowMap::Init(device);
		EnvironmentIBL::Init(device);
		// After it: the arrays are filled by its two convolutions, and Begin
		// asks it how many roughness levels a face size can carry.
		ProbeArray::Init(device);
		PostProcess::Init(device);
		AutoExposure::Init(device);
	}

	void Renderer::Shutdown()
	{
		AutoExposure::Shutdown();
		PostProcess::Shutdown();
		ProbeArray::Shutdown();
		EnvironmentIBL::Shutdown();
		ShadowMap::Shutdown();
		UIRenderer::Shutdown();
		ViewportGrid::Shutdown();
		Skybox::Shutdown();
		Particles::Gpu::Shutdown();
		ParticleRenderer::Shutdown();
		DebugRenderer::Shutdown();
		Renderer3D::Shutdown();
		Renderer2D::Shutdown();
		s_CommandList = nullptr;
		s_Device = nullptr;
	}

	void Renderer::BeginFrame(RHI::RHICommandList* commandList)
	{
		s_CommandList = commandList;
		s_FrameCount++;

		// Belt and braces: nothing outside a scene pass should ever read a
		// non-zero jitter, and a frame that threw one away half-drawn would
		// otherwise hand it to the next one's probe captures.
		s_Jitter = Vec2(0.0f, 0.0f);
		s_CameraMotion = nullptr;
		s_ScreenReflections = nullptr;

		// Frees the per-frame buffer pools. Anything that draws a scene more
		// than once per frame depends on this having run first.
		Renderer2D::BeginFrame();
		Renderer3D::BeginFrame();
		DebugRenderer::BeginFrame();
		ParticleRenderer::BeginFrame();
		Skybox::BeginFrame();
		UIRenderer::BeginFrame();
		// Cascades are rendered per frame, if at all. Anything left from the
		// last one describes a camera that has since moved.
		ShadowMap::Invalidate();
		EnvironmentIBL::BeginFrame();
		PostProcess::BeginFrame();
		AutoExposure::BeginFrame();
	}

	void Renderer::EndFrame()
	{
		s_CommandList = nullptr;
	}

	uint64_t Renderer::GetFrameCount()
	{
		return s_FrameCount;
	}

	void Renderer::ResetFrameCount()
	{
		s_FrameCount = 0;
	}

	void Renderer::SetJitter(const Vec2& ndcOffset)
	{
		s_Jitter = ndcOffset;
	}

	Vec2 Renderer::GetJitter()
	{
		return s_Jitter;
	}

	void Renderer::SetCameraMotion(CameraMotion* motion)
	{
		s_CameraMotion = motion;
	}

	CameraMotion* Renderer::GetCameraMotion()
	{
		return s_CameraMotion;
	}

	void Renderer::SetScreenReflections(const ScreenReflections* reflections)
	{
		s_ScreenReflections = reflections;
	}

	const Renderer::ScreenReflections* Renderer::GetScreenReflections()
	{
		return s_ScreenReflections;
	}

	RHI::RHICommandList* Renderer::GetCommandList()
	{
		return s_CommandList;
	}

	RHI::RHIDevice& Renderer::GetDevice()
	{
		RV_CORE_ASSERT(s_Device, "Renderer has no device");
		return *s_Device;
	}

	bool Renderer::HasDevice()
	{
		return s_Device != nullptr;
	}

	void Renderer::OnWindowResize(unsigned int width, unsigned int height)
	{
		if (s_Device)
			s_Device->OnResize(width, height);
	}

	void Renderer::SetWireframe(bool enabled)
	{
		Renderer2D::SetWireframe(enabled);
		Renderer3D::SetWireframe(enabled);
	}

	bool Renderer::IsWireframe()
	{
		return Renderer2D::IsWireframe();
	}

	namespace
	{
		uint32_t s_TargetSamples = 1;
	}

	uint32_t Renderer::GetTargetSamples()
	{
		return s_TargetSamples;
	}

	void Renderer::SetTargetFormats(RHI::Format color, RHI::Format depth, uint32_t samples,
									RHI::Format velocity, RHI::Format normal)
	{
		s_TargetSamples = samples;

		Renderer2D::SetTargetFormats(color, depth, samples, velocity, normal);
		Renderer3D::SetTargetFormats(color, depth, samples, velocity, normal);
		DebugRenderer::SetTargetFormats(color, depth, samples, velocity, normal);
		ParticleRenderer::SetTargetFormats(color, depth, samples, velocity, normal);
		Skybox::SetTargetFormats(color, depth, samples, velocity, normal);
		ViewportGrid::SetTargetFormats(color, depth, samples, velocity, normal);
	}
}
