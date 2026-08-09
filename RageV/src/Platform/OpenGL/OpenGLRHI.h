#pragma once

// OpenGL implementation of the RHI, on 4.5+ direct-state-access entry points.
//
// The impedance mismatch with Vulkan is mostly about *when* things happen.
// Vulkan records commands and validates state up front; GL executes
// immediately against a global context. So:
//   - RHICommandList methods issue GL calls straight away
//   - a "pipeline" is a program plus a bundle of state applied on bind
//   - a "resource set" is a deferred list of glBindBufferRange /
//     glBindTextureUnit calls, replayed when the set is bound
//   - BeginFrame/EndFrame have no swapchain to manage; EndFrame swaps buffers
//
// (set, binding) pairs collapse to GL's flat per-type namespace through the
// FlatBindingMap computed from shader reflection.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

struct GLFWwindow;

namespace RageV::GL
{
	using namespace RageV::RHI;

	class OpenGLDevice;

	class OpenGLBufferRHI final : public RHIBuffer
	{
	public:
		OpenGLBufferRHI(OpenGLDevice& device, const BufferDesc& desc);
		~OpenGLBufferRHI() override;

		void  Upload(const void* data, uint64_t size, uint64_t offset = 0) override;
		void* GetMappedPointer() override { return m_Mapped; }

		uint32_t GetHandle() const { return m_Handle; }

	private:
		uint32_t m_Handle = 0;
		void*    m_Mapped = nullptr;
	};

	class OpenGLSamplerRHI final : public RHISampler
	{
	public:
		explicit OpenGLSamplerRHI(const SamplerDesc& desc);
		~OpenGLSamplerRHI() override;

		uint32_t GetHandle() const { return m_Handle; }

	private:
		uint32_t m_Handle = 0;
	};

	class OpenGLTextureRHI final : public RHITexture
	{
	public:
		OpenGLTextureRHI(OpenGLDevice& device, const TextureDesc& desc);
		// Non-owning wrapper for render-target attachments.
		OpenGLTextureRHI(const TextureDesc& desc, uint32_t handle, bool owned);
		~OpenGLTextureRHI() override;

		void Upload(const void* data, uint64_t size) override;
		void UploadLayer(const void* data, uint64_t size, uint32_t layer) override;
		void GenerateMips() override;
		uint64_t GetImGuiHandle() override { return (uint64_t)m_Handle; }

		uint32_t GetHandle() const { return m_Handle; }

	private:
		uint32_t m_Handle = 0;
		bool     m_Owned = true;
	};

	class OpenGLShaderRHI final : public RHIShader
	{
	public:
		OpenGLShaderRHI(OpenGLDevice& device, const CompiledShader& compiled);
		~OpenGLShaderRHI() override;

		uint32_t GetProgram() const { return m_Program; }
		const FlatBindingMap& GetBindings() const { return m_Bindings; }
		// Backing store for the push-constant block, which SPIRV-Cross re-emits
		// as a uniform buffer. Zero when the shader declares none.
		uint32_t GetPushConstantBuffer() const { return m_PushConstantBuffer; }

	private:
		uint32_t       m_Program = 0;
		uint32_t       m_PushConstantBuffer = 0;
		FlatBindingMap m_Bindings;
	};

	class OpenGLPipelineRHI final : public RHIPipeline
	{
	public:
		OpenGLPipelineRHI(OpenGLDevice& device, const GraphicsPipelineDesc& desc);
		~OpenGLPipelineRHI() override;

		// Applies the program, the fixed-function state, and the vertex format.
		void Bind();

		uint32_t GetVertexArray() const { return m_VertexArray; }
		uint32_t GetProgram() const;
		const FlatBindingMap& GetBindings() const;
		uint32_t GetTopology() const { return m_GLTopology; }
		uint32_t GetPushConstantBuffer() const;

	private:
		void BuildVertexArray();

		uint32_t m_VertexArray = 0;
		uint32_t m_GLTopology = 0;
		VertexLayout m_ResolvedLayout;
	};

	class OpenGLResourceSetRHI final : public RHIResourceSet
	{
	public:
		OpenGLResourceSetRHI(OpenGLDevice& device, const Ref<OpenGLPipelineRHI>& pipeline, uint32_t set);
		~OpenGLResourceSetRHI() override = default;

		void SetUniformBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetStorageBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
							  uint64_t offset = 0, uint64_t range = 0) override;
		void SetTexture(uint32_t binding, const Ref<RHITexture>& texture,
						const Ref<RHISampler>& sampler, uint32_t arrayIndex = 0) override;
		void Commit() override;

		// Replays the accumulated bindings against the GL context.
		void Apply();

	private:
		struct BufferBinding
		{
			uint32_t Point = 0;
			uint32_t Buffer = 0;
			uint64_t Offset = 0;
			uint64_t Range = 0;
			// GL_UNIFORM_BUFFER or GL_SHADER_STORAGE_BUFFER. The two index
			// separate binding-point spaces, so the target has to travel with
			// the point or a storage buffer at point 0 would displace the
			// uniform buffer at point 0.
			uint32_t Target = 0;
		};
		struct TextureBinding
		{
			uint32_t Unit = 0;
			uint32_t Texture = 0;
			uint32_t Sampler = 0;
		};

