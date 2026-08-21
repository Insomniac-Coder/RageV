#pragma once

// Frustum culling on the GPU, for the depth pass (roadmap 8.3).
//
// **What this replaces is a walk, not a draw call.** Instancing already
// collapses a scale scene's twenty thousand objects into five DrawIndexed
// calls, so there was never a draw-call problem to solve; what cost was the
// per-object CPU work that ran whether or not anything was visible. A frame
// opens a depth view per cascade, per casting spot light and per face of every
// point light's cube, and each one walked every object, tested it, multiplied a
// matrix for each survivor, sorted the survivors so runs of one mesh would be
// contiguous, and copied the result into a buffer to upload. Five or more times
// over the same objects, every frame. `scenes/scale_sky.rage` -- the same
// twenty thousand objects with the camera pointed at the sky, nothing visible,
// one draw, zero triangles -- still cost 8.26 ms a frame, of which the shadow
// maps were 3.01 ms.
//
// Here the CPU writes one table a frame and each view costs a dispatch. The
// count that used to be `end - start` on the CPU is read out of the buffer the
// compute pass wrote, which is what RHICommandList::DrawIndexedIndirect is
// for.
//
// The layout is what makes the sort unnecessary. Every object is assigned a
// *mesh slot* before it arrives, each slot owns a fixed range of the instance
// buffer long enough for every object that named it, and an atomic hands out
// places inside that range. Contiguity is a property of the layout rather than
// a result of sorting, and the range cannot overrun however many survive.
//
// Static meshes only. A skinned caster needs its bones, which are a per-frame
// CPU product, and there are a handful of them against tens of thousands of
// the other kind; terrain chunks pick a level of detail per camera and there
// are dozens. Both stay on the path they were on, in the same pass, drawn
// after the indirect draws.
//
// Vulkan and OpenGL both, unlike the ray-traced work: the pieces this needs --
// compute pipelines, storage buffers, indirect draws -- exist on both. A
// device without compute leaves it unavailable and the CPU path runs, which is
// also what happens when either shader fails to compile.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/RHI/RHICommandList.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Math/Math.h"

#include <vector>

namespace RageV
{
	class GpuCull
	{
	public:
		// One object, as the cull pass reads it. Ninety-six bytes: a model
		// matrix and the world-space box the frustum test needs.
		//
		// `Slot` sits in the padding a vec3 centre would have had anyway, and
		// the shader reads it back with floatBitsToUint -- these four bytes
		// are an index, not a number. The alternative is a second buffer of
		// one uint per object, which is another descriptor and another cache
		// line for something that fits here for free.
		struct Object
		{
			Mat4     Model;
			Vec3     Centre;
			uint32_t Slot = 0;
			Vec3     Extents;
			uint32_t Pad = 0;
		};
		static_assert(sizeof(Object) == 96, "Must match Object in cull_casters.rvshader");
		static_assert(offsetof(Object, Centre) == 64, "std430 puts the centre after the matrix");
		static_assert(offsetof(Object, Slot) == 76, "The slot is the centre vec4's w");
		static_assert(offsetof(Object, Extents) == 80, "The extents are the next vec4");

		// One mesh slot's indirect draw, plus the one thing an indirect draw
		// has no field for.
		//
		// The base cannot ride in the command's own FirstInstance because the
		// backends disagree about it -- Vulkan folds it into gl_InstanceIndex,
		// OpenGL does not fold it into gl_InstanceID -- so the draw pushes it
		// as a constant and reads it from the CPU's copy of this array. The
		// stride handed to DrawIndexedIndirect is therefore twenty-four rather
		// than the command's own twenty, which both backends accept.
		struct SlotCommand
		{
			RHI::DrawIndexedIndirectCommand Draw;
			uint32_t InstanceBase = 0;
		};
		static_assert(sizeof(SlotCommand) == 24, "Must match SlotCommand in cull_casters.rvshader");

		// What the draw side needs and the GPU does not: which mesh to bind
		// before a slot's indirect draw, and where the slot's instances start.
		struct Slot
		{
			RHI::Ref<Mesh> MeshRef;
			uint32_t InstanceBase = 0;
		};

		// One view's culled result: the arguments its draws read, and the
		// instances they read through them.
		struct View
		{
			RHI::Ref<RHI::RHIBuffer> Commands;
			RHI::Ref<RHI::RHIBuffer> Instances;
			uint32_t SlotCount = 0;

			bool IsValid() const { return Commands && Instances && SlotCount > 0; }
		};

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Turns the whole thing off without unbuilding it (`--gpu-cull=off`).
		// IsAvailable then answers no, no table is built, and every depth view
		// walks its casters -- which is the only way to compare the two paths
		// on one machine in one build.
		static void SetEnabled(bool enabled);

		// Whether a view can be culled at all: compute, both shaders, and the
		// buffers. False means every caller falls back to the CPU walk.
		static bool IsAvailable();

		// Once a frame, before any view. Rewinds the ring of per-view slots
		// and forgets last frame's table.
		static void BeginFrame();

		// The frame's object table and the per-slot draw template, uploaded.
		// `objects` is in any order -- the slot each names is what groups them
		// -- and `slots` must be as long as the highest slot named plus one,
		// with each entry's InstanceBase already set to the running total of
		// the ones before it. `owner` is whoever built it, so that two scenes
		// drawn in one frame cannot be handed each other's table.
		//
		// Returns false if the table could not be uploaded, in which case no
		// view will cull and the callers take the CPU path.
		static bool SetObjects(const void* owner, const std::vector<Object>& objects,
							   const std::vector<SlotCommand>& slots);

		// Whether `owner` has already given a usable table this frame.
		//
		// **A frame reaches the draw list from more than one place** -- the
		// shadow pass and the lit pass, and once per viewport on top -- and
		// each raises the dirty flag on the way in, so the list is rebuilt
		// several times a frame. Rebuilding the *table* with it would upload
		// ninety-six bytes an object each time, which at sixty thousand
		// objects costs more than the walks the table exists to remove. The
		// first build of a frame is the one that counts.
		static bool HasObjects(const void* owner);

		// Records one view's reset and cull dispatches, and the barriers that
		// make the result readable as indirect arguments and as vertex-stage
		// storage.
		//
		// **Must be called outside a render pass**: a dispatch is not
		// permitted inside one. The depth passes call it from the prepare
		// callback ShadowMap invokes before it opens the pass, which is the
		// only reason that callback exists.
		//
		// Returns an invalid View if anything is missing; the caller then
		// walks the objects itself.
		static View Cull(RHI::RHICommandList& cmd, const Mat4& viewProjection);

		// How many views culled this frame, for the statistics line.
		static uint32_t GetViewCount();
	};
}
