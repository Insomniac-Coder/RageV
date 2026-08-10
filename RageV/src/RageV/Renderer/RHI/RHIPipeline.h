#pragma once

// A pipeline bakes shaders, vertex layout and fixed-function state into one
// immutable object. This is the piece the old RenderAPI could not express:
// state was set one call at a time against a global context, which Vulkan has
// no equivalent for.

#include "RHITypes.h"
#include "RHIShader.h"

namespace RageV::RHI
{
	struct GraphicsPipelineDesc
	{
		std::string          Name;
		Ref<RHIShader>       Shader;
		VertexLayout         VertexInput;
		PrimitiveTopology    Topology    = PrimitiveTopology::TriangleList;
		RasterizerState      Rasterizer  = {};
		DepthStencilState    DepthStencil = {};
		BlendPreset          Blend       = BlendPreset::AlphaBlend;

		// Formats of the attachments this pipeline renders into. Vulkan needs
		// them at creation time; OpenGL ignores them.
		std::vector<Format>  ColorFormats;
		Format               DepthFormat = Format::D24_UNORM_S8_UINT;
		uint32_t             Samples     = 1;
	};

	class RHIPipeline
	{
	public:
		virtual ~RHIPipeline() = default;

		const GraphicsPipelineDesc& GetDesc() const { return m_Desc; }
		const ShaderReflection& GetReflection() const { return m_Desc.Shader->GetReflection(); }

	protected:
		explicit RHIPipeline(GraphicsPipelineDesc desc) : m_Desc(std::move(desc)) {}
		GraphicsPipelineDesc m_Desc;
	};

	// A compute pipeline is a shader and nothing else.
	//
	// Deliberately not a GraphicsPipelineDesc with the graphics half left at
	// its defaults: there is no vertex layout, no topology, no raster state,
	// no blend and no attachment to name, and a desc carrying a dozen fields
	// that mean nothing invites someone to set one and wonder why it had no
	// effect.
	//
	// The work group size is not here either -- it belongs to the shader,
	// which declares it in `layout(local_size_x = ...)`. Repeating it on this
	// side would be a second place for it to be wrong.
	struct ComputePipelineDesc
	{
		std::string    Name;
		Ref<RHIShader> Shader;
	};

	class RHIComputePipeline
	{
	public:
		virtual ~RHIComputePipeline() = default;

		const ComputePipelineDesc& GetDesc() const { return m_Desc; }
		const ShaderReflection& GetReflection() const { return m_Desc.Shader->GetReflection(); }

		// What the shader declared as its local size. A dispatch is in
		// *groups*, so this is what a caller divides its element count by --
		// and reading it from the shader is what stops the two disagreeing
		// after an edit.
		uint32_t GetWorkGroupSizeX() const
		{
			return m_Desc.Shader->GetReflection().LocalSize[0];
		}

		// Groups needed to cover `elements` invocations, rounding up. The
		// shader must still bounds-check: the last group runs full and its
		// tail addresses elements that do not exist.
		uint32_t GroupsFor(uint32_t elements) const
		{
			const uint32_t size = GetWorkGroupSizeX() ? GetWorkGroupSizeX() : 1;
			return (elements + size - 1) / size;
		}

	protected:
		explicit RHIComputePipeline(ComputePipelineDesc desc) : m_Desc(std::move(desc)) {}
		ComputePipelineDesc m_Desc;
	};
}
