"""RT-2a (RT-13's first half, pulled forward because every pre-lit signal
needs it): the skinned and layered kinds are drawn into the G-buffer pass.
- Renderer3D: SkinnedGBufferShader / LayeredGBufferShader (the same files
  with RV_GBUFFER), their pipelines and sets, the sets' uploads beside the
  lit kinds', and DrawGBufferPending -- the pending draws (skinned, layered,
  and any static not culled on the GPU) drawn with the G-buffer pipelines
  in the split's G-buffer half.
- The lit shader's signal inputs no longer exclude the two kinds, and their
  sets carry the bindings again.
"""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

# ---------------------------------------------------------------- shader guard
p = 'RageVEditor/assets/shaders/include/pbr_fragment.glsl'; s, nl = load(p)
if 'since RT-2' in s:
    print('shader guard already patched')
else:
  s = rep(s, nl, """// pipeline does not reference cannot be written on its set. Not the skinned
// and layered kinds: the G-buffer pass does not draw them yet, so the pass
// has nothing for their pixels and they keep the loop -- found on the
// bridge's terrain (RT-1); drawing them into the G-buffer is the RT series'
// item for surfaces outside it.
#if defined(RV_RAY_SHADOWS)""", """// pipeline does not reference cannot be written on its set. The skinned and
// layered kinds are in the G-buffer pass since RT-2 (RT-1 found them outside
// it on the bridge's terrain) and read the signals like the plain kind.
#if defined(RV_RAY_SHADOWS)""")
  pat = re.compile(r"(!defined\(RV_IRRADIANCE_FILL\)) \\(\r?\n)\t&& !defined\(RV_SKINNED\) && !defined\(RV_LAYERED\)(\r?\n)(#define RV_DIRECT_SIGNAL_INPUT)")
  assert len(pat.findall(s)) == 1
  s = pat.sub(lambda m: m.group(1) + m.group(2) + m.group(4), s)
  save(p, s)

# ---------------------------------------------------------------- Renderer3D.cpp
p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
# members: shaders, pipelines, sets
s = rep(s, nl, """			Ref<RHIShader>   GBufferShader;
			Ref<RHIShader>   MaskedGBufferShader;
""", """			Ref<RHIShader>   GBufferShader;
			Ref<RHIShader>   MaskedGBufferShader;
			// The skinned and layered kinds' G-buffer variants (RT-2): the same
			// sources with RV_GBUFFER, so every opaque kind is in the G-buffer.
			Ref<RHIShader>   SkinnedGBufferShader;
			Ref<RHIShader>   LayeredGBufferShader;
""")
s = rep(s, nl, """			Ref<RHIPipeline> GBufferPipeline;
			Ref<RHIPipeline> MaskedGBufferPipeline;
""", """			Ref<RHIPipeline> GBufferPipeline;
			Ref<RHIPipeline> MaskedGBufferPipeline;
			Ref<RHIPipeline> SkinnedGBufferPipeline;
			Ref<RHIPipeline> LayeredGBufferPipeline;
""")
s = rep(s, nl, """				Ref<RHIResourceSet> GBufferSet;
				Ref<RHIResourceSet> MaskedGBufferSet;
""", """				Ref<RHIResourceSet> GBufferSet;
				Ref<RHIResourceSet> MaskedGBufferSet;
				Ref<RHIResourceSet> SkinnedGBufferSet;   // RT-2: the pending kinds' G-buffer sets
				Ref<RHIResourceSet> LayeredGBufferSet;
""")
# the shaders
s = rep(s, nl, """		s_Data->GBufferShader = nullptr;
		s_Data->MaskedGBufferShader = nullptr;
		{
			std::vector<std::string> gbuffer = defines;
			gbuffer.push_back("RV_GBUFFER");
			if (auto compiledGBuffer = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader", gbuffer))
				s_Data->GBufferShader = s_Data->Device->CreateShader(*compiledGBuffer);
			else
				RV_CORE_WARN("Renderer3D: the G-buffer variant of pbr.rvshader did not compile; "
							 "drawing with the depth prepass and no G-buffer pass");
""", """		s_Data->GBufferShader = nullptr;
		s_Data->MaskedGBufferShader = nullptr;
		s_Data->SkinnedGBufferShader = nullptr;
		s_Data->LayeredGBufferShader = nullptr;
		{
			std::vector<std::string> gbuffer = defines;
			gbuffer.push_back("RV_GBUFFER");
			if (auto compiledGBuffer = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader", gbuffer))
				s_Data->GBufferShader = s_Data->Device->CreateShader(*compiledGBuffer);
			else
				RV_CORE_WARN("Renderer3D: the G-buffer variant of pbr.rvshader did not compile; "
							 "drawing with the depth prepass and no G-buffer pass");
			// The skinned and layered kinds (RT-2): without these the terrain and
			// the characters were outside every pre-lit signal.
			if (auto compiledSkinned = ShaderCompiler::CompileFromFile("assets/shaders/pbr_skinned.rvshader", gbuffer))
				s_Data->SkinnedGBufferShader = s_Data->Device->CreateShader(*compiledSkinned);
			else
				RV_CORE_WARN("Renderer3D: the skinned G-buffer variant did not compile; "
							 "skinned meshes stay outside the G-buffer");
			if (auto compiledLayered = ShaderCompiler::CompileFromFile("assets/shaders/pbr_layered.rvshader", gbuffer))
				s_Data->LayeredGBufferShader = s_Data->Device->CreateShader(*compiledLayered);
			else
				RV_CORE_WARN("Renderer3D: the layered G-buffer variant did not compile; "
							 "terrain stays outside the G-buffer");
""")
# the pipelines
s = rep(s, nl, """		s_Data->GBufferPipeline = nullptr;
		s_Data->MaskedGBufferPipeline = nullptr;
		if (s_Data->GBufferShader && s_Data->TargetVelocity != Format::Undefined""",
       """		s_Data->GBufferPipeline = nullptr;
		s_Data->MaskedGBufferPipeline = nullptr;
		s_Data->SkinnedGBufferPipeline = nullptr;
		s_Data->LayeredGBufferPipeline = nullptr;
		if (s_Data->GBufferShader && s_Data->TargetVelocity != Format::Undefined""")
