#pragma once
#include "RageV/Core/UUID.h"
// For FocusTarget. EntityRef is a UUID in a struct and nothing else, so this
// costs no dependency worth the name -- and the struct is what stops an entity
// reference being assignable from an asset handle by accident.
#include "RageV/Scene/EntityRef.h"

namespace RageV
{
	// How a rendered frame is *graded*: what it looks like, as opposed to what
	// it cost.
	//
	// **This is the whole of a `.rvpostprofile`**, and it is the home every
	// item in roadmap phase 9 lands in -- the colour-grading LUT, auto
	// exposure, vignette and grain, depth of field, motion blur, ambient
	// occlusion and screen-space reflections. They are added here and to
	// `PostSettingsRegistry`, and nothing else has to be touched to put them
	// on disk, in the inspector and in front of a C# script. ENGINE-NOTES 7s.
	//
	// A profile is attached to a `CameraComponent`, and it is optional: a
	// camera with no profile renders with exactly these defaults. That is why
	// there is no "is this field set?" bit beside each value -- there is
	// nothing underneath a profile to inherit from, so every field it holds
	// is a value it means.
	//
	// **Editing one edits the asset**, and an asset can be shared. Two cameras
	// pointed at the same profile are two cameras that will always agree,
	// which is usually what is wanted and is occasionally a surprise; the
	// inspector says which file it is writing to for that reason.
	// How finely the screen-space bounce is gathered (ENGINE-NOTES 7az).
	// Low and Medium differ only in taps; High also gathers at full
	// resolution, which is where most of its cost is.
	// How finely ambient occlusion is computed, for either form.
	//
	// The pass has always run at half resolution and upsampled -- occlusion is
	// low frequency and paying full rate for it buys detail the blur removes,
	// which is the same trade the bloom chain and the depth-of-field gather
	// make. It is a trade rather than a law: a crease a pixel wide is the one
	// thing half resolution cannot resolve, and a scene of thin geometry wants
	// the choice. So the resolution is now the setting, and the old boolean's
	// `true` is Half -- exactly what it used to do.
	enum class AoDetail : uint32_t
	{
		Off,
		// **Fractions of the ray budget for the traced form, and of the
		// resolution for the screen-space one.** The two forms have different
		// things to spend, which is the same split RayDetail already makes:
		//
		//   ray-traced   Quarter 2 rays, Half 4 rays, Full 8 rays -- all at
		//                the frame's own resolution
		//   screen-space Quarter 1/4, Half 1/2, Full full resolution -- it
		//                has no ray count to trade
		//
		// Four rays is where the traced form ships because the 9x9 separable
		// blur after it absorbs the difference: measured on the showroom in
		// both lighting modes, camp and demo at 1280x720, eight rays against
		// four differ by 0.03 to 0.19 mean levels of 255 and leave the
		// local-noise metric unmoved to four decimals -- while costing
		// 0.550 ms against 0.286, a 48% cut to the pass and 239 us off the
		// frame, faster in six runs of six.
		//
		// Full keeps eight because the tail is not nothing: 0.1-0.2% of
		// pixels move by more than four levels and the worst measured 27,
		// past the 25-level line this engine calls visible. A scene of fine
		// contact shadows can have the rays back.
		//
		// Quarter is *inserted*, not appended, and that is safe only because
		// these are written to disk by name -- `RayTracedAmbientOcclusion:
		// Half` -- so no existing project is reinterpreted by the shift. An
		// older file's boolean `true` still maps to Half.
		Quarter,
		Half,
		Full,
	};

	// How much a ray-traced effect spends per pixel.
	//
	// The same two axes the rasterised gathers have had since 7az, and in the
	// same order: Low and Medium differ in how many rays each pixel casts,
	// High also runs at the frame's own resolution rather than half of it --
	// which is where most of its cost is and most of its sharpness. Off is
	// off, and hands the effect back to whichever screen-space form the
	// profile asks for.
	enum class RayDetail : uint32_t
	{
		Off,
		Low,
		Medium,
		High,
	};

