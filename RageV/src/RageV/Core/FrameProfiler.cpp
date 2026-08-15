#include <rvpch.h>
#include "FrameProfiler.h"
#include "EngineConfig.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"
#include <spdlog/fmt/fmt.h>

namespace RageV
{
	namespace
	{
		struct FrameRecord
		{
			float Total = 0.0f;
			float Phases[(int)FramePhase::Count]{};

			// Negative means the device did not resolve one for this phase --
			// distinct from zero, which is a phase that ran and cost nothing
			// measurable.
			float GpuTotal = -1.0f;
			float GpuPhases[(int)FramePhase::Count]{};
		};

		std::vector<FrameRecord> s_Frames;
		bool s_Collecting = false;

		// This frame's GPU numbers, filled by CollectGpu from results that
		// belong to a frame a couple back. Averaged over a run they are the
		// run's; attached to one frame they would be a lie, which is why
		// nothing reports a single frame's GPU time.
		float s_GpuPhases[(int)FramePhase::Count]{};
		float s_GpuTotal = -1.0f;
		bool s_GpuSeen = false;
		// Slots claimed this frame: which phase each pair belongs to, and how
		// far the bump allocator has got.
		struct ClaimedScope { FramePhase Phase; uint32_t Begin; uint32_t End; };

		// Per frame in flight, not one list.
		//
		// The results read this frame belong to the frame that last used this
		// pool, so the claims that explain them are that frame's too. A single
		// list is cleared at the top of every frame and is therefore empty by
		// the time the results arrive -- which showed up as a total GPU time
		// with no per-phase breakdown behind it.
		std::vector<std::vector<ClaimedScope>> s_ClaimHistory;
		uint32_t s_NextSlot = kFirstScopeSlot;

		std::vector<ClaimedScope>& ClaimsFor(uint32_t frame)
		{
			if (s_ClaimHistory.size() <= frame)
				s_ClaimHistory.resize(frame + 1);
			return s_ClaimHistory[frame];
		}

		// Exponential moving averages, updated every frame regardless of
		// whether anything is collecting. The editor reads these.
		float s_LiveCpu[(int)FramePhase::Count]{};
		float s_LiveGpu[(int)FramePhase::Count]{};
		float s_LiveGpuFrame = 0.0f;
		constexpr float kSmoothing = 0.05f;

		void Smooth(float& average, float sample)
		{
			average = average == 0.0f ? sample : average + (sample - average) * kSmoothing;
		}

		const char* kPhaseNames[(int)FramePhase::Count] =
		{
			"wait (gpu/vsync)",
			"environment prefilter",
			"shadow maps",
			"reflection probes",
			"render graph",
			"imgui",
			"present",
		};
	}

	float FrameProfiler::s_Phases[(int)FramePhase::Count]{};

	const char* FramePhaseName(FramePhase phase)
	{
		const int index = (int)phase;
		return index >= 0 && index < (int)FramePhase::Count ? kPhaseNames[index] : "?";
	}

	void FrameProfiler::BeginFrame()
	{
		for (float& phase : s_Phases)
			phase = 0.0f;
		s_GpuTotal = -1.0f;
		for (float& phase : s_GpuPhases)
			phase = -1.0f;
	}

	bool FrameProfiler::ClaimGpuScope(FramePhase phase, uint32_t& beginSlot, uint32_t& endSlot)
	{
		if (s_NextSlot + 1 >= RHI::RHIDevice::kTimestampSlots || !Renderer::HasDevice())
			return false;

		beginSlot = s_NextSlot++;
		endSlot = s_NextSlot++;
		ClaimsFor(Renderer::GetDevice().GetFrameIndex()).push_back({ phase, beginSlot, endSlot });
		return true;
	}