s = rep(s, nl, """			if (s_Data->MaskedGBufferShader)
			{
				gbuffer.Name = "Renderer3D.gbuffer.masked";
				gbuffer.Shader = s_Data->MaskedGBufferShader;
				s_Data->MaskedGBufferPipeline = s_Data->Device->CreatePipeline(gbuffer);
			}
""", """			if (s_Data->MaskedGBufferShader)
			{
				gbuffer.Name = "Renderer3D.gbuffer.masked";
				gbuffer.Shader = s_Data->MaskedGBufferShader;
				s_Data->MaskedGBufferPipeline = s_Data->Device->CreatePipeline(gbuffer);
			}
			if (s_Data->SkinnedGBufferShader)
			{
				gbuffer.Name = "Renderer3D.gbuffer.skinned";
				gbuffer.Shader = s_Data->SkinnedGBufferShader;
				s_Data->SkinnedGBufferPipeline = s_Data->Device->CreatePipeline(gbuffer);
			}
			if (s_Data->LayeredGBufferShader)
			{
				gbuffer.Name = "Renderer3D.gbuffer.layered";
				gbuffer.Shader = s_Data->LayeredGBufferShader;
				s_Data->LayeredGBufferPipeline = s_Data->Device->CreatePipeline(gbuffer);
			}
""")
# the sets: created beside the others, filled by the targets loop
s = rep(s, nl, """		if (s_Data->MaskedGBufferPipeline && !slot.MaskedGBufferSet)
			slot.MaskedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->MaskedGBufferPipeline, 0);
""", """		if (s_Data->MaskedGBufferPipeline && !slot.MaskedGBufferSet)
			slot.MaskedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->MaskedGBufferPipeline, 0);
		if (s_Data->SkinnedGBufferPipeline && !slot.SkinnedGBufferSet)
			slot.SkinnedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->SkinnedGBufferPipeline, 0);
		if (s_Data->LayeredGBufferPipeline && !slot.LayeredGBufferSet)
			slot.LayeredGBufferSet = s_Data->Device->CreateResourceSet(s_Data->LayeredGBufferPipeline, 0);
""")
s = rep(s, nl, """										  slot.TransparentSet, slot.TransparentGpuSet,
										  slot.GBufferSet, slot.MaskedGBufferSet };""",
       """										  slot.TransparentSet, slot.TransparentGpuSet,
										  slot.GBufferSet, slot.MaskedGBufferSet,
										  slot.SkinnedGBufferSet, slot.LayeredGBufferSet };""")
