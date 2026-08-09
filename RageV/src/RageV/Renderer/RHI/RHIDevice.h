#pragma once

// The device owns the swapchain, the frame lifecycle and every resource
// factory. One instance exists per window.

#include "RHITypes.h"
#include "RHIResources.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "RHIResourceSet.h"
#include "RHICommandList.h"
#include <functional>

namespace RageV::RHI
{
	// The platform window the swapchain presents to, opaque on purpose: this
	// is a public header and no third-party type may appear in one. Pass
	// whatever Window::GetNativeWindow() returns; the backend casts it back to
	// the windowing library's type in its own .cpp, which is the only place
	// that knows what the handle really is.
	using NativeWindowHandle = void*;

	struct DeviceDesc
	{
		Backend             Backend          = Backend::Vulkan;
		NativeWindowHandle  Window           = nullptr;
		uint32_t            Width            = 1600;
		uint32_t            Height           = 900;
		bool                VSync            = true;
		bool                EnableValidation = false;
		uint32_t            FramesInFlight   = 2;
	};

	class RHIDevice
	{
	public:
		virtual ~RHIDevice() = default;

		static Scope<RHIDevice> Create(const DeviceDesc& desc);

		virtual Backend           GetBackend() const = 0;
		virtual const DeviceCaps& GetCaps()    const = 0;

		// ------------------------------------------------------------------
		// Frame lifecycle
		// ------------------------------------------------------------------
		// Acquires a swapchain image and returns the command list to record
		// into. Returns nullptr when the frame must be skipped -- the swapchain
		// is out of date or the window is minimised -- in which case EndFrame
		// must not be called.
		virtual RHICommandList* BeginFrame() = 0;
		virtual void EndFrame() = 0;

		virtual void WaitIdle() = 0;

		// Hands back the pixels of the frame about to be presented, once.
		//
		// The only way to check what an engine actually put on screen without
		// a person looking at it. Draw-call counts are not a substitute: a
		// pass that clears the backbuffer after the scene was drawn produces a
		// blank window and a perfectly healthy draw count, which is precisely
		// the bug the standalone runtime shipped with.
		//
		// Tightly scoped on purpose. It stalls the GPU, it fires for one frame
		// and then disarms itself, and the callback runs on the frame that
		// satisfies it -- so it is a diagnostic, not a capture pipeline.
		//
		// Pixels are RGBA8, top row first, tightly packed.
		using CaptureCallback = std::function<void(const uint8_t* rgba, uint32_t width,
												   uint32_t height)>;
		virtual void RequestCapture(CaptureCallback callback) = 0;
		virtual void OnResize(uint32_t width, uint32_t height) = 0;
		virtual void SetVSync(bool enabled) = 0;

		// What the swapchain is actually presenting with, which is not the
		// same as what the config asked for once anything has toggled it.
		// Anything reporting the frame time has to read this rather than the
		// startup setting, or it keeps saying "vsync is on" after it is off.
		virtual bool IsVSync() const = 0;

		// --- GPU timing ---------------------------------------------------
		// Timestamps go into a pool of numbered slots, one pool per frame in
		// flight. A frame writes into its own pool; the results are read back
		// when that pool comes round again, by which point the fence has
		// already guaranteed the GPU finished with it. That is why the numbers
		// a profiler reports are a couple of frames old, and why reading them
		// costs no stall.
		//
		// Slots are the caller's to assign. FrameProfiler maps two per phase.
		static constexpr uint32_t kTimestampSlots = 64;

		// Nanoseconds per tick. Vulkan reports it per device; OpenGL is already
		// nanoseconds.
		virtual double GetTimestampPeriodNs() const { return 1.0; }

		// Ticks for the frame whose pool was just recycled, one per slot, and a
		// parallel flag for whether that slot was actually written -- a phase
		// that did not run this frame has no timestamp, and a stale value from
		// two frames ago would read as a plausible duration.
		virtual const std::vector<uint64_t>& GetResolvedTimestamps() const
		{
			static const std::vector<uint64_t> none;
			return none;
		}
		virtual const std::vector<uint8_t>& GetResolvedTimestampFlags() const
		{
			static const std::vector<uint8_t> none;
			return none;
		}

		virtual uint32_t GetFramesInFlight() const = 0;
		// Which frame slot is currently being recorded. Per-frame resources
		// (uniform buffers, descriptor sets) must be indexed by this.
		virtual uint32_t GetFrameIndex() const = 0;

		virtual Format GetSwapchainFormat() const = 0;
		virtual Format GetSwapchainDepthFormat() const = 0;
		virtual uint32_t GetSwapchainWidth()  const = 0;
		virtual uint32_t GetSwapchainHeight() const = 0;

		// ------------------------------------------------------------------
		// Resource creation
		// ------------------------------------------------------------------
		virtual Ref<RHIBuffer>       CreateBuffer(const BufferDesc& desc) = 0;
		virtual Ref<RHITexture>      CreateTexture(const TextureDesc& desc) = 0;
		virtual Ref<RHISampler>      CreateSampler(const SamplerDesc& desc) = 0;
		virtual Ref<RHIShader>       CreateShader(const CompiledShader& compiled) = 0;
		virtual Ref<RHIPipeline>     CreatePipeline(const GraphicsPipelineDesc& desc) = 0;
		virtual Ref<RHIRenderTarget> CreateRenderTarget(const RenderTargetDesc& desc) = 0;
		virtual Ref<RHIResourceSet>  CreateResourceSet(const Ref<RHIPipeline>& pipeline, uint32_t set) = 0;
	};
}
