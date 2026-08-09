#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"
#include <functional>

namespace RageV
{
	// One slice of a cascaded shadow map.
	struct ShadowCascade
	{
		// World space straight to shadow-map lookup coordinates: xy are texture
		// coordinates and z is the depth to compare against, after the divide
		// by w. The texture-space bias is folded in on the CPU, including the
		// vertical flip one backend needs and the other must not have -- see
		// HANDOFF §5, which is where that cost a day.
		Mat4 LookupMatrix{ 1.0f };

		// Without the bias, for rendering the cascade.
		Mat4 ViewProjection{ 1.0f };

		// How far down the view axis this cascade reaches. Selection compares
		// against these in order.
		float SplitDepth = 0.0f;

		// World-space size of one shadow texel. Normal-offset bias scales with
		// it, because the error being corrected is one texel wide by
		// definition: a fixed offset is too small on the near cascade and
		// detaches shadows on the far one.
		float TexelWorldSize = 0.0f;
	};

	// A shadow map belonging to one positional light.
	//
	// Simpler than a cascade in every way that matters: a spot has a frustum
	// and a point has a range, so neither needs fitting to whatever the camera
	// happens to be looking at. That is the entire reason cascades exist, and
	// the entire reason these are cheap by comparison.
	struct LocalShadow
	{
		enum class Kind : uint32_t { None = 0, Cascades = 1, Spot = 2, Point = 3 };

		Kind Type = Kind::None;
		int  Slot = -1;

		// Spot only: world space to lookup coordinates, bias folded in.
		Mat4 LookupMatrix{ 1.0f };

		// Point only. The comparison reference is rebuilt in the shader from
		// the distance along the major axis, so it needs the same far the faces
		// were rendered with. The near is a shared constant -- see
		// kPointShadowNear.
		float FarClip = 25.0f;

		// World size of one of this light's shadow texels, per unit of distance
		// from it. Unlike a directional cascade, where a texel is a fixed size,
		// a positional light's texels grow with distance and so must its bias.
		float TexelScale = 0.0f;
	};

	// Shared with pbr.rvshader, which rebuilds a point light's comparison depth
	// from the same projection. The two have to agree or every comparison is
	// against a depth from a different frustum.
	constexpr float kPointShadowNear = 0.05f;

	// Cascaded shadow maps for one directional light.
	//
	// Directional only, deliberately. A directional light has no position, so
	// its shadow has to cover whatever the camera can see -- which is the whole
	// reason cascades exist and the only case where the fitting is hard. Spot
	// and point lights have a frustum and a range of their own and are a
	// simpler problem on top of this one.
	class ShadowMap
	{
	public:
		static constexpr uint32_t kMaxCascades = 4;

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();
		static bool IsReady();

		// Fits `count` cascades to the camera's frustum.
		//
		// Pure, and checked by the test suite. Every property that matters here
		// is invisible in a still image and obvious in motion: a cascade that
		// changes size as the camera turns makes shadow edges crawl, and one
		// that is not snapped to its own texel grid makes them shimmer on every
		// sub-texel step. Neither shows up in a screenshot.
		static void ComputeCascades(const Mat4& cameraTransform,
									float fovYRadians, float aspect,
									float nearClip, float farClip,
									const Vec3& lightDirection,
									uint32_t count, uint32_t resolution,
									float lambda, bool flipLookupY,
									ShadowCascade* out);

		// Draws the scene from one cascade's point of view. Given the matrix to
		// transform world positions by; the caller draws whatever casts.
		using DrawCasters = std::function<void(const Mat4& viewProjection)>;

		// Renders every cascade. Must be called outside a render pass -- it
		// opens one per cascade.
		static void Render(RHI::RHICommandList& cmd, const ShadowCascade* cascades,
						   uint32_t count, uint32_t resolution, const DrawCasters& draw);

		// What the last Render produced. The lit pass reads these rather than
		// being handed them, because it is called once per viewport and the
		// cascades belong to the frame.
		static const ShadowCascade* GetCascades();
		static uint32_t GetResolution();

		// Which light in the scene's list the cascades were fitted to. -1 when
		// nothing casts.
		static int GetLightIndex();
		static void SetLightIndex(int index);

		// --- positional lights ---------------------------------------------
		// Up to this many spot and point lights cast at once. Four of each is
		// four extra scene renders and twenty-four; past that the answer is
		// fewer shadow-casting lights, not more slots.
		static constexpr uint32_t kMaxLocal = 4;

		// One perspective depth map. `viewProjection` is the light's own
		// frustum, so nothing is fitted and nothing crawls.
		static void RenderSpot(RHI::RHICommandList& cmd, uint32_t slot, uint32_t resolution,
							   const Mat4& viewProjection, const DrawCasters& draw);

		// Six faces of one, into a depth cube. The face basis is the same table
		// the reflection probes use -- the two features disagree about nothing.
		static void RenderPoint(RHI::RHICommandList& cmd, uint32_t slot, uint32_t resolution,
								const Vec3& position, float farClip,
								const DrawCasters& draw);

		// Which map each light in the scene's list ended up with. Recorded here
		// rather than on the light, because the assignment is made while
		// rendering shadows and read while rendering the scene -- two passes
		// that build their light lists separately and must agree.
		//
		// Must match the shader's MAX_LIGHTS.
		static constexpr uint32_t kMaxLights = 8;

		static void Assign(uint32_t lightIndex, const LocalShadow& shadow);
		static const LocalShadow& GetAssignment(uint32_t lightIndex);

		static RHI::Ref<RHI::RHITexture> GetSpotTexture(uint32_t slot);
		static RHI::Ref<RHI::RHITexture> GetPointTexture(uint32_t slot);

		// A cube of "everything passes", for the slots a scene does not use.
		static RHI::Ref<RHI::RHITexture> GetEmptyCube();

		// Nothing has been rendered this frame, so surfaces must not sample.
		static void Invalidate();
		static bool HasCascades();
		static uint32_t GetCascadeCount();

		static RHI::Ref<RHI::RHITexture> GetCascadeTexture(uint32_t index);
		static RHI::Ref<RHI::RHISampler> GetSampler();

		// The 1x1 fully-lit depth bound where a cascade does not exist. A
		// comparison sampler still has to be given something, and "everything
		// passes" is the only harmless answer.
		static RHI::Ref<RHI::RHITexture> GetEmptyTexture();
	};
}