# the uploads: what the pending kinds' lit sets get, the G-buffer twins get too
s = rep(s, nl, """			slot.SkinnedSet->SetStorageBuffer(11, slot.Bones, 0,
											  (uint64_t)boneCount * sizeof(Mat4));
			if (s_Data->Bindless)
			{
				slot.SkinnedSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.SkinnedSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.SkinnedSet->Commit();
		}
""", """			slot.SkinnedSet->SetStorageBuffer(11, slot.Bones, 0,
											  (uint64_t)boneCount * sizeof(Mat4));
			if (s_Data->Bindless)
			{
				slot.SkinnedSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.SkinnedSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.SkinnedSet->Commit();
		}
		if (slot.SkinnedGBufferSet)
		{
			// The skinned kind's G-buffer set (RT-2): the same buffers as its lit set.
			slot.SkinnedGBufferSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
													 (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.SkinnedGBufferSet->SetStorageBuffer(7, slot.Instances, 0,
													 (uint64_t)instanceRows * sizeof(InstanceData));
			slot.SkinnedGBufferSet->SetStorageBuffer(11, slot.Bones, 0,
													 (uint64_t)boneCount * sizeof(Mat4));
			if (s_Data->Bindless)
			{
				slot.SkinnedGBufferSet->SetStorageBuffer(13, slot.Materials, 0,
														 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.SkinnedGBufferSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.SkinnedGBufferSet->Commit();
		}
""")
s = rep(s, nl, """		if (slot.LayeredSet)
		{
			slot.LayeredSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
											  (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.LayeredSet->SetStorageBuffer(7, slot.Instances, 0,
											  (uint64_t)instanceRows * sizeof(InstanceData));
			if (s_Data->Bindless)
			{
				slot.LayeredSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.LayeredSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
""", """		if (slot.LayeredGBufferSet)
		{
			// The layered kind's G-buffer set (RT-2): the same buffers as its lit set.
			slot.LayeredGBufferSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
													 (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.LayeredGBufferSet->SetStorageBuffer(7, slot.Instances, 0,
													 (uint64_t)instanceRows * sizeof(InstanceData));
			if (s_Data->Bindless)
			{
				slot.LayeredGBufferSet->SetStorageBuffer(13, slot.Materials, 0,
														 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.LayeredGBufferSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.LayeredGBufferSet->Commit();
		}
		if (slot.LayeredSet)
		{
			slot.LayeredSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
											  (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.LayeredSet->SetStorageBuffer(7, slot.Instances, 0,
											  (uint64_t)instanceRows * sizeof(InstanceData));
			if (s_Data->Bindless)
			{
				slot.LayeredSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.LayeredSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
""")
# the draw: the pending kinds into the G-buffer, before the split hands the lit half over
s = rep(s, nl, """		if (split)
		{
			s_Data->LitPending = true;
			return;
		}
""", """		if (split)
		{
			// The pending draws -- skinned, layered, and any static the GPU did
			// not cull -- into the G-buffer too (RT-2), with their own kinds'
			// G-buffer pipelines; the lit half redraws them over this depth.
			DrawGBufferPending(cmd, slot, count);
			s_Data->LitPending = true;
			return;
		}
""")
# the helper itself, a static free function beside DrawLitBody, defined before EndScene
s = rep(s, nl, """	void Renderer3D::EndScene()
	{""", """	// RT-2: the pending draws' G-buffer half -- DrawLitBody's vertex path with
	// each kind's G-buffer pipeline and set, no meshlets, no glow. What is not
	// here is not in the G-buffer, and no pre-lit signal sees it.
	static void DrawGBufferPending(RHICommandList* cmd, Renderer3DData::SceneSlot& slot,
								   uint32_t count)
	{
		const uint32_t opaqueCount = Math::Min(count, s_Data->TransparentBegin);
		DrawKind boundKind = DrawKind::Static;
		DrawBucket boundBucket = DrawBucket::Opaque;
		bool anyPipelineBound = false;
		uint32_t start = 0;
		while (start < opaqueCount)
		{
			uint32_t end = start + 1;
			while (end < opaqueCount &&
				   s_Data->Pending[end].Bucket == s_Data->Pending[start].Bucket &&
				   s_Data->Pending[end].Kind == s_Data->Pending[start].Kind &&
				   s_Data->Pending[end].MeshKey == s_Data->Pending[start].MeshKey &&
				   s_Data->Pending[end].MaterialKey == s_Data->Pending[start].MaterialKey &&
				   s_Data->Pending[end].IndexCount == s_Data->Pending[start].IndexCount)
			{
				end++;
			}
			const PendingDraw& first = s_Data->Pending[start];
			const bool masked = first.Bucket == DrawBucket::Masked;
			const Ref<RHIPipeline>& pipeline =
				first.Kind == DrawKind::Skinned ? s_Data->SkinnedGBufferPipeline
				: first.Kind == DrawKind::Layered ? s_Data->LayeredGBufferPipeline
				: (masked && s_Data->MaskedGBufferPipeline) ? s_Data->MaskedGBufferPipeline
				: s_Data->GBufferPipeline;
			const Ref<RHIResourceSet>& gbufferSet =
				first.Kind == DrawKind::Skinned ? slot.SkinnedGBufferSet
				: first.Kind == DrawKind::Layered ? slot.LayeredGBufferSet
				: (masked && s_Data->MaskedGBufferPipeline && slot.MaskedGBufferSet) ? slot.MaskedGBufferSet
				: slot.GBufferSet;
			if (!pipeline || !gbufferSet || !first.MeshRef)
			{
				start = end;
				continue;
			}
			if (!anyPipelineBound || boundKind != first.Kind || boundBucket != first.Bucket)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, gbufferSet);
				if (s_Data->Bindless)
					cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
				boundKind = first.Kind;
				boundBucket = first.Bucket;
				anyPipelineBound = true;
			}
			if (first.Kind == DrawKind::Layered)
			{
				if (first.LayeredRef)
					first.LayeredRef->Bind(*cmd, pipeline, 1);
			}
			else if (first.MaterialRef && !s_Data->Bindless)
			{
				first.MaterialRef->Bind(*cmd, pipeline, 1);
			}
			ObjectPushConstants object;
			object.BaseInstance = (int32_t)start;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			cmd->BindVertexBuffer(0, first.MeshRef->GetVertexBuffer());
			cmd->BindIndexBuffer(first.MeshRef->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(first.IndexCount, end - start);
			s_Data->DrawCalls++;
			start = end;
		}
	}

	void Renderer3D::EndScene()
	{""")