	// **Where indirect light comes from: solved every frame, or read off disk.**
	//
	// Two options and deliberately not three. A field that is solved at runtime
	// and kept in memory -- what the reflection probes call `Cached` -- is a
	// useful thing for the engine to do while *producing* a bake, and a
	// confusing thing to offer an author: it looks like baking, costs like
	// realtime on the frames it runs, and survives nothing. So the choice is
	// between computing indirect light continuously and reading the answer that
	// was computed once.
	//
	// **Baked needs a field on disk**, which needs an irradiance volume in the
	// scene and a bake to have been run. Without either, the renderer says so
	// and falls back to Realtime rather than rendering a scene with no indirect
	// light at all and letting somebody wonder why it looks flat.
	// **How ray counts are spent against a cost target.**
	//
	// The counts are otherwise fixed per quality rung while their *cost* is
	// not: a ray that hits geometry costs far more than one that escapes, so
	// the same eight ambient-occlusion rays a pixel measured 3.30 ms with the
	// showroom's car close and 2.17 ms with it far. Making the count the
	// variable is what turns that into a flat frame rather than a visible dip.
	// **Exponential height fog.** Density falls off with world height, so the
	// air is thick at ground level and thin above it -- which is what a valley,
	// a harbour or a night bridge actually look like, and what a plain
	// distance fog cannot express.
	struct FogSettings
	{
		bool Enabled = false;

		// What the fog is, in linear HDR. Distant geometry tends to this, and
		// so does the sky, so it is effectively the horizon colour.
		Vec3 Color{ 0.55f, 0.60f, 0.68f };

		// Density at the reference height, per metre. The scale that matters:
		// 0.005 is a clear evening, 0.05 is weather.
		float Density = 0.02f;

		// How fast density falls with height, per metre. Zero is a uniform
		// volume -- distance fog, as a special case rather than a second mode.
		float HeightFalloff = 0.12f;

		// The world height the density is quoted at. Put it at the water, not
		// at the origin, or a scene built above zero starts in clear air.
		float Height = 0.0f;

		// How far a ray travels before any fog accumulates. Physically zero;
		// in practice a cockpit or a bonnet a metre from the camera should not
		// haze, and that is what this is for.
		float StartDistance = 0.0f;

		// The most the fog may take. Not physical -- it is how a scene keeps a
		// landmark readable through air thick enough to sell the distance in
		// front of it.
		float MaxOpacity = 1.0f;
	};

	enum class RayBudgetMode : uint32_t
	{
		// Every rung gets its full count and an expensive view simply takes
		// longer. The default, because a renderer that quietly lowers its own
		// quality is worse than a slow one when nobody asked.
		Off,

		// **A ceiling in milliseconds on the ray passes themselves.**
		//
		// The ray passes, not the frame: budgeting the whole frame is a
		// category error, because most of a frame is raster, shadow maps and
		// post that no ray count can pay for. A machine whose fixed costs
		// already exceed the target would pin the rays at their floor forever
		// and still miss it -- stripped of quality *and* slow.
		//
		// Still absolute, so it says nothing about the hardware. Right for a
		// fixed target -- a console, a kiosk, this scene on this laptop.
		Absolute,

		// **A share of the frame the rays may occupy.**
		//
		// Hardware-relative, which is the difference that matters: a slower
		// GPU renders a longer frame and spends the same *proportion* of it on
		// rays, rather than being stripped bare trying to hit a number it was
		// never going to reach. Raising the resolution or turning on
		// supersampling lengthens the frame and the rays get proportionally
		// more, which is also right -- their share of the picture has not
		// changed.
		Fractional,
	};

	enum class GiSource : uint32_t
	{
		Realtime,
		Baked,
	};

	enum class GiDetail : uint32_t
	{
		Low,      // half resolution, 12 taps
		Medium,   // half resolution, 24 taps
		High,     // full resolution, 24 taps
	};

