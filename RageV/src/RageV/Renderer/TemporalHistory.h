#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// What the image in TemporalHistory::Previous() was drawn with.
	//
	// Reprojection asks "where was this pixel last frame", and *last frame*
	// means this chain's last frame. So the matrix that answers it has exactly
	// the scope the history does, and lives with it.
	//
	// **This was one value for the whole process, and that was the ghost.**
	// It was written by every BeginScene and read by the next, on the argument
	// that the scene pass is the last caller in a frame -- true of the runtime,
	// and false of the editor, which draws the viewport and the game view in
	// one frame from two different cameras. The game view then differenced its
	// camera against the *editor* camera, so every pixel carried the velocity
	// of the gap between them, and TAA dutifully fetched its history from
	// there: a second copy of the whole scene, faint at feedback 0.6, sliding
	// as the editor camera moved. ENGINE-NOTES 7u.
	struct CameraMotion
	{
		Mat4 ViewProjection{ 1.0f };
		Vec2 Jitter{ 0.0f, 0.0f };
	};

	// Somewhere for a temporal filter to keep last frame's result.
	//
	// **Owned by whoever builds the frame, not by the render graph**, and that
	// is forced rather than chosen: the graph pools its targets by shape and
	// hands out whichever is free, so a target has no identity from one frame
	// to the next. "The image I wrote last frame" is not a thing it can
	// express. The editor's viewport image is owned outside for the same
	// reason, and RenderGraph::Import exists to bring one back in.
	//
	// A pair, ping-ponged: this frame writes one and reads the other, so the
	// filter never samples the target it is drawing into. The one being
	// written is also what the rest of the chain reads -- bloom and tone
	// mapping consume the accumulated image, not the jittered one -- so there
	// is no copy anywhere.
	//
	// One of these per *frame chain*, not one per process. The editor builds
	// two frames from the same scene, its viewport and the game's, and they
	// are different sizes showing different cameras; a shared history would
	// have each dragging the other's image behind it.
	//
	// TAA was the first user. Screen-space reflections are the second: the
	// trace written at the end of one frame is what the next frame's lighting
	// reads (ENGINE-NOTES 7af), and "last frame's result, one per chain, a
	// pair so nothing reads what it writes" is exactly this class. The
	// Motion() below is TAA's alone; SSR does not touch it.
	class TemporalHistory
	{
	public:
		// Makes sure two targets of this size exist, allocating or resizing if
		// not. A size change throws the history away -- reprojecting into an
		// image of another size is meaningless, and stretching it would smear
		// a whole frame every time somebody drags a panel edge. `name` is the
		// debug name the pair is created under, so a capture can tell one
		// history from another.
		// **`secondFormat` gives the pair a second attachment**, for a filter
		// that has to remember something about a pixel besides its colour.
		//
		// The GI denoiser is the case that asked for it: a sample counter and
		// the first two luminance moments are per-pixel state that must survive
		// to the next frame, and the colour target has no room -- its alpha is
		// a validity flag the lit shader multiplies into the bounce, so it
		// cannot carry anything else. A second attachment ping-pongs with the
		// first and shares its lifetime, which is what makes the two impossible
		// to get out of step.
		//
		// Undefined means one attachment, exactly as before.
		void Prepare(RHI::RHIDevice& device, uint32_t width, uint32_t height,
					 RHI::Format format, const char* name = "TemporalHistory",
					 RHI::Format secondFormat = RHI::Format::Undefined);

		// This frame's output, and last frame's. Null before Prepare.
		const RHI::Ref<RHI::RHIRenderTarget>& Current() const  { return m_Targets[m_Cursor]; }
		const RHI::Ref<RHI::RHIRenderTarget>& Previous() const { return m_Targets[m_Cursor ^ 1]; }

		// Whether Previous() holds a frame of this scene at this size. False
		// on the first frame, after a resize, and after anything else that
		// makes the accumulation a lie -- the resolve then takes the current
		// frame whole rather than blending with nonsense.
		bool HasHistory() const { return m_Valid; }

		// Swaps the pair. Called by BuildFrame once the pass is declared, so a
		// caller cannot forget it and spend the session reading the frame it
		// is writing.
		void Advance();

		// Forgets the history without freeing anything. Used when the filter
		// did not run this frame: switching to FXAA for ten seconds and back
		// must not resume from a ten-second-old image.
		void Invalidate() { m_Valid = false; }

		// Frees both targets. The editor does this when a viewport closes.
		void Release();

		// The camera that drew the history, for the pass that reprojects it.
		// Handed to the scene draw by the frame graph and updated by BeginScene,
		// the same way the jitter is.
		CameraMotion& Motion() { return m_Motion; }

	private:
		CameraMotion m_Motion;

		RHI::Ref<RHI::RHIRenderTarget> m_Targets[2];
		uint32_t m_Cursor = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHI::Format m_Format = RHI::Format::Undefined;
		// Part of what a reallocation tests, like the first: asking for a
		// second attachment where there was none has to rebuild the pair, and
		// silently keeping the old one would leave a pass writing an
		// attachment that does not exist.
		RHI::Format m_SecondFormat = RHI::Format::Undefined;
		bool m_Valid = false;
	};
}
