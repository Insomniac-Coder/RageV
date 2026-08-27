#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"
#include <functional>
#include <string>
#include <vector>

namespace RageV
{
	// A frame described as passes and the targets they read and write, rather
	// than as a sequence of calls that happen to be in the right order.
	//
	// ## What this is for, and what it is not
	//
	// The usual argument for a render graph is derived barriers. That argument
	// does not apply here, and pretending it did would mean building a second
	// layout tracker on top of a working one: this RHI already transitions a
	// target's textures to a shader-readable layout when its pass ends, and
	// back to an attachment layout when one begins. Sampling a previous pass's
	// output is already correct without the graph knowing anything.
	//
	// What is *not* solved without it, and what this exists for:
	//
	//   - **Somebody has to own the intermediate targets.** Phase 3 adds an HDR
	//     buffer, a bloom chain, a BRDF LUT and shadow maps. Hanging each off
	//     whichever layer happened to need it first is how a renderer becomes
	//     impossible to reason about, and how two features end up with two
	//     half-shared copies of the same buffer.
	//
	//   - **A frame should be inspectable.** With passes declared, "what does
	//     this frame do, in what order, reading what" is a data structure
	//     rather than a reading exercise across three files.
	//
	//   - **Mistakes should fail loudly.** A pass that samples something no
	//     earlier pass wrote is a black screen at runtime and an error at
	//     compile time. Compile() returns the list.
	//
	// ## Deliberately absent
	//
	// **Aliasing** (reusing one allocation for two targets whose lifetimes do
	// not overlap) and **reordering** (shuffling passes to overlap work) are
	// both in the literature and both out of scope. They are memory and
	// occupancy optimisations for frames far larger than this one, and each
	// needs lifetime analysis that is only correct if it is complete. When the
	// frame is big enough to want them, the declarations they need are already
	// here.
	//
	// See docs/ENGINE-NOTES.md section 6.

	using RGResource = uint32_t;
	constexpr RGResource kRGInvalid = ~0u;

	// A target the graph owns: colour attachments and one depth. A graph
	// resource is a target rather than a single attachment, because that is
	// the unit the RHI binds.
	struct RGTargetDesc
	{
		std::string Name;

		// Undefined means depth-only, which is what a shadow map is.
		RHI::Format Color = RHI::Format::R16G16B16A16_SFLOAT;

		// Attachments after the first. Almost always empty -- one colour and
		// one depth is what a pass wants.
		//
		// The exception is a technique whose passes write different
		// attachments of one image set over a shared depth buffer:
		// weighted-blended transparency accumulates into two of them while the
		// scene's colour sits in the first, and all three must depth-test
		// against the same buffer. Declaring them together here is what makes
		// that depth shared rather than three targets each with a depth of
		// their own.
		std::vector<RHI::Format> ExtraColors;

		// Undefined means no depth.
		RHI::Format Depth = RHI::Format::D32_SFLOAT;

		// Fraction of the frame's output size. Half-resolution bloom is 0.5;
		// a shadow map sets Width and Height instead and ignores this.
		float Scale = 1.0f;
		uint32_t Width = 0;
		uint32_t Height = 0;

		// Coverage samples per pixel. One is an ordinary target.
		//
		// A multisampled target's attachments cannot be sampled directly, so
		// the RHI keeps a single-sampled twin per colour attachment, resolves
		// into it when a pass ends, and hands *that* out -- which is why
		// nothing else in this file has to know. ENGINE-NOTES 7q.
		uint32_t Samples = 1;

		// Whether a later pass samples the depth. Colour is always sampleable;
		// depth costs an extra usage flag and is usually not read.
		bool SampleDepth = false;

		uint32_t Layers = 1;
	};

	enum class RGLoad
	{
		Clear,       // start from the clear value
		Preserve,    // keep what a previous pass left
	};

	class RenderGraph;

	// What the graph opens before a pass runs.
	//
	// The distinction is not cosmetic: a compute dispatch inside a render pass
	// is illegal on Vulkan, so a pass that computes must not begin one. That
	// is the entire difference, and it is why this is a kind rather than a
	// flag a graphics pass could also set.
	//
	// **Standalone** is that same rule arrived at from the other side. A pass
	// whose product is not an attachment -- a volume texture written through
	// imageStore -- still has to raster something to make fragments, and it
	// still has to fence those writes against the draws that read them. A
	// barrier may not be recorded inside a render pass either, so a pass that
	// needs both has to own the render pass it draws in. The graph opens
	// nothing for it and asks no target of it, exactly as with a compute pass.
	enum class RGPassKind
	{
		Graphics,
		Compute,
		Standalone,
	};

