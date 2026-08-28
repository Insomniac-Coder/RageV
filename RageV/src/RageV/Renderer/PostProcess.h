#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// The fullscreen passes that run after the scene: bloom, tone mapping and
	// anti-aliasing.
	//
	// Each entry point records one fullscreen triangle into a pass the caller
	// has already begun -- the render graph owns the targets and the ordering,
	// and this owns the shaders and the arithmetic. Keeping those apart is what
	// lets the chain be rearranged without touching any of the maths.
	class PostProcess
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Resets the per-frame descriptor pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// Bloom stage 1: threshold and halve. Source is the linear HDR scene.
		static void Prefilter(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  uint32_t width, uint32_t height, RHI::Format outputFormat,
							  float threshold, float knee, float clamp);

		// Bloom stage 2: halve again. Source is the level above.
		static void Downsample(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							   uint32_t sourceWidth, uint32_t sourceHeight,
							   RHI::Format outputFormat);

		// Bloom stage 3: blur the smaller level up onto the larger one.
		// Additive, so the level being drawn into keeps what it already had.
		static void Upsample(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							 uint32_t sourceWidth, uint32_t sourceHeight,
							 RHI::Format outputFormat, float radius);

		// Exposure, bloom, ACES and the transfer function, in one pass. This is
		// where linear HDR becomes an image.
		// `lut` is a 3D texture or null; null grades nothing, which is the same
		// picture as no LUT at all. `lutSize` is its entries per axis, and the
		// shader needs it because a LUT is sampled at texel centres rather
		// than at the colour itself. ENGINE-NOTES 7t.
		// What the camera adds on top of what the scene is, all of it optional
		// and all of it off by default. Three effects at three points in this
		// one pass, because each models something different about a lens or a
		// film stock. ENGINE-NOTES 7w.
		struct LensParams
		{
			// Before anything: three taps of the scene, radially offset.
			float Aberration = 0.0f;
			// Still before the tone curve, because it is less light arriving.
			float Vignette = 0.0f;
			float VignetteSmoothness = 0.5f;
			// After the curve and after the LUT, because it is not a colour
			// anybody graded. Seeded from the frame number so that it animates
			// and still reproduces.
			float Grain = 0.0f;
			float GrainSize = 1.0f;

			// Auto exposure's answer, or null when the camera did not ask for
			// it. Read as a storage buffer rather than passed as a number,
			// because the value is computed on the GPU and reading it back to
			// hand it in would be either a stall or a value whose age depends
			// on how far ahead the GPU is. ENGINE-NOTES 7y.
			//
			// Null takes the exposure argument whole; non-null multiplies the
			// two, which is what makes the manual slider a compensation.
			RHI::Ref<RHI::RHIBuffer> Exposure;
		};

		static void Tonemap(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& scene,
							const RHI::Ref<RHI::RHITexture>& bloom, RHI::Format outputFormat,
							float exposure, float bloomIntensity,
							const RHI::Ref<RHI::RHITexture>& lut = nullptr,
							uint32_t lutSize = 0, float lutStrength = 1.0f,
							const LensParams& lens = {});

		// Anti-aliasing, on the tone-mapped image.
		static void FXAA(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
						 uint32_t width, uint32_t height, RHI::Format outputFormat,
						 float contrastThreshold, float relativeThreshold);

		// SMAA, in the three passes it takes. Also on the tone-mapped image,
		// and for the same reason FXAA is. ENGINE-NOTES 7n.

		// 1: where the discontinuities are, into RG8.
		static void SmaaEdges(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  uint32_t width, uint32_t height, RHI::Format outputFormat,
							  float threshold, float localContrast);

		// 2: reconstruct the line each edge belongs to and store its coverage.
		static void SmaaWeights(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& edges,
								uint32_t width, uint32_t height, RHI::Format outputFormat);

		// 3: mix each pixel along the axis its coverage came from.
		static void SmaaBlend(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
							  const RHI::Ref<RHI::RHITexture>& weights,
							  uint32_t width, uint32_t height, RHI::Format outputFormat);

		// SSAA's resolve: average an N-times-larger image down to this one.
		// On the linear HDR scene, before tone mapping -- see the shader.
		static void SsaaResolve(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
								uint32_t sourceWidth, uint32_t sourceHeight,
								RHI::Format outputFormat, int factor);

		// TAA's resolve: blend the jittered frame into the accumulated one.
		//
		// On the linear HDR scene, in the same slot as the SSAA resolve and
		// for the same reason. `history` is last frame's output of this same
		// pass and `velocity` the scene's motion vectors; `hasHistory` is
		// false on the first frame and after a resize, where the correct
		// answer is the current frame whole. ENGINE-NOTES 7r.
		static void TemporalResolve(RHI::RHICommandList& cmd,
									const RHI::Ref<RHI::RHITexture>& current,
									const RHI::Ref<RHI::RHITexture>& history,
									const RHI::Ref<RHI::RHITexture>& velocity,
									uint32_t width, uint32_t height,
									RHI::Format outputFormat,
									float feedback, bool hasHistory);

		// Depth of field, in the three passes it takes. On the linear HDR
		// scene, after the anti-aliasing resolve and before bloom -- see
		// ENGINE-NOTES 7z for why both halves of that matter.
		struct FocusParams
		{
			float FocusDistance = 5.0f;   // metres
			float FocalLength = 0.05f;    // metres
			float FNumber = 2.8f;
			float MaxRadius = 24.0f;      // pixels of the output
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
		};

		// 1: colour and the signed circle of confusion, at half size.
		// `depth` is the scene target's depth attachment, which is *not*
		// necessarily the same size as `scene` -- under SSAA it is larger.
		// Sampled with normalised coordinates for exactly that reason.
		static void DofPrepass(RHI::RHICommandList& cmd,
							   const RHI::Ref<RHI::RHITexture>& scene,
							   const RHI::Ref<RHI::RHITexture>& depth,
							   uint32_t frameHeight, RHI::Format outputFormat,
							   const FocusParams& focus);

		// 2: the gather, weighted so a tap contributes only where its own
		// circle of confusion reaches the pixel being written.
		static void DofGather(RHI::RHICommandList& cmd,
							  const RHI::Ref<RHI::RHITexture>& source,
							  uint32_t width, uint32_t height,
							  RHI::Format outputFormat, float maxRadius);

		// 3: the blur back over the sharp frame, at full resolution.
		static void DofComposite(RHI::RHICommandList& cmd,
								 const RHI::Ref<RHI::RHITexture>& scene,
								 const RHI::Ref<RHI::RHITexture>& blurred,
								 RHI::Format outputFormat);

		// Motion blur (9.5), four passes. ENGINE-NOTES 7ab.
		//
		// 1: velocity and depth packed into one image, which is where the
		// velocity's vertical convention and the depth's non-linearity are
		// both settled -- once, so the passes after never think about either.
		static void MotionBlurPack(RHI::RHICommandList& cmd,
								   const RHI::Ref<RHI::RHITexture>& velocity,
								   const RHI::Ref<RHI::RHITexture>& depth,
								   uint32_t width, uint32_t height,
								   float nearClip, float farClip,
								   RHI::Format outputFormat);

		// 2: the dominant velocity per tile of `tileSize` source pixels.
		static void MotionBlurTileMax(RHI::RHICommandList& cmd,
									  const RHI::Ref<RHI::RHITexture>& packed,
									  uint32_t sourceWidth, uint32_t sourceHeight,
									  float tileSize, RHI::Format outputFormat);

		// 3: each tile takes the largest of its 3x3 neighbours, so a fast
		// object one tile over is known here too.
		static void MotionBlurNeighborMax(RHI::RHICommandList& cmd,
										  const RHI::Ref<RHI::RHITexture>& tiles,
										  uint32_t tileWidth, uint32_t tileHeight,
										  RHI::Format outputFormat);

		// 4: the reconstruction gather along the neighbourhood's dominant
		// velocity, weighted by depth per tap.
		static void MotionBlurGather(RHI::RHICommandList& cmd,
									 const RHI::Ref<RHI::RHITexture>& scene,
									 const RHI::Ref<RHI::RHITexture>& packed,
									 const RHI::Ref<RHI::RHITexture>& tiles,
									 uint32_t width, uint32_t height,
									 float shutter, float maxRadius,
									 RHI::Format outputFormat);

		// What every pass that reconstructs view space from depth needs: the
		// clip planes, the projection's two inverse diagonal scales, and the
		// view matrix whose rotation brings the scene's world-space normal
		// into the frame it reconstructs. SSAO and SSR both carry one, and
		// carry the same one. ENGINE-NOTES 7ae.
		struct ViewReconstruction
		{
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvProjection0 = 1.0f;
			float InvProjection1 = 1.0f;
			Mat4 View{ 1.0f };

			// The NDC offset TAA added to the projection this frame.
			//
			// The depth buffer these passes read was rendered *through* the
			// jittered projection, so the point a texel describes is not the
			// point its own uv reconstructs to -- it is that point shifted by
			// the jitter. A pass that stays in screen space never notices,
			// because ViewToUv is the exact inverse and the offset cancels
			// end to end. A pass that leaves for world space does: RTAO casts
			// from the reconstructed position, and an origin that moves with
			// the jitter sequence makes the occlusion move with it too.
			// ENGINE-NOTES 7bq.
			float JitterX = 0.0f;
			float JitterY = 0.0f;
		};

		// SSAO (9.6), four passes -- the blur runs twice. ENGINE-NOTES 7ac.
		//
		// 1: occlusion at half resolution. The normal is the surface
		// attachment's where the scene wrote one, reconstructed from chosen
		// depth neighbours where it did not (7ae).
		static void SsaoCompute(RHI::RHICommandList& cmd,
								const RHI::Ref<RHI::RHITexture>& depth,
								const RHI::Ref<RHI::RHITexture>& surface,
								uint32_t width, uint32_t height,
								const ViewReconstruction& view, float radius,
								RHI::Format outputFormat);

		// 1, ray-traced (ENGINE-NOTES 7ao): the same occlusion pass with the
		// taps cast as short rays into `structure` instead of probed in the
		// depth buffer -- same output, same blur and apply after it. Skipped
		// (nothing written) on a device that could not compile it, which is
		// any without ray queries; the caller does not ask for it there.
		static void RtaoCompute(RHI::RHICommandList& cmd,
								const RHI::Ref<RHI::RHITexture>& depth,
								const RHI::Ref<RHI::RHITexture>& surface,
								const RHI::Ref<RHI::RHIAccelerationStructure>& structure,
								uint32_t width, uint32_t height,
								const ViewReconstruction& view, float radius,
								// Rays a pixel: AoDetail::Full casts eight, the rungs
								// below it four.
								uint32_t taps,
								RHI::Format outputFormat);

		// Global illumination, screen-space (9.12). ENGINE-NOTES 7at.
		//
		// 1: one bounce at half resolution. The same geometry as SSAO's first
		// pass -- position and normal from the depth buffer and the surface
		// attachment -- with the taps gathering the lit image's colour instead
		// of counting occluders. Irradiance in RGB, linear depth in A, which
		// is the packing SsaoBlur reads: passes 2 and 3 are *that* blur, and
		// its own blur reads.
		static void SsgiCompute(RHI::RHICommandList& cmd,
								const RHI::Ref<RHI::RHITexture>& depth,
								const RHI::Ref<RHI::RHITexture>& surface,
								const RHI::Ref<RHI::RHITexture>& scene,
								// The indirect light already in `scene`, which
								// the gather subtracts off every tap so that it
								// does not read its own answer (7ay).
								const RHI::Ref<RHI::RHITexture>& contributed,
								uint32_t width, uint32_t height,
								const ViewReconstruction& view, float radius,
								// How many taps each pixel takes (7az).
								float taps,
								RHI::Format outputFormat);

		// 2 and 3: the same separable blur SSAO uses, widened to carry three
		// channels of light with the depth in alpha. A copy rather than a
		// shared shader: moving SSAO's own depth into alpha to share one broke
		// occlusion on OpenGL, and the check caught it (7at).
		static void SsgiBlur(RHI::RHICommandList& cmd,
							 const RHI::Ref<RHI::RHITexture>& source,
							 uint32_t width, uint32_t height,
							 float directionX, float directionY,
							 RHI::Format outputFormat);

		// 4: the resolve into the frame's Indirect buffer (ENGINE-NOTES 7av).
		// 4 was a resolve of its own until 9.13c: with the gather's feedback
		// loop closed, the screen-space chain ends on the same GiDenoise the
		// traced form does, and `ssgi_apply` went with it (ENGINE-NOTES 7ay).

		// 2 and 3: the separable depth-aware blur, one axis per call.
		static void SsaoBlur(RHI::RHICommandList& cmd,
							 const RHI::Ref<RHI::RHITexture>& source,
							 uint32_t width, uint32_t height,
							 float directionX, float directionY,
							 RHI::Format outputFormat);

		// 4: the multiply onto the linear HDR image.
		// Exponential height fog over the depth buffer, on the linear image
		// before tone mapping. `view` is what the pass needs to put a pixel
		// back in the world: the same reconstruction RTAO and the traced
		// bounce use, because three passes disagreeing about where a pixel is
		// would be three different bugs.
		struct FogView
		{
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvProjection0 = 1.0f;
			float InvProjection1 = 1.0f;
			Mat4  View{ 1.0f };
		};

		static void Fog(RHI::RHICommandList& cmd,
						const RHI::Ref<RHI::RHITexture>& scene,
						const RHI::Ref<RHI::RHITexture>& depth,
						const FogSettings& fog, const FogView& view,
						RHI::Format outputFormat);

		static void SsaoApply(RHI::RHICommandList& cmd,
							  const RHI::Ref<RHI::RHITexture>& scene,
							  const RHI::Ref<RHI::RHITexture>& occlusion,
							  float intensity, RHI::Format outputFormat);

		// SSR (9.7, 9.10), three passes. ENGINE-NOTES 7ad, 7ag.
		struct SsrParams
		{
			ViewReconstruction View;
			float MaxDistance = 20.0f;   // metres
			float Thickness = 0.5f;      // metres
		};

		// How many levels the hi-Z pyramid holds, how many of them the fine
		// atlas holds (the coarse atlas has the rest), and the atlases' size
		// for a trace of `width` x `height`. The layout is
		// include/hiz_atlas.glsl; this is the C++ that has to agree with it
		// about the outer size and the split.
		static constexpr uint32_t kSsrHiZLevels = 6;
		static constexpr uint32_t kSsrHiZFineLevels = 3;
		static void SsrHiZSize(uint32_t traceWidth, uint32_t traceHeight,
							   uint32_t& atlasWidth, uint32_t& atlasHeight);

		// 0a: the fine levels of the min-depth pyramid, from the scene depth.
		// `traceWidth`/`traceHeight` are level 0's size.
		static void SsrHiZFine(RHI::RHICommandList& cmd,
							   const RHI::Ref<RHI::RHITexture>& depth,
							   uint32_t traceWidth, uint32_t traceHeight,
							   float nearClip, float farClip, RHI::Format outputFormat);

		// 0b: the coarse levels, from the fine atlas's last level.
		static void SsrHiZCoarse(RHI::RHICommandList& cmd,
								 const RHI::Ref<RHI::RHITexture>& fine,
								 uint32_t traceWidth, uint32_t traceHeight,
								 float farClip, RHI::Format outputFormat);

		// 1: the walk through the atlases, at half resolution. Writes hit uv
		// + confidence.
		static void SsrTrace(RHI::RHICommandList& cmd,
							 const RHI::Ref<RHI::RHITexture>& hiZFine,
							 const RHI::Ref<RHI::RHITexture>& hiZCoarse,
							 const RHI::Ref<RHI::RHITexture>& surface,
							 uint32_t width, uint32_t height,
							 const SsrParams& params, RHI::Format outputFormat);

		// 2: sample the hit, blur by roughness, and write radiance + confidence
		// for *next* frame's lighting to read. It blends nothing itself: the
		// PBR shader swaps the probe's reflected radiance for this under the
		// exact weight the probe would have had. ENGINE-NOTES 7af.
		static void SsrResolve(RHI::RHICommandList& cmd,
							   const RHI::Ref<RHI::RHITexture>& scene,
							   const RHI::Ref<RHI::RHITexture>& trace,
							   const RHI::Ref<RHI::RHITexture>& surface,
							   uint32_t width, uint32_t height,
							   RHI::Format outputFormat);

		// A straight copy, for when anti-aliasing is off but the chain still
		// has to land in the target the caller wanted.
		static void Blit(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHITexture>& source,
						 RHI::Format outputFormat);

		// The indirect buffer's temporal stage (ENGINE-NOTES 7av): this
		// frame's raw estimate accumulated onto what the last frame resolved,
		// reprojected through the velocity buffer.
		//
		// `feedback` is how much of the history survives -- much higher than
		// TAA's, because irradiance is low frequency and the estimate under it
		// is four samples wide. Zero disables the accumulation exactly, which
		// is what makes "the denoiser converges" a falsifiable claim rather
		// than an impression. `hasHistory` false takes the current frame
		// whole, the same contract TemporalResolve has.
		// **One a-trous iteration over the indirect buffer**, guided by the
		// scene's depth and surface normal so it only averages points that
		// share a surface. `stride` is the iteration's reach -- 1, 2, 4 --
		// which is what lets three cheap passes cover a 15-tap radius.
		//
		// Run before GiDenoise: filtering the raw trace and then accumulating
		// leaves the history ping-pong alone, where filtering the accumulated
		// result would have the lit pass and the next frame's history reading
		// two different buffers.
		static void GiSpatial(RHI::RHICommandList& cmd,
							  const RHI::Ref<RHI::RHITexture>& indirect,
							  const RHI::Ref<RHI::RHITexture>& depth,
							  const RHI::Ref<RHI::RHITexture>& surface,
							  uint32_t width, uint32_t height, float stride,
							  float nearClip, float farClip,
							  RHI::Format outputFormat);

		static void GiDenoise(RHI::RHICommandList& cmd,
							  const RHI::Ref<RHI::RHITexture>& current,
							  const RHI::Ref<RHI::RHITexture>& history,
							  const RHI::Ref<RHI::RHITexture>& velocity,
							  uint32_t width, uint32_t height,
							  float feedback, bool hasHistory,
							  RHI::Format outputFormat);

		static bool IsReady();

	private:
		// Which shader. Pipelines are cached per (shader, output format),
		// because the chain writes into an HDR format and then an LDR one, and
		// a pipeline bakes the format it renders to.
		enum class Shader
		{
			Prefilter, Downsample, Upsample, Tonemap, FXAA, Blit,
			SmaaEdges, SmaaWeights, SmaaBlend,
			SsaaResolve,
			TaaResolve,
			DofPrepass, DofGather, DofComposite,
			MotionBlurPack, MotionBlurTileMax, MotionBlurNeighborMax, MotionBlurGather,
			SsaoCompute, SsaoBlur, SsaoApply,
			SsrTrace, SsrResolve,
			SsrHiZ,
			RtaoCompute,
			// Appended, never inserted: ShaderPath below is indexed by this
			// enum's order.
			SsgiCompute, SsgiBlur, GiDenoise,
			Fog,
			GiSpatial,
			Count
		};

		// How a binding wants to read its texture.
		//
		// Bloom and FXAA sample between texels and want the filter; SMAA's
		// first two passes address exact texels and its edge and weight maps
		// are *classifications*, not colours -- a filtered read of a flag
		// halfway between "edge" and "no edge" is a value that means nothing.
		enum class Sampling { Linear, Point };

		// `third` is only for the temporal resolve, which needs the frame, the
		// history and the motion vectors at once. Null bindings are skipped
		// rather than filled: writing a binding the shader never declared is
		// out of range for the layout, and the driver takes it badly.
		static void Dispatch(RHI::RHICommandList& cmd, Shader shader, RHI::Format outputFormat,
							 const RHI::Ref<RHI::RHITexture>& first,
							 const RHI::Ref<RHI::RHITexture>& second,
							 const void* params, uint32_t paramSize,
							 Sampling firstSampling = Sampling::Linear,
							 Sampling secondSampling = Sampling::Linear,
							 const RHI::Ref<RHI::RHITexture>& third = nullptr,
							 Sampling thirdSampling = Sampling::Linear,
							 // Also binding 3, and never alongside the buffer
							 // below: the layout is the shader's, and no shader
							 // declares both. SSGI's gather is the one pass that
							 // wants a fourth image (ENGINE-NOTES 7ay).
							 const RHI::Ref<RHI::RHITexture>& fourth = nullptr,
							 Sampling fourthSampling = Sampling::Linear,
							 // Bound at binding 3 when the shader declares it.
							 // Only the tonemap does, for auto exposure.
							 const RHI::Ref<RHI::RHIBuffer>& storage = nullptr,
							 // Bound at binding 4 when the shader declares it.
							 // Only the ray-traced occlusion pass does (7ao).
							 const RHI::Ref<RHI::RHIAccelerationStructure>& structure = nullptr);
	};
}
