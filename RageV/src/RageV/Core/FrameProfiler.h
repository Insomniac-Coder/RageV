#pragma once
#include <cstdint>
#include <chrono>

namespace RageV
{
	// Where a frame's CPU time went, and what a run of frames cost.
	//
	// This exists because the only frame-time number this engine had was one a
	// person read off a panel with vsync on, which measures the display and not
	// the renderer. Everything that followed from it -- including the claim that
	// removing 84 draws a frame was worth something -- was unsupported.
	//
	// It measures both processors. The CPU half is wall time around a scope,
	// which locates where the CPU *waits* as readily as where it works -- with
	// vsync on it will happily attribute a whole frame to whichever call
	// happened to block. The GPU half is a pair of timestamps in the frame's
	// command buffer, which measures when the recorded work actually ran.
	//
	// Reading them together is the point. Phases summing to the frame means the
	// CPU is the limit; a GPU total near the frame means it is not; neither
	// means the gap is the present or the vsync wait. Any one of the three
	// alone has been misread here before.
	enum class FramePhase
	{
		EnvironmentPrefilter,	// GGX roughness levels, once per environment
		Shadows,				// cascades, spot maps, point cubes
		Probes,					// reflection probe faces
		Graph,					// BuildFrame, record and execute
		ImGui,					// the editor's own UI
		Present,				// device EndFrame, which is where a vsync wait lands

		Count
	};

	const char* FramePhaseName(FramePhase phase);

	// The whole frame takes the first pair; everything else is handed out as
	// it is asked for.
	//
	// Two slots per phase would be simpler and is what this did first, but a
	// phase can run more than once in a frame -- the editor fits shadows to
	// each of its two viewports and runs the graph for both -- and the second
	// pass then rewrote a query the first had already written, which Vulkan
	// rejects outright. Spans are summed per phase instead, so a phase that
	// runs twice reports what both cost.
	constexpr uint32_t kWholeFrameBeginSlot = 0;
	constexpr uint32_t kWholeFrameEndSlot = 1;
	constexpr uint32_t kFirstScopeSlot = 2;

	class FrameProfiler
	{
	public:
		// Clears the current frame's phase accumulators. Called once per frame
		// by the application loop whether or not anyone is collecting.
		static void BeginFrame();

		// Files the frame away. `frameMs` is the whole frame, measured by the
		// loop, so it includes everything no phase covers.
		static void EndFrame(float frameMs);

		static void Add(FramePhase phase, float milliseconds);

		// Start collecting. Frames recorded before this are discarded, which is
		// how the warm-up -- first-frame allocation, shader compilation, the
		// first environment prefilter -- stays out of the numbers.
		static void StartCollecting();
		static bool IsCollecting();
		static uint32_t CollectedFrames();

		// Milliseconds, over the collected frames.
		static float MeanFrameMs();
		static float MedianFrameMs();
		static float PercentileFrameMs(float fraction);
		static float MinFrameMs();
		static float MaxFrameMs();
		static float MeanPhaseMs(FramePhase phase);

		// --- GPU -------------------------------------------------------------
		// Reads back whatever the device resolved this frame and files it with
		// the CPU numbers. Called once per frame by the application, after
		// BeginFrame, because that is when the device has just recycled a pool.
		//
		// The results belong to a frame a couple back, not to this one. Over a
		// run that does not matter and the average is the average; for a single
		// frame it does, which is why nothing here reports one.
		static void CollectGpu();

		static bool HasGpuTimings();

		// A rolling average kept whether or not a benchmark is collecting, so
		// the editor's panel has something to show. Smoothed, because the raw
		// per-frame value at 600 FPS is unreadable.
		static float LivePhaseMs(FramePhase phase);
		static float LiveGpuPhaseMs(FramePhase phase);
		static float LiveGpuFrameMs();
		static float MeanGpuFrameMs();
		static float MeanGpuPhaseMs(FramePhase phase);

		// Claims the next free pair of slots for `phase`. Returns false when the
		// frame has used them all, in which case the scope is CPU only.
		static bool ClaimGpuScope(FramePhase phase, uint32_t& beginSlot, uint32_t& endSlot);

		// Every collected frame's total, so a caller can do its own statistics.
		static void Reset();

		// Writes the report to the log: the phases, the CPU total, the frame,
		// and the gap between the last two -- plus the settings the numbers are
		// only meaningful next to. A frame time without its vsync state is not
		// a measurement, and printing them apart is how the last one was
		// misread.
		static void LogReport(const char* label);

	private:
		static float s_Phases[(int)FramePhase::Count];
	};

	// One phase of one frame, on both processors.
	//
	// The CPU half is this scope's wall time. The GPU half is a pair of
	// timestamps written into the frame's command buffer, which measure when
	// the work this scope recorded actually *ran* -- a different question, and
	// the one that matters once the CPU is no longer the limit.
	class ProfileScope
	{
	public:
		explicit ProfileScope(FramePhase phase)
			: m_Phase(phase), m_Start(std::chrono::steady_clock::now())
		{
			m_Gpu = OpenGpuScope(phase, m_BeginSlot, m_EndSlot);
		}

		~ProfileScope()
		{
			const auto elapsed = std::chrono::steady_clock::now() - m_Start;
			FrameProfiler::Add(m_Phase,
				(float)std::chrono::duration<double, std::milli>(elapsed).count());

			if (m_Gpu)
				CloseGpuScope(m_EndSlot);
		}

		ProfileScope(const ProfileScope&) = delete;
		ProfileScope& operator=(const ProfileScope&) = delete;

	private:
		// Out of line: writing a timestamp needs the command list, and this
		// header is included from places that must not pull the renderer in.
		static bool OpenGpuScope(FramePhase phase, uint32_t& beginSlot, uint32_t& endSlot);
		static void CloseGpuScope(uint32_t endSlot);

		FramePhase m_Phase;
		std::chrono::steady_clock::time_point m_Start;
		uint32_t m_BeginSlot = 0;
		uint32_t m_EndSlot = 0;
		// False when there was no command list to record into, or the frame ran
		// out of slots.
		bool m_Gpu = false;
	};
}

// Two levels, because a single ## would paste the token `__LINE__` rather than
// the line number and two scopes in one function would then collide.
#define RV_PROFILE_JOIN2(a, b) a##b
#define RV_PROFILE_JOIN(a, b) RV_PROFILE_JOIN2(a, b)
#define RV_PROFILE_PHASE(phase) \
	::RageV::ProfileScope RV_PROFILE_JOIN(rv_profile_scope_, __LINE__)(phase)