	// How the plane of sharp focus is chosen.
	//
	// **Manual is the lens and Target is the photographer.** In Manual the
	// three optical controls mean exactly what they say and nothing moves them
	// -- which is right for a fixed shot and wrong for anything that moves,
	// because a focus distance is a distance to a *subject* and a camera that
	// travels leaves its subject behind. The showroom's orbit did precisely
	// that: focus nailed at the starting radius of 7.3 m, and at the closest
	// zoom the entire car sat in front of the near limit.
	//
	// Target names the subject instead and lets the engine solve for it every
	// frame. See PostSettings::FocusTarget for what it solves.
	enum class FocusMode : uint32_t
	{
		Manual,
		Target,
	};

	struct PostSettings
	{
		// Applied before the tone curve, which is what makes it an exposure
		// control rather than a brightness one: it slides the scene along the
		// response curve instead of scaling the result of it.
		// **Flat, like every other post field**, because the registry that
		// serialises the profile and draws the panel takes a pointer to a
		// member -- so a nested struct would need machinery that nothing else
		// here needs. FogSettings above is the shape the *pass* is handed;
		// FrameGraphBuilder fills one from these.
		bool  Fog = false;
		Vec3  FogColor{ 0.55f, 0.60f, 0.68f };
		float FogDensity = 0.02f;
		float FogHeightFalloff = 0.12f;
		float FogHeight = 0.0f;
		float FogStartDistance = 0.0f;
		float FogMaxOpacity = 1.0f;

		float Exposure = 1.0f;

		bool BloomEnabled = true;

		// Brightness at which a pixel starts to bleed. Above 1 only genuinely
		// over-bright things glow, which is usually what is wanted.
		float BloomThreshold = 1.0f;

		// Width of the ramp around the threshold. Zero is a hard cut, which
		// pops as something crosses it and reads as flickering.
		float BloomKnee = 0.5f;

		float BloomIntensity = 0.06f;

		// Ceiling on what a single pixel may contribute to bloom.
		//
		// Without one, anything very bright and very small -- the sun reflected
		// in curved metal is the usual culprit, a few hundred nits across less
		// than a texel of the mip being read -- survives the whole chain as an
		// isolated blob that floats in the air near the surface that produced
		// it. Every engine has this control for the same reason.
		//
		// It bounds the contribution, not the pixel: the scene keeps its real
		// values, and only what bleeds out of them is limited.
		float BloomClamp = 16.0f;

		// --- colour grading -----------------------------------------------------
		// A `.cube` lookup table, applied **after** the tone curve, on
		// display-referred values -- which is what a LUT exported from any
		// grading tool was authored against, so one does here what it did
		// there. The cost of that choice, stated rather than hidden: a LUT at
		// this point cannot recover highlight detail ACES has already
		// compressed. ENGINE-NOTES 7t.
		//
		// Spelled UUID rather than AssetHandle for the reason SkyTexture is:
		// they are the same type, and the asset layer already depends on this
		// one.
		UUID ColorLut = UUID::Invalid();

		// How much of the graded result is used, against the ungraded one.
		// A look is rarely wanted at full strength on the first try, and a
		// dial is the difference between grading and re-exporting.
		float ColorLutStrength = 1.0f;

		// --- lens and film ------------------------------------------------------
		// Three effects that model the camera rather than the scene, and each
		// runs at a different point in the chain because each is a different
		// physical thing. ENGINE-NOTES 7w.
		//
		// --- auto exposure (9.2). ENGINE-NOTES 7y ---------------------------
		//
		// **Off by default, and off is exact**: no compute is dispatched and
		// the tone mapping pass takes `Exposure` above unchanged. That is what
		// keeps every existing screenshot check valid without a line of change
		// in any of them, and it is the same guarantee the three lens effects
		// below rest on.
		//
		// On, `Exposure` stops being the exposure and becomes exposure
		// *compensation* -- it multiplies what the metering worked out. Taking
		// the slider away would mean the only fix for a scene the metering
		// reads wrong was switching the feature off.
		bool AutoExposure = false;

