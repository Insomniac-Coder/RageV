#include <rvpch.h>
#include "FrameProfiler.h"
#include "EngineConfig.h"
#include "RageV/Renderer/Renderer3D.h"

namespace RageV
{
	namespace
	{
		struct FrameRecord
		{
			float Total = 0.0f;
			float Phases[(int)FramePhase::Count]{};
		};

		std::vector<FrameRecord> s_Frames;
		bool s_Collecting = false;

		const char* kPhaseNames[(int)FramePhase::Count] =
		{
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
	}

	void FrameProfiler::Add(FramePhase phase, float milliseconds)
	{
		const int index = (int)phase;
		if (index >= 0 && index < (int)FramePhase::Count)
			s_Phases[index] += milliseconds;
	}

	void FrameProfiler::EndFrame(float frameMs)
	{
		if (!s_Collecting)
			return;

		FrameRecord record;
		record.Total = frameMs;
		for (int i = 0; i < (int)FramePhase::Count; i++)
			record.Phases[i] = s_Phases[i];

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

		float accounted = 0.0f;
		for (int i = 0; i < (int)FramePhase::Count; i++)
		{
			const float phase = MeanPhaseMs((FramePhase)i);
			accounted += phase;

			RV_CORE_INFO("[benchmark]   {0:<22} {1:.3f} ms  ({2:.1f}%)",
						 kPhaseNames[i], phase, mean > 0.0f ? 100.0f * phase / mean : 0.0f);
		}

		// The interesting line. These are CPU timings; whatever the frame costs
		// beyond them is the loop waiting, which is either the GPU or the
		// present. Naming the remainder is what stops a small CPU total from
		// being read as the frame.
		RV_CORE_INFO("[benchmark]   {0:<22} {1:.3f} ms  ({2:.1f}%)", "CPU accounted",
					 accounted, mean > 0.0f ? 100.0f * accounted / mean : 0.0f);
		RV_CORE_INFO("[benchmark]   {0:<22} {1:.3f} ms  -- CPU idle: the GPU, the "
					 "present, or the vsync wait", "unaccounted", mean - accounted);

		RV_CORE_INFO("[benchmark]   {0} mesh draws, {1} culled, {2} triangles",
					 Renderer3D::GetDrawCallCount(), Renderer3D::GetCulledCount(),
					 Renderer3D::GetTriangleCount());
	}
}
