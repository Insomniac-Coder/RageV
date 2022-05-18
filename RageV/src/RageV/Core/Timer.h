#pragma once

struct ProfileData
{
	const char* name;
	double time;
};

template<typename Fn>
class Timer
{
public:
	Timer(const char* name, Fn&& func) : m_Name(name), m_Func(func)
	{
		m_Start = std::chrono::high_resolution_clock::now();
	}

	~Timer()
	{
		std::chrono::steady_clock::time_point m_End = Stop();
		std::chrono::duration<double, std::micro> difference = m_End - m_Start;

		m_Func({ m_Name, difference.count() });
	}
private:
	std::chrono::steady_clock::time_point Stop()
	{
		return std::chrono::high_resolution_clock::now();
	}
	const char* m_Name;
	std::chrono::steady_clock::time_point m_Start;
	Fn m_Func;
};

#define PROFILER(name) Timer time##__LINE__(name, [&](ProfileData profileData) { m_ProfileDataList.push_back(profileData); })