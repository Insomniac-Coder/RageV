#pragma once

// What the loading screen is told, and by whom.
//
// Startup is split in two so that neither half has to be thread-safe:
//
//   Phase 1, on a worker: open the project, scan the registry, parse the
//     scene, and warm the import cache. All of it is files and CPU. It is
//     also all of the *slow* part -- cooking a project's textures for the
//     first time is tens of seconds.
//
//   Phase 2, on the main thread: turn the loaded scene's assets into GPU
//     resources, a slice per frame.
//
// The split is what removes the usual difficulty. The RHI is not thread-safe
// and an OpenGL context belongs to one thread, so a worker that wanted to
// create textures would need every device call marshalled back. It does not
// want to: by the time the expensive work is done, uploading is a read and a
// copy. **No device call ever leaves the main thread, because none is ever
// made anywhere else.**
//
// Throughout both phases the main thread pumps the window and draws the bar,
// which is what stops Windows painting the window white and calling it
// unresponsive. Design: ENGINE-NOTES 7l.

#include "Core.h"

#include <atomic>
#include <mutex>
#include <string>

namespace RageV::Boot
{
	// What the loading screen renders.
	struct Status
	{
		// The stage, for the line above the bar: "Opening project".
		std::string Phase;
		// What is happening inside it, for the line below: a file name.
		// Empty is normal and the screen simply leaves that line out.
		std::string Detail;
		// 0..1 across the whole boot, never backwards.
		float Fraction = 0.0f;
		bool  Done = false;
	};

	// Written by whichever thread is loading, read by the main thread every
	// frame. The lock is uncontended in practice -- one writer, one reader,
	// sixty times a second -- and a lock-free version of this would be
	// cleverness bought with nothing.
	class RV_API Progress
	{
	public:
		// A stage owning [begin, end] of the bar. The weights are guesses
		// informed by measurement rather than a live estimate: the honest
		// alternative would be timing a boot to predict a boot.
		void BeginPhase(std::string name, float begin, float end);

		// What is happening right now. Cheap enough to call per asset.
		void SetDetail(std::string detail);

		// How far through the current phase, 0..1, mapped into the range the
		// phase owns. Clamped, and never allowed to move the bar backwards --
		// a bar that retreats reads as a bug even when the number is right.
		void Advance(float withinPhase);

		void Finish();

		// The window was closed while loading. The worker polls this between
		// assets and gives up at the next safe point, so closing a slow boot
		// does not mean waiting for it to finish first.
		void Cancel();
		bool Cancelled() const { return m_Cancelled.load(std::memory_order_relaxed); }

		bool IsDone() const { return m_Done.load(std::memory_order_relaxed); }

		Status Get() const;

	private:
		mutable std::mutex m_Mutex;
		std::string m_Phase;
		std::string m_Detail;
		float m_Begin = 0.0f;
		float m_End = 1.0f;
		float m_Fraction = 0.0f;

		// Outside the lock so Cancelled() can be polled in a tight loop
		// without serializing against the writer.
		std::atomic<bool> m_Cancelled{ false };
		std::atomic<bool> m_Done{ false };
	};
}
