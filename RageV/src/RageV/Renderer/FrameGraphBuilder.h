#pragma once
#include "RageV/Renderer/RenderGraph.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/RenderSettings.h"
#include "RageV/Renderer/TemporalHistory.h"
#include "RageV/Renderer/AutoExposure.h"
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

		// Where auto exposure keeps what it has adapted to.
		//
		// One per frame chain, for the same reason History is and with the
		// same consequence for getting it wrong: two chains sharing one
		// adapted value would each drag the other's exposure about, every
		// frame. ENGINE-NOTES 7y and 7u.
		//
		// Null means the frame uses the profile's manual exposure, which is
		// also what happens when the profile has not asked for auto exposure
		// at all -- so a caller that does not care about the feature does not
		// have to know it exists.
		ExposureState* Exposure = nullptr;

		// Where screen-space reflections keep last frame's trace for this
		// frame's lighting to read. One per frame chain, ping-ponged, exactly
		// as History is and for the same reasons; the two chains of the editor
		// are different sizes showing different cameras, and a trace made for
		// one is nonsense to the other. ENGINE-NOTES 7af.
		//
		// Null means the feature cannot run for this caller -- there is
		// nowhere to keep the trace -- and the frame renders with the probe
		// alone, which is what a probe capture and scenetest want. A caller
		// that has asked for SSR on its profile and passes null gets no
		// reflections and no error, the same shape as TAA with no History.
		TemporalHistory* Reflections = nullptr;

		// Where global illumination keeps last frame's indirect diffuse for
		// this frame's lighting to read. ENGINE-NOTES 7av.
		//
		// One per frame chain, ping-ponged, for the reasons History and
		// Reflections are: the editor's two chains are different sizes showing
		// different cameras, and an indirect buffer made for one is nonsense
		// to the other.
		//
		// **Albedo-free**: RGB is the irradiance arriving from the scene, A the
		// confidence, and the lit shader multiplies by the surface's own base
		// colour when it reads it. That is what lets the screen-space form
		// stop standing its lit pixel in for an albedo it never had.
		//
		// Whichever form is enabled writes it -- the screen-space gather or the
		// traced ray pass -- so the exclusivity rule is a choice of writer
		// rather than two code paths.
		//
		// Null means the feature cannot run for this caller: there is nowhere
		// to keep the buffer, so the frame renders with the probe alone. The
		// same shape TAA has with no History, and what a probe capture and
		// scenetest want.
		TemporalHistory* Indirect = nullptr;

		// **Where the ray budget keeps its allocation between frames.** One
		// texel per 16x16 tile: the counts each ray type was given, and the
		// luminance the tile had, which is how the next frame measures what
		// moved. The history is not an optimisation -- the temporal term is the
		// design's most useful importance signal, and eased allocation is what
		// stops a count stepping visibly, so without this pair the allocator can
		// neither see motion nor avoid popping.
		//
		// Null means no adaptive budget for this caller: every pixel takes the
		// average, which is the fixed behaviour that came before. Probe captures
		// and scenetest want exactly that.
		TemporalHistory* RayBudget = nullptr;

		// **Ray-traced occlusion's own accumulation.**
		//
		// RTAO is the one noisy term with no temporal filter at all: it is
		// applied *after* the TAA resolve, so nothing downstream ever averages
		// it, and a 9x9 separable blur is its whole defence -- which is why
		// contact shadows come out softer than the rays could resolve.
		//
		// Its own history rather than TAA's, deliberately. Moving the pass
		// inside TAA would only help people running TAA, and this engine ships
		// MSAA and FXAA too; a filter that belongs to the effect works whatever
		// the anti-aliasing is. It also drags the ray budget's block with it,
		// since AO reads the allocation.
		//
		// Null disables it, and then AO behaves exactly as it did.
		TemporalHistory* Occlusion = nullptr;

		// **The frame time the loop handed down**, and the reason it is passed
		// rather than measured: an adaptation driven by this is a function of
		// the frame *number* under --frame-time, which is what keeps every
		// screenshot comparison in this repository valid with the feature on.
		// ENGINE-NOTES 7y.
		float DeltaSeconds = 0.0f;

		// The three settings blocks the frame reads, from their three owners:
		// the scene, the project, and the camera's profile. Copies rather than
		// pointers, because the passes below outlive this call -- a graph pass
		// is a lambda that runs at execute time, and capturing a reference to
		// the caller's stack is how a frame ends up grading itself with
		// whatever the memory held. ENGINE-NOTES 7s.
		SceneEnvironment Environment;
		RenderSettings Render;
		PostSettings Post;

		// The camera's clip planes, which is what turns a depth buffer value
		// back into metres. Depth of field, motion blur and SSAO all read
		// them, and they need real distances rather than the non-linear 0..1
		// the buffer holds.
		//
		// Passed rather than derived: the graph never sees the projection, and
		// recovering the planes from a matrix it does not have would be
		// inventing a dependency to avoid two floats. ENGINE-NOTES 7z.
		float NearClip = 0.05f;
		float FarClip = 1000.0f;

		// Inverses of the projection's [0][0] and [1][1] -- what turns an NDC
		// coordinate back into a view-space one, which is how SSAO
		// reconstructs positions from depth. Same passed-not-derived argument
		// as the planes above. The defaults are a 90-degree square frustum:
		// wrong for almost any real camera, but a caller that never sets them
		// gets occlusion with a skewed radius rather than a crash, and the
		// only such callers are tests that never look. ENGINE-NOTES 7ac.
		float InvProjection0 = 1.0f;
		float InvProjection1 = 1.0f;

		// The camera's view matrix. SSR needs its rotation to bring the
		// world-space normal the scene wrote into the view space its ray
		// marches in. Identity when nobody sets it, which makes an unset
		// caller's reflections point along world axes -- visibly wrong and
		// never a crash, and only tests are such callers. ENGINE-NOTES 7ad.
		Mat4 View{ 1.0f };

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

	// The same, for ray tracing (ENGINE-NOTES 7am, 7an): the project's
	// checkbox, then --raytracing=, then the device -- on on a device without
	// ray queries is off, and the fallback is logged once. Everything that
	// needs to know whether the rays are running asks here.
	bool ResolveRayTracing(const RenderSettings& render);

	// The two traced twins (ENGINE-NOTES 7ao): the project's checkbox under
	// ResolveRayTracing's answer, then --rt-reflections= / --rt-ao=, then
	// what the device can. Reflections also need bindless materials to shade
	// a hit, and say so once when they cannot. The post profile's SSR and
	// SSAO rows ask these to know whether they have been taken over.
	RayDetail ResolveRayTracedReflections(const RenderSettings& render);
	// The roughness window a mirror ray is weighed in over, per level.
	Vec2 RayDetailGloss(RayDetail detail);
	AoDetail ResolveRayTracedAmbientOcclusion(const RenderSettings& render);
	// The third (ENGINE-NOTES 7at): indirect diffuse cast from the shading
	// point rather than gathered off the screen. Needs bindless as well, for
	// the same reason reflections do. The post profile's Global illumination
	// row asks this to know whether it has been taken over.
	RayDetail ResolveRayTracedGlobalIllumination(const RenderSettings& render);
	// What a level costs per pixel. Stated once so the target's scale and the
	// shader's ray count cannot be given different answers.
	//
	//   Low  2 rays, quarter res    Medium  2 rays, half    High  4 rays, half
	//
	// Nothing traces at full resolution: resolution is the cheaper axis and the
	// harder one to see. What the trace target is divided by, and how many rays
	// each of its pixels casts.
	uint32_t RayDetailDivisor(RayDetail detail);
	int      RayDetailRays(RayDetail detail);
	// How deep the traced bounce goes (ENGINE-NOTES 7ax): the project's
	// setting under --gi-bounces=, clamped to 1 or 2 here as well as at the
	// flag, because a serialized int can hold anything and the shader reads
	// this as a loop bound.
	int ResolveGiBounces(const PostSettings& post);
	// The voxel form (8.1, ENGINE-NOTES 7bc): the project's setting under
	// --voxel-gi=, and only where the device can -- compute and fragment-stage
	// image stores, which VoxelGI::IsReady answers. Does not ask whether the
	// traced form wins; BuildFrame and the scene resolve that order
	// themselves, rays first. The post profile's GiRadius row asks this to
	// know whether it has been taken over.
	bool ResolveVoxelGlobalIllumination(const PostSettings& post);

	// This frame's sub-pixel offset for a target of this size, in NDC.
	//
	// A centred Halton(2,3) point, so successive frames land in the gaps the
	// earlier ones left rather than wherever a random generator puts them.
	// Indexed by the frame *number*, and the sequence repeats every `phase`
	// frames -- which is what makes "frame 30 and frame 38 are the same
	// picture" something a check can assert, and a clock cannot accidentally
	// satisfy. ENGINE-NOTES 7r.
	//
	// `scale` is the offset's reach as a fraction of a pixel and `phase` is
	// the sequence length; both come from RenderSettings, and both are
	// clamped here rather than at the call site so that a hand-edited project
	// file cannot ask for a phase of zero and divide by it. Defaulted to what
	// they were when they were constants, so the checks that call this
	// directly say what they are testing and nothing else has to.
	//
	// In NDC rather than pixels because the shader subtracts it back out of
	// the motion vectors, and a quantity stated twice in two conventions is a
	// quantity applied twice on one axis and never on the other.
	Vec2 TemporalJitter(uint64_t frame, uint32_t width, uint32_t height,
						float scale = 1.0f, int phase = 8);

	// The sequence length actually used for a given setting: the clamp above,
	// exposed, so a check can ask "which frame repeats this one" instead of
	// reproducing the clamp and drifting from it.
	uint32_t TemporalJitterPhase(int phase = 8);

	// The same projection, shifted by a sub-pixel offset in NDC.
	//
	// Applied to the camera once, where the scene is drawn, so that everything
	// in the scene pass -- meshes, sky, grid, particles, world text -- moves
	// together. Applying it per renderer would mean each new one has to
	// remember, and the symptom of forgetting is a half-pixel misregistration
	// that reads as "the temporal filter is soft on particles".
	Mat4 JitterProjection(const Mat4& projection, const Vec2& ndcOffset);
}
