#pragma once
#include <cstdint>
#include <functional>

namespace RageV
{
	// A 64-bit random identifier, not an RFC 4122 UUID.
	//
	// 64 bits is enough at scene scale -- a million entities in one project
	// gives a collision probability around 3e-8 -- and it fits in a register,
	// which matters because every entity and asset reference will carry one.
	//
	// Zero is reserved to mean "no reference" and is never generated, so a
	// default-constructed field and a real ID cannot be confused.
	class UUID
	{
	public:
		UUID();                                    // random, never zero
		constexpr UUID(uint64_t value) : m_Value(value) {}
		UUID(const UUID&) = default;

		constexpr operator uint64_t() const { return m_Value; }
		constexpr bool IsValid() const { return m_Value != 0; }

		static constexpr UUID Invalid() { return UUID(0); }

	private:
		uint64_t m_Value;
	};
}

namespace std
{
	template<>
	struct hash<RageV::UUID>
	{
		size_t operator()(const RageV::UUID& uuid) const noexcept
		{
			// Already uniformly distributed; hashing it again would only cost
			// cycles.
			return static_cast<size_t>(static_cast<uint64_t>(uuid));
		}
	};
}
