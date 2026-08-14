#pragma once
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Renderer/TemporalHistory.h"
#include <functional>

namespace RageV
{
	// The standard frame, built once and used by both applications.
	//
	// The editor and the runtime differ in where the image ends up -- a panel's
	// texture or the swapchain -- and in nothing else. Writing the chain twice
	// would mean two places to add a pass to, and they would drift within a
	// week of shadows landing.
	struct FrameDesc
	{
		// Where the finished image goes. The backbuffer for a game, an imported
		// target for a viewport panel.
		RGResource Output = kRGInvalid;

		uint32_t Width = 0;
		uint32_t Height = 0;

		// Draws the scene into the HDR target. Everything after that is the
		// same regardless of what drew it.
		std::function<void(RGPassContext&)> DrawScene;

		// Drawn into the HDR target after the scene, with the depth buffer
		// still attached -- collider wireframes need to be occluded by the
		// geometry they sit inside. Optional.
		std::function<void(RGPassContext&)> DrawOverlay;

		// Order-independent transparency, into accumulation and revealage
		// attachments that share the scene's depth. Optional, and costs two
		// full-resolution targets plus a resolve when it is set -- so it is
		// only wired up when something actually wants it.
		//
		// Whatever draws here must write two outputs and use the weighted
		// blend presets; see ParticleRenderer for the shape of it.
		std::function<void(RGPassContext&)> DrawTransparent;

		// Composites the two attachments above back over the scene. Given the
		// accumulation and revealage textures, in that order.
		std::function<void(RGPassContext&, const RHI::Ref<RHI::RHITexture>&,
						   const RHI::Ref<RHI::RHITexture>&)> ResolveTransparent;

		// The screen-space UI layer, drawn last of all -- after tone mapping and
		// after anti-aliasing, straight into the output in LDR.
		//
		// That position is the whole design, and it costs something worth
		// stating: **UI is not tone mapped and cannot bloom.** In exchange, a
		// glyph is never softened by FXAA and never pushed through a transfer
		// function that was fitted to a scene's dynamic range. Every engine
		// makes this trade for an overlay canvas, and it is why overlay text
		// stays crisp. See ENGINE-NOTES 7d.
		std::function<void(RGPassContext&)> DrawUI;

		// Where a temporal filter keeps last frame's result.
		//
		// Supplied by the caller because the graph cannot: its targets are
		// pooled by shape and reused within a frame, so none of them has an
		// identity that survives to the next one. Null means TAA falls back to
		// no filter at all -- which is what scenetest and a probe capture
		// want, and is a good deal better than a mode that silently
		// accumulates into whichever target happened to be free.
		//
		// One per frame chain. The editor has two, because its viewport and
		// the game's are different sizes showing different cameras.
		TemporalHistory* History = nullptr;

		// The three settings blocks the frame reads, from their three owners:
		// the scene, the project, and the camera's profile. Copies rather than
		// pointers, because the passes below outlive this call -- a graph pass
		// is a lambda that runs at execute time, and capturing a reference to
		// the caller's stack is how a frame ends up grading itself with
		// whatever the memory held. ENGINE-NOTES 7s.
		SceneEnvironment Environment;
		RenderSettings Render;
		PostSettings Post;

		Vec4 ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f };

		// The format the output expects. The chain's last pass writes it.
		RHI::Format OutputFormat = RHI::Format::R8G8B8A8_UNORM;
	};

	// Adds scene, bloom, tonemap and anti-aliasing passes to the graph.
	//
	// Nothing is executed here -- the caller compiles and executes, so a failed
	// compile is reported where the frame is owned.
	void BuildFrame(RenderGraph& graph, const FrameDesc& desc);

	// Which filter is actually running: the project's choice unless ragev.ini
	// or --aa overrode it, and None if the post-process stack failed to come
	// up.
	//
	// Public because more than the frame graph needs the answer, and two
	// places deciding it independently is a bug this repository has already
	// shipped once -- see the note on the definition.
	AntiAliasing ResolveAntiAliasing(const RenderSettings& render);

	// This frame's sub-pixel offset for a target of this size, in NDC.
	//
	// A centred Halton(2,3) point, so successive frames land in the gaps the
	// earlier ones left rather than wherever a random generator puts them.
	// Indexed by the frame *number*, and the sequence repeats every
	// TemporalJitterPhase() frames -- which is what makes "frame 30 and frame
	// 38 are the same picture" something a check can assert, and a clock
	// cannot accidentally satisfy. ENGINE-NOTES 7r.
	//
	// In NDC rather than pixels because the shader subtracts it back out of
	// the motion vectors, and a quantity stated twice in two conventions is a
	// quantity applied twice on one axis and never on the other.
	Vec2 TemporalJitter(uint64_t frame, uint32_t width, uint32_t height);
	uint32_t TemporalJitterPhase();

	// The same projection, shifted by a sub-pixel offset in NDC.
	//
	// Applied to the camera once, where the scene is drawn, so that everything
	// in the scene pass -- meshes, sky, grid, particles, world text -- moves
	// together. Applying it per renderer would mean each new one has to
	// remember, and the symptom of forgetting is a half-pixel misregistration
	// that reads as "the temporal filter is soft on particles".
	Mat4 JitterProjection(const Mat4& projection, const Vec2& ndcOffset);
}
