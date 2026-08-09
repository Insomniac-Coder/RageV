#include <rvpch.h>
#include "RenderGraph.h"
#include "RageV/Core/Log.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		bool SameShape(const RGTargetDesc& a, const RGTargetDesc& b)
		{
			return a.Color == b.Color && a.Depth == b.Depth &&
				   a.SampleDepth == b.SampleDepth && a.Layers == b.Layers;
		}
	}

	// -------------------------------------------------------------------------
	// Builder
	// -------------------------------------------------------------------------
	void RGPassBuilder::Write(RGResource target, RGLoad load)
	{
		RenderGraph::Pass& pass = m_Graph.m_Passes[m_Pass];

		if (pass.Output != kRGInvalid)
		{
			m_Graph.m_Errors.push_back("pass '" + pass.Name + "' writes more than one target");
			return;
		}

		if (target >= m_Graph.m_Resources.size())
		{
			m_Graph.m_Errors.push_back("pass '" + pass.Name + "' writes an unknown target");
			return;
		}

		pass.Output = target;
		pass.Load = load;

		// Preserve means a previous pass put something there worth keeping, so
		// it does not count as producing the resource -- otherwise a chain of
		// passes that all preserve would never be caught reading nothing.
		if (load == RGLoad::Clear)
			m_Graph.m_Resources[target].Written = true;
	}

	void RGPassBuilder::Sample(RGResource target)
	{
		RenderGraph::Pass& pass = m_Graph.m_Passes[m_Pass];

		if (target >= m_Graph.m_Resources.size())
		{
			m_Graph.m_Errors.push_back("pass '" + pass.Name + "' samples an unknown target");
			return;
		}

		// Checked here rather than in Compile, so the message can name the pass
		// that did it while the pass is still the thing being described.
		RenderGraph::Resource& resource = m_Graph.m_Resources[target];
		if (!resource.Written && !resource.Imported)
		{
			m_Graph.m_Errors.push_back("pass '" + pass.Name + "' samples '" + resource.Name +
									   "', which no earlier pass has written");
		}

		resource.Read = true;
		pass.Samples.push_back(target);
	}

	void RGPassBuilder::SetClearColor(const Vec4& color)
	{
		m_Graph.m_Passes[m_Pass].ClearColor = color;
	}

	void RGPassBuilder::SetClearDepth(float depth)
	{
		m_Graph.m_Passes[m_Pass].ClearDepth = depth;
	}

	void RGPassBuilder::DisableDepth()
	{
		m_Graph.m_Passes[m_Pass].UseDepth = false;
	}

	// -------------------------------------------------------------------------
	// Context
	// -------------------------------------------------------------------------
	Ref<RHITexture> RGPassContext::Color(RGResource target) const
	{
		if (!Graph)
			return nullptr;

		const RenderGraph::Pass& pass = Graph->m_Passes[Pass];
		const auto& samples = pass.Samples;

		// Undeclared reads return nothing rather than working by accident. A
		// read that works without being declared is a dependency the graph
		// cannot see, and the first thing to break when passes are reordered.
		if (std::find(samples.begin(), samples.end(), target) == samples.end())
		{
			RV_CORE_WARN("Pass '{0}' asked for a target it did not declare with Sample()",
						 pass.Name);
			return nullptr;
		}

		const RenderGraph::Resource& resource = Graph->m_Resources[target];
		return resource.Target ? resource.Target->GetColorTexture(0) : nullptr;
	}

	Ref<RHITexture> RGPassContext::Depth(RGResource target) const
	{
		if (!Graph)
			return nullptr;

		const RenderGraph::Pass& pass = Graph->m_Passes[Pass];
		const auto& samples = pass.Samples;

		if (std::find(samples.begin(), samples.end(), target) == samples.end())
		{
			RV_CORE_WARN("Pass '{0}' asked for a depth target it did not declare with Sample()",
						 pass.Name);
			return nullptr;
		}

		const RenderGraph::Resource& resource = Graph->m_Resources[target];
		return resource.Target ? resource.Target->GetDepthTexture() : nullptr;
	}

	// -------------------------------------------------------------------------
	// Graph
	// -------------------------------------------------------------------------
	RenderGraph::RenderGraph(RHIDevice& device)
		: m_Device(device)
	{
	}

	RenderGraph::~RenderGraph() = default;

	void RenderGraph::Begin(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;

		m_Passes.clear();
		m_Resources.clear();
		m_Errors.clear();
		m_Compiled = false;

		for (Pooled& pooled : m_Pool)
			pooled.InUse = false;

		// Resource 0 is always the swapchain, so Backbuffer() needs no lookup
		// and no special case anywhere else.
		Resource backbuffer;
		backbuffer.Name = "Backbuffer";
		backbuffer.Imported = true;
		backbuffer.IsBackbuffer = true;
		m_Resources.push_back(std::move(backbuffer));
	}

	RGResource RenderGraph::Import(const Ref<RHIRenderTarget>& target, const char* name)
	{
		Resource resource;
		resource.Name = name ? name : "imported";
		resource.Imported = true;
		resource.Target = target;
		// Imported targets carry whatever the previous frame left in them, so
		// reading one before this frame writes it is legitimate.
		resource.Written = true;

		m_Resources.push_back(std::move(resource));
		return (RGResource)(m_Resources.size() - 1);
	}

	RGResource RenderGraph::CreateTarget(const RGTargetDesc& desc)
	{
		Resource resource;
		resource.Name = desc.Name.empty() ? "target" : desc.Name;
		resource.Desc = desc;

		m_Resources.push_back(std::move(resource));
		return (RGResource)(m_Resources.size() - 1);
	}

	void RenderGraph::AddPass(const char* name, PassSetup setup, PassExecute execute)
	{
		Pass pass;
		pass.Name = name ? name : "pass";
		pass.SetupFn = std::move(setup);
		pass.ExecuteFn = std::move(execute);

		m_Passes.push_back(std::move(pass));

		// Run immediately rather than in Compile, so declarations are made in
		// the order the passes were added and a read can be checked against
		// what has been written so far.
		RGPassBuilder builder(*this, m_Passes.size() - 1);
		if (m_Passes.back().SetupFn)
			m_Passes.back().SetupFn(builder);
	}

	Ref<RHIRenderTarget> RenderGraph::AcquireTarget(const RGTargetDesc& desc,
													uint32_t width, uint32_t height)
	{
		for (Pooled& pooled : m_Pool)
		{
			if (pooled.InUse || !SameShape(pooled.Desc, desc))
				continue;

			pooled.InUse = true;

			// Resized here, during Compile, and never while passes record --
			// resizing a target a command buffer has bound destroys images it
			// is still holding.
			if (pooled.Width != width || pooled.Height != height)
			{
				pooled.Target->Resize(width, height);
				pooled.Width = width;
				pooled.Height = height;
			}

			return pooled.Target;
		}

		RenderTargetDesc target;
		target.Width = width;
		target.Height = height;
		target.Layers = desc.Layers;
		target.DebugName = "rg." + desc.Name;

		if (desc.Color != Format::Undefined)
			target.ColorAttachments = { { desc.Color } };

		target.HasDepth = desc.Depth != Format::Undefined;
		target.DepthAttachment.Format = desc.Depth;
		target.DepthSampled = desc.SampleDepth;

		Pooled pooled;
		pooled.Desc = desc;
		pooled.Width = width;
		pooled.Height = height;
		pooled.Target = m_Device.CreateRenderTarget(target);
		pooled.InUse = true;

		m_Pool.push_back(pooled);
		return pooled.Target;
	}

	bool RenderGraph::Compile()
	{
		if (m_Passes.empty())
			m_Errors.push_back("the graph has no passes");

		for (const Pass& pass : m_Passes)
		{
			if (pass.Output == kRGInvalid)
				m_Errors.push_back("pass '" + pass.Name + "' writes nothing");
		}

		// A target created and never drawn into is either a mistake or dead
		// weight. Worth reporting rather than silently allocating.
		for (size_t i = 1; i < m_Resources.size(); i++)
		{
			const Resource& resource = m_Resources[i];
			if (!resource.Imported && !resource.Written)
			{
				m_Errors.push_back("target '" + resource.Name +
								   "' is created but nothing writes it");
			}
		}

		if (!m_Errors.empty())
			return false;

		// Allocation happens only once the frame is known to be valid, so a
		// rejected frame does not leave targets behind it.
		for (Resource& resource : m_Resources)
		{
			if (resource.Imported)
				continue;

			const uint32_t width = resource.Desc.Width != 0
								 ? resource.Desc.Width
								 : (uint32_t)Math::Max(1.0f, (float)m_Width * resource.Desc.Scale);
			const uint32_t height = resource.Desc.Height != 0
								  ? resource.Desc.Height
								  : (uint32_t)Math::Max(1.0f, (float)m_Height * resource.Desc.Scale);

			resource.Target = AcquireTarget(resource.Desc, width, height);
		}

		m_Compiled = true;
		return true;
	}

	void RenderGraph::Execute(RHICommandList& cmd)
	{
		if (!m_Compiled)
		{
			RV_CORE_ERROR("RenderGraph::Execute before a successful Compile");
			return;
		}

		for (size_t i = 0; i < m_Passes.size(); i++)
		{
			Pass& pass = m_Passes[i];
			Resource& output = m_Resources[pass.Output];

			RenderPassBeginInfo begin;
			begin.Target = output.IsBackbuffer ? nullptr : output.Target.get();
			begin.ClearColor = pass.Load == RGLoad::Clear;
			begin.ClearDepth = pass.Load == RGLoad::Clear;
			begin.UseDepth = pass.UseDepth;
			begin.Clear.Color[0] = pass.ClearColor.r;
			begin.Clear.Color[1] = pass.ClearColor.g;
			begin.Clear.Color[2] = pass.ClearColor.b;
			begin.Clear.Color[3] = pass.ClearColor.a;
			begin.Clear.Depth = pass.ClearDepth;

			RGPassContext context{ cmd };
			context.Graph = this;
			context.Pass = i;

			if (output.IsBackbuffer)
			{
				context.Width = m_Device.GetSwapchainWidth();
				context.Height = m_Device.GetSwapchainHeight();
			}
			else if (output.Target)
			{
				context.Width = output.Target->GetWidth();
				context.Height = output.Target->GetHeight();
			}

			// Named in the capture, so a frame in RenderDoc reads the same way
			// the declaration does.
			cmd.PushDebugGroup(pass.Name.c_str());
			cmd.BeginRenderPass(begin);

			if (pass.ExecuteFn)
				pass.ExecuteFn(context);

			cmd.EndRenderPass();
			cmd.PopDebugGroup();
		}
	}

	const std::string& RenderGraph::GetPassName(size_t pass) const
	{
		static const std::string none;
		return pass < m_Passes.size() ? m_Passes[pass].Name : none;
	}

	void RenderGraph::GetTargetSize(RGResource target, uint32_t& width, uint32_t& height) const
	{
		width = 0;
		height = 0;

		if (target >= m_Resources.size() || !m_Resources[target].Target)
			return;

		width = m_Resources[target].Target->GetWidth();
		height = m_Resources[target].Target->GetHeight();
	}

	size_t RenderGraph::GetPooledTargetCount() const
	{
		return m_Pool.size();
	}
}
