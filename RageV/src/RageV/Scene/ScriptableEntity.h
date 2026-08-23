#pragma once
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include "RageV/Asset/Asset.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/RenderSettings.h"
#include "Entity.h"
#include "RageV/Math/Math.h"
#include <string>
#include <vector>

// Declaration-site script markers. To the compiler they are nothing -- the
// build's generator (rvgen) reads the source text, and these are how a
// declaration says "register me" without a trailing RV_REGISTER_SCRIPT block:
//
//     RVShowInEditor
//     float Swing = 0.34f;      // in the inspector, stored in the scene
//
//     RVCallable
//     void Ring();              // nameable by a UI button's OnClick
//
// `RVScript` before the class registers a script with no fields at all.
// Marked members must be public: the generated code names them from outside
// the class, exactly as a hand-written registration would. Defined here so
// they compile away identically whether or not a generator ever runs.
#define RVShowInEditor
#define RVCallable
#define RVScript

namespace RageV
{
	class Scene;

	// One collision or overlap, from the point of view of the script being
	// told about it.
	//
	// The same event reaches both entities involved, each with Other set to the
	// one it is not and Normal pointing back at itself -- so a script can be
	// written without knowing which side of the pair it happens to be.
	struct Collision
	{
		// Invalid if it has already been destroyed this step. Test with
		// `if (collision.Other)` before reaching into it.
		Entity Other;

		// Either collider is a trigger. Always true in OnTrigger*, always false
		// in OnCollision*; carried so one shared handler can tell.
		bool Trigger = false;

		// World space, and both zero on Exit -- there is no contact left to
		// describe by then.
		Vec3 Point{ 0.0f };
		// Points from Other towards this entity, so moving along it is moving
		// away from whatever was hit.
		Vec3 Normal{ 0.0f };

		// Closing speed along the normal when they met, in units per second,
		// never negative. Scale an impact sound or a damage value by it.
		float ImpactSpeed = 0.0f;
	};

	// The surface a native script sees.
	//
	// Deliberately narrow and stated in engine terms rather than EnTT terms: a
	// script gets Entity values, never raw handles. Play mode restores the
	// scene by recreating entities, so a stored handle is dangling the moment
	// Stop is pressed -- the undo system learned that the expensive way.
	//
	// This is also the surface C# will mirror. Every change to it afterwards
	// costs a binding update, a marshalling update and a class-library update,
	// so it is worth building deliberately once.
	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() = default;

		// Once, the first time the script is stepped after Play.
		virtual void OnCreate() {}

		// --- the two rates ---------------------------------------------------
		//
		// The names say *when*, not what, because when is the only thing that
		// decides whether the code inside is correct. Gameplay goes in OnTick;
		// presentation goes in OnFrame.

		// Every fixed simulation step. `dt` is the same number every call -- see
		// GetFixedDeltaTime -- so behaviour is identical on every machine, which
		// is the whole point of a fixed step. Anything another player, a replay
		// or the physics has to agree with belongs here.
		//
		// A frame may run zero of these, one, or several.
		virtual void OnTick(Timestep dt) {}

		// Every frame, with the real elapsed time, which varies. For things
		// nothing else has to agree about: a camera, a look-at, a fade, a
		// number counting up on the screen.
		//
		// Runs *after* the physics transforms have been interpolated for this
		// frame, so what it reads is what is about to be drawn -- which is the
		// reason it exists. A camera driven from OnTick at 240 Hz updates on one
		// frame in four against a world that updates on all four, and that reads
		// worse than no smoothing at all because the stutter is differential.
		//
		// Never runs before OnCreate, and never on a script the fixed pass has
		// not created yet: a newly spawned entity gets its first OnTick before
		// its first OnFrame.
		//
		// **Not for gameplay.** Anything that moves a body, scores a point or
		// decides an outcome from here is frame-rate dependent, which is exactly
		// what the fixed step exists to prevent.
		virtual void OnFrame(Timestep dt) {}

		// On destruction, and when play mode stops.
		virtual void OnDestroy() {}

		// --- physics ---------------------------------------------------------
		// Delivered after the simulation step that produced them, so a script
		// reacting to a hit is reacting to a resolved one.
		//
		// Solid colliders. Enter and Exit fire once each per pair; Stay fires
		// every step in between.
		//
		// A pair that has come to rest does NOT report Exit when the simulation
		// puts it to sleep -- the objects are still touching, and the engine
		// only reports what changed physically.
		virtual void OnCollisionEnter(const Collision& collision) {}
		virtual void OnCollisionStay(const Collision& collision) {}
		virtual void OnCollisionExit(const Collision& collision) {}

		// Colliders marked IsTrigger, which report overlaps without resisting
		// them. Note that a trigger only sees bodies that are awake: something
		// that falls asleep inside one stops being reported until it moves
		// again.
		virtual void OnTriggerEnter(const Collision& collision) {}
		virtual void OnTriggerStay(const Collision& collision) {}
		virtual void OnTriggerExit(const Collision& collision) {}