		// The stops the histogram spans. A pixel outside them lands in an end
		// bin rather than being dropped -- except below the bottom, which is
		// reserved and discarded, because a night scene is mostly pixels with
		// no light in them and letting them vote meters the darkness.
		float AutoExposureMinLog = -8.0f;
		float AutoExposureMaxLog = 4.0f;

		// The tails thrown away before averaging. This is the whole reason for
		// keeping a histogram instead of a log average: the top few percent are
		// the sun and the specular hits, and an average chases them every time
		// the camera turns past something bright.
		float AutoExposureLowPercent = 0.5f;
		float AutoExposureHighPercent = 0.95f;

		// What the metered average is exposed to. 0.18 is middle grey.
		float AutoExposureKey = 0.18f;

		// Bounds on the result, for a scene with nothing in it to meter.
		float AutoExposureMin = 0.03f;
		float AutoExposureMax = 32.0f;

		// How fast it moves, in stops per second. Converted against the frame
		// time with `1 - exp(-rate * dt)` rather than `rate * dt`, so ten steps
		// of 10 ms land where one step of 100 ms does.
		float AutoExposureSpeed = 3.0f;

		// --- depth of field (9.4). ENGINE-NOTES 7z --------------------------
		//
		// **Off by default, and off exactly**: no passes are added at all, so
		// the chain is the one that ran before the feature existed.
		//
		// Runs after the anti-aliasing resolve and before bloom. After the
		// resolve because reprojecting a temporal filter over an
		// already-defocused image asks it to reconcile a blur with a history
		// blurred differently; before bloom because a bright out-of-focus
		// highlight should glow as the disc it has become.
		bool DepthOfField = false;

		// The lens, in the units a photographer already has. The circle of
		// confusion comes from the thin-lens equation rather than from a blur
		// slider, so f/1.4 does what f/1.4 does and the relationship between
		// the three is not something to rediscover per scene.
		//
		// Manual or driven by a subject. Manual is the default and is what
		// every profile written before this existed loads as, which is the
		// behaviour those profiles were authored against.
		FocusMode Focus = FocusMode::Manual;

		// Where the plane of sharp focus is, in metres.
		//
		// **Written by hand in Manual and computed in Target**, where it
		// becomes the view-space depth of the focus target -- the depth along
		// the camera's forward axis, which is what the circle-of-confusion
		// prepass compares against, and not the straight-line distance to it.
		// The two differ by the cosine of the angle off-axis, which is nothing
		// for a subject in the middle of frame and 15% for one at the corner
		// of a wide one.
		float FocusDistance = 5.0f;

		// What to focus on, in Target mode. Empty leaves the manual distance
		// alone, so an unset target is a profile that still renders.
		//
		// **An entity reference living in an asset, which is a first for this
		// project and is a real trade-off.** A `.rvpostprofile` is shared: two
		// cameras can point at one, and the UUID in here only names something
		// in the scene it was authored against. Opening the same profile in
		// another scene draws "Missing entity" in the inspector rather than
		// silently focusing on nothing.
		//
		// It is here anyway because the alternative is worse. Putting it on
		// the CameraComponent keeps the reference scene-local and splits the
		// depth-of-field controls across two inspectors -- the mode on the
		// camera, the optics in the profile -- so a single question ("what is
		// this shot focused on?") would be answered in two places that can
		// disagree. A profile is already authored per scene in practice; a
		// reference that can go stale, and says so, is the smaller cost.
		EntityRef FocusTarget;

		// How much of the subject has to be sharp, in Target mode: the
		// fraction of its half-depth that must stay inside the depth of field.
		//
		// **This is what makes Target mode more than an autofocus.** Putting
		// the plane on the subject fixes where the sharpness is and not how
		// much of it there is -- at 4.6 m a 60 mm at f/8 has about three
		// metres of field and the showroom's car is 4.8 m long, so focusing
		// perfectly on its centre still leaves the nose soft. So the aperture
		// is solved for as well, from the subject's own measured depth:
		//
		//     N = (r / (d - r)) * f^2 / (d - f) * frameHeight / (sensor * CoC)
		//
		// which is the thin-lens circle of confusion in the prepass, inverted
		// for the f-number that puts the near edge of the subject exactly on
		// the acceptable-blur threshold.
		//
		// 1 keeps the whole subject sharp, which is the product-photograph
		// answer and the default. Lower stops *up* rather than down and throws
		// the far end away -- 0.2 is the shallow portrait look, with the near
		// fifth of the subject sharp and the rest falling off. The solved
		// f-number is clamped to the range the aperture slider offers, so a
		// subject that cannot be contained simply gets the smallest opening a
		// real lens has.
		float SubjectCoverage = 1.0f;

