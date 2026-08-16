#pragma once

// Ray-traced shadows, stage 1 (ENGINE-NOTES 7am): the frame's top-level
// acceleration structure, built once per frame from every mesh the scene
// has, and handed to the lit pass so its fragment shader can trace a shadow
// ray toward the directional light instead of looking a cascade up.
//
// The renderer side of the RHI's acceleration structures, and nothing about
// it is a picture: it collects instances, builds a TLAS into the frame's
// slot, and answers "is there one this frame". On a device that cannot trace
// -- OpenGL always -- Init leaves it unavailable and every call is a no-op,
// which is how the shadow-map path stays exactly what it was.
//
// One structure per frame in flight, because the previous frame's fragment
// shaders may still be tracing into theirs; and one *empty* structure, built
// once with no instances, bound wherever the layout declares the binding and
// no scene was traced this frame -- a probe capture, a frame before the
// first RenderShadows -- so the binding is never left unwritten.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	class Mesh;

	class RayShadows
	{
	public:
		// The set 0 binding the lit shaders declare the structure at under
		// RV_RAY_SHADOWS, and the one Renderer3D writes.
		static constexpr uint32_t kBinding = 14;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Whether the device can trace at all. False on OpenGL, always.
		static bool IsAvailable();

		// Once per frame: forgets last frame's build, so a frame that traces
		// nothing (a probe capture) does not report a stale structure.
		static void BeginFrame();

		// Collect this frame's casters, then build. Build records into `cmd`
		// and must run outside a render pass -- Scene::RenderShadows, before
		// the graph, is where it is called from. Building twice in one frame
		// (the editor renders shadows for two cameras) is a no-op the second
		// time: the scene did not change between them.
		static void ClearInstances();
		static void AddInstance(const RHI::Ref<Mesh>& mesh, const Mat4& world);
		static void Build(RHI::RHICommandList& cmd);

		// True after Build ran this frame.
		static bool IsActive();
		// This frame's structure when active, otherwise the empty one --
		// never null on an available device, so a set can always be written.
		static const RHI::Ref<RHI::RHIAccelerationStructure>& GetStructure();

		static uint32_t GetInstanceCount();
	};
}
