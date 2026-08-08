#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Owns the frame lifecycle and hands the active command list to whoever
	// needs it. Layers have no command-list parameter, so this plays the same
	// role the old RenderCommand static did -- one well-known place to reach
	// the current recording context -- but scoped to a frame.
	class Renderer
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		static void BeginFrame(RHI::RHICommandList* commandList);
		static void EndFrame();

		// Null outside a frame, and between BeginFrame returning nullptr and
		// the next successful frame.
		static RHI::RHICommandList* GetCommandList();
		static RHI::RHIDevice& GetDevice();
		static bool HasDevice();

		static void OnWindowResize(unsigned int width, unsigned int height);

		// Applied to both the 2D and 3D renderers, since a wireframe view that
		// only covered half the scene would be misleading.
		static void SetWireframe(bool enabled);
		static bool IsWireframe();

		// Formats of whatever both renderers are currently drawing into.
		static void SetTargetFormats(RHI::Format color, RHI::Format depth);
	};
}