# the lit-side bindings: the two kinds are back in
s = rep(s, nl, """			&& (sceneSet == slot.Set || sceneSet == slot.MaskedSet
				|| sceneSet == slot.GpuSet || sceneSet == slot.MaskedGpuSet))""",
       """			&& (sceneSet == slot.Set || sceneSet == slot.SkinnedSet || sceneSet == slot.LayeredSet
				|| sceneSet == slot.MaskedSet || sceneSet == slot.GpuSet || sceneSet == slot.MaskedGpuSet))""")
s = rep(s, nl, """			// Not the skinned and layered sets: their kinds are outside the
			// G-buffer and keep the loop, so their layouts have no such binding.
			const Ref<RHIResourceSet> litSets[] = { slot.Set, slot.MaskedSet, slot.GpuSet, slot.MaskedGpuSet };""",
       """			const Ref<RHIResourceSet> litSets[] = { slot.Set, slot.SkinnedSet, slot.LayeredSet,
													slot.MaskedSet, slot.GpuSet, slot.MaskedGpuSet };""")
s = rep(s, nl, """		// pipeline does not reference is not in its layout); not the skinned
		// and layered kinds, which are outside the G-buffer (RT-1). Black here; the""",
       """		// pipeline does not reference is not in its layout). Black here; the""")
save(p, s)

print('RT-2a patched')