		// Whether the target above is actually in the scene being drawn.
		//
		// **Runtime state, not a setting**, on the same terms as
		// UIButtonComponent::Hovered: it is not registered, so it is not in the
		// scene file and not in the inspector, because a profile does not *have*
		// a resolved target -- a profile plus a scene does. A shared profile is
		// resolved in one scene and not in the other, and neither answer belongs
		// on disk.
		//
		// Written by Scene::ResolveFocus for what the renderer is handed, and by
		// the editor's profile drawer for what the inspector shows -- which is
		// what stops the greyed rows underneath claiming a target is answering
		// them when there is no target here to answer.
		//
		// True by default so that anything which never sets it behaves exactly
		// as it did: a profile read cold, a C# script listing the fields, a
		// PostSettings on the stack in a check.
		bool TargetResolved = true;

		// In millimetres, the way lenses are sold. 50 is normal on the 35 mm
		// sensor these are measured against; longer is both narrower and
		// shallower.
		float FocalLength = 50.0f;

		// The f-number. Small is a wide aperture and a shallow field: f/1.4
		// throws a background away, f/16 keeps most of a scene sharp.
		float Aperture = 2.8f;

		// Ceiling on the blur radius, in pixels of the output. The gather's
		// tap count is chosen against this: let the radius grow without bound
		// and the disc thins into a ring of separate dots, which reads as a
		// broken effect rather than as a shallow one.
		float MaxBokehRadius = 24.0f;

		// --- SSR (9.7). ENGINE-NOTES 7ad ------------------------------------
		//
		// Reflections traced through the depth buffer for anything already
		// on screen, blended over the probe's answer where the trace is
		// confident and left to the probe where it is not. Off adds no pass
		// and is exact.
		bool ScreenSpaceReflections = false;

		// How far a reflected ray may travel before it gives up, in metres.
		float SsrMaxDistance = 20.0f;

		// How far behind the depth surface a ray step may land and still
		// count as a hit, in metres. Thin objects want it small; a large
		// value lets rays hit walls through railings.
		float SsrThickness = 0.5f;

		// A scale on the traced reflection's share of the pixel. 1 is the
		// weight the material implies.
		float SsrIntensity = 1.0f;

		// --- global illumination (9.12). ENGINE-NOTES 7at -------------------
		//
		// One bounce of diffuse light gathered from what is already on the
		// screen: a red wall throws red onto the white floor beside it. The
		// same four-pass shape SSAO has -- a half-resolution gather, the two
		// blurs (literally SSAO's shader), and an apply -- because the
		// question has the same noise and the same low frequency.
		//
		// A post pass has the lit colour and the normal but not the albedo,
		// and indirect diffuse is albedo x irradiance, so the pixel's own
		// colour stands in for its albedo: a black surface receives nothing
		// and a red one receives red, which are the ends of the range that
		// matter, and a dark surface under a bright light receives too much,
		// which is the stated cost. Light bounces only from what is on
		// screen and in front of the camera -- the two failures the ray-traced
		// form exists to fix. Off adds no pass and is exact.
		bool GlobalIllumination = false;

		// Realtime or baked, for the rasterised form. Only means anything while
		// `GlobalIllumination` is on, which is why the inspector hides it
		// otherwise.
		GiSource GiSource = GiSource::Realtime;

		// World metres a bounce may travel. Small is colour bleeding in
		// corners; large is room-scale and costs cache misses, the same trade
		// the AO radius makes.
		float GiRadius = 2.0f;