		Ref<OpenGLPipelineRHI> m_Pipeline;
		std::vector<BufferBinding>  m_Buffers;
		std::vector<TextureBinding> m_Textures;
	};

	class OpenGLRenderTargetRHI final : public RHIRenderTarget
	{
	public:
		OpenGLRenderTargetRHI(OpenGLDevice& device, const RenderTargetDesc& desc);
		~OpenGLRenderTargetRHI() override;

		void Resize(uint32_t width, uint32_t height) override;
		Ref<RHITexture> GetColorTexture(uint32_t index = 0) const override;
		Ref<RHITexture> GetDepthTexture() const override { return m_Depth; }

		uint32_t GetFramebuffer() const { return m_Framebuffer; }

	private:
		void Build();
		void Destroy();

		OpenGLDevice& m_Device;
		uint32_t m_Framebuffer = 0;
		std::vector<Ref<OpenGLTextureRHI>> m_Color;
		Ref<OpenGLTextureRHI> m_Depth;
	};

	class OpenGLCommandListRHI final : public RHICommandList
	{
	public:
		explicit OpenGLCommandListRHI(OpenGLDevice& device);

		void BeginRenderPass(const RenderPassBeginInfo& info) override;
		void EndRenderPass() override;
		void SetViewport(const Viewport& viewport) override;
		void SetScissor(const Rect2D& scissor) override;
		void BindPipeline(const Ref<RHIPipeline>& pipeline) override;
		void BindResourceSet(uint32_t set, const Ref<RHIResourceSet>& resources) override;
		void BindVertexBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer, uint64_t offset = 0) override;
		void BindIndexBuffer(const Ref<RHIBuffer>& buffer, IndexType type, uint64_t offset = 0) override;
		void PushConstants(ShaderStage stages, uint32_t offset, uint32_t size, const void* data) override;
		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
				  uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
						 uint32_t firstIndex = 0, int32_t vertexOffset = 0,
						 uint32_t firstInstance = 0) override;
		void WriteTimestamp(uint32_t slot) override;
		void GenerateMips(const Ref<RHITexture>& texture) override;
		void CopyToTextureLayer(const Ref<RHITexture>& source,
								const Ref<RHITexture>& destination,
								uint32_t layer, uint32_t mip = 0) override;

		void PushDebugGroup(const char* name) override;
		void PopDebugGroup() override;

	private:
		OpenGLDevice&      m_Device;
		OpenGLPipelineRHI* m_BoundPipeline = nullptr;
		uint32_t m_IndexType = 0;
		uint64_t m_IndexOffset = 0;

		// Framebuffers kept for CopyToTextureLayer's blit. Created on first use
		// and reattached each time; a blit needs both ends bound, and GL has no
		// image-to-image copy that can also flip.
		uint32_t m_CopyRead = 0;
		uint32_t m_CopyDraw = 0;
	};

	class OpenGLDevice final : public RHIDevice
	{
	public:
		explicit OpenGLDevice(const DeviceDesc& desc);
		~OpenGLDevice() override;

		Backend GetBackend() const override { return Backend::OpenGL; }
		const DeviceCaps& GetCaps() const override { return m_Caps; }

		RHICommandList* BeginFrame() override;
		void EndFrame() override;
		void WaitIdle() override;
		void RequestCapture(CaptureCallback callback) override;
		void OnResize(uint32_t width, uint32_t height) override;
		void SetVSync(bool enabled) override;

		// GL has no frames-in-flight concept the application must manage; the
		// driver pipelines internally. Reporting 1 keeps per-frame resource
		// arrays (uniform buffers, resource sets) correctly sized.
		uint32_t GetFramesInFlight() const override { return 1; }
		uint32_t GetFrameIndex() const override { return 0; }

		Format GetSwapchainFormat() const override { return Format::B8G8R8A8_UNORM; }
		Format GetSwapchainDepthFormat() const override { return Format::D24_UNORM_S8_UINT; }
		uint32_t GetSwapchainWidth()  const override { return m_Width; }
		uint32_t GetSwapchainHeight() const override { return m_Height; }

		Ref<RHIBuffer>       CreateBuffer(const BufferDesc& desc) override;
		Ref<RHITexture>      CreateTexture(const TextureDesc& desc) override;
		Ref<RHISampler>      CreateSampler(const SamplerDesc& desc) override;
		Ref<RHIShader>       CreateShader(const CompiledShader& compiled) override;
		Ref<RHIPipeline>     CreatePipeline(const GraphicsPipelineDesc& desc) override;
		Ref<RHIRenderTarget> CreateRenderTarget(const RenderTargetDesc& desc) override;
		Ref<RHIResourceSet>  CreateResourceSet(const Ref<RHIPipeline>& pipeline, uint32_t set) override;

		GLFWwindow* GetWindow() const { return m_Window; }

		// GL timestamps are already nanoseconds.
		double GetTimestampPeriodNs() const override { return 1.0; }
		const std::vector<uint64_t>& GetResolvedTimestamps() const override { return m_ResolvedTicks; }
		const std::vector<uint8_t>& GetResolvedTimestampFlags() const override { return m_ResolvedWritten; }

		// Called by the command list. Records into the ring half this frame is
		// writing, and notes the slot so the readback knows it holds anything.
		void RecordTimestamp(uint32_t slot);

	private:
		void QueryCaps();
		void CreateTimestampQueries();
		// Reads the half written last frame and swaps. GL has one queue and no
		// frames in flight, so the ring is what keeps the readback from being a
		// stall: last frame's results are ready, this frame's are not.
		void RecycleTimestampQueries();

		GLFWwindow* m_Window = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_VSync = true;
		DeviceCaps m_Caps;
		Scope<OpenGLCommandListRHI> m_CommandList;

		// Armed by RequestCapture, consumed and cleared by the next EndFrame.
		CaptureCallback m_Capture;

		// Two halves: one being written, one being read.
		std::vector<uint32_t> m_TimestampQueries[2];
		std::vector<uint8_t>  m_TimestampWritten[2];
		uint32_t m_TimestampRing = 0;
		bool m_TimestampsSupported = false;

		std::vector<uint64_t> m_ResolvedTicks;
		std::vector<uint8_t>  m_ResolvedWritten;
	};
}
