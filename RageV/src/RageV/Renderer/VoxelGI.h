#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "Light.h"
#include "Mesh.h"
#include "Material.h"
#include "PostProcess.h"
#include <functional>

namespace RageV
{
	// What one frame's voxel update is asked for. Resolved from RenderSettings
	// by the scene, clamped here to what the grid can be.
	struct VoxelGiSettings
	{
		// Voxels along each cascade's side: 32, 64 or 128.
		int Resolution = 64;
		// How many cascades, 1 to 4, each twice the voxel size of the last.
		int Cascades = 3;
		// The finest cascade's voxel, in metres.
		float VoxelSize = 0.25f;
		// 1 lights the grid from the lights; 2 also from last frame's grid.
		int Bounces = 1;
		// The lit shader's normal-offset scale, for the cascade lookup.
		float ShadowNormalOffset = 0.9f;
	};

	// Voxel global illumination (8.1, ENGINE-NOTES 7bc): the scene rasterised
	// into a clipmap of 3D textures around the camera, lit from the cascaded
	// shadow maps, mipped, and cone-traced at shade time. The raster-side
	// form that sees off screen -- no ray hardware, both backends -- and the
	// third writer of the frame's Indirect buffer, whose gather replaces
	// SSGI's at the head of the chain the two share.
	//
	// Four passes a frame, all before the graph beside the shadow maps
	// (Update), and one inside it (Gather). The scene owns the walk -- which
	// meshes, with which material -- and this owns everything after Submit.
	class VoxelGI
	{
	public:
		static constexpr uint32_t kMaxCascades = 4;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Whether the device can do this at all and the shaders compiled:
		// compute, fragment-stage image stores, and the five shaders.
		static bool IsReady();

		// The GPU has finished with this frame's slots. Called by
		// Renderer3D::BeginFrame.
		static void BeginFrame();

		// The walk: the scene calls Submit for every caster inside the box
		// `min`..`max`, which is the outermost cascade's volume this frame.
		using DrawCasters = std::function<void(const Vec3& min, const Vec3& max)>;

		// One frame's update, recorded outside any render pass: fits the
		// cascades around `cameraPosition`, clears the occupancy, voxelises
		// through `draw`, lights every occupied voxel from `lights` and the
		// shadow cascades ShadowMap rendered this frame, and builds the mip
		// chain. After this HasGrid() is true and GetRadiance() is the lit
		// chain the gather reads.
		static void Update(RHI::RHICommandList& cmd, const VoxelGiSettings& settings,
						   const Vec3& cameraPosition, const LightList& lights,
						   const DrawCasters& draw);

		// One caster, inside Update's walk. Static meshes only (7bc): a
		// skinned mesh submitted here would be voxelised in its bind pose,
		// which is a wrong answer rather than a missing one, so the scene
		// does not submit them.
		static void Submit(const RHI::Ref<Mesh>& mesh, const Mat4& world,
						   const RHI::Ref<Material>& material, const MaterialParams& params);

		// Nothing was lit this frame: the gather must not run. Called where
		// the form is off, so a grid left over from before is not read.
		static void Invalidate();
		static bool HasGrid();

		// The gather, in the graph where SSGI's compute pass runs: position
		// and normal from the depth buffer and the surface attachment (RTAO's
		// rule), six cones through the lit chain, and SSGI's packing out --
		// irradiance in RGB, linear depth in A -- so the blur and GI denoise
		// after it run unchanged.
		static void Gather(RHI::RHICommandList& cmd,
						   const RHI::Ref<RHI::RHITexture>& depth,
						   const RHI::Ref<RHI::RHITexture>& surface,
						   uint32_t width, uint32_t height,
						   const PostProcess::ViewReconstruction& view,
						   RHI::Format outputFormat);

		// For the stats panel and the checks: how many casters the last
		// update voxelised, and how many draw calls it took.
		static unsigned int GetCasterCount();
		static unsigned int GetDrawCallCount();
	};
}
