#pragma once
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/Environment.h"
#include <functional>

namespace RageV
{
	// The standard frame, built once and used by both applications.
	//
	// The editor and the runtime differ in where the image ends up -- a panel's
	// texture or the swapchain -- and in nothing else. Writing the chain twice
	// would mean two places to add a pass to, and they would drift within a
	// week of shadows landing.
	struct FrameDesc
	{
		// Where the finished image goes. The backbuffer for a game, an imported
		// target for a viewport panel.
		RGResource Output = kRGInvalid;

		uint32_t Width = 0;
		uint32_t Height = 0;

		// Draws the scene into the HDR target. Everything after that is the
		// same regardless of what drew it.
		std::function<void(RGPassContext&)> DrawScene;

		// Drawn into the HDR target after the scene, with the depth buffer
		// still attached -- collider wireframes need to be occluded by the
		// geometry they sit inside. Optional.
		std::function<void(RGPassContext&)> DrawOverlay;

		SceneEnvironment Environment;

		Vec4 ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f };

		// The format the output expects. The chain's last pass writes it.
		RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
	};

	// Adds scene, bloom, tonemap and anti-aliasing passes to the graph.
	//
	// Nothing is executed here -- the caller compiles and executes, so a failed
	// compile is reported where the frame is owned.
	void BuildFrame(RenderGraph& graph, const FrameDesc& desc);
}
