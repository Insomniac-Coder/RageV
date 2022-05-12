#pragma once

namespace RageV
{

	class Timestep {

	public:
		Timestep(float time) { m_Time = time; }

		operator float() const { return m_Time; }
		const float GetMilliSeconds() const { return m_Time * 1000.f; }
		const float GetSeconds() const { return m_Time; }
	private:
		float m_Time;

	};

}