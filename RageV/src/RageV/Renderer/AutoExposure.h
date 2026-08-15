#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"

namespace RageV
{
	// Auto exposure: how bright the scene is, measured on the GPU, and moved
	// toward over time. ENGINE-NOTES 7y.
	//
	// Two dispatches. The first bins the scene's luminance into a histogram;
	// the second reduces that to one exposure and blends it with the last
	// frame's. Both run as render-graph *compute* passes between the scene
	// pass and tone mapping, because the thing they read is a target the graph
	// owns.
	//
	// **Nothing is read back to the CPU to decide what to draw.** The result
	// lives in a buffer the tone mapping pass reads. A readback would be
	// either a stall or a value whose age depends on how far ahead the GPU is,
	// and the second one puts the frame's appearance back under the scheduler
	// -- which is the whole thing 7y exists to avoid.

	// What one frame chain remembers between frames.
	//
	// **One of these per chain, not one per process**, and that is forced
	// rather than tidy: the editor draws its viewport and the game view from
	// two cameras in one frame. A shared adapted value would be written by
	// whichever drew last and read by both, so a bright game view would darken
	// the viewport and the viewport would brighten it back, every frame. That
	// is exactly the shape of the ghost in 7u, one layer up -- which is why it
	// lives beside TemporalHistory and is owned by the same thing.
	class ExposureState
	{
	public:
		// Allocates the histogram and the state buffer if they do not exist.
		// Cheap and idempotent; called every frame auto exposure is on.
		void Prepare(RHI::RHIDevice& device);

		// The buffer tone mapping reads. Null before Prepare.
		const RHI::Ref<RHI::RHIBuffer>& Exposure() const { return m_Exposure; }
		const RHI::Ref<RHI::RHIBuffer>& Histogram() const { return m_Histogram; }

		// Whether this chain has adapted to a measurement it can trust. False
		// makes the next frame *adopt* what it measures instead of sliding
		// toward it from nothing: a level that opens two stops wrong and
		// drifts into place over a second is a bug everybody sees, and it
		// would also make a screenshot depend on how far past the time
		// constant it was taken.
		//
		// **Two frames, not one**, and the reason is specific rather than
		// cautious. The reduce pass clears the histogram at the end of every
		// frame, so every frame's bins start clean -- *except the first*,
		// whose only clear is the one issued from the CPU when the buffer was
		// created. That is a staging copy with no ordering against the
		// dispatch that reads it: on OpenGL it landed first, on Vulkan it did
		// not, and the first frame therefore metered uninitialised memory.
		//
		// Adopting that and then smoothing away from it took about four
		// seconds to come right, reproducibly, which is what made it look like
		// a deliberately slow adaptation rather than a bug. Adopting on the
		// second dispatch instead means the frame that is trusted is always
		// one a reduce pass has cleaned up after. ENGINE-NOTES 7y.
		bool HasAdapted() const { return m_Dispatches > 1; }
		void MarkAdapted() { if (m_Dispatches < 2) m_Dispatches++; }

		// Forgets the adaptation without freeing anything. Used when the
		// feature was off last frame, or the scene changed underneath: resuming
		// from a ten-second-old exposure is the same mistake TemporalHistory's
		// Invalidate exists to prevent.
		void Invalidate() { m_Dispatches = 0; }

		void Release();

	private:
		RHI::Ref<RHI::RHIBuffer> m_Histogram;
		RHI::Ref<RHI::RHIBuffer> m_Exposure;
		// 0 = never metered, 1 = metered once on a histogram nobody had
		// cleared, 2+ = trustworthy. See HasAdapted.
		uint32_t m_Dispatches = 0;
	};

	class AutoExposure
	{
	public:
		// What the camera's profile asked for, already resolved.
		struct Params
		{
			// The stops the histogram spans. Outside them a pixel lands in the
			// end bin rather than being discarded -- except below, where bin 0
			// is reserved and dropped, because a night scene is mostly pixels
			// with no light in them and letting them vote meters the darkness.
			float MinLogLuminance = -8.0f;
			float MaxLogLuminance = 4.0f;

			// The tails to throw away. This is the entire reason for keeping a
			// histogram instead of an average: the top few percent are the sun
			// and the specular hits.
			float LowPercentile = 0.5f;
			float HighPercentile = 0.95f;

			// What the average is exposed *to*. 0.18 is middle grey.
			float MiddleGrey = 0.18f;

			// Bounds on the result, in case a scene has nothing in it.
			float MinExposure = 0.03f;
			float MaxExposure = 32.0f;

			// How fast, in stops per second, roughly. Converted to a blend
			// weight against this frame's delta by the caller.
			float SpeedUp = 3.0f;
			float SpeedDown = 1.0f;

			// **The frame time the loop handed down**, never a clock of this
			// module's own and never real elapsed time. That is what makes the
			// adaptation a function of the frame number under --frame-time,
			// and it is the whole of how auto exposure stays compatible with
			// every screenshot comparison in this repository. ENGINE-NOTES 7y.
			float DeltaSeconds = 0.0f;
		};

		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// Resets the dispatch-slot cursor. Called by Renderer::BeginFrame with
		// the other pooled renderers: each dispatch takes fresh sets and fresh
		// params buffers, because the editor meters two chains in one frame
		// and both are *recorded* before either runs -- a set (or a buffer)
		// reused across them is rewritten under the first chain's bind.
		static void BeginFrame();

		// Whether the pipelines compiled. False on a device with no compute,
		// and the caller then falls back to the manual exposure rather than
		// refusing to draw.
		static bool IsReady();

		// Records both dispatches. **Must be called outside a render pass** --
		// it is meant to be the body of a RenderGraph compute pass.
		//
		// `scene` is the linear HDR image, before tone mapping, which is the
		// only place the numbers mean anything: after the curve every scene
		// looks correctly exposed by construction.
		static void Dispatch(RHI::RHICommandList& cmd,
							 const RHI::Ref<RHI::RHITexture>& scene,
							 uint32_t width, uint32_t height,
							 ExposureState& state, const Params& params);

		// The blend weight for one frame, exposed because it is the piece
		// worth testing on its own.
		//
		// `1 - exp(-dt * rate)` rather than `rate * dt`, and the difference is
		// the entire point: the naive form is a first-order approximation that
		// diverges exactly when the frame rate does, so ten steps of 10 ms
		// would not land where one step of 100 ms did. Framerate independence
		// is the assertion that fails silently, so it is the one written first.
		static float BlendWeight(float rate, float deltaSeconds);
	};
}
