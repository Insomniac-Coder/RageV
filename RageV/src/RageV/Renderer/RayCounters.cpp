#include <rvpch.h>
#include "RayCounters.h"

namespace RageV
{
	using namespace RHI;

	namespace
	{
		struct RayCountersData
		{
			RHIDevice* Device = nullptr;
			bool Available = false;
			// One per frame in flight. Two frames overlap on the GPU, and a
			// fill at the top of frame N would race the atomics of frame
			// N-1 still running if they shared one buffer.
			std::vector<Ref<RHIBuffer>> Buffers;
			RayCounters::Sample Last;
			RayCounters::Sample Fresh;
			std::vector<uint8_t> Scratch;
		};

		std::unique_ptr<RayCountersData> s_Data;
		const Ref<RHIBuffer> s_Null;
		const RayCounters::Sample s_NoSample;
	}

	float RayCounters::Sample::RaysPerFragment() const
	{
		return Lanes[LitFragments] > 0 ? (float)((double)TotalRays() / Lanes[LitFragments]) : 0.0f;
	}

	float RayCounters::Sample::LightsPerFragment() const
	{
		return Lanes[LitFragments] > 0 ? (float)((double)Lanes[LightsWalked] / Lanes[LitFragments])
									   : 0.0f;
	}

	float RayCounters::Sample::LightsPerHit() const
	{
		return Lanes[Hits] > 0 ? (float)((double)Lanes[HitLightsWalked] / Lanes[Hits]) : 0.0f;
	}

	float RayCounters::Sample::TemporalConfidence() const
	{
		return Lanes[TaaPixels] > 0 ? (float)((double)Lanes[TaaReused] / Lanes[TaaPixels]) : -1.0f;
	}

	void RayCounters::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<RayCountersData>();
		s_Data->Device = &device;
		// The counters are a ray-tracing instrument, and the lit shaders
		// declare the binding under the same condition the structure is
		// declared under. A device that cannot trace has no rays to count
		// and no layout that expects the buffer.
		s_Data->Available = device.GetCaps().SupportsRayQuery;
		if (!s_Data->Available)
			return;

		const uint32_t frames = device.GetFramesInFlight();
		s_Data->Buffers.resize(frames);
		for (uint32_t i = 0; i < frames; i++)
		{
			BufferDesc desc;
			desc.Size = kBytes;
			// Storage for the atomics, TransferSrc for the readback copy;
			// DeviceLocal adds the transfer destination the fill needs.
			desc.Usage = BufferUsage::Storage | BufferUsage::TransferSrc;
			desc.Memory = MemoryDomain::DeviceLocal;
			desc.DebugName = "RayCounters." + std::to_string(i);
			s_Data->Buffers[i] = device.CreateBuffer(desc);
			if (!s_Data->Buffers[i])
			{
				RV_CORE_ERROR("Ray counters: the device refused a {0}-byte buffer; rays will "
							  "not be counted this session", kBytes);
				s_Data->Available = false;
				s_Data->Buffers.clear();
				return;
			}
		}
		RV_CORE_INFO("Ray counters: {0} lanes, read back one frame late", (uint32_t)Count);
	}

	void RayCounters::Shutdown()
	{
		s_Data.reset();
	}

	bool RayCounters::IsAvailable()
	{
		return s_Data && s_Data->Available;
	}

	void RayCounters::BeginFrame(RHICommandList& cmd)
	{
		if (!s_Data)
			return;
		s_Data->Fresh.Valid = false;
		if (!s_Data->Available)
			return;

		const Ref<RHIBuffer>& buffer = s_Data->Buffers[s_Data->Device->GetFrameIndex()];

		// What this slot counted the last time round, and this frame's copy
		// armed in the same call. Read before the zero below is recorded --
		// the order in the command buffer does not matter to the readback
		// (the copy runs at EndFrame either way), but the intent reads right.
		if (s_Data->Device->ReadBuffer(buffer, 0, kBytes, s_Data->Scratch)
			&& s_Data->Scratch.size() >= kBytes)
		{
			// The slots summed into the lanes -- except the maximum, which
			// is the largest of them.
			const uint32_t* words = reinterpret_cast<const uint32_t*>(s_Data->Scratch.data());
			uint64_t sums[Count]{};
			for (uint32_t slot = 0; slot < kSlots; slot++)
			{
				for (uint32_t lane = 0; lane < Count; lane++)
				{
					const uint32_t value = words[slot * Count + lane];
					if (lane == LightsMax)
						sums[lane] = std::max<uint64_t>(sums[lane], value);
					else
						sums[lane] += value;
				}
			}
			for (uint32_t lane = 0; lane < Count; lane++)
				s_Data->Fresh.Lanes[lane] = (uint32_t)std::min<uint64_t>(sums[lane], 0xFFFFFFFFu);
			s_Data->Fresh.Valid = true;
			s_Data->Last = s_Data->Fresh;
		}

		// Zero, then the barrier that puts the zero before the first atomic.
		// The previous use of this buffer was two frames ago and behind the
		// slot's fence, so nothing is still adding to it.
		cmd.FillBuffer(buffer, 0, kBytes, 0u);
		cmd.BufferBarrier(buffer, BufferSync::TransferWrite, BufferSync::ShaderWrite);
	}

	const Ref<RHIBuffer>& RayCounters::Buffer()
	{
		if (!s_Data || !s_Data->Available)
			return s_Null;
		return s_Data->Buffers[s_Data->Device->GetFrameIndex()];
	}

	const RayCounters::Sample& RayCounters::Last()
	{
		return s_Data ? s_Data->Last : s_NoSample;
	}

	const RayCounters::Sample& RayCounters::Fresh()
	{
		return s_Data ? s_Data->Fresh : s_NoSample;
	}
}
