#include <rvpch.h>
#include "UUID.h"
#include <random>

namespace RageV
{
	namespace
	{
		// One generator for the process. Seeded from random_device rather than
		// the clock so two scenes created in the same millisecond -- which the
		// deserializer can easily do -- cannot produce the same ID sequence.
		std::mt19937_64& Engine()
		{
			static std::random_device seed;
			static std::mt19937_64 engine(seed());
			return engine;
		}

		// Lower bound of 1: zero is reserved for "no reference".
		std::uniform_int_distribution<uint64_t>& Distribution()
		{
			static std::uniform_int_distribution<uint64_t> distribution(1, UINT64_MAX);
			return distribution;
		}
	}

	UUID::UUID()
		: m_Value(Distribution()(Engine()))
	{
	}
}
