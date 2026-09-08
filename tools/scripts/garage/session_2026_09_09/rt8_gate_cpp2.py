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

patch(r'RageV/src/RageV/Renderer/Renderer3D.h', [
(
"""										 const RHI::Ref<RHI::RHITexture>& waveMotion = nullptr);
		// The two pictures the second pass wrote, for the water draw that""",
"""										 const RHI::Ref<RHI::RHITexture>& waveMotion = nullptr,
										 // **RT-8 job 3's measurement, and only that.** The
										 // sea's normal this frame and its surface a frame
										 // back, so the pass can ask the signal contract's
										 // geometric gate whether it would have kept each
										 // water pixel. The answer goes to the ray counters
										 // and changes no picture.
										 const RHI::Ref<RHI::RHITexture>& surface = nullptr,
										 const RHI::Ref<RHI::RHITexture>& previousSignature = nullptr);
		// The two pictures the second pass wrote, for the water draw that""",
    'AccumulateWaterLamps declaration'),
])

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
