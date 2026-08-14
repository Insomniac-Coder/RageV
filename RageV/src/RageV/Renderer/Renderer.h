#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

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

		// How many frames have been drawn since the main loop started.
		//
		// **Not since the process started**, and the difference is the whole
		// reason this is here. Loading draws frames too, and how many depends
		// on whether the import cache was warm -- so anything indexed by a
		// process-wide count would land on a different value between two runs
		// of the same build. The temporal jitter is indexed by this, and a
		// jitter that moves between runs makes every screenshot check in
		// tools/scripts irreproducible with a failure that looks like noise.
		// ENGINE-NOTES 7r.
		static uint64_t GetFrameCount();

		// Called once when the main loop begins, for the reason above. The
		// same idea as the fixed step's Reset beside it: this is where the
		// clock starts.
		static void ResetFrameCount();

		// The scene camera's sub-pixel offset for this frame, in NDC.
		//
		// Set around the passes that draw into the scene target and cleared
		// after them, so it is zero everywhere else -- which is what keeps it
		// out of the two places it must never reach: a shadow cascade, which
		// is reused across frames and would shimmer along every edge, and a
		// reflection probe, which assembles one cube out of six frames and
		// would assemble six differently-offset faces.
		//
		// Zero for every mode but TAA.
		static void SetJitter(const Vec2& ndcOffset);
		static Vec2 GetJitter();

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
		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined);

		// What the last SetTargetFormats said. Anything that renders the scene
		// into a target of its own -- a reflection probe face -- has to match
		// it, because the pipelines are built once for one count.
		static uint32_t GetTargetSamples();
	};
}
