"""RT-2a, second landing: DrawGBufferPending drew nothing -- it read the opaque
range from TransparentBegin, which the lit draw computes later; and a pending
static draw needs a G-buffer set indexed by the CPU visibility list (slot.Set's
shape), not the GPU cull's list the indirect G-buffer set carries."""
import os
os.chdir(r'C:\Users\ism19\Code\RageV')
exec(open('tools/scripts/garage/session_2026_09_06/t4_prelude.py', encoding='utf-8').read())

p = 'RageV/src/RageV/Renderer/Renderer3D.cpp'; s, nl = load(p)
assert 'PendingGBufferSet' not in s
s = rep(s, nl, """				Ref<RHIResourceSet> SkinnedGBufferSet;   // RT-2: the pending kinds' G-buffer sets
				Ref<RHIResourceSet> LayeredGBufferSet;
""", """				Ref<RHIResourceSet> SkinnedGBufferSet;   // RT-2: the pending kinds' G-buffer sets
				Ref<RHIResourceSet> LayeredGBufferSet;
				// The pending static and masked draws' G-buffer sets: slot.Set's shape
				// (the CPU visibility list), where GBufferSet carries the GPU cull's.
				Ref<RHIResourceSet> PendingGBufferSet;
				Ref<RHIResourceSet> PendingMaskedGBufferSet;
""")
s = rep(s, nl, """		if (s_Data->SkinnedGBufferPipeline && !slot.SkinnedGBufferSet)
			slot.SkinnedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->SkinnedGBufferPipeline, 0);
""", """		if (s_Data->SkinnedGBufferPipeline && !slot.SkinnedGBufferSet)
			slot.SkinnedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->SkinnedGBufferPipeline, 0);
		if (s_Data->GBufferPipeline && !slot.PendingGBufferSet)
			slot.PendingGBufferSet = s_Data->Device->CreateResourceSet(s_Data->GBufferPipeline, 0);
		if (s_Data->MaskedGBufferPipeline && !slot.PendingMaskedGBufferSet)
			slot.PendingMaskedGBufferSet = s_Data->Device->CreateResourceSet(s_Data->MaskedGBufferPipeline, 0);
""")
s = rep(s, nl, """										  slot.GBufferSet, slot.MaskedGBufferSet,
										  slot.SkinnedGBufferSet, slot.LayeredGBufferSet };""",
       """										  slot.GBufferSet, slot.MaskedGBufferSet,
										  slot.SkinnedGBufferSet, slot.LayeredGBufferSet,
										  slot.PendingGBufferSet, slot.PendingMaskedGBufferSet };""")
s = rep(s, nl, """		slot.Set->SetStorageBuffer(7, slot.Instances, 0,
								   (uint64_t)instanceRows * sizeof(InstanceData));
		slot.Set->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
								   (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
""", """		slot.Set->SetStorageBuffer(7, slot.Instances, 0,
								   (uint64_t)instanceRows * sizeof(InstanceData));
		slot.Set->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
								   (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
		// The pending static and masked draws' G-buffer sets (RT-2): slot.Set's
		// buffers, so BaseInstance indexes the same visibility list.
		for (const Ref<RHIResourceSet>& pendingSet : { slot.PendingGBufferSet, slot.PendingMaskedGBufferSet })
		{
			if (!pendingSet)
				continue;
			pendingSet->SetStorageBuffer(7, slot.Instances, 0,
										 (uint64_t)instanceRows * sizeof(InstanceData));
			pendingSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
										 (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			if (s_Data->Bindless)
			{
				pendingSet->SetStorageBuffer(13, slot.Materials, 0,
											 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					pendingSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			pendingSet->Commit();
		}
""")
s = rep(s, nl, """		const uint32_t opaqueCount = Math::Min(count, s_Data->TransparentBegin);
		DrawKind boundKind = DrawKind::Static;""", """		// The opaque range, found as the lit draw finds it (TransparentBegin is
		// its, computed later): up to the first blended draw.
		uint32_t opaqueCount = count;
		for (uint32_t i = 0; i < count; i++)
		{
			if (s_Data->Pending[i].Bucket == DrawBucket::Blended)
			{
				opaqueCount = i;
				break;
			}
		}
		DrawKind boundKind = DrawKind::Static;""")
s = rep(s, nl, """			const Ref<RHIResourceSet>& gbufferSet =
				first.Kind == DrawKind::Skinned ? slot.SkinnedGBufferSet
				: first.Kind == DrawKind::Layered ? slot.LayeredGBufferSet
				: (masked && s_Data->MaskedGBufferPipeline && slot.MaskedGBufferSet) ? slot.MaskedGBufferSet
				: slot.GBufferSet;""", """			const Ref<RHIResourceSet>& gbufferSet =
				first.Kind == DrawKind::Skinned ? slot.SkinnedGBufferSet
				: first.Kind == DrawKind::Layered ? slot.LayeredGBufferSet
				: (masked && s_Data->MaskedGBufferPipeline && slot.PendingMaskedGBufferSet) ? slot.PendingMaskedGBufferSet
				: slot.PendingGBufferSet;""")
save(p, s)
print('RT-2a second landing patched')
