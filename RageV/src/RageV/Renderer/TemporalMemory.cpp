#include <rvpch.h>
#include "TemporalMemory.h"
#include "RageV/Core/EngineConfig.h"

#include <algorithm>
#include <cmath>

namespace RageV
{
	namespace
	{
		// In the engine's module, not the header: the runtime and the editor
		// are other modules, and a function-local static in a header would be
		// a second copy there (the ImGui two-module trap).
		uint32_t s_Current = 0;
		uint32_t s_Last = 0;
		uint32_t s_Still = 0;
		uint32_t s_ReferenceSamples = 0;

		std::string Define(const char* name, uint32_t frames)
		{
			return std::string(name) + "=" + std::to_string(frames) + ".0";
		}
	}

	std::vector<std::string> TemporalMemory::ShaderDefines()
	{
		return { Define("RV_MEMORY_TAA", kTaa), Define("RV_MEMORY_SETTLED", kSettled),
				 Define("RV_MEMORY_GI", kGiDenoise) };
	}

	void TemporalMemory::Note(float frames)
	{
		if (frames > 0.0f)
			s_Current = std::max(s_Current, (uint32_t)std::ceil(frames));
	}

	void TemporalMemory::BeginFrame()
	{
		s_Last = s_Current;
		s_Current = 0;
	}

	uint32_t TemporalMemory::Longest()
	{
		return std::max(s_Last, s_Current);
	}

	void TemporalMemory::Observe(bool cameraStill, bool lightingReady)
	{
		s_Still = cameraStill && lightingReady ? s_Still + 1 : 0;
	}

	bool TemporalMemory::Settled()
	{
		// RT2-2b: the reference has one memory, its mean, and it is full at N.
		if (const uint32_t goal = EngineConfig::Get().ReferenceSamples; goal > 0)
			return s_ReferenceSamples >= goal;
		// Strictly more: the frame that fills the longest history is the one
		// after it has been written that many times.
		return s_Still > Longest();
	}

	void TemporalMemory::NoteReferenceSamples(uint32_t samples)
	{
		s_ReferenceSamples = samples;
	}

	uint32_t TemporalMemory::StillFrames()
	{
		return s_Still;
	}
}
