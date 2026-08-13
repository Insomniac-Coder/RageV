#include <rvpch.h>
#include "Boot.h"

#include <algorithm>

namespace RageV::Boot
{
	void Progress::BeginPhase(std::string name, float begin, float end)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		m_Phase = std::move(name);
		m_Detail.clear();
		m_Begin = std::clamp(begin, 0.0f, 1.0f);
		m_End = std::clamp(end, m_Begin, 1.0f);

		// Entering a phase moves the bar to that phase's start, but only
		// forwards: a phase that turns out to be a no-op must not rewind what
		// the last one showed.
		m_Fraction = std::max(m_Fraction, m_Begin);
	}

	void Progress::SetDetail(std::string detail)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Detail = std::move(detail);
	}

	void Progress::Advance(float withinPhase)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		const float t = std::clamp(withinPhase, 0.0f, 1.0f);
		m_Fraction = std::max(m_Fraction, m_Begin + (m_End - m_Begin) * t);
	}

	void Progress::Finish()
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Fraction = 1.0f;
			m_Detail.clear();
		}

		// Last, and after the fraction is already 1: the main thread stops
		// drawing the moment it sees this, and a screen whose final frame
		// showed 94% is a screen that looks like it gave up.
		m_Done.store(true, std::memory_order_release);
	}

	void Progress::Cancel()
	{
		m_Cancelled.store(true, std::memory_order_relaxed);
	}

	Status Progress::Get() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		Status status;
		status.Phase = m_Phase;
		status.Detail = m_Detail;
		status.Fraction = m_Fraction;
		status.Done = m_Done.load(std::memory_order_acquire);
		return status;
	}
}
