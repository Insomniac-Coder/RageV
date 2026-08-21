#pragma once
#include <deque>
#include <string>

namespace RageV::EditorUI
{
	// The strip along the bottom of the editor: what the project folder is
	// doing, in one line.
	//
	// **It exists because the editor had no way to say small things.** A file
	// changing on disk, an asset being reloaded, a scene being saved -- each is
	// too small for a dialog, too important to put only in a log panel nobody
	// has open, and invisible if it goes nowhere. The result was an editor that
	// silently showed a stale mesh, which is exactly the class of problem a
	// status line is for: not an error, just something that happened.
	//
	// One line, always present. A bar that appears when there is news and
	// vanishes otherwise moves every panel underneath it by its own height,
	// which is worse than the space it saves.
	class StatusBar
	{
	public:
		enum class Kind
		{
			Info,      // something happened and it went fine
			Working,   // something is happening now
			Warning,   // it went through but is worth knowing
			Error      // it did not go through
		};

		// `detail` is the second half, drawn dimmer: the message says what
		// happened and the detail says to what. Splitting them means the eye
		// finds the verb in the same place every time.
		void Post(Kind kind, std::string message, std::string detail = {});

		// The right-hand end: a standing summary rather than news, so it can
		// hold what the watcher is watching without competing with what just
		// happened.
		void SetStanding(std::string text) { m_Standing = std::move(text); }

		// Draws at the current cursor, filling the width. Height is fixed and
		// available before the call so a caller can reserve the space.
		void Draw();
		static float Height();

		// Seconds since the newest message. The bar fades on this rather than
		// clearing, because a line that disappears takes the answer to "what
		// did it just say" with it.
		void Tick(float deltaSeconds) { m_Age += deltaSeconds; }

	private:
		struct Entry
		{
			Kind What = Kind::Info;
			std::string Message;
			std::string Detail;
		};

		// Enough to answer "what were the last few things" in the tooltip and
		// no more. This is a status bar, not a log: the Build Log panel is
		// where anything worth scrolling belongs.
		static constexpr size_t kHistory = 12;

		std::deque<Entry> m_History;
		std::string m_Standing;
		float m_Age = 0.0f;
	};
}