	void FrameProfiler::CollectGpu()
	{
		if (!Renderer::HasDevice())
			return;

		RHI::RHIDevice& device = Renderer::GetDevice();

		// This frame slot's claims are the ones that produced the results the
		// device just resolved -- the pool and the list are recycled together.
		std::vector<ClaimedScope>& claimed = ClaimsFor(device.GetFrameIndex());

		const std::vector<uint64_t>& ticks = device.GetResolvedTimestamps();
		const std::vector<uint8_t>& written = device.GetResolvedTimestampFlags();
		const double period = device.GetTimestampPeriodNs();

		if (ticks.empty() || period <= 0.0)
		{
			claimed.clear();
			s_NextSlot = kFirstScopeSlot;
			return;
		}

		auto span = [&](uint32_t first, uint32_t last) -> float
		{
			if (first >= ticks.size() || last >= ticks.size())
				return -1.0f;
			if (!written[first] || !written[last])
				return -1.0f;
			// Unsigned, and the end can legitimately equal the begin. It should
			// never be *less*; if a driver hands back something out of order,
			// report nothing rather than an enormous number.
			if (ticks[last] < ticks[first])
				return -1.0f;

			return (float)((double)(ticks[last] - ticks[first]) * period / 1.0e6);
		};

		// Summed per phase, so a phase that ran twice reports both. The editor
		// fits shadows to each viewport and runs the graph for both, and
		// reporting only one of them would understate it by half.
		for (const ClaimedScope& scope : claimed)
		{
			const float ms = span(scope.Begin, scope.End);
			if (ms < 0.0f)
				continue;

			const int index = (int)scope.Phase;
			s_GpuPhases[index] = s_GpuPhases[index] < 0.0f ? ms : s_GpuPhases[index] + ms;
			s_GpuSeen = true;
		}

		s_GpuTotal = span(kWholeFrameBeginSlot, kWholeFrameEndSlot);
		if (s_GpuTotal >= 0.0f)
			s_GpuSeen = true;

		// Read, so this slot is free for the frame about to record into it.
		claimed.clear();
		s_NextSlot = kFirstScopeSlot;
	}

	bool FrameProfiler::HasGpuTimings() { return s_GpuSeen; }

	float FrameProfiler::LivePhaseMs(FramePhase phase)
	{
		const int index = (int)phase;
		return index >= 0 && index < (int)FramePhase::Count ? s_LiveCpu[index] : 0.0f;
	}

	float FrameProfiler::LiveGpuPhaseMs(FramePhase phase)
	{
		const int index = (int)phase;
		return index >= 0 && index < (int)FramePhase::Count ? s_LiveGpu[index] : 0.0f;
	}

	float FrameProfiler::LiveGpuFrameMs() { return s_LiveGpuFrame; }

	bool ProfileScope::OpenGpuScope(FramePhase phase, uint32_t& beginSlot, uint32_t& endSlot)
	{
		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return false;

		if (!FrameProfiler::ClaimGpuScope(phase, beginSlot, endSlot))
			return false;

		cmd->WriteTimestamp(beginSlot);
		return true;
	}

	void ProfileScope::CloseGpuScope(uint32_t endSlot)
	{
		if (RHI::RHICommandList* cmd = Renderer::GetCommandList())
			cmd->WriteTimestamp(endSlot);
	}

	void FrameProfiler::Add(FramePhase phase, float milliseconds)
	{
		const int index = (int)phase;
		if (index >= 0 && index < (int)FramePhase::Count)
			s_Phases[index] += milliseconds;
	}

	void FrameProfiler::EndFrame(float frameMs)
	{
		// Before the early return: the editor's panel is not a benchmark and
		// still wants numbers.
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			Smooth(s_LiveCpu[i], s_Phases[i]);
			if (s_GpuPhases[i] >= 0.0f)
				Smooth(s_LiveGpu[i], s_GpuPhases[i]);
		}
		if (s_GpuTotal >= 0.0f)
			Smooth(s_LiveGpuFrame, s_GpuTotal);

		if (!s_Collecting)
			return;

