#pragma once
#include "RageV/Math/Math.h"
#include "Camera.h"
#include "Light.h"
#include "Environment.h"
#include "RenderSettings.h"
#include "Mesh.h"
#include "Material.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Lit mesh rendering. Unlike Renderer2D, geometry is not merged: each mesh
	// keeps its own buffers and per-object data goes through push constants, so
	// a draw costs a push-constant write rather than a descriptor update.
	class Renderer3D
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when a shader did not compile. Meshes draw nothing in that
		// state, which is easier to diagnose from here than from the picture.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined);
		static void SetWireframe(bool enabled);

		// Resets the per-frame scene-slot pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// `environmentMap` is what surfaces reflect -- the scene's environment
		// map, a probe's capture, or the sky's gradient baked into a small
		// cube. Null means nothing is reflected, not that the term is
		// undefined: the binding is filled with a neutral cube and the
		// intensity set to zero.
		//
		// `jitter` is the sub-pixel offset already folded into `camera`, in
		// NDC. It is passed as well as applied because the motion vectors have
		// to be free of it: what the velocity attachment means is where the
		// *surface* moved, and half a pixel of camera offset is not that. The
		// default is the honest one -- a caller that did not jitter its camera
		// must not have the correction applied to it either.
		//
		// Two settings blocks rather than one: `environment` is what the scene
		// owns (ambient and sky) and `render` is what the project owns (the
		// shadow normal offset, here). ENGINE-NOTES 7s.
		static void BeginScene(const Camera& camera, const Mat4& cameraTransform,
							   const LightList& lights = {},
							   const SceneEnvironment& environment = {},
							   const RenderSettings& render = {},
							   const RHI::Ref<RHI::RHITexture>& environmentMap = nullptr,
							   const RHI::Ref<RHI::RHITexture>& irradianceMap = nullptr,
							   const Vec2& jitter = Vec2(0.0f, 0.0f));
		static void EndScene();

		// Depth only, from a light. Wraps the same mesh binding the lit path
		// uses, because a shadow pass draws the same geometry and differs only
		// in what it writes.
		//
		// The light's view-projection is folded into each draw's model matrix,
		// so a caster costs one push constant and nothing else -- no descriptor
		// set is bound in this pass at all.
		static void BeginShadow(const Mat4& viewProjection);
		static void DrawMeshShadow(const RHI::Ref<Mesh>& mesh, const Mat4& transform);

		// The same pose the lit pass was given. Without this a skinned figure
		// walks and its shadow stands still in the bind pose.
		static void DrawSkinnedMeshShadow(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
										  const std::vector<Mat4>& bones);
		static void EndShadow();

		// `probe` is which cube of the reflection-probe arrays this object
		// reflects, chosen by the scene against the object's own position. Slot
		// 0 is the sky, so it is the answer for an object no probe reaches --
		// not a sentinel, and not something the shader has to branch on.
		//
		// Passed per object rather than set as state before a run of draws,
		// because it lands in the instance stream: two objects that chose
		// differently are still one instanced draw, and making it state would
		// invite exactly the per-batch shape this replaced.
		// `params` is the material's scalars with the entity's overrides already
		// folded in. Passed rather than read from `material`, because the
		// material is *shared* -- writing an override into it would change
		// every other object using it, and reading from it would make overrides
		// impossible. The scalars ride in the instance stream, so this costs a
		// struct copy and no draw calls.
		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
							 const RHI::Ref<Material>& material,
							 const MaterialParams& params, uint32_t probe,
							 const Mat4* previousTransform = nullptr);
		// Where this instance was last frame, or null for "it did not move".
		//
		// A pointer with a null default rather than a required argument: every
		// caller that has no idea is telling the truth by saying nothing, and
		// the answer it gets -- zero velocity -- is the correct one for static
		// geometry, which is most of a scene. ENGINE-NOTES 7r.


		// A mesh a skeleton moves. `bones` is one matrix per bone, already
		// composed with the inverse binds -- ComposeSkinning produces exactly
		// this, and an empty list draws the bind pose.
		//
		// Separate from DrawMesh rather than an overload with a default,
		// because a skinned mesh must never reach the static pipeline: the
		// vertex layouts differ and the static one would read joint indices as
		// texture coordinates.
		static void DrawSkinnedMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
									const RHI::Ref<Material>& material,
									const MaterialParams& params,
									const std::vector<Mat4>& bones, uint32_t probe,
									const Mat4* previousTransform = nullptr);

		// A mesh whose surface is four materials in painted proportions -- a
		// terrain chunk (ENGINE-NOTES 7aq). Drawn by the third lit pipeline,
		// whose set 1 the layered material binds itself; the instance's scalars
		// are layer 0's, and its material record on the bindless path is
		// layer 0's too, which is what a traced reflection of it shades with.
		// `layered` must have had Refresh called this frame. `indexCount` is
		// how many of the mesh's indices to draw, from the first -- the whole
		// mesh, or the part before its skirts while the camera is under the
		// ground (7ap); clamped to the mesh's count.
		static void DrawLayeredMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
									const RHI::Ref<LayeredMaterial>& layered, uint32_t probe,
									uint32_t indexCount,
									const Mat4* previousTransform = nullptr);

		// Shared by every mesh that has no material of its own.
		static RHI::Ref<Material> GetDefaultMaterial();

		// How many meshes were skipped by frustum culling this frame, across
		// every pass. Reported so the saving is visible rather than assumed.
		static unsigned int GetCulledCount();
		static void CountCulled();

		// The most lights any one cluster ended up holding, and the number that
		// actually decides a fragment's cost. If it equals the scene's light
		// count, clustering did nothing and is pure overhead.
		static unsigned int GetMaxCellLoad();
		static unsigned int GetLightCount();

		static unsigned int GetDrawCallCount();
		static unsigned int GetTriangleCount();

		// Whether the lit shaders trace the directional light's shadow instead
		// of sampling cascades (ENGINE-NOTES 7am). Switching recompiles the two
		// lit shaders with RV_RAY_SHADOWS and rebuilds their pipelines, which
		// the SPIR-V cache makes cheap after the first time; called by
		// Scene::RenderShadows once it has resolved the mode. A no-op on a
		// device that cannot trace, whatever it is asked.
		static void SetRayTracedShadows(bool enabled);
		static bool IsRayTracedShadows();

		// Ray-traced reflections (ENGINE-NOTES 7ao): the lit shaders compiled
		// with RV_RAY_REFLECTIONS trace the mirror ray from every glossy
		// pixel and shade the hit through the ray-instance table this
		// renderer writes at set 0 binding 15. Only ever on with the shadows
		// on and the heap available -- Scene::RenderShadows resolves that.
		static void SetRayTracedReflections(bool enabled);
		static bool IsRayTracedReflections();

		// Whether the lit pass reads material textures through the bindless
		// heap this session (ENGINE-NOTES 7al): the device can, and
		// --bindless did not say no. Reported rather than assumed, because a
		// device that lacks the feature takes the bound path silently and a
		// pixel comparison between the two needs to know which it got.
		static bool IsBindless();
		// The heap itself, or null on the bound path: what a layered material
		// registers its maps in (7aq).
		static TextureHeap* GetTextureHeap();
		// How many heap slots are live and how many remain, for the stats
		// panel; both zero on the bound path.
		static unsigned int GetHeapLiveCount();
		static unsigned int GetHeapFreeCount();

	private:
		static void EnsurePipeline();
		static bool CompileLitShaders();
	};
}
