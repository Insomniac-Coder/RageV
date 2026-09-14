#pragma once
#include <vector>
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
		// RT-6.3: where the eye was. The reflection accumulator needs it to
		// rebuild last frame's reflection direction -- the one thing that
		// changes on a mirror the camera orbits while every surface test it
		// has says nothing has changed at all.
		Vec4 Eye{ 0.0f, 0.0f, 0.0f, 0.0f };
		// RT-6.9: and which way it looked. A previous facing is not
		// recoverable from the view-projection without inverting it, and the
		// camera-cut test needs one; written beside the eye by the same code.
		Vec4 Forward{ 0.0f, 0.0f, -1.0f, 0.0f };
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
		// **And a third**, for the reflection accumulator, which remembers the
		// reflector under each texel (normal, plane, image distance) and what
		// it learned about it (roughness, moments, which history it took).
		void Prepare(RHI::RHIDevice& device, uint32_t width, uint32_t height,
					 RHI::Format format, const char* name = "TemporalHistory",
					 RHI::Format secondFormat = RHI::Format::Undefined,
					 RHI::Format thirdFormat = RHI::Format::Undefined,
					 RHI::Format fourthFormat = RHI::Format::Undefined,
					 // RT-6.5: and a fifth, for the reflection accumulator's object
					 // id -- the one history test that position, facing and material
					 // cannot stand in for, because the plane test's tolerance is a
					 // quarter of a metre at twenty.
					 RHI::Format fifthFormat = RHI::Format::Undefined);

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
		RHI::Format m_ThirdFormat = RHI::Format::Undefined;
		RHI::Format m_FourthFormat = RHI::Format::Undefined;   // the pair kind's twin (RT-first T5)
		RHI::Format m_FifthFormat = RHI::Format::Undefined;    // the reflector's object id (RT-6.5)
		bool m_Valid = false;
	};

	// **Measured change (docs/RT-MEASURED-CHANGE.md, phase 1): what the next
	// frame needs to light last frame's sampled points again.**
	//
	// One per frame chain, for TemporalHistory's reason. The record is one
	// target of eight RGBA32F lanes on the 3x3 block grid -- one sampled pixel
	// per block: its world position and packed pixel, the G-buffer's surface,
	// albedo and id texels exactly as the trace read them, the luminance the
	// trace wrote there, and the lamps the trace chose and their weights.
	// Beside it, the numbers the trace walked its random draws under, which a
	// re-light has to walk again: the frame, whether the draws were animated,
	// the lamps per pixel, the eye, and the upload's lamp order.
	//
	// **One target, not a pair.** The re-light reads it before the record pass
	// writes it, in the same frame, so nothing samples what it draws into; and a
	// record is only rewritten when something the direct light depends on has
	// changed since it was taken -- a still scene keeps the one it has.
	struct MeasuredChangeHistory
	{
		RHI::Ref<RHI::RHIRenderTarget> Record;
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint32_t Lanes = 0;
		std::vector<uint32_t> LightIds;
		float Frame = 0.0f;
		float Animated = 0.0f;
		float Rays = 0.0f;
		Vec4 Eye{ 0.0f, 0.0f, 0.0f, 0.0f };
		// The camera the record was taken from. The map a re-light makes is read
		// where each pixel's history came from -- last frame's grid -- so a
		// re-light runs only on a record taken under last frame's camera, and the
		// record is retaken every frame the camera moves.
		Mat4 View{ 1.0f };
		float InvProjection0 = 0.0f;
		float InvProjection1 = 0.0f;
		// And last frame's camera, whatever was recorded.
		Mat4 LastView{ 1.0f };
		float LastInvProjection0 = 0.0f;
		float LastInvProjection1 = 0.0f;
		bool HaveLast = false;
		// Everything the direct light depended on when the record was taken --
		// the lamps and their cull records, the cluster and field blocks, the
		// ray structure's contents -- as one number (not the camera: nothing a
		// re-light reads moves with it). The same number now means a re-light
		// could only answer "no change", so it is not run.
		uint64_t RecordKey = 0;
		bool Written = false;
		// Set by the re-light as it runs: whether this frame has a change map the
		// accumulate and the resolve should read.
		bool RelitThisFrame = false;

		// Makes sure the record exists at this size and lane count (eight for the
		// direct light, whose choices are part of it; four for the reflections);
		// a new one holds nothing.
		void Prepare(RHI::RHIDevice& device, uint32_t width, uint32_t height, uint32_t lanes = 8);
		void Invalidate()
		{
			Written = false;
			RelitThisFrame = false;
			HaveLast = false;
		}
		void Release()
		{
			Record = nullptr;
			Width = Height = 0;
			LightIds.clear();
			Invalidate();
		}
	};
}
