#pragma once

// Ray-traced shadows (ENGINE-NOTES 7am, 7an): the frame's top-level
// acceleration structure, built once per frame from every mesh the scene
// has, and handed to the lit pass so its fragment shader can trace a shadow
// ray toward each light instead of looking a map up.
//
// The renderer side of the RHI's acceleration structures, and nothing about
// it is a picture: it collects instances, poses the skinned ones, builds a
// TLAS into the frame's slot, and answers "is there one this frame". On a
// device that cannot trace -- OpenGL always -- Init leaves it unavailable
// and every call is a no-op, which is how the shadow-map path stays exactly
// what it was.
//
// One structure per frame in flight, because the previous frame's fragment
// shaders may still be tracing into theirs; and one *empty* structure, built
// once with no instances, bound wherever the layout declares the binding and
// no scene was traced this frame -- a probe capture, a frame before the
// first RenderShadows -- so the binding is never left unwritten.
//
// A skinned caster with a pose (7an) is posed by a compute pass into a
// buffer of its own and traces through a per-frame bottom-level structure
// refit from that buffer, so its shadow is the shadow of its pose. Casters
// are a pool reused by index in the order the scene walks them; one posed
// buffer, one structure and one set per frame in flight each, for the same
// reason the TLAS has one.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Math/Math.h"

#include <vector>

namespace RageV
{
	class Mesh;

	// What a ray's hit needs to know about the instance it hit (ENGINE-NOTES
	// 7ao), kept beside each TLAS instance in build order so the instance's
	// custom index is the row. Renderer3D turns these into the ray-instance
	// table the lit shader reads.
	struct RayCaster
	{
		RHI::Ref<Mesh>     MeshRef;
		RHI::Ref<Material> MaterialRef;
		MaterialParams     Params;
		// The compute-posed positions of a skinned caster; null for a static
		// one, whose positions are the mesh's own.
		RHI::Ref<RHI::RHIBuffer> Posed;
		// The same opaque id AreaEmitter carries, so a hit on this instance
		// can be told whether the emitter list answers for its emissive.
		uint64_t Owner = 0;
		// MeshComponent::Static, as the instance was built: which of the two
		// masks below it carries, and the RAY_INSTANCE_STATIC bit a hit reads
		// to decide whether the fully baked lights are in the field for it.
		bool Static = false;
	};

	class RayShadows
	{
	public:
		// The set 0 binding the lit shaders declare the structure at under
		// RV_RAY_SHADOWS, and the one Renderer3D writes.
		static constexpr uint32_t kBinding = 14;

		// **Which of two worlds an instance belongs to**, as the structure's
		// per-instance mask (AccelerationInstance::Mask; ENGINE-NOTES 7cx).
		// Every instance carries exactly one bit: static geometry
		// (MeshComponent::Static) or moving. A ray's cull mask then picks:
		// the runtime traces with both (0xFF), the bake's solve with
		// kMaskStatic alone -- a moving object is not baked -- and the
		// subtractive shadow a static pixel traces toward a fully baked lamp
		// with kMaskMoving alone. Mirrored by RV_RAY_MASK_STATIC and
		// RV_RAY_MASK_MOVING in pbr_fragment.glsl; the two must agree.
		static constexpr uint8_t kMaskStatic = 0x01;
		static constexpr uint8_t kMaskMoving = 0x02;

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
		//
		// `bones`, when given and non-empty, is the caster's pose -- the
		// animator's skinning matrices, the same vector the lit pass is
		// given -- and the caster is posed and refit before the build.
		// Without it a skinned mesh traces as its bind pose through the
		// structure the mesh caches.
		static void ClearInstances();
		// `isStatic` is MeshComponent::Static as the scene resolved it (a
		// skinned caster is never static): it chooses the instance's mask.
		static void AddInstance(const RHI::Ref<Mesh>& mesh, const Mat4& world,
								const std::vector<Mat4>* bones = nullptr,
								const RHI::Ref<Material>& material = nullptr,
								const MaterialParams& params = MaterialParams{},
								uint64_t owner = 0, bool isStatic = false);
		static void Build(RHI::RHICommandList& cmd);

		// This frame's casters, one per instance the structure was built
		// from, in the order it was built -- what a hit's custom index names.
		static const std::vector<RayCaster>& GetCasters();

		// True after Build ran this frame.
		static bool IsActive();
		// This frame's structure when active, otherwise the empty one --
		// never null on an available device, so a set can always be written.
		static const RHI::Ref<RHI::RHIAccelerationStructure>& GetStructure();

		static uint32_t GetInstanceCount();
		// How many of this frame's instances were posed and refit.
		static uint32_t GetSkinnedCount();
	};
}
