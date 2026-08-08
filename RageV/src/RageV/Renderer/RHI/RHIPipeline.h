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
}
