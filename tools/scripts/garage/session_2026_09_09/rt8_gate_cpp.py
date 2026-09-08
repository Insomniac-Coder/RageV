"""RT-8 job 3, the plumbing: the sea remembers what its surface was.

The contract's gate asks last frame's surface a question, so the measurement
needs one. A third attachment on the water light's history pair carries the
sea's normal and its plane a frame back, at full float so the storage is not
what is being measured. Nothing reads it but the counters.
"""
import io, sys

def patch(p, edits):
    src = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in src
    s = src.replace('\r\n', '\n')
    for old, new, what in edits:
        if s.count(old) != 1:
            sys.exit('%s: %s matched %d' % (p, what, s.count(old)))
        s = s.replace(old, new, 1)
    io.open(p, 'w', encoding='utf-8', newline='\r\n' if crlf else '\n').write(s)
    print(p, 'patched')

# ---- the pipeline grows an attachment (the trap this session already paid) --
patch(r'RageV/src/RageV/Renderer/Renderer3D.cpp', [
(
"""			accumulate.ColorFormats = { Format::R16G16B16A16_SFLOAT,
										Format::R16G16B16A16_SFLOAT };
			accumulate.BlendPerAttachment = { BlendPreset::Opaque, BlendPreset::Opaque };""",
"""			// **And a third: what the sea's surface was here.** RT-8 job 3's
			// measurement needs last frame's normal and plane to ask the
			// contract's gate its question, and a full float because a bay is
			// a kilometre across -- a half's step out there is half a metre,
			// which would be measuring the storage instead of the geometry.
			// **The count on this list is the pipeline's own**: growing the
			// target alone leaves the write going nowhere, in silence, which
			// is exactly what cost this session an afternoon (2026-09-08).
			accumulate.ColorFormats = { Format::R16G16B16A16_SFLOAT,
										Format::R16G16B16A16_SFLOAT,
										Format::R32G32B32A32_SFLOAT };
			accumulate.BlendPerAttachment = { BlendPreset::Opaque, BlendPreset::Opaque,
											  BlendPreset::Opaque };""",
    'water accumulate pipeline'),
(
"""	void Renderer3D::AccumulateWaterLamps(const RHI::Ref<RHITexture>& diffuse,
										  const RHI::Ref<RHITexture>& specular,
										  const RHI::Ref<RHITexture>& position,
										  const RHI::Ref<RHITexture>& previousDiffuse,
										  const RHI::Ref<RHITexture>& previousSpecular,
										  CameraMotion& motion, bool hasHistory,
										  const Ref<RHITexture>& waveMotion)""",
"""	void Renderer3D::AccumulateWaterLamps(const RHI::Ref<RHITexture>& diffuse,
										  const RHI::Ref<RHITexture>& specular,
										  const RHI::Ref<RHITexture>& position,
										  const RHI::Ref<RHITexture>& previousDiffuse,
										  const RHI::Ref<RHITexture>& previousSpecular,
										  CameraMotion& motion, bool hasHistory,
										  const Ref<RHITexture>& waveMotion,
										  const Ref<RHITexture>& surface,
										  const Ref<RHITexture>& previousSignature)""",
    'AccumulateWaterLamps signature'),
(
"""		slot.LampAccumulateInputs->SetTexture(5, waveMotion ? waveMotion : position,
											  s_Data->PointSampler);
		slot.LampAccumulateInputs->Commit();""",
"""		slot.LampAccumulateInputs->SetTexture(5, waveMotion ? waveMotion : position,
											  s_Data->PointSampler);
		// **RT-8 job 3's measurement, and it reads nothing else.** The sea's
		// normal this frame, and the normal and plane it had last frame, so
		// the pass can ask the signal contract's geometric gate whether it
		// would have kept this pixel. The answer goes to the counters and
		// nowhere else -- the water's own average is untouched.
		slot.LampAccumulateInputs->SetTexture(6, surface ? surface : position,
											  s_Data->PointSampler);
		slot.LampAccumulateInputs->SetTexture(7, previousSignature ? previousSignature : position,
											  s_Data->PointSampler);
		slot.LampAccumulateInputs->Commit();""",
    'AccumulateWaterLamps bindings'),
])

# ---- the header ---------------------------------------------------------
patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
(
"""									 CameraMotion& motion, bool hasHistory,
									 const RHI::Ref<RHI::RHITexture>& waveMotion = nullptr);""",
"""									 CameraMotion& motion, bool hasHistory,
									 const RHI::Ref<RHI::RHITexture>& waveMotion = nullptr,
									 // RT-8 job 3's measurement: this frame's sea normal
									 // and last frame's signature, read by the counters
									 // alone.
									 const RHI::Ref<RHI::RHITexture>& surface = nullptr,
									 const RHI::Ref<RHI::RHITexture>& previousSignature = nullptr);""",
    'AccumulateWaterLamps declaration'),
])

# ---- the frame graph ----------------------------------------------------
patch(r'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp', [
(
"""						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampAverage",
									  Format::R16G16B16A16_SFLOAT);""",
"""						// RT-8 job 3: and a third attachment, the sea's surface a
						// frame back -- octahedral normal, plane distance, mask.
						// Full float because the plane distance is a world
						// coordinate and the bay is a kilometre across.
						light.Prepare(Renderer::GetDevice(),
									  desc.Width * (uint32_t)supersample,
									  desc.Height * (uint32_t)supersample,
									  Format::R16G16B16A16_SFLOAT, "WaterLampAverage",
									  Format::R16G16B16A16_SFLOAT,
									  Format::R32G32B32A32_SFLOAT);""",
    'water light history prepare'),
(
"""										light.Motion(), light.HasHistory(),
										// RT-8: attachment 3 is the wave's own motion.
										context.Color(waterSurface, 3));""",
"""										light.Motion(), light.HasHistory(),
										// RT-8: attachment 3 is the wave's own motion.
										context.Color(waterSurface, 3),
										// RT-8 job 3's measurement: the sea's normal
										// now, and its surface a frame back.
										context.Color(waterSurface, 0),
										context.Color(pastLight, 2));""",
    'AccumulateWaterLamps call'),
])