		FrameRecord record;
		record.Total = frameMs;
		record.GpuTotal = s_GpuTotal;
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			record.Phases[i] = s_Phases[i];
			record.GpuPhases[i] = s_GpuPhases[i];
		}

		s_Frames.push_back(record);
	}

	void FrameProfiler::StartCollecting()
	{
		s_Frames.clear();
		s_Collecting = true;
	}

	bool FrameProfiler::IsCollecting() { return s_Collecting; }

	uint32_t FrameProfiler::CollectedFrames() { return (uint32_t)s_Frames.size(); }

	void FrameProfiler::Reset()
	{
		s_Frames.clear();
		s_Collecting = false;
		s_GpuSeen = false;
		s_ClaimHistory.clear();
		s_NextSlot = kFirstScopeSlot;
	}

	namespace
	{
		// Frames whose GPU value never resolved are left out of the average
		// rather than counted as zero, which would drag it toward nothing.
		float MeanResolved(float FrameRecord::* member)
		{
			double sum = 0.0;
			uint32_t n = 0;
			for (const FrameRecord& frame : s_Frames)
			{
				if (frame.*member >= 0.0f) { sum += frame.*member; n++; }
			}
			return n > 0 ? (float)(sum / n) : -1.0f;
		}
	}

	float FrameProfiler::MeanGpuFrameMs() { return MeanResolved(&FrameRecord::GpuTotal); }

	float FrameProfiler::MeanGpuPhaseMs(FramePhase phase)
	{
		const int index = (int)phase;
		if (index < 0 || index >= (int)FramePhase::Count)
			return -1.0f;

		double sum = 0.0;
		uint32_t n = 0;
		for (const FrameRecord& frame : s_Frames)
		{
			if (frame.GpuPhases[index] >= 0.0f) { sum += frame.GpuPhases[index]; n++; }
		}
		return n > 0 ? (float)(sum / n) : -1.0f;
	}

	float FrameProfiler::MeanFrameMs()
	{
		if (s_Frames.empty())
			return 0.0f;

		double sum = 0.0;
		for (const FrameRecord& frame : s_Frames)
			sum += frame.Total;

		return (float)(sum / (double)s_Frames.size());
	}

	float FrameProfiler::PercentileFrameMs(float fraction)
	{
		if (s_Frames.empty())
			return 0.0f;

		std::vector<float> totals;
		totals.reserve(s_Frames.size());
		for (const FrameRecord& frame : s_Frames)
			totals.push_back(frame.Total);

		std::sort(totals.begin(), totals.end());

		const size_t index = (size_t)std::clamp(
			(double)fraction * (double)(totals.size() - 1), 0.0, (double)(totals.size() - 1));
		return totals[index];
	}

	float FrameProfiler::MedianFrameMs() { return PercentileFrameMs(0.5f); }

	float FrameProfiler::MinFrameMs()
	{
		if (s_Frames.empty())
			return 0.0f;

		float smallest = s_Frames.front().Total;
		for (const FrameRecord& frame : s_Frames)
			smallest = std::min(smallest, frame.Total);
		return smallest;
	}

	float FrameProfiler::MaxFrameMs()
	{
		if (s_Frames.empty())
			return 0.0f;

		float largest = s_Frames.front().Total;
		for (const FrameRecord& frame : s_Frames)
			largest = std::max(largest, frame.Total);
		return largest;
	}

	float FrameProfiler::MeanPhaseMs(FramePhase phase)
	{
		const int index = (int)phase;
		if (s_Frames.empty() || index < 0 || index >= (int)FramePhase::Count)
			return 0.0f;

		double sum = 0.0;
		for (const FrameRecord& frame : s_Frames)
			sum += frame.Phases[index];

		return (float)(sum / (double)s_Frames.size());
	}

	void FrameProfiler::LogReport(const char* label)
	{
		if (s_Frames.empty())
		{
			RV_CORE_WARN("[benchmark] no frames were collected");
			return;
		}

		const EngineConfig& config = EngineConfig::Get();
		const float mean = MeanFrameMs();

		RV_CORE_INFO("[benchmark] {0}", label);
		RV_CORE_INFO("[benchmark]   {0}, vsync {1}, validation {2}, {3}x{4}, {5} frames",
					 EngineConfig::BackendName(config.Backend),
					 config.VSync ? "ON" : "off",
					 config.EnableValidation ? "on" : "off",
					 config.WindowWidth, config.WindowHeight,
					 (uint32_t)s_Frames.size());

		// Said every time, because the last frame-time number this project had
		// was quoted without it and meant nothing as a result.
		if (config.VSync)
			RV_CORE_WARN("[benchmark]   vsync is ON: this is the display's refresh, "
						 "not the renderer's cost. Re-run with --vsync=off.");

		RV_CORE_INFO("[benchmark]   frame  mean {0:.3f} ms  median {1:.3f}  p95 {2:.3f}  "
					 "min {3:.3f}  max {4:.3f}  ({5:.1f} FPS)",
					 mean, MedianFrameMs(), PercentileFrameMs(0.95f),
					 MinFrameMs(), MaxFrameMs(), mean > 0.0f ? 1000.0f / mean : 0.0f);

		const float gpuFrame = MeanGpuFrameMs();
		const bool gpu = HasGpuTimings();

		RV_CORE_INFO("[benchmark]   {0:<22} {1:>9}  {2:>9}", "phase", "CPU ms", "GPU ms");

		auto column = [](float value)
		{
			// A phase with no GPU pair is not a phase that cost nothing.
			return value >= 0.0f ? fmt::format("{:.3f}", value) : std::string("  --");
		};

		float accounted = 0.0f;
		float gpuAccounted = 0.0f;
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			const float phase = MeanPhaseMs((FramePhase)i);
			const float phaseGpu = MeanGpuPhaseMs((FramePhase)i);
			accounted += phase;
			if (phaseGpu >= 0.0f)
				gpuAccounted += phaseGpu;

			RV_CORE_INFO("[benchmark]   {0:<22} {1:>9.3f}  {2:>9}",
						 kPhaseNames[i], phase, column(phaseGpu));
		}

		RV_CORE_INFO("[benchmark]   {0:<22} {1:>9.3f}  {2:>9}", "accounted",
					 accounted, gpu ? column(gpuAccounted) : std::string("  --"));

		// The interesting line, and the reason the GPU column exists. Whatever
		// the frame costs beyond the CPU total is the loop waiting; until there
		// were GPU timings, what it was waiting *for* was a guess.
		RV_CORE_INFO("[benchmark]   {0:<22} {1:>9.3f}  -- CPU idle: the GPU, the "
					 "present, or the vsync wait", "unaccounted", mean - accounted);

		if (gpu && gpuFrame >= 0.0f)
		{
			RV_CORE_INFO("[benchmark]   {0:<22} {1:>9.3f} ms of GPU work in a {2:.3f} ms frame",
						 "whole frame (GPU)", gpuFrame, mean);

			// The wait row is real time but not real *work* -- counting it
			// toward "CPU bound" would call a vsync-limited frame CPU bound,
			// which is the exact misreading the row exists to end.
			const float waiting = MeanPhaseMs(FramePhase::Wait);
			const float working = accounted - waiting;

			// No verdict under vsync at all. The wait row catches Vulkan's
			// block, but OpenGL's driver throttles inside whatever call
			// overfills its queue -- ImGui's submission, usually -- so its
			// vsync wait masquerades as a fat *working* phase and any verdict
			// computed from these numbers repeats the lie.
			RV_CORE_INFO("[benchmark]   verdict: {0}",
						 config.VSync
						   ? "at the display's refresh (vsync ON); the phase split "
							 "includes the driver's throttle wherever it blocks"
						   : (gpuFrame > mean * 0.85f
								? "GPU bound -- the frame is about as long as the GPU work in it"
								: (working > mean * 0.85f
									 ? "CPU bound -- the phases account for the frame"
									 : (waiting > mean * 0.5f
										  ? "waiting -- most of the frame is the display's "
											"refresh or the GPU being waited on"
										  : "neither dominates; the gap is present or vsync"))));
		}
		else
		{
			RV_CORE_WARN("[benchmark]   no GPU timings resolved; the split above is CPU only");
		}

		RV_CORE_INFO("[benchmark]   {0} lights, busiest cluster holds {1}",
					 Renderer3D::GetLightCount(), Renderer3D::GetMaxCellLoad());
		RV_CORE_INFO("[benchmark]   {0} mesh draws, {1} culled, {2} triangles",
					 Renderer3D::GetDrawCallCount(), Renderer3D::GetCulledCount(),
					 Renderer3D::GetTriangleCount());
	}
}