	// What a pass declares about itself, during setup.
	// One colour attachment a pass binds, and what it starts from.
	struct RGAttachment
	{
		uint32_t Index = 0;
		Vec4 Clear{ 0.0f, 0.0f, 0.0f, 1.0f };
	};

	class RGPassBuilder
	{
	public:
		// Draw into this target. A pass writes exactly one, binding every
		// colour attachment it has.
		void Write(RGResource target, RGLoad load = RGLoad::Clear);

		// Draw into some of a target's colour attachments, each starting from
		// its own value.
		//
		// The subset is not an optimisation. A pipeline's declared colour
		// formats must match what the pass binds, so a target carrying three
		// attachments cannot be written by pipelines that declare one --
		// which is every pipeline that draws the scene. Binding a subset is
		// what lets one target with one depth buffer serve both.
		void WriteAttachments(RGResource target, const std::vector<RGAttachment>& attachments,
							  RGLoad load = RGLoad::Clear);

		// Read this target's colour, or depth, in a shader. Declaring it is
		// what lets Compile catch a pass reading something nothing produced --
		// and what makes the frame's shape visible without reading the passes.
		void Sample(RGResource target);

		void SetClearColor(const Vec4& color);
		void SetClearDepth(float depth);

		// Skip the depth attachment even though the target has one. The UI
		// pass does this: a pass that declares depth requires every pipeline
		// drawn in it to declare a matching depth format.
		void DisableDepth();

		// Keep the depth a previous pass wrote while still clearing this
		// pass's colour attachments.
		//
		// RGLoad says one thing about both, which is right until a pass wants
		// fresh colour over existing depth -- a transparent pass needs its
		// accumulation targets cleared and the opaque depth intact, or it
		// either accumulates last frame's particles or stops being occluded
		// by the scene.
		void PreserveDepth();

	private:
		friend class RenderGraph;
		explicit RGPassBuilder(RenderGraph& graph, size_t pass)
			: m_Graph(graph), m_Pass(pass) {}

		RenderGraph& m_Graph;
		size_t m_Pass;
	};

	// What a pass is handed when it runs.
	struct RGPassContext
	{
		RHI::RHICommandList& Cmd;

		// The size of the target being drawn into, which is not always the
		// frame's: a half-resolution pass wants its own dimensions.
		uint32_t Width = 0;
		uint32_t Height = 0;

		// Textures of a target declared with Sample. Null if it was not
		// declared, rather than silently returning something -- an undeclared
		// read is the bug this is trying to make visible.
		//
		// `attachment` picks among a multi-attachment target's colours; zero
		// is the only one most targets have.
		RHI::Ref<RHI::RHITexture> Color(RGResource target, uint32_t attachment = 0) const;
		RHI::Ref<RHI::RHITexture> Depth(RGResource target) const;

		const RenderGraph* Graph = nullptr;
		size_t Pass = 0;
	};

	class RenderGraph
	{
	public:
		using PassSetup = std::function<void(RGPassBuilder&)>;
		using PassExecute = std::function<void(RGPassContext&)>;

		explicit RenderGraph(RHI::RHIDevice& device);

		// Names this graph in GPU breadcrumbs. The editor records two graphs
		// into one command buffer with identical pass names; without this a
		// post-mortem cannot say which of the two "SSAO" passes died.
		void SetName(std::string name) { m_Name = std::move(name); }
		~RenderGraph();

		// Starts a new frame's description. `width` and `height` are the size
		// scaled targets are relative to -- the window, or the viewport panel.
		//
		// Targets are allocated and resized here rather than while passes run,
		// because resizing a target that a command buffer has already bound
		// destroys images it is holding.
		void Begin(uint32_t width, uint32_t height);

		// The swapchain. Always available, never owned.
		RGResource Backbuffer() const { return kBackbuffer; }

		// A target somebody else owns -- the editor's viewport image, which
		// ImGui samples and so must outlive the frame.
		RGResource Import(const RHI::Ref<RHI::RHIRenderTarget>& target, const char* name);

		// A target the graph owns, pooled across frames and resized with the
		// output. Identical descs in the same frame produce separate targets:
		// two passes wanting "a half-res buffer" want two of them.
		RGResource CreateTarget(const RGTargetDesc& desc);

		void AddPass(const char* name, PassSetup setup, PassExecute execute);

		// A pass that dispatches instead of drawing.
		//
		// It declares `Sample` for whatever it reads and writes no target, so
		// it is the one kind of pass for which "writes nothing" is correct
		// rather than a mistake -- its output is a buffer, which the graph does
		// not pool and does not need to know about.
		//
		// It runs where it is declared, like every other pass, and it does
		// **not** begin a render pass: a dispatch inside one is illegal on
		// Vulkan. That is the whole of the difference.
		//
		// This exists because the alternative is worse. The thing a compute
		// pass usually wants to read is a target the graph owns and pools, and
		// reaching for one from outside the graph is exactly what the graph
		// exists to stop. ENGINE-NOTES 7y.
		void AddComputePass(const char* name, PassSetup setup, PassExecute execute);

