"""The ratio-estimator resolve (Stachowiak 2015, UE/Frostbite SSSR): the trace
fires one ray per texel and writes its direction and pdf beside the
radiance; the resolve reuses the neighbours' HIT POINTS re-aimed from this
texel, weighted by this texel's own lobe over the neighbour's pdf. No
parallax blur, no over-reach: a band stays as wide as the lobe, a puddle
texel keeps its narrow lobe among rough neighbours. Replaces the three
a-trous footprint passes (patch_footprint.py) with one pass."""
import os, re
os.chdir(r'C:\Users\ism19\Code\RageV')

def read(p): return open(p, 'rb').read().decode('utf-8')
def write(p, s): open(p, 'wb').write(s.encode('utf-8')); print('ok', p)
def patch(p, pairs):
    s = read(p); nl = '\r\n' if '\r\n' in s else '\n'
    for old, new in pairs:
        n = new.replace('\n', nl)
        if isinstance(old, tuple):   # (start marker, end marker): replace the span through the end marker's line
            a = s.index(old[0].replace('\n', nl)); b = s.index(old[1].replace('\n', nl), a); b = s.index(nl, b) + len(nl)
            s = s[:a] + n + s[b:]; continue
        o = old.replace('\n', nl)
        assert s.count(o) == 1, (p, s.count(o), old[:70]); s = s.replace(o, n)
    write(p, s)

# Renderer3D half of patch_ratio.py, re-run alone after the first run stopped here.
# ---- renderer: trace pipeline with two attachments, resolve binds the hit --
patch('RageV/src/RageV/Renderer/Renderer3D.cpp', [(
"""			if (pass == 2)
			{
				// The reflector under the texel, and what was learned of it.
				for (int extra = 0; extra < 2; ++extra)
				{
					reflection.ColorFormats.push_back(Format::R16G16B16A16_SFLOAT);
					reflection.BlendPerAttachment.push_back(BlendPreset::Opaque);
				}
			}
""",
"""			// The trace writes its ray's direction and pdf beside the radiance;
			// the accumulator its surface and moments beside the picture.
			const int extras = pass == 0 ? 1 : pass == 2 ? 2 : 0;
			for (int extra = 0; extra < extras; ++extra)
			{
				reflection.ColorFormats.push_back(Format::R16G16B16A16_SFLOAT);
				reflection.BlendPerAttachment.push_back(BlendPreset::Opaque);
			}
"""),
("""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface,
										int stride)
	{""",
"""	void Renderer3D::ResolveReflections(const RHI::Ref<RHITexture>& fresh,
										const RHI::Ref<RHITexture>& hit,
										const RHI::Ref<RHITexture>& depth,
										const RHI::Ref<RHITexture>& surface)
	{"""),
("""		if (!cmd || !slot.LampSet || !fresh || !depth || !surface)
			return;

		if (!slot.ReflectionResolveInputs)""",
"""		if (!cmd || !slot.LampSet || !fresh || !hit || !depth || !surface)
			return;

		if (!slot.ReflectionResolveInputs)"""),
("""		slot.ReflectionResolveInputs->SetTexture(2, surface, s_Data->PointSampler);
		slot.ReflectionResolveInputs->Commit();
""",
"""		slot.ReflectionResolveInputs->SetTexture(2, surface, s_Data->PointSampler);
		slot.ReflectionResolveInputs->SetTexture(3, hit, s_Data->PointSampler);
		slot.ReflectionResolveInputs->Commit();
"""),
("""		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		push.Probe.z = (float)Math::Max(stride, 1);

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""",
"""		push.History.w = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;

		cmd->BindPipeline(s_Data->ReflectionResolvePipeline);""")])
patch('RageV/src/RageV/Renderer/Renderer3D.h', [(
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface,
									   int stride);""",
"""		static void ResolveReflections(const RHI::Ref<RHI::RHITexture>& fresh,
									   const RHI::Ref<RHI::RHITexture>& hit,
									   const RHI::Ref<RHI::RHITexture>& depth,
									   const RHI::Ref<RHI::RHITexture>& surface);""")])
print('all patched')
