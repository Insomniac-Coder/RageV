#include <rvpch.h>
#include "FrameProfiler.h"
#include <atomic>
#include <cstdlib>
#include <new>

// **Allocations per frame (RT2-2f)**: the engine module's operator new,
// replaced only to count. It allocates exactly as the default one does --
// malloc, and free in delete -- so memory crossing into the runtime or the
// editor (same shared C runtime, /MD) is freed by the same heap whichever
// module frees it. The array and nothrow forms are left to the library, whose
// defaults call these two; the aligned forms keep their own pair, uncounted
// (the engine aligns a handful of things, at startup).
//
// A relaxed add per allocation: a few nanoseconds against the allocation
// itself, which is the point -- the counter cannot make what it counts look
// cheaper.

namespace
{
	// Constant-initialised, so it is zero before any constructor runs --
	// operator new is called during static initialisation too.
	std::atomic<uint64_t> s_Allocations{ 0 };
}

uint64_t RageV::AllocationCount()
{
	return s_Allocations.load(std::memory_order_relaxed);
}

void* operator new(std::size_t size)
{
	s_Allocations.fetch_add(1, std::memory_order_relaxed);
	for (;;)
	{
		if (void* memory = std::malloc(size ? size : 1))
			return memory;
		std::new_handler handler = std::get_new_handler();
		if (!handler)
			throw std::bad_alloc();
		handler();
	}
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	std::free(memory);
}