		// A scale on the gathered light's share of the pixel. Read by the
		// ray-traced form too, so a scene tuned under one is not re-tuned
		// under the other.
		float GiIntensity = 1.0f;

		// How finely the screen-space gather is taken (ENGINE-NOTES 7az).
		// Half resolution and twelve taps were constants, and they are why
		// bleed read as a wash.
		//
		// The spatial blur that follows the gather is handed the *gather's*
		// dimensions, so its radius narrows with the resolution rather than
		// smearing the extra detail straight back off -- which is the way this
		// dial would otherwise buy nothing at High.
		//
		// The ray-traced form does not read this: its cost is rays, and
		// `GiBounces` below is where that lives.
		GiDetail GiQuality = GiDetail::Low;

		// How much of last frame's indirect estimate survives into this one
		// (ENGINE-NOTES 7av). Far higher than TAA's feedback because
		// irradiance is low frequency and has no edges to smear, and because
		// the estimate underneath the traced form is four rays wide -- what
		// makes those converge is accumulating a different four every frame.
		//
		// **Zero disables the accumulation exactly**, which is what makes
		// "the denoiser converges" a measurable claim rather than an
		// impression.
		//
		// **Read by the traced form only**, unlike the intensity above. The
		// screen-space gather reads the lit image, and the lit image carries
		// last frame's indirect, so accumulating its output compounds it --
		// ten times the calibrated bleed, with the two backends 2.03 levels
		// apart. Until that gather reads an image without the indirect term
		// in it, this dial does nothing on that path, and the manual says so.
		float GiDenoise = 0.9f;

		// How many times light bounces before the reflection probe answers for
		// the rest (ENGINE-NOTES 7ax). 1 shades each bounce ray's hit with the
		// probe's guess at the indirect light arriving there; 2 replaces that
		// guess with one more traced ray, whose *own* indirect term takes the
		// probe -- so the recursion ends at depth two by construction rather
		// than by a counter. Light then reaches a surface that can see nothing
		// directly lit, which one bounce leaves dark.
		//
		// Consulted by the traced form and by the voxel form (7bc), where 2
		// means the grid is also lit from last frame's grid -- one bounce more
		// each frame, converging on every bounce on a still scene. The
		// screen-space gather has one bounce and no way to have two.
		//
		// **Here rather than in Render Settings since 10.6 (7bg).** The old
		// comment said the opposite -- that this costs rays and 9.0 gives
		// Render Settings what a frame costs -- and that reading did not
		// survive contact: `GlobalIllumination` adds four passes and
		// `GiQuality` changes the gather's resolution, and both have always
		// been profile settings. The line 9.0 draws in practice is the
		// *hardware* budget, not any cost at all. The consequence is real and
		// stated: a camera cut between profiles can change the ray budget.
		int GiBounces = 1;

		// --- voxel global illumination (8.1, ENGINE-NOTES 7bc) ----------------
		//
		// Which rasterisation form answers `GlobalIllumination` above: gather
		// the bounce from a voxelised scene rather than from the screen. The
		// scene is rasterised each frame into a clipmap of 3D textures around
		// the camera, lit from the shadow cascades, mipped, and cone-traced
		// from every pixel, so light arrives from behind the camera, behind
		// other things and off every edge of the frame -- what the screen
		// gather cannot know -- on both backends, with no ray hardware.
		//
		// `GlobalIllumination` stays the on switch; this picks the form.
		// Ray-traced GI wins where it runs and the grid is then not built at
		// all, which is what greys this and the three dials below. Off by
		// default, so no project changes appearance. `GiRadius` is not read by
		// this form -- a cone runs to the cascade's edge.
		bool VoxelGlobalIllumination = false;

		// Voxels along each cascade's side: 32, 64 or 128. Memory and the
		// voxelisation cost go with the cube of it; 64 is the sensible
		// default and 128 is a statement.
		int VoxelGiResolution = 64;

