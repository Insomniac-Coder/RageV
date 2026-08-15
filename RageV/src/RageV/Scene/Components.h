#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "RageV/Core/UUID.h"
#include "RageV/Scene/EntityRef.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsTypes.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Animation/Skeleton.h"
#include "SceneCamera.h"
#include "ScriptableEntity.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/Mesh.h"
#include "RageV/Renderer/Material.h"
#include "RageV/Renderer/ReflectionProbe.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// Stable identity. Added to every entity at creation, preserved across
	// save/load, and the only durable way to name an entity -- entt::entity
	// handles are recycled and mean nothing outside one registry instance.
	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
		IDComponent(UUID id) : ID(id) {}
	};

	// Parent/child links, by UUID rather than by handle so they survive
	// serialization.
	//
	// Children is derived state: only Parent is written to disk, and the child
	// lists are rebuilt on load. Storing both would let them disagree, and a
	// hierarchy that disagrees with itself is the kind of bug that only shows
	// up three features later.
	struct RelationshipComponent
	{
		UUID Parent = UUID::Invalid();
		std::vector<UUID> Children;

		RelationshipComponent() = default;
		RelationshipComponent(const RelationshipComponent&) = default;
	};

	struct TagComponent {
		std::string Name;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& name) { Name = name; }
	};

	struct TransformComponent
	{
		// Local, relative to the parent. World is derived from these.
		Vec3 Position{ 0.0f, 0.0f, 0.0f };
		Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
		Vec3 Scale{ 1.0f };

		// Recomputed unconditionally by Scene::UpdateWorldTransforms in one
		// top-down pass per frame.
		//
		// Deliberately not a dirty-flag cache. A flag has to be set at every
		// write site -- the inspector, the gizmo, scripts, the serializer, and
		// everything added later -- and a single missed one leaves an object
		// silently rendering in the wrong place. Recomputing is O(n) at scene
		// scale and cannot be got wrong.
		Mat4 World{ 1.0f };

		// Where `World` was on the previous frame, which is what a motion
		// vector is made of.
		//
		// Derived state, so it is not serialized and not shown: a saved scene
		// that carried a previous transform would be claiming an object had
		// been moving before it was loaded.
		//
		// Copied once per frame by AdvanceMotionHistory rather than inside
		// PropagateTransform, because the transforms are recomputed several
		// times a frame -- the update, a probe capture, a shadow pass -- and
		// "the value before this call" is zero motion by the second one.
		Mat4 PreviousWorld{ 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const Vec3& position) { Position = position; }

		Mat4 GetLocalTransform() const
		{
			Mat4 rotation = Math::ToMat4(Math::FromEuler(Rotation));
			return	Math::Translate(Mat4(1.0f), Position) *
					rotation *
					Math::Scale(Mat4(1.0f), Scale);
		}
	};

	struct CameraComponent
	{
		RageV::SceneCamera Camera;

		// Which camera the game view renders through: the lowest rank wins,
		// 0 highest priority through 99 lowest.
		//
		// A rank rather than the `isPrimary` flag this replaced. A boolean can
		// be true on two cameras at once, and then which one renders depends on
		// registry iteration order -- so adding a camera could silently change
		// the view. A rank always has a single winner, and ties break on entity
		// id so the answer is the same on every run.
		int ViewRank = 0;

		bool fixedAspectRatio = false;

		// How this camera's frame is graded: a `.rvpostprofile`, or nothing.
		//
		// **Optional, and invalid means the neutral grade** -- not "no post
		// processing", which would be a different and much more surprising
		// thing to have to opt out of. A camera with no profile tone maps and
		// blooms exactly the way every scene did before profiles existed.
		//
		// On the camera rather than on the scene because a grade describes a
		// *view*, and a scene can hold several: the editor already renders two
		// of the same scene at once, and one grade between them cannot tell
		// them apart. It is also where a post *volume* would write when it
		// wants to blend one grade into another, which is the direction this
		// goes next. ENGINE-NOTES 7s.
		AssetHandle PostProfile = AssetHandle::Invalid();

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
	};

	struct LightComponent
	{
		RageV::Light Light;

		LightComponent() = default;
		LightComponent(const LightComponent&) = default;
		LightComponent(const RageV::Light& light) : Light(light) {}
	};

	// How often a probe re-renders what it can see.
	enum class ProbeUpdate : uint32_t
	{
		// Captured once and kept. Costs nothing after the first frame, which is
		// what makes it the right answer for a room, a corridor, or anything
		// else whose surroundings do not move.
		Baked = 0,

		// Re-captured continuously, one face per frame by default. Costs one
		// extra scene render per frame and shows moving objects in reflections.
		Realtime = 1,
	};

	// How often a *realtime* probe takes its next capture step. A reflection
	// is seen through a rough or curved surface, so it can lag the scene by a
	// few frames without anyone noticing -- which is why the demo's probe at
	// 15 Hz looks identical to per-frame and costs a quarter as much. The
	// values are what the dropdown shows, in its order; PerFrame is last and
	// is the default because it is what every scene did before this existed.
	enum class ProbeRate : uint32_t
	{
		Hz15 = 0,
		Hz30 = 1,
		Hz45 = 2,
		Hz60 = 3,
		PerFrame = 4,
	};

	// Captures the scene into a cube map from one point, for surfaces near it
	// to reflect.
	//
	// The scene picks one probe per render: the nearest whose influence radius
	// contains the camera, and the sky otherwise. Per-object selection would
	// mean rebinding the scene descriptor set per draw, and blending between
	// probes needs both bound at once -- neither is worth it before the thing
	// works with one.
	struct ReflectionProbeComponent
	{
		ProbeUpdate Update = ProbeUpdate::Baked;

		// Per face. Reflections are seen through a rough surface or a curved
		// one, so this can be far smaller than it feels like it should be: 128
		// is six 128x128 renders and is hard to fault on anything but a mirror.
		//
		// Signed because the reflected inspector has no unsigned widget, and a
		// second numeric type for one field is worse than one clamp.
		int Resolution = 128;

		float NearClip = 0.05f;
		float FarClip = 100.0f;

		// How far from this probe its capture is still a reasonable answer.
		// Reflections come from a single point, so the further a surface is from
		// that point the more wrong the parallax, and past some distance the sky
		// is the better lie.
		float Influence = 20.0f;

		// Realtime only. Six renders in one frame is a visible hitch; one per
		// frame is a sixth of the cost and a sixth of a second of latency on a
		// reflection.
		int FacesPerFrame = 1;

		// Realtime only: how often the next capture step is taken. PerFrame is
		// the old behaviour; a Hz rate skips frames between steps, which is
		// where a realtime probe's cost actually goes.
		ProbeRate Rate = ProbeRate::PerFrame;

		// Runtime state. Not serialized: a capture is derived from the scene,
		// like a shadow map, and writing one into a text file would be storing
		// a render in a scene description.
		std::shared_ptr<ReflectionProbe> Probe;
		uint32_t NextFace = 0;
		// Seconds since the last capture step, against Rate's interval.
		float RateAccumulator = 0.0f;
		// Set when the probe has never been captured, or when something that
		// invalidates the capture changed. A baked probe watches this; a
		// realtime one ignores it and re-captures regardless.
		bool Dirty = true;

		ReflectionProbeComponent() = default;
		ReflectionProbeComponent(const ReflectionProbeComponent&) = default;
	};

	// 3D geometry, referenced by handle rather than embedded. A scene file
	// carries the handle; the vertex data stays in whatever the handle points
	// at, which is a built-in primitive or an imported model.
	struct MeshComponent
	{
		AssetHandle Mesh = PrimitiveHandle(PrimitiveType::Cube);

		// Null means the renderer's shared default material.
		//
		// A handle rather than a `Ref<Material>`, which is what this was: a Ref
		// is per entity by construction, so nothing could share a material and
		// nothing could store the texture maps -- a scene file has no way to
		// write a pointer, so an imported model's maps survived until the first
		// save and no longer.
		//
		// Invalid() spelled out, like every other asset field here, because a
		// default-constructed UUID is *random*. Left implicit, every mesh
		// claimed a material that had never existed.
		AssetHandle Material = AssetHandle::Invalid();

		// Per-entity scalar overrides, applied on top of the material's own.
		//
		// These are free, and that is why they exist rather than forcing a
		// separate asset per colour. `BaseColor`, `EmissiveColor` and `Surface`
		// already travel per instance in the renderer's instance stream --
		// deliberately, so a thousand cubes differing only in colour stay one
		// draw. Overriding them costs nothing and **cannot split a batch**,
		// because a batch is keyed on the descriptor set, and the descriptor
		// set holds the maps and the sampler and none of these.
		//
		// The line is exactly the one `Material::GetBatchKey` already draws:
		// the asset is the maps, the override is the scalars.
		//
		// Each has its own flag rather than a sentinel value, because every
		// one of these has a legitimate zero -- a black emissive, a roughness
		// of 0, a fully transparent base colour -- and a sentinel would make
		// one of them unsayable.
		bool OverrideBaseColor = false;
		Vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };

		bool OverrideEmissive = false;
		Vec4 EmissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };

		bool OverrideMetallic = false;
		float Metallic = 0.0f;

		bool OverrideRoughness = false;
		float Roughness = 0.5f;

		bool OverrideOcclusion = false;
		float Occlusion = 1.0f;

		// How strongly the material's normal map bends the surface normal. 1 is
		// the map as authored, 0 ignores it, and above 1 exaggerates.
		//
		// Free like the rest: NormalScale already travels in the instance
		// stream as Surface.w, because the shader has always read it from
		// there. Worth having per entity rather than only per material, since
		// the right strength depends on how close the object gets to the camera
		// -- the same brick at arm's length and across a courtyard want
		// different relief from one shared material.
		bool OverrideNormalScale = false;
		float NormalScale = 1.0f;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
		MeshComponent(PrimitiveType primitive) : Mesh(PrimitiveHandle(primitive)) {}
		MeshComponent(AssetHandle mesh) : Mesh(mesh) {}

		// The material's parameters with this entity's overrides folded in.
		//
		// Here rather than in the renderer so the scene, the tests and any
		// future tool all get the same answer, and so there is one place the
		// precedence is stated: the override wins where it is set.
		MaterialParams ResolveParams(const MaterialParams& base) const
		{
			MaterialParams params = base;
			if (OverrideBaseColor) params.BaseColor = BaseColor;
			if (OverrideEmissive)  params.EmissiveColor = EmissiveColor;
			if (OverrideMetallic)  params.Metallic = Metallic;
			if (OverrideRoughness) params.Roughness = Roughness;
			if (OverrideOcclusion) params.Occlusion = Occlusion;
			if (OverrideNormalScale) params.NormalScale = NormalScale;
			return params;
		}
	};

	// Plays a clip from the model a MeshComponent points at.
	//
	// Separate from MeshComponent so a character's body, head and clothing can
	// be three meshes driven by one animator, which is how every rig of any
	// size is built. The animator lives on the entity that owns the skeleton
	// and the meshes are its children.
	struct AnimatorComponent
	{
		// Which clip of the model's own list. -1 is the bind pose, which is a
		// legitimate state and not an error -- it is what a character that has
		// never been told to move should look like.
		int Clip = 0;
		bool Playing = true;
		bool Loop = true;
		float Speed = 1.0f;

		// Whether this animator advances while the scene is only being
		// *edited*. Off by default, and that default is the point.
		//
		// An editor whose characters are all mid-stride is an editor where
		// nothing holds still to be placed against, where every screenshot of
		// the scene differs, and where a paused frame is not a state anybody
		// asked for. Animation belongs to the running game; previewing it is a
		// deliberate act, per animator, and this is the switch for it.
		//
		// It changes nothing about Play: a scene that is running animates
		// whatever this says.
		bool RunInEditor = false;

		// How long a change of `Clip` takes to cross-fade, in seconds. Zero
		// snaps, which is what this did before there was a blend at all.
		//
		// On the animator rather than on the clip, because the right length
		// is a property of the *transition*: the same walk clip wants a long
		// ease from an idle and none at all from a stumble. Per-transition
		// tables are the next step up and are not worth it until something
		// asks.
		float BlendTime = 0.15f;

		// Seconds into the clip. Not serialized: it means nothing outside the
		// run that produced it, for the same reason an audio voice is not.
		float Time = 0.0f;

		// --- the cross-fade, all runtime and none of it serialized ----------
		//
		// A transition starts when `Clip` stops matching `Active`, so nothing
		// has to call a Play function: the inspector, a script and the
		// serializer all set one field and get the same easing. `Started`
		// exists so the *first* update reconciles the two without fading in
		// from a clip that never played.
		int   Active = 0;
		bool  Started = false;

		// The outgoing clip, or -1 when nothing is fading.
		//
		// It keeps its own clock and keeps advancing while it fades out. A
		// frozen source is the cheaper version and it is visibly wrong: a walk
		// stops mid-stride and slides for the length of the blend, which reads
		// as a hitch in the new clip rather than as the old one ending.
		int   FadingFrom = -1;
		float FadingTime = 0.0f;
		float FadeElapsed = 0.0f;

		// The pose, rebuilt each frame. Kept here rather than in the renderer
		// so a script can read a bone's position -- a weapon in a hand needs
		// exactly this, and recomputing it would be a second answer to a
		// question already answered.
		std::vector<Mat4> Skinning;

		AnimatorComponent() = default;
		AnimatorComponent(const AnimatorComponent&) = default;
	};

	// Takes part in the physics simulation. Needs a ColliderComponent to have
	// any shape; without one the entity is simulated as a point and falls
	// through everything.
	struct RigidBodyComponent
	{
		BodyType Type = BodyType::Dynamic;

		float Mass = 1.0f;
		float Friction = 0.4f;
		// 0 is a dead drop, 1 bounces back to the height it fell from. Above
		// about 0.95 a stack gains energy and never settles.
		float Restitution = 0.1f;

		// Bleeds off velocity over time. A little of both is what stops light
		// objects drifting forever on a flat surface.
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;

		// Scales gravity for this body alone: 0 floats, negative rises.
		float GravityFactor = 1.0f;

		// Locks rotation. What a character controller wants, since a capsule
		// that tips over stops being a character.
		bool FreezeRotation = false;

		RigidBodyComponent() = default;
		RigidBodyComponent(const RigidBodyComponent&) = default;
		RigidBodyComponent(BodyType type) : Type(type) {}
	};

	struct ColliderComponent
	{
		ColliderShape Shape = ColliderShape::Box;

		// Box. Half-extents, so the default is a 1x1x1 cube -- matching the
		// cube primitive, so a cube with a collider lines up without tuning.
		Vec3 HalfExtents{ 0.5f };

		// Sphere and capsule.
		float Radius = 0.5f;
		// Capsule: the cylindrical section between the two caps, so the total
		// height is this plus two radii.
		float Height = 1.0f;

		// Moves the shape relative to the entity, for a collider that should
		// not sit on the origin -- feet at the bottom of a character, say.
		Vec3 Offset{ 0.0f };

		// Reports overlaps without resisting them.
		bool IsTrigger = false;

		ColliderComponent() = default;
		ColliderComponent(const ColliderComponent&) = default;
		ColliderComponent(ColliderShape shape) : Shape(shape) {}
	};

	// A sound this entity can play, positioned where the entity is.
	//
	// One clip per component rather than a list. A component holding several
	// would need a way to say which one to play, which is a script's job -- and
	// a script can play any clip through the one-shot helpers without a
	// component at all.
	struct AudioSourceComponent
	{
		AssetHandle Clip = AssetHandle::Invalid();
		AudioBus Bus = AudioBus::SFX;

		float Volume = 1.0f;
		// Also changes speed: this is a resampling ratio, not a shift. 2 is an
		// octave up and half as long.
		float Pitch = 1.0f;
		bool Loop = false;

		// Starts when the scene starts playing. Off for anything a script or a
		// collision is meant to trigger.
		bool PlayOnAwake = true;

		// Positioned in the world, so it is quieter further away and pans as
		// the listener turns. Off for music and UI.
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;

		// Decoded while playing rather than up front. For music; a long track
		// decoded into memory costs tens of megabytes to play once.
		bool Stream = false;

		// Runtime only, and deliberately not a registered field: a voice is
		// meaningless outside the run that created it, so it is neither
		// serialized nor shown.
		AudioVoice Voice = 0;

		AudioSourceComponent() = default;
		AudioSourceComponent(const AudioSourceComponent& other)
		{
			*this = other;
		}

		// Copying a component must not copy the voice, for the same reason a
		// script instance is not copied: two components would own one playing
		// sound and both would stop it.
		AudioSourceComponent& operator=(const AudioSourceComponent& other)
		{
			Clip = other.Clip;
			Bus = other.Bus;
			Volume = other.Volume;
			Pitch = other.Pitch;
			Loop = other.Loop;
			PlayOnAwake = other.PlayOnAwake;
			Spatial = other.Spatial;
			MinDistance = other.MinDistance;
			MaxDistance = other.MaxDistance;
			Stream = other.Stream;
			Voice = 0;
			return *this;
		}
	};

	// How a particle quad faces the world. Billboard turns to the camera --
	// smoke, fire, sparks in a 3D scene. Flat lies in the XY plane facing +Z,
	// which is what a 2D game's particles are.
	enum class ParticleFacing { Billboard, Flat };

	// Alpha reads as matter -- smoke, dust, debris -- and needs drawing back
	// to front. Additive reads as light -- fire, sparks, magic -- and sums the
	// same from any order, which also makes it the cheaper one to draw.
	//
	// WeightedBlended is alpha that does not need the sort: fragments
	// accumulate and a resolve pass works out the answer, so a thousand
	// overlapping particles land in any order and look the same. It is what
	// a GPU emitter wants, since sorting its pool would mean reading it back.
	// The cost is two extra full-resolution targets and a resolve, paid once
	// per frame no matter how many emitters use it -- and nothing at all when
	// none do.
	enum class ParticleBlend { Alpha, Additive, WeightedBlended };

	// World leaves a particle where it was born -- smoke keeps hanging where
	// the chimney was. Local carries it with the emitter -- a torch flame
	// moves with the torch.
	enum class ParticleSpace { World, Local };

	// One live particle. CPU-simulated; the GPU path keeps the same layout on
	// its own buffers and never reads it back.
	struct Particle
	{
		Vec3 Position{ 0.0f };   // emitter space per ParticleSpace
		Vec3 Velocity{ 0.0f };
		float Age = 0.0f;
		float Lifetime = 1.0f;
		float Rotation = 0.0f;   // radians, around the view axis
		float Spin = 0.0f;       // radians per second
	};

	struct ParticleEmitterComponent
	{
		// Continuous emission. Zero with a Burst is the explosion shape: one
		// bang when the scene starts or when a script asks, nothing after.
		bool  Emit = true;
		float Rate = 20.0f;              // particles per second

		// Consumed on the next simulation step, then zero. Authored non-zero
		// it fires once on the first step after Play -- which is what makes an
		// explosion prefab work with no script at all. Scripts write it too,
		// in either language.
		int   Burst = 0;

		float Lifetime = 1.5f;           // seconds each particle lives
		float LifetimeJitter = 0.25f;    // fraction of Lifetime, randomised away

		// The cone particles leave through, in the emitter's own frame.
		Vec3  Direction{ 0.0f, 1.0f, 0.0f };
		float Spread = 25.0f;            // half-angle, degrees
		float Speed = 3.0f;
		float SpeedJitter = 0.3f;        // fraction of Speed

		// The emitter's own gravity, not the physics world's: snow wants a
		// drift, sparks want a plunge, and neither is a rigid body.
		Vec3  Gravity{ 0.0f, -3.0f, 0.0f };
		float Drag = 0.0f;               // fraction of velocity lost per second

		// The two-point ramps every emitter has always had. They are not
		// legacy: they are the common case, they need no asset, and an emitter
		// authored before curves existed keeps working untouched. The curves
		// below override them one at a time.
		float SizeStart = 0.25f;
		float SizeEnd = 0.05f;
		Vec4  ColorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec4  ColorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };

		// Shapes, for the ramps a straight line cannot say: smoke that swells
		// fast then holds, a spark that flashes and decays, a puff that fades
		// in *and* out.
		//
		// Each is optional and independent. Unset -- the default -- means the
		// pair above still decides that channel, so authoring a size curve does
		// not oblige anyone to author a colour one. Alpha is deliberately its
		// own curve rather than the gradient's fourth channel: opacity and hue
		// almost never want the same shape, and splitting them is what lets a
		// gradient be reused across emitters that fade differently.
		AssetHandle SizeCurve = AssetHandle::Invalid();     // scalar, multiplies nothing -- it *is* the size
		AssetHandle ColorGradient = AssetHandle::Invalid(); // three channels, RGB
		AssetHandle AlphaCurve = AssetHandle::Invalid();    // scalar
		float Spin = 0.0f;               // max degrees per second, signed at random

		ParticleFacing Facing = ParticleFacing::Billboard;
		ParticleBlend  Blend = ParticleBlend::Alpha;
		ParticleSpace  Space = ParticleSpace::World;

		// Optional sprite; a plain white quad without one.
		AssetHandle Texture = AssetHandle::Invalid();

		int  MaxParticles = 1000;

		// Simulate on the GPU: the pool lives in a storage buffer, a compute
		// pass integrates it, and the CPU never touches a particle again.
		// The visual contract is the CPU path's; what changes is who pays.
		bool SimulateOnGpu = false;

		// --- runtime, neither serialized nor copied --------------------------
		std::vector<Particle> Pool;
		float EmitCarry = 0.0f;
		uint32_t Rng = 0;                // 0 means "seed me on first use"

		ParticleEmitterComponent() = default;
		ParticleEmitterComponent(const ParticleEmitterComponent& other) { *this = other; }

		// The pool is the run's, not the file's: play mode copies the scene,
		// and a copied emitter starts empty exactly as a loaded one does.
		ParticleEmitterComponent& operator=(const ParticleEmitterComponent& other)
		{
			Emit = other.Emit;
			Rate = other.Rate;
			Burst = other.Burst;
			Lifetime = other.Lifetime;
			LifetimeJitter = other.LifetimeJitter;
			Direction = other.Direction;
			Spread = other.Spread;
			Speed = other.Speed;
			SpeedJitter = other.SpeedJitter;
			Gravity = other.Gravity;
			Drag = other.Drag;
			SizeStart = other.SizeStart;
			SizeEnd = other.SizeEnd;
			ColorStart = other.ColorStart;
			ColorEnd = other.ColorEnd;
			Spin = other.Spin;
			Facing = other.Facing;
			Blend = other.Blend;
			Space = other.Space;
			Texture = other.Texture;
			MaxParticles = other.MaxParticles;
			SimulateOnGpu = other.SimulateOnGpu;
			Pool.clear();
			EmitCarry = 0.0f;
			Rng = 0;
			return *this;
		}
	};

	// Where the scene is heard from.
	//
	// Optional: with no listener in the scene the primary camera is used, which
	// is what is wanted almost every time and means audio works in a scene
	// nobody thought about it in. Add one to hear from somewhere other than the
	// camera -- a first-person game whose camera sits at the eyes but whose
	// listener belongs at the character.
	struct AudioListenerComponent
	{
		// Lowest wins, ties broken on entity id. The same rule as a camera's
		// ViewRank, and for the same reason: a boolean can be true twice.
		int ListenerRank = 0;

		AudioListenerComponent() = default;
		AudioListenerComponent(const AudioListenerComponent&) = default;
	};

	// ---------------------------------------------------------------------
	// The game's UI
	// ---------------------------------------------------------------------
	//
	// Entities and the ordinary hierarchy, deliberately -- not a second
	// parent/child system, and not ImGui. ImGui is the *editor's* UI and is
	// immediate mode, which is right for tools and wrong for a game: a game's
	// UI is authored in a scene, serialized, driven by scripts and has to run
	// with no editor present. See ENGINE-NOTES 7d.

	enum class CanvasScaleMode : int32_t
	{
		// One UI unit is one screen pixel. What a debug overlay wants, and what
		// nobody shipping a game wants: a HUD laid out this way is half the size
		// on a display with twice the pixels.
		ConstantPixels = 0,
		// One UI unit is a fraction of the reference resolution. Everything
		// scales with the window, so a layout authored once holds everywhere.
		ScaleWithScreen = 1,
	};

	// The root of a UI tree. Everything under it is laid out in its space.
	struct UICanvasComponent
	{
		CanvasScaleMode ScaleMode = CanvasScaleMode::ScaleWithScreen;

		// The size the layout was authored against. 1920x1080 because that is
		// what a mock-up arrives in.
		Vec2 ReferenceResolution{ 1920.0f, 1080.0f };

		// Which axis the scale follows: 0 matches the width, 1 the height, and
		// between is a blend of the two.
		//
		// It matters because a window is rarely the reference's shape. Matching
		// width alone makes a HUD grow when the window gets wider *and shorter*,
		// which is how a health bar ends up off the bottom of an ultrawide.
		// Halfway is the safe default and is what Unity ships.
		float MatchWidthOrHeight = 0.5f;

		// Between canvases. Within one, UIRectComponent::SortOrder decides.
		int32_t SortOrder = 0;

		UICanvasComponent() = default;
		UICanvasComponent(const UICanvasComponent&) = default;
	};

	// A rectangle, positioned against its parent's.
	//
	// **Anchors, not a position.** Position and size cannot express "twenty
	// pixels in from the top-right corner, at every resolution", which is the
	// entire problem UI layout exists to solve -- and it is why this does not
	// reuse TransformComponent.
	//
	// The model is Unity's, on purpose: it is the one that actually solves
	// resolution independence, and it is the one anybody arriving here has
	// already learned. A simpler scheme saves a week and costs every author who
	// discovers it cannot pin a corner.
	//
	//   left   = parent.left + AnchorMin.x * parent.width  + OffsetMin.x
	//   top    = parent.top  + AnchorMin.y * parent.height + OffsetMin.y
	//   right  = parent.left + AnchorMax.x * parent.width  + OffsetMax.x
	//   bottom = parent.top  + AnchorMax.y * parent.height + OffsetMax.y
	//
	// With the anchors equal, the offsets read as a position and a size. With
	// them apart, they read as margins and the rectangle stretches. That one
	// formula is both behaviours, which is why there is no mode switch.
	struct UIRectComponent
	{
		// Fractions of the parent, y down like everything else on screen: (0,0)
		// is its top left corner and (1,1) its bottom right.
		Vec2 AnchorMin{ 0.5f, 0.5f };
		Vec2 AnchorMax{ 0.5f, 0.5f };

		// Pixels from the anchors. Min is the left and top edge, Max the right
		// and bottom.
		Vec2 OffsetMin{ -100.0f, -20.0f };
		Vec2 OffsetMax{ 100.0f, 20.0f };

		// Within a canvas. Lower draws first, so higher sits on top; ties fall
		// back to hierarchy order. Explicit rather than implied by creation
		// order, because a UI whose layering changes when a prefab is re-saved
		// is a UI nobody can rely on.
		int32_t SortOrder = 0;

		bool Visible = true;

		// Whether the pointer stops here rather than reaching the game.
		//
		// **Off by default, which is not what Unity does, and the difference is
		// deliberate.** There, every graphic is a raycast target unless you say
		// otherwise, so a score label across the middle of the screen silently
		// eats the clicks aimed through it -- and the symptom, "I cannot shoot
		// when the crosshair is over my score", points nowhere near the cause.
		//
		// A HUD is overwhelmingly labels and decoration, and none of it should
		// take input. A button takes the pointer whether or not this is set, so
		// the only thing left needing it is the deliberate case: the backdrop of
		// a modal, which is one entity and one checkbox, chosen by somebody who
		// is already thinking about blocking input.
		bool BlocksPointer = false;

		UIRectComponent() = default;
		UIRectComponent(const UIRectComponent&) = default;
	};

	// A rectangle filled with a colour, or with an image.
	struct UIImageComponent
	{
		AssetHandle Texture = AssetHandle::Invalid();

		// Multiplied into the image, and the whole colour when there is none.
		// Alpha is the usual way to fade a panel in.
		Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		UIImageComponent() = default;
		UIImageComponent(const UIImageComponent&) = default;
	};

	enum class UITextAlign : int32_t
	{
		Left = 0,
		Center = 1,
		Right = 2,
	};

	struct UITextComponent
	{
		std::string Text = "Text";

		// A `.rvfont` baked by tools/rvfont. Nothing at runtime can read a
		// `.ttf`, so this never points at one.
		AssetHandle Font = AssetHandle::Invalid();

		// Em size in canvas units. Below the atlas's own smallest sharp size
		// the antialiasing degrades -- a property of how the font was baked,
		// which rvfont prints when it bakes one.
		float Size = 32.0f;

		Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		UITextAlign Align = UITextAlign::Left;

		// Wrap to the rectangle's width. Off means only an explicit newline
		// starts a line, and long text simply runs past the edge -- which is
		// occasionally what a single-line label wants.
		bool Wrap = true;

		// Multiplier on the font's own line height.
		float LineSpacing = 1.0f;

		UITextComponent() = default;
		UITextComponent(const UITextComponent&) = default;
	};

	// A rectangle that answers the pointer.
	//
	// It has no look of its own: the tints multiply into whatever
	// UIImageComponent is on the same entity, so a button is an image plus this,
	// and skinning one is skinning the image.
	//
	// **The defaults sit below white on purpose.** The colour picker works in
	// 0..1, so a tint cannot brighten past what the image already is -- which
	// leaves rest-at-white with nowhere for hover to go. Resting slightly dim
	// buys that room in the direction the picker can express. Anyone who wants
	// the image drawn exactly as authored sets Normal to white and gets a
	// darkening hover instead, which is one click and the other convention.
	struct UIButtonComponent
	{
		// Off leaves the rectangle drawn at its normal tint and passing the
		// pointer through, which is what a greyed-out button wants.
		bool Interactable = true;

		Vec4 NormalColor{ 0.85f, 0.85f, 0.85f, 1.0f };
		Vec4 HoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vec4 PressedColor{ 0.6f, 0.6f, 0.6f, 1.0f };

		// --- what the click does ---------------------------------------------
		//
		// The entity carrying the script, and the method on it to call. Both
		// authored in the inspector, both stored in the scene.
		//
		// **Leaving the target empty means this entity**, which is the shape
		// most buttons want -- a script on the button itself -- and saves
		// dragging an entity onto its own slot to say so.
		//
		// A button may also be read by polling (`WasButtonClicked`), and an
		// empty method is exactly that rather than a mistake: the two
		// mechanisms are the same click seen two ways, and a menu is likely to
		// use both.
		EntityRef OnClickTarget;
		std::string OnClickMethod;

		// --- state, not settings ---------------------------------------------
		//
		// Written every frame by UI::UpdatePointer and read by scripts. Not
		// registered with the ComponentRegistry, which is what keeps it out of
		// the scene file and out of the inspector: a click that survived a save
		// and reload would be a click nobody made.

		bool Hovered = false;

		// Down *on this button*. Dragging off it while held clears this and
		// restores it on return, the same as every desktop toolkit -- a press
		// that slides off the button is a cancelled press, not a click.
		bool Pressed = false;

		// A completed press: down and up, both on this button. True for one
		// simulation step, on the same terms as InputMap's action edges -- a
		// frame that runs no step carries it forward rather than losing it.
		bool Clicked = false;

		UIButtonComponent() = default;
		UIButtonComponent(const UIButtonComponent&) = default;
	};

	// How text in the world is oriented.
	enum class TextBillboard : int32_t
	{
		// The text lies in the entity's own plane, facing its local +Z. A sign,
		// a number painted on a floor, a label on a wall.
		None = 0,
		// Always square to the camera. A nameplate, damage numbers.
		Full = 1,
		// Turns to face the camera but stays upright. What a nameplate over a
		// character usually wants: Full tips the text back when the camera
		// looks down, and a row of tipped labels reads as a bug.
		Upright = 2,
	};

	// Text drawn in the scene rather than on the screen.
	//
	// Same font asset, same layout, same shader as UITextComponent -- the
	// differences are that this is positioned by an ordinary TransformComponent,
	// is occluded by geometry, and goes into the HDR target *before* tone
	// mapping, so it is lit-looking and can bloom where a HUD cannot.
	struct WorldTextComponent
	{
		std::string Text = "Text";

		// A `.rvfont` baked by tools/rvfont, exactly as the UI one.
		AssetHandle Font = AssetHandle::Invalid();

		// Em height in **world units**, then multiplied by the entity's scale.
		// One means a capital letter is roughly a metre.
		float Size = 0.5f;

		Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		UITextAlign Align = UITextAlign::Center;

		// In world units. Zero never wraps, which is what a nameplate wants.
		float WrapWidth = 0.0f;

		float LineSpacing = 1.0f;

		TextBillboard Billboard = TextBillboard::Upright;

		WorldTextComponent() = default;
		WorldTextComponent(const WorldTextComponent&) = default;
	};

	// Marks the root of an entity tree stamped out from a prefab asset.
	//
	// The instance is fully materialised into the scene -- every entity is a
	// real entity and anything about it can be edited. This records where it
	// came from, which is what "select all instances" and, later, propagating
	// an edit back to the source will hang off.
	//
	// NOT YET: editing a prefab does not update instances already placed. That
	// needs each instance entity to remember which prefab entity it came from
	// and a diff of what has been changed since -- and the hard part is not the
	// diff, it is deciding what an added or reordered child means.
	struct PrefabComponent
	{
		AssetHandle Source = AssetHandle::Invalid();

		PrefabComponent() = default;
		PrefabComponent(const PrefabComponent&) = default;
		PrefabComponent(AssetHandle source) : Source(source) {}
	};

	// A script by name, instantiated through ScriptRegistry.
	//
	// The name is what goes to disk. Holding a factory instead -- which is what
	// this did -- meant a script could only be attached from C++ with a
	// compile-time type, so it could be neither chosen in the inspector nor
	// saved: a scene with scripted behaviour lost all of it on save.
	//
	// The instance is deleted through ScriptableEntity's virtual destructor, so
	// the per-type destroy thunk this used to carry is gone with it.
	// Field values authored in the inspector, for either kind of script.
	//
	// Shared between the native and managed components because the rule is the
	// same in both: only fields somebody actually changed are stored, so editing
	// a default in code reaches every entity that never overrode it. Text,
	// because the scene file is text and because a tagged union would be two
	// languages' worth of upkeep for an operation that happens when a person
	// types rather than per step.
	struct ScriptFieldOverrides
	{
		struct Entry
		{
			std::string Name;
			std::string Value;
		};
		std::vector<Entry> Values;

		const std::string* Find(const std::string& name) const
		{
			for (const Entry& entry : Values)
			{
				if (entry.Name == name)
					return &entry.Value;
			}
			return nullptr;
		}

		void Set(const std::string& name, const std::string& value)
		{
			for (Entry& entry : Values)
			{
				if (entry.Name == name)
				{
					entry.Value = value;
					return;
				}
			}
			Values.push_back({ name, value });
		}

		void Clear(const std::string& name)
		{
			Values.erase(std::remove_if(Values.begin(), Values.end(),
										[&name](const Entry& entry) { return entry.Name == name; }),
						 Values.end());
		}

		bool Empty() const { return Values.empty(); }
	};

	// A C# script on an entity.
	//
	// Deliberately separate from NativeScriptComponent rather than one component
	// with a language flag. The two have different failure modes, different
	// lifetimes and different inspector needs -- a managed script can be
	// recompiled while the editor runs, a native one cannot -- and folding them
	// together would mean every use site asking which kind it was holding.
	//
	// An entity may carry both. Nothing in the design prevents it and the
	// simulation steps them in a defined order: native first, then managed.
	struct ManagedScriptComponent
	{
		// The C# type, as written in the assembly: "Example" or "Game.Enemy".
		std::string ScriptName;

		// What to call this component in the inspector. A tag, nothing more:
		// nothing looks it up and nothing validates it. Which script runs is
		// ScriptName's job, and that has to be a name the build actually has.
		std::string Label;

		// Values the inspector authored, applied to the instance after it is
		// created and before OnCreate runs.
		//
		// Stored as text keyed by field name, because the scene file is text and
		// because the alternative -- a variant that must be kept in step with
		// the managed field types in two languages -- is upkeep bought for
		// nothing on an operation that happens when somebody types.
		//
		// Only fields the user actually changed are stored. A field left alone
		// keeps whatever the script's own initialiser gave it, so editing a
		// default in code changes every entity that never overrode it.
		ScriptFieldOverrides Fields;

		// Forwarded so a caller says `script.Set("Speed", "2")` rather than
		// reaching through to the storage. Both script components expose the
		// same three, because from the inspector's point of view they are the
		// same component in two languages.
		const std::string* Find(const std::string& name) const { return Fields.Find(name); }
		void Set(const std::string& name, const std::string& value) { Fields.Set(name, value); }
		void Clear(const std::string& name) { Fields.Clear(name); }

		// Runtime only, not serialized. Zero means nothing is instantiated.
		int32_t Handle = 0;
		// Which name the live instance was built from, so choosing a different
		// script in the inspector mid-run actually takes effect.
		std::string ActiveScript;

		ManagedScriptComponent() = default;
		ManagedScriptComponent(const std::string& script) : ScriptName(script) {}

		// The handle is not copied. Two components owning one managed instance
		// would both destroy it, and EnTT copies components when a scene is
		// duplicated -- which is what play mode does on every press of Play.
		ManagedScriptComponent(const ManagedScriptComponent& other)
			: ScriptName(other.ScriptName), Fields(other.Fields) {}

		ManagedScriptComponent& operator=(const ManagedScriptComponent& other)
		{
			ScriptName = other.ScriptName;
			Fields = other.Fields;
			Handle = 0;
			ActiveScript.clear();
			return *this;
		}

	};

	struct NativeScriptComponent
	{
		std::string ScriptName;

		// A tag for the inspector, on the same terms as the managed component's.
		std::string Label;

		// Runtime only. Created on the first simulation step after Play and
		// destroyed with the component, so neither is serialized.
		ScriptableEntity* Instance = nullptr;

		// Which name the live instance was built from. Without it, changing the
		// choice in the inspector while the scene runs does nothing at all --
		// the step only ever created an instance when there was not one, so the
		// old script kept running under the new name.
		std::string ActiveScript;

		NativeScriptComponent() = default;
		NativeScriptComponent(const std::string& script) : ScriptName(script) {}

		// Copying a component must not copy the pointer, or two components
		// would own one instance and both would delete it. EnTT copies
		// components when a scene is duplicated.
		// Neither the instance nor what it was built from is copied: two
		// components owning one instance would both delete it.
		NativeScriptComponent(const NativeScriptComponent& other)
			: ScriptName(other.ScriptName), Label(other.Label), Fields(other.Fields) {}

		NativeScriptComponent& operator=(const NativeScriptComponent& other)
		{
			ScriptName = other.ScriptName;
			Label = other.Label;
			Fields = other.Fields;
			Instance = nullptr;
			ActiveScript.clear();
			return *this;
		}

		// Applied to the instance after it is constructed and before OnCreate,
		// so a script that reads its own configuration in OnCreate sees what the
		// inspector set rather than the constructor's default.
		ScriptFieldOverrides Fields;

		// Forwarded so a caller says `script.Set("Speed", "2")` rather than
		// reaching through to the storage. Both script components expose the
		// same three, because from the inspector's point of view they are the
		// same component in two languages.
		const std::string* Find(const std::string& name) const { return Fields.Find(name); }
		void Set(const std::string& name, const std::string& value) { Fields.Set(name, value); }
		void Clear(const std::string& name) { Fields.Clear(name); }
	};

}