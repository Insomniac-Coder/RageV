#pragma once
#include "RageV/Core/Core.h"
#include "imgui.h"

// One ImGui context across a DLL boundary.
//
// ImGui keeps its entire state behind a single global pointer, and a global is
// per-module: the engine DLL and the executable each link their own copy of the
// ImGui code, so each has its own. Everything the engine draws would go to one
// context and everything the editor draws to another -- the second of which
// nobody ever begins a frame on, so its first call asserts or crashes.
//
// The fix ImGui documents is to hand the context and the allocators across
// once, from the module that created them to the module that did not. That has
// to happen in the consumer, which is why Bind() is inline here rather than a
// function in the engine: it must run against the *caller's* copy of the ImGui
// globals to have any effect.
//
// Call it once, before any other ImGui call -- OnAttach is the natural place.
namespace RageV::ImGuiBinding
{
	// The engine's context, created by ImGuiLayer.
	RV_API ImGuiContext* Context();

	// The allocators that context's memory came from. Handing these over as
	// well is not optional: a widget allocated by one module and freed by the
	// other would otherwise cross allocators, and ImGui's default allocator is
	// a static in each copy of the code.
	RV_API void Allocators(ImGuiMemAllocFunc* alloc, ImGuiMemFreeFunc* free, void** userData);

	inline void Bind()
	{
		ImGuiMemAllocFunc alloc = nullptr;
		ImGuiMemFreeFunc  free = nullptr;
		void* userData = nullptr;

		Allocators(&alloc, &free, &userData);
		if (alloc && free)
			ImGui::SetAllocatorFunctions(alloc, free, userData);

		ImGui::SetCurrentContext(Context());
	}
}