		// How many cascades, 1 to 4, each covering twice the distance of the
		// last at half the detail. Three at the defaults reaches 64 metres.
		int VoxelGiCascades = 3;

		// The finest cascade's voxel, in metres. A wall thinner than this
		// leaks light through itself, and a room smaller than the cascade's
		// extent is mostly empty grid.
		float VoxelGiVoxelSize = 0.25f;

		// --- SSAO (9.6). ENGINE-NOTES 7ac -----------------------------------
		//
		// Occlusion from depth alone, applied as a multiply on the lit image
		// -- which darkens direct light too, the stated compromise of every
		// forward-plus-post AO. Treat it as contact shadowing and keep the
		// intensity restrained. Off adds no pass and is exact.
		AoDetail AmbientOcclusion = AoDetail::Off;

		// World metres the hemisphere reaches. Small is contact darkening in
		// creases; large is soft room-scale shading and costs cache misses.
		float AoRadius = 0.5f;

		// An exponent on the occlusion, so an open surface (occlusion 1) is
		// untouched at any setting and only the dark end deepens.
		float AoIntensity = 1.0f;

		// --- motion blur (9.5). ENGINE-NOTES 7ab ----------------------------
		//
		// A reconstruction gather along the motion vectors the scene already
		// writes for TAA -- the blur happens where the motion lands, so an
		// object smears over what it passes rather than stopping at its own
		// silhouette. Off adds no pass and is exact.
		bool MotionBlur = false;

		// Fraction of the frame the virtual shutter is open. 0.5 is the
		// 180-degree shutter every film camera defaults to. Scales the
		// per-frame velocity directly, so it stays honest at any frame rate.
		float MotionBlurShutter = 0.5f;

		// Ceiling on the smear in pixels, and also the tile size the dominant
		// motion is tracked at: a blur that can reach further than its tiles
		// can see tears at tile boundaries.
		float MotionBlurMaxRadius = 20.0f;

		// **All three default to off, and off is exact.** The shader branches
		// past each rather than computing a no-op, so a profile that has not
		// touched them renders the same bytes as a build without them -- which
		// is what keeps every existing screenshot check valid.

		// How dark the corners go. Runs in linear light, *before* the tone
		// curve: a vignette is less light reaching the corner, so it should
		// roll off through the same response curve the rest of the frame does.
		// Applied afterwards it multiplies display values and reads as a
		// shadow somebody painted on.
		float VignetteIntensity = 0.0f;

		// How gradually it arrives. Low is a hard circle, high is a slow
		// darkening that reaches most of the frame.
		float VignetteSmoothness = 0.5f;

		// Lateral dispersion, in fractions of the frame's width. Three taps of
		// the scene at three offsets, on the linear sample, because a lens
		// disperses before the sensor sees anything.
		//
		// The bloom is deliberately not dispersed: it is already blurred wider
		// than any sane offset, so two more taps would shift something nobody
		// could see had moved.
		float ChromaticAberration = 0.0f;

		// Film grain, applied last -- after the tone curve **and** after the
		// LUT. Grain is the texture of the recording medium, not a colour
		// anybody graded: run it before the LUT and the grade re-maps the
		// noise, so grain changes character with the look instead of sitting
		// on top of it.
		//
		// Built from two octaves of value noise rather than a hash per pixel,
		// so it is round clumps of varying size instead of a grid of squares,
		// and it is strongest in the *midtones* -- film has no variation left
		// to show once nothing is exposed or everything is. ENGINE-NOTES 7x.
		//
		// Animated, and seeded from the *frame number* rather than a clock, so
		// that rendering frame 30 twice produces the same bytes. That is the
		// rule TAA's jitter already follows and for the same reason -- see 7r.
		float FilmGrain = 0.0f;

		// How coarse the grain is, in pixels per speck -- the period of the
		// noise lattice. Larger reads as a faster stock.
		//
		// Two rather than one, because one puts the finest of the three
		// octaves past what the pixel grid can resolve: it sharpens into noise
		// instead of showing specks, which is the look this stopped being.
		float FilmGrainSize = 2.0f;
	};
}