		// A pass that records its own render passes, and its own barriers.
		//
		// The graph begins nothing for it, so it runs at a point where a
		// barrier is legal -- which is the whole reason it exists. The
		// irradiance fill is the case it was added for: it rasters one
		// fragment per cell of a volume texture and writes the cells with
		// imageStore, so the attachment it draws into is a rasteriser driver
		// nothing reads, while what it produces is a texture the graph does
		// not own and could not pool.
		//
		// It declares Sample for whatever it reads, writes no target, and is
		// otherwise ordinary: it runs where it is declared. What comes with
		// it is an obligation -- **the pass owns its own synchronisation**,
		// because nothing else knows it wrote anything.
		void AddStandalonePass(const char* name, PassSetup setup, PassExecute execute);

		// Validates and allocates. False means the frame is not runnable and
		// Errors() says why. Nothing is recorded on failure -- a half-executed
		// frame is harder to diagnose than one that refused.
		bool Compile();
		const std::vector<std::string>& Errors() const { return m_Errors; }

		void Execute(RHI::RHICommandList& cmd);

		// For tests and a future frame inspector.
		size_t GetPassCount() const { return m_Passes.size(); }
		const std::string& GetPassName(size_t pass) const;
		size_t GetTargetCount() const { return m_Resources.size(); }
		// Pooled targets currently alive, across all frames.
		size_t GetPooledTargetCount() const;

		// The resolved size of a target after Compile. Zero before it, and for
		// the backbuffer, whose size belongs to the swapchain.
		void GetTargetSize(RGResource target, uint32_t& width, uint32_t& height) const;

	private:
		friend class RGPassBuilder;
		friend struct RGPassContext;

		static constexpr RGResource kBackbuffer = 0;

		struct Resource
		{
			std::string Name;
			RGTargetDesc Desc;
			bool Imported = false;
			bool IsBackbuffer = false;

			// Resolved during Compile.
			RHI::Ref<RHI::RHIRenderTarget> Target;

			// Which pass last wrote it, for validating reads.
			bool Written = false;
			bool Read = false;
		};

		struct Pass
		{
			std::string Name;
			PassSetup SetupFn;
			PassExecute ExecuteFn;
			RGPassKind Kind = RGPassKind::Graphics;

			RGResource Output = kRGInvalid;
			RGLoad Load = RGLoad::Clear;
			std::vector<RGResource> Samples;
			// Empty binds every colour attachment the target has.
			std::vector<RGAttachment> Attachments;

			Vec4 ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
			float ClearDepth = RHI::kDepthClear;
			bool UseDepth = true;
			// Set by PreserveDepth: clear colour, keep depth.
			bool KeepDepth = false;
		};

		void AddPassOfKind(const char* name, RGPassKind kind,
						   PassSetup setup, PassExecute execute);

		RHI::Ref<RHI::RHIRenderTarget> AcquireTarget(const RGTargetDesc& desc,
													 uint32_t width, uint32_t height);

		RHI::RHIDevice& m_Device;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		std::vector<Resource> m_Resources;
		std::vector<Pass> m_Passes;
		std::vector<std::string> m_Errors;
		bool m_Compiled = false;
		std::string m_Name;

		// "graph/pass" per pass index, built once and reused.
		//
		// The frame loop used to compose this string every pass of every
		// frame -- an allocation and a concat, thirty-odd times, paid by the
		// code whose job is to measure the frame. The passes are rebuilt each
		// frame but their names are the same ones in the same order, so a
		// compare against the cached name is enough to know the composed form
		// still stands.
		struct PassScopeName
		{
			std::string PassName;
			std::string Scope;
			// Resolved lazily: pass timings can be switched on at runtime from
			// the editor, and an id taken while they were off is kNoPass.
			uint32_t Id = 0xFFFFFFFFu;
		};
		std::vector<PassScopeName> m_ScopeNames;
		// The graph name the cache above was composed against.
		std::string m_ScopeNamesFor;

		// Kept between frames so a stable frame allocates nothing after the
		// first. Keyed by description and size.
		struct Pooled
		{
			RGTargetDesc Desc;
			uint32_t Width = 0;
			uint32_t Height = 0;
			RHI::Ref<RHI::RHIRenderTarget> Target;
			bool InUse = false;
		};
		std::vector<Pooled> m_Pool;
	};
}
