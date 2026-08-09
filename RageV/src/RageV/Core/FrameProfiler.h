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
	// It measures **CPU wall time only**, on purpose, and the report says so.
	// The sum of the phases against the whole frame is the useful number: if
	// they add up, the CPU is the cost and the phases say which part; if the
	// frame is much longer than the phases, the CPU is waiting and the answer is
	// on the GPU, where a timestamp query would have to go next. Reporting a
	// small CPU total as though it were the frame would be the same mistake in a
	// new place.
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

	// One phase of one frame. Adds to the phase whatever the scope cost.
	class ProfileScope
	{
	public:
		explicit ProfileScope(FramePhase phase)
			: m_Phase(phase), m_Start(std::chrono::steady_clock::now())
		{
		}

		~ProfileScope()
		{
			const auto elapsed = std::chrono::steady_clock::now() - m_Start;
			FrameProfiler::Add(m_Phase,
				(float)std::chrono::duration<double, std::milli>(elapsed).count());
		}

		ProfileScope(const ProfileScope&) = delete;
		ProfileScope& operator=(const ProfileScope&) = delete;

	private:
		FramePhase m_Phase;
		std::chrono::steady_clock::time_point m_Start;
	};
}

// Two levels, because a single ## would paste the token `__LINE__` rather than
// the line number and two scopes in one function would then collide.
#define RV_PROFILE_JOIN2(a, b) a##b
#define RV_PROFILE_JOIN(a, b) RV_PROFILE_JOIN2(a, b)
#define RV_PROFILE_PHASE(phase) \
	::RageV::ProfileScope RV_PROFILE_JOIN(rv_profile_scope_, __LINE__)(phase)
