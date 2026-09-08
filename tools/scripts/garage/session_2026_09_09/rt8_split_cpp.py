# -*- coding: utf-8 -*-
"""RT-8 job 1: the engine side of the split -- three modes of one shader."""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)


def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = CRLF in src
    s = src.replace(CRLF, LF)
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
    print(p, 'patched')


patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
(
"""		static void TraceDirectWater(RHI::RHICommandList& cmd,""",
"""		// **RT-8 job 1: which half of the sea's direct light this call is.**
		//
		// Fused does both jobs at once, which is what the pass did when it was
		// one pass and what it still does at full resolution. Choose walks the
		// cluster list and keeps K lamps by reservoir sampling; Shade reads
		// that and shades them. The split exists because the picking is the
		// expensive half and it does not need to run at every pixel, while the
		// shading does -- which is exactly the shape the sea's own two passes
		// have always had, and why they are cheap.
		enum class DirectWaterMode { Fused, Choose, Shade };
		static void TraceDirectWater(RHI::RHICommandList& cmd,""",
    'mode enum'),
(
"""									 // choice has always been made on.
									 int block = 1);""",
"""									 // choice has always been made on.
									 int block = 1,
									 DirectWaterMode mode = DirectWaterMode::Fused,
									 // Shade only: what Choose decided.
									 const RHI::Ref<RHI::RHITexture>& choice = nullptr,
									 const RHI::Ref<RHI::RHITexture>& worth = nullptr);""",
    'signature'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""			Ref<RHIShader>   DirectWaterShader;
			Ref<RHIPipeline> DirectWaterPipeline;""",
"""			// RT-8 job 1: one per mode -- fused, choose, shade.
			Ref<RHIShader>   DirectWaterShader[3];
			Ref<RHIPipeline> DirectWaterPipeline[3];""",
    'members'),
(
"""		s_Data->DirectWaterShader = nullptr;
		s_Data->DirectWaterPipeline = nullptr;""",
"""		for (int mode = 0; mode < 3; ++mode)
		{
			s_Data->DirectWaterShader[mode] = nullptr;
			s_Data->DirectWaterPipeline[mode] = nullptr;
		}""",
    'reset'),
(
"""				{
					std::vector<std::string> waterDefines = traceDefines;
					waterDefines.push_back("RV_DIRECT_WATER");
					if (auto sea = ShaderCompiler::CompileFromFile(
							"assets/shaders/direct_trace.rvshader", waterDefines))
					{
						s_Data->DirectWaterShader = s_Data->Device->CreateShader(*sea);
					}
					else
					{
						RV_CORE_WARN("Renderer3D: direct_trace.rvshader did not compile for the "
									 "sea; the water keeps its own choose and shade passes");
					}
				}""",
"""				// One compile per mode, so there is one copy of the score, the
				// term, the visibility and the field handling rather than three.
				for (int mode = 0; mode < 3; ++mode)
				{
					std::vector<std::string> waterDefines = traceDefines;
					waterDefines.push_back("RV_DIRECT_WATER");
					if (mode == 1)
						waterDefines.push_back("RV_DIRECT_CHOOSE");
					else if (mode == 2)
						waterDefines.push_back("RV_DIRECT_SHADE");
					if (auto sea = ShaderCompiler::CompileFromFile(
							"assets/shaders/direct_trace.rvshader", waterDefines))
					{
						s_Data->DirectWaterShader[mode] = s_Data->Device->CreateShader(*sea);
					}
					else
					{
						RV_CORE_WARN("Renderer3D: direct_trace.rvshader did not compile for the "
									 "sea (mode {0}); the water keeps its own choose and shade "
									 "passes", mode);
					}
				}""",
    'compiles'),
(
"""	bool Renderer3D::CanTraceDirectWater()
	{
		return s_Data && s_Data->DirectWaterShader != nullptr;
	}""",
"""	bool Renderer3D::CanTraceDirectWater()
	{
		return s_Data && s_Data->DirectWaterShader[0] != nullptr
			&& s_Data->DirectWaterShader[1] != nullptr
			&& s_Data->DirectWaterShader[2] != nullptr;
	}""",
    'CanTrace'),
(
"""									  Format targetColor,
									  const GiTraceView& view, int rays, int block)
	{
		if (!s_Data || !s_Data->DirectWaterShader || !position || !surface || !material)
			return;
		if (!s_Data->ActiveScene)
			return;
		if (!s_Data->DirectWaterPipeline)
		{
			GraphicsPipelineDesc direct;
			direct.Name = "Renderer3D.direct.water";
			direct.Shader = s_Data->DirectWaterShader;
			direct.Topology = PrimitiveTopology::TriangleList;
			direct.Rasterizer.Cull = CullMode::None;
			direct.Blend = BlendPreset::Opaque;
			direct.DepthStencil.DepthTestEnable = false;
			direct.DepthStencil.DepthWriteEnable = false;
			direct.ColorFormats = { targetColor, targetColor };
			direct.BlendPerAttachment = { BlendPreset::Opaque, BlendPreset::Opaque };
			direct.DepthFormat = Format::Undefined;
			s_Data->DirectWaterPipeline = s_Data->Device->CreatePipeline(direct);
			for (auto& frame : s_Data->SceneSlots)
				for (auto& slot : frame)
					slot.DirectWaterInputs = nullptr;
		}
		if (!s_Data->DirectWaterPipeline)
			return;
		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!slot.LampSet)
			return;
		if (!slot.DirectWaterInputs)
			slot.DirectWaterInputs =
				s_Data->Device->CreateResourceSet(s_Data->DirectWaterPipeline, 3);
		if (!slot.DirectWaterInputs)
			return;
		slot.DirectWaterInputs->SetTexture(0, position, s_Data->PointSampler);
		slot.DirectWaterInputs->SetTexture(1, surface, s_Data->PointSampler);
		slot.DirectWaterInputs->SetTexture(2, material, s_Data->PointSampler);
		// Declared and unread: a declared binding left empty is a validation
		// error, and the sea has no id lane to put here.
		slot.DirectWaterInputs->SetTexture(3, surface, s_Data->PointSampler);
		slot.DirectWaterInputs->Commit();""",
"""									  Format targetColor,
									  const GiTraceView& view, int rays, int block,
									  DirectWaterMode mode,
									  const Ref<RHITexture>& choice,
									  const Ref<RHITexture>& worth)
	{
		const int slotIndex = (int)mode;
		if (!s_Data || !s_Data->DirectWaterShader[slotIndex]
			|| !position || !surface || !material)
		{
			return;
		}
		if (!s_Data->ActiveScene)
			return;
		// The shade half cannot invent a choice, and a validation layer would
		// see the empty binding before the picture did.
		if (mode == DirectWaterMode::Shade && (!choice || !worth))
			return;
		if (!s_Data->DirectWaterPipeline[slotIndex])
		{
			GraphicsPipelineDesc direct;
			direct.Name = mode == DirectWaterMode::Choose ? "Renderer3D.direct.water.choose"
						: mode == DirectWaterMode::Shade  ? "Renderer3D.direct.water.shade"
														  : "Renderer3D.direct.water";
			direct.Shader = s_Data->DirectWaterShader[slotIndex];
			direct.Topology = PrimitiveTopology::TriangleList;
			direct.Rasterizer.Cull = CullMode::None;
			direct.Blend = BlendPreset::Opaque;
			direct.DepthStencil.DepthTestEnable = false;
			direct.DepthStencil.DepthWriteEnable = false;
			// **The choice is two whole-integer attachments**: a lamp index is
			// not a thing to interpolate, and the reciprocal probability beside
			// it is the one number the estimate divides by.
			const Format written = mode == DirectWaterMode::Choose
								 ? Format::R32G32B32A32_UINT : targetColor;
			direct.ColorFormats = { written, written };
			direct.BlendPerAttachment = { BlendPreset::Opaque, BlendPreset::Opaque };
			direct.DepthFormat = Format::Undefined;
			s_Data->DirectWaterPipeline[slotIndex] = s_Data->Device->CreatePipeline(direct);
			for (auto& frame : s_Data->SceneSlots)
				for (auto& slot : frame)
					slot.DirectWaterInputs[slotIndex] = nullptr;
		}
		if (!s_Data->DirectWaterPipeline[slotIndex])
			return;
		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!slot.LampSet)
			return;
		if (!slot.DirectWaterInputs[slotIndex])
			slot.DirectWaterInputs[slotIndex] = s_Data->Device->CreateResourceSet(
				s_Data->DirectWaterPipeline[slotIndex], 3);
		if (!slot.DirectWaterInputs[slotIndex])
			return;
		const Ref<RHIResourceSet>& inputs = slot.DirectWaterInputs[slotIndex];
		inputs->SetTexture(0, position, s_Data->PointSampler);
		inputs->SetTexture(1, surface, s_Data->PointSampler);
		inputs->SetTexture(2, material, s_Data->PointSampler);
		// Declared and unread: a declared binding left empty is a validation
		// error, and the sea has no id lane to put here.
		inputs->SetTexture(3, surface, s_Data->PointSampler);
		if (mode == DirectWaterMode::Shade)
		{
			inputs->SetTexture(4, choice, s_Data->PointSampler);
			inputs->SetTexture(5, worth, s_Data->PointSampler);
		}
		inputs->Commit();""",
    'body'),
(
"""		cmd.BindPipeline(s_Data->DirectWaterPipeline);
		cmd.BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, slot.DirectWaterInputs);""",
"""		cmd.BindPipeline(s_Data->DirectWaterPipeline[slotIndex]);
		cmd.BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, inputs);""",
    'bind'),
(
"""				// RT-8 job 1: the same four slots, carrying the sea's layer.
				Ref<RHIResourceSet> DirectWaterInputs;""",
"""				// RT-8 job 1: the same four slots, carrying the sea's layer --
				// one set per mode, since the layouts differ by two bindings.
				Ref<RHIResourceSet> DirectWaterInputs[3];""",
    'slot member'),
])
