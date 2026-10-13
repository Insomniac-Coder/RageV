#pragma once
#include <atomic>
#include <cstdint>

namespace RageV::RHI
{
	// **RT2-1: GPU memory by category.** Every lasting allocation the GPU layer
	// makes is added under its category when made and taken off when the
	// allocator frees it -- inside the deletion queue, not when the last
	// reference drops -- so the categories sum to what the allocator holds, and
	// a report shows any gap as "other" rather than hiding it. What the driver
	// holds outside the allocator (the swapchain, pipelines, descriptor pools,
	// its own) is the driver's figure minus the allocator's.
	enum class MemoryCategory : uint8_t
	{
		Auto,         // chosen from the resource's usage when created
		Textures,     // sampled images: materials, LUTs, probes, the bakes
		Targets,      // images a frame draws or writes: render targets, storage images, shadow maps
		Histories,    // images kept from one frame to the next (TemporalHistory)
		Geometry,     // vertex and index buffers
		Buffers,      // other GPU-only buffers
		Uploads,      // CPU-visible buffers: per-frame data, staging, readback
		StaticBlas,   // acceleration structures of meshes that do not deform
		DynamicBlas,  // ... and of skinned meshes, rebuilt or refitted per frame
		Tlas,         // the top level and its instance buffer
		AsScratch,    // acceleration-structure build scratch
		Count
	};

	inline const char* MemoryCategoryName(MemoryCategory category)
	{
		switch (category)
		{
			case MemoryCategory::Textures:    return "textures";
			case MemoryCategory::Targets:     return "targets";
			case MemoryCategory::Histories:   return "histories";
			case MemoryCategory::Geometry:    return "geometry";
			case MemoryCategory::Buffers:     return "buffers";
			case MemoryCategory::Uploads:     return "uploads";
			case MemoryCategory::StaticBlas:  return "static BLAS";
			case MemoryCategory::DynamicBlas: return "dynamic BLAS";
			case MemoryCategory::Tlas:        return "TLAS";
			case MemoryCategory::AsScratch:   return "AS scratch";
			default:                          return "?";
		}
	}

	class MemoryCounts
	{
	public:
		static void Add(MemoryCategory category, uint64_t bytes)    { Slot(category) += bytes; }
		static void Remove(MemoryCategory category, uint64_t bytes) { Slot(category) -= bytes; }
		static uint64_t Bytes(MemoryCategory category)              { return Slot(category).load(); }

	private:
		static std::atomic<uint64_t>& Slot(MemoryCategory category)
		{
			static std::atomic<uint64_t> slots[(size_t)MemoryCategory::Count] = {};
			return slots[(size_t)category];
		}
	};

	struct MemoryReport
	{
		bool     Measured = false;           // false where the backend cannot say (OpenGL)
		uint64_t Categories[(size_t)MemoryCategory::Count] = {};
		uint64_t Allocated = 0;              // the allocator's live allocations, every heap
		uint64_t Reserved = 0;               // the device memory blocks those live in
		// The driver's own figures for the device-local heaps (VK_EXT_memory_budget):
		// this process's usage, and how much it may use before the driver pushes back.
		// Without the extension the allocator estimates them, and DriverFigures is false.
		bool     DriverFigures = false;
		uint64_t DeviceLocalUsage = 0;
		uint64_t DeviceLocalBudget = 0;

		uint64_t CategorySum() const
		{
			uint64_t sum = 0;
			for (uint64_t bytes : Categories)
				sum += bytes;
			return sum;
		}
	};
}
