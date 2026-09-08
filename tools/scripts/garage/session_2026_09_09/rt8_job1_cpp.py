# -*- coding: utf-8 -*-
"""RT-8 job 1, the engine half: DirectTrace over the sea's layer.

The same pass, compiled once more with RV_DIRECT_WATER, reading the water
surface layer in the four slots the G-buffer uses. Everything the two share --
the reservoir, the shadow ray, the field handling, the light walk -- is shared
in fact rather than kept as a second copy.
"""
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
"""		static bool CanTraceDirectLight();""",
"""		static bool CanTraceDirectLight();
		// RT-8 job 1: and whether the sea can take the same route.
		static bool CanTraceDirectWater();
		// **RT-8 job 1: the direct light over the sea's own layer.** Its
		// position where a depth would be -- the sea writes no depth and the
		// buffer under it holds the seabed -- its normal with the RMS slope
		// and the wind angle beside it, its albedo with the specular dial.
		static void TraceDirectWater(RHI::RHICommandList& cmd,
									 const RHI::Ref<RHI::RHITexture>& position,
									 const RHI::Ref<RHI::RHITexture>& surface,
									 const RHI::Ref<RHI::RHITexture>& material,
									 RHI::Format targetColor,
									 const GiTraceView& view, int rays);""",
    'declarations'),
])

patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""			Ref<RHIShader>   DirectShader;
			Ref<RHIPipeline> DirectPipeline;""",
"""			Ref<RHIShader>   DirectShader;
			Ref<RHIPipeline> DirectPipeline;
			// RT-8 job 1: the same shader again, over the sea's layer and with
			// the sea's lobe. A second compile rather than a runtime branch,
			// because the branch would sit inside the light loop.
			Ref<RHIShader>   DirectWaterShader;
			Ref<RHIPipeline> DirectWaterPipeline;""",
    'members'),
(
"""		s_Data->DirectShader = nullptr;
		s_Data->DirectPipeline = nullptr;""",
"""		s_Data->DirectShader = nullptr;
		s_Data->DirectPipeline = nullptr;
		s_Data->DirectWaterShader = nullptr;
		s_Data->DirectWaterPipeline = nullptr;""",
    'reset'),
(
"""				if (auto direct = ShaderCompiler::CompileFromFile("assets/shaders/direct_trace.rvshader",
																 traceDefines))
				{
					s_Data->DirectShader = s_Data->Device->CreateShader(*direct);
				}""",
"""				if (auto direct = ShaderCompiler::CompileFromFile("assets/shaders/direct_trace.rvshader",
																 traceDefines))
				{
					s_Data->DirectShader = s_Data->Device->CreateShader(*direct);
				}
				// RT-8 job 1: and once more for the sea. A failure here leaves
				// the water passes doing their own choosing and shading, which
				// is what they did before -- so it is a warning, not an error.
				{
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
    'water compile'),
(
"""	bool Renderer3D::CanTraceDirectLight()
	{
		return s_Data && s_Data->DirectShader != nullptr;
	}""",
"""	bool Renderer3D::CanTraceDirectLight()
	{
		return s_Data && s_Data->DirectShader != nullptr;
	}

	bool Renderer3D::CanTraceDirectWater()
	{
		return s_Data && s_Data->DirectWaterShader != nullptr;
	}

	// **RT-8 job 1: the sea's direct light, through the pass every other
	// surface uses.**
	//
	// The sea kept its own copy of this -- `water_choose` scores every lamp
	// that reaches a patch and keeps four by reservoir sampling, `water_shade`
	// shades them and traces their shadow rays -- and DirectTrace has done
	// exactly that for the rest of the frame since RT-first T5, with the same
	// reservoir and the same ray. The one thing that is genuinely the sea's is
	// the lobe, and that is a branch inside DirectTerm rather than a pass.
	//
	// The four slots carry the sea's layer instead of the G-buffer's: position
	// where the depth would be, the normal with the RMS slope and the wind
	// angle, the albedo with the specular dial, and the id slot unused because
	// a sea is one surface, never static, with nothing baked into it.
	void Renderer3D::TraceDirectWater(RHICommandList& cmd,
									  const Ref<RHITexture>& position,
									  const Ref<RHITexture>& surface,
									  const Ref<RHITexture>& material,
									  Format targetColor,
									  const GiTraceView& view, int rays)
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
		slot.DirectWaterInputs->Commit();

		struct DirectParams
		{
			float NearClip, FarClip, InvP0, InvP1;
			float FlipY, Rays, Frame, Animated;
			Vec4  CameraRow0, CameraRow1, CameraRow2, CameraPosition;
		} params{};
		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		params.FlipY = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		params.Rays = (float)Math::Clamp(rays, 0, 8);
		params.Frame = s_Data->Scene.GlobalIllumination.y;
		const Vec4& jitter = s_Data->Scene.Jitter;
		params.Animated = (jitter.x != 0.0f || jitter.y != 0.0f || jitter.z != 0.0f || jitter.w != 0.0f)
						? 1.0f : 0.0f;
		const Mat4 camera = Math::Inverse(view.View);
		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0], 0.0f);
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], 0.0f);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], 0.0f);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], 0.0f);

		cmd.BindPipeline(s_Data->DirectWaterPipeline);
		cmd.BindResourceSet(0, slot.LampSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, slot.DirectWaterInputs);
		cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd.Draw(3);
	}""",
    'TraceDirectWater'),
])

# The resource-set slot beside the one it copies.
src = io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
old = '\t\t\t\tRef<RHIResourceSet> DirectInputs;'
if s.count(old) != 1:
    sys.exit('DirectInputs matched %d' % s.count(old))
s = s.replace(old, old + '\n\t\t\t\t// RT-8 job 1: the same four slots, carrying the sea\'s layer.\n'
                          '\t\t\t\tRef<RHIResourceSet> DirectWaterInputs;', 1)
io.open(r'RageV/src/RageV/Renderer/Renderer3D.cpp', 'w', encoding='utf-8',
        newline=CRLF if crlf else LF).write(s)
print('DirectWaterInputs added')
