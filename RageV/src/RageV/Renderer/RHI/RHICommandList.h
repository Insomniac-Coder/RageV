#pragma once

// Commands are recorded into a list rather than issued against a global
// context. This is the interface the old RenderAPI could not provide: its
// DrawIndexed had no notion of a frame, a target, or a bound pipeline, so
// Vulkan had nowhere to put a command buffer.

#include "RHITypes.h"
#include "RHIResources.h"
#include "RHIPipeline.h"
#include "RHIResourceSet.h"

namespace RageV::RHI
{
	struct RenderPassBeginInfo
	{
		// nullptr targets the swapchain.
		RHIRenderTarget* Target = nullptr;
		ClearValue       Clear;
		bool             ClearColor = true;
		bool             ClearDepth = true;
		// Attach depth at all. A pass that declares a depth attachment requires
		// every pipeline drawn in it to declare a matching depth format, so
		// passes that do not need depth -- the ImGui overlay -- must opt out.
		bool             UseDepth = true;
	};

	class RHICommandList
	{
	public:
		virtual ~RHICommandList() = default;

		virtual void BeginRenderPass(const RenderPassBeginInfo& info) = 0;
		virtual void EndRenderPass() = 0;

		virtual void SetViewport(const Viewport& viewport) = 0;
		virtual void SetScissor(const Rect2D& scissor) = 0;

		virtual void BindPipeline(const Ref<RHIPipeline>& pipeline) = 0;
		virtual void BindResourceSet(uint32_t set, const Ref<RHIResourceSet>& resources) = 0;
		virtual void BindVertexBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer, uint64_t offset = 0) = 0;
		virtual void BindIndexBuffer(const Ref<RHIBuffer>& buffer, IndexType type, uint64_t offset = 0) = 0;

		virtual void PushConstants(ShaderStage stages, uint32_t offset, uint32_t size, const void* data) = 0;

		virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
						  uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

		virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
								 uint32_t firstIndex = 0, int32_t vertexOffset = 0,
								 uint32_t firstInstance = 0) = 0;

		// Copies a rendered 2D image onto one array layer of another texture,
		// in the orientation that sampling that layer expects. Same size, same
		// format, mip 0. Must be called outside a render pass.
		//
		// "In the orientation sampling expects" is doing real work here. The
		// two backends store a rendered image with opposite row order -- Vulkan
		// writes row 0 at the top, OpenGL at the bottom -- and for an ordinary
		// texture that cancels against the equally opposite texture coordinate
		// convention, so nothing anywhere has to know. Cube faces have no such
		// luck: their orientation comes from a face table both specifications
		// share, and the data has to match it. So one backend copies and the
		// other blits upside down, and callers see neither.
		// Records a GPU timestamp into this frame's pool at `slot`.
		//
		// After every command already recorded, not at the top of the pipe: the
		// point is when the work *finished*, and a top-of-pipe timestamp
		// answers a different question that looks the same in a report.
		virtual void WriteTimestamp(uint32_t slot) = 0;

		// Builds a texture's mip chain *in this command buffer*, in order with
		// everything already recorded into it.
		//
		// RHITexture::GenerateMips submits its own command buffer and waits, so
		// it runs before anything recorded into the frame -- which meant a
		// reflection probe's cube had its mips built from an empty mip 0, and
		// every rough surface reflecting that probe read black until the next
		// full round of faces six frames later. Anything whose mip 0 was
		// written by this frame has to use this instead.
		virtual void GenerateMips(const Ref<RHITexture>& texture) = 0;

		virtual void CopyToTextureLayer(const Ref<RHITexture>& source,
										const Ref<RHITexture>& destination,
										uint32_t layer, uint32_t mip = 0) = 0;

		// Debug-marker scopes; show up in RenderDoc and Nsight.
		virtual void PushDebugGroup(const char* name) = 0;
		virtual void PopDebugGroup() = 0;

		// Escape hatch for third-party libraries that render themselves and
		// need the underlying handle -- Dear ImGui's backends being the reason
		// this exists. Returns a VkCommandBuffer on Vulkan and nullptr on
		// OpenGL, which has no such object. Engine code should not use it.
		virtual void* GetNativeHandle() const { return nullptr; }
	};
}