		// --- not a hook ------------------------------------------------------
		//
		// OnUpdate was the fixed-step callback until it became OnTick, and this
		// is here so that a script still written against the old name fails to
		// build rather than failing to run. Without it, a derived OnUpdate that
		// forgot `override` -- which the language does not require -- would
		// quietly become a brand new method nobody ever calls, and the script
		// would simply stop doing anything.
		//
		// `final` turns both spellings into the same clear compiler error.
		// Delete it once no script anywhere predates the rename.
		virtual void OnUpdate(Timestep dt) final { (void)dt; }

	protected:
		// --- identity --------------------------------------------------------
		Entity GetEntity() const { return m_Entity; }
		Scene& GetScene() const;
		UUID GetUUID();
		const std::string& GetName();
		void SetName(const std::string& name);

		// --- components ------------------------------------------------------
		template<typename T>
		T& GetComponent() { return m_Entity.GetComponent<T>(); }

		template<typename T>
		bool HasComponent() { return m_Entity.HasComponent<T>(); }

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args) { return m_Entity.AddComponent<T>(std::forward<Args>(args)...); }

		template<typename T>
		void RemoveComponent() { m_Entity.RemoveComponent<T>(); }

		// --- transform -------------------------------------------------------
		// Local, relative to the parent. World is derived.
		Vec3& GetPosition();
		Vec3& GetRotation();   // radians
		Vec3& GetScale();

		void Translate(const Vec3& delta);
		void Rotate(const Vec3& eulerDelta);
		void LookAt(const Vec3& target, const Vec3& up = { 0.0f, 1.0f, 0.0f });

		Mat4 GetWorldTransform();
		Vec3 GetWorldPosition();

		// The entity's own axes, so "forward" means forward for this object
		// rather than for the world.
		Vec3 GetForward();
		Vec3 GetRight();
		Vec3 GetUp();

		// --- other entities --------------------------------------------------
		// Invalid when nothing matches; test with `if (entity)`.
		Entity FindEntityByName(const std::string& name);
		Entity FindEntityByUUID(UUID id);
		std::vector<Entity> FindEntitiesByName(const std::string& name);

		Entity Spawn(const std::string& name = "Entity");
		Entity SpawnPrefab(AssetHandle prefab);

		// Deferred to the end of the simulation step. Destroying an entity
		// while the script pass is walking them would invalidate the iteration,
		// and a script destroying itself mid-update would delete the object it
		// is executing in.
		void Destroy();
		void Destroy(Entity entity);

		// --- hierarchy -------------------------------------------------------
		Entity GetParent();
		void SetParent(Entity parent);
		std::vector<Entity> GetChildren();

		// --- physics ---------------------------------------------------------
		// All no-ops outside play mode, and on an entity with no rigid body.
		// Velocities and impulses are world space.
		//
		// Forces accumulate over a step and are cleared by it; impulses change
		// velocity at once. A jump is an impulse, a thruster is a force.
		void AddForce(const Vec3& force);
		void AddImpulse(const Vec3& impulse);
		void SetLinearVelocity(const Vec3& velocity);
		Vec3 GetLinearVelocity();

		// Nearest body along the ray, which may be this one. Direction need not
		// be normalised; the ray extends to its length. Test with `if (hit)`.
		RayHit Raycast(const Vec3& origin, const Vec3& direction);

		// --- terrain (ENGINE-NOTES 7au) --------------------------------------

		// How high the ground is under a world point, from whichever terrain
		// covers it.
		//
		//     float ground;
		//     if (GetTerrainHeight(GetWorldPosition(), ground))
		//         GetPosition().y = ground + 0.5f;
		//
		// **False means there is no terrain there**, and the bool is not
		// decoration: the heightfield clamps to its own extent, so a call that
		// only returned a float would answer a point well off the edge with
		// the rim's height. `height` is left at zero on a miss.
		//
		// Prefer this to a downward Raycast for ground queries. The ray needs
		// a length guessed in advance, only exists while the scene is playing,
		// and finds nothing on a terrain whose Collision is off; this is exact
		// against the terrain's own samples in all three cases, and it tracks
		// a sculpt as the stroke lands.
		//
		// Where several terrains cover the point the highest surface wins,
		// which is the answer a body dropped from above would get.
		bool GetTerrainHeight(const Vec3& worldPosition, float& height);
		// The same, on one named terrain. False for an entity with no
		// TerrainComponent.
		bool GetTerrainHeight(Entity terrain, const Vec3& worldPosition, float& height);

		// --- rendering ---------------------------------------------------------
		//
		// Three of them, because a setting has three possible owners and
		// pretending otherwise is what this used to do. ENGINE-NOTES 7s.
		//
		// All three are writable and read fresh when the frame is built, so a
		// change made in OnTick or OnFrame is on screen that frame. None of
		// them is saved: these are runtime overrides, and a game dimming its
		// own bloom should not quietly edit an asset.

		// What the frame costs, from the project: anti-aliasing and its
		// parameters, shadows.
		//
		//     GetRenderSettings().AA = AntiAliasing::SMAA;
		//
		// Note the machine can still override the mode -- `ragev.ini` and
		// `--aa=` are checked after this -- so writing AA and reading back the
		// picture is not guaranteed to agree.
		RenderSettings& GetRenderSettings();

		// Where the frame is, from the scene: ambient light and the sky.
		//
		//     GetEnvironment().AmbientIntensity = 0.0f;
		SceneEnvironment& GetEnvironment();

		// How the frame is graded, from the profile the scene's primary camera
		// points at.
		//
		//     if (PostSettings* post = GetPostSettings())
		//         post->Exposure = 0.6f;   // a flashbang wearing off
		//
		// **Null when that camera has no profile**, which is the honest answer:
		// with no profile there is no grade to write to, and a silent no-op
		// would look like the engine ignoring the write. Attach a
		// `.rvpostprofile` in the inspector and this stops being null.
		PostSettings* GetPostSettings();

		// --- audio -----------------------------------------------------------
		// This entity's AudioSourceComponent. Restarts it if it is already
		// playing; does nothing without the component.
		AudioVoice PlaySource();
		void StopSource();
		bool IsSourcePlaying();

		// Fire and forget, at this entity's position. Nothing to stop and
		// nothing to keep: the returned voice is only useful if the sound is
		// long enough to want to interrupt. Pitch 1 plays as recorded; a
		// small random spread around 1 is what stops a repeated impact from
		// sounding like a sampler.
		AudioVoice PlayOneShot(AssetHandle clip, float volume = 1.0f, float pitch = 1.0f);
		// The same, from an arbitrary point -- a particle burst, a ricochet,
		// somewhere no entity stands.
		static AudioVoice PlayOneShotAt(AssetHandle clip, const Vec3& position,
										float volume = 1.0f, float pitch = 1.0f);
		// The same, but unpositioned -- the listener hears it at full volume
		// wherever they are. UI, narration, a stinger.
		static AudioVoice PlayOneShot2D(AssetHandle clip, float volume = 1.0f, float pitch = 1.0f);

		// --- input -----------------------------------------------------------
		// By action name, never by keycode. See RageV/Core/InputMap.h.
		//
		// The edges answer against the caller's own clock, so OnTick and
		// OnFrame may both ask: one step sees a press, one frame sees it, and
		// neither spends the other's.
		static bool IsActionDown(const std::string& action);
		static bool WasActionPressed(const std::string& action);
		static bool WasActionReleased(const std::string& action);
		static float GetAxis(const std::string& axis);

		// Whether the game's UI has the pointer this frame.
		//
		// **Ask this before acting on a click.** The action map does not know a
		// canvas exists, so without it a press on the pause button also fires
		// the weapon behind it -- and the bug reads as a gameplay bug, nowhere
		// near the menu that caused it.
		//
		//     if (!IsPointerOverUI() && WasActionPressed("Fire"))
		//         Fire();
		//
		// Keyboard actions are unaffected: this is about the pointer.
		static bool IsPointerOverUI();

		// --- the game's UI ---------------------------------------------------
		//
		// Conveniences over components that are perfectly reachable with
		// GetComponent -- they exist because this is the surface C# mirrors,
		// and because a label and a button are what a game touches most.

		// A UITextComponent's string, on this entity or another. Silently does
		// nothing without the component; GetText answers empty.
		void SetText(const std::string& text);
		void SetText(Entity entity, const std::string& text);
		std::string GetText();
		std::string GetText(Entity entity);

		// A completed press -- down and up, both on the same button. The same
		// contract WasActionPressed has, so polling it from OnTick sees each
		// click exactly once, and so does polling it from OnFrame.
		//
		// The no-argument form is a script living on the button itself, which
		// is the usual shape.
		bool WasButtonClicked();
		bool WasButtonClicked(Entity entity);

		// Whether the pointer is on it right now -- a level, not an edge, so
		// it is read every frame and written straight back to whatever it
		// drives. The canvas tints a hovered button's own image and nothing
		// else, so a label or an icon that changes on hover needs this.
		bool IsButtonHovered();
		bool IsButtonHovered(Entity entity);

		// --- time ------------------------------------------------------------
		// The fixed step. The same number OnTick is handed, every call.
		static float GetFixedDeltaTime();
		// Seconds since the process started.
		static float GetTime();

		// How far this frame falls between the last simulation step and the
		// next, from 0 to 1. The engine already applies it to simulated bodies
		// before OnFrame runs; this is for smoothing something the engine does
		// not know about -- a value a script computed in OnTick and wants to
		// draw between steps.
		//
		//     float x = Math::Lerp(m_Previous, m_Current, GetInterpolationAlpha());
		//
		// Meaningless in OnTick, where it is whatever the last frame left.
		static float GetInterpolationAlpha();

	private:
		Entity m_Entity;
		friend class Scene;
	};
}
