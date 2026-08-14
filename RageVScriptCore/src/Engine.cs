using System;

namespace RageV;

/// <summary>Where a log line goes. Matches the native levels.</summary>
public enum LogLevel
{
	Trace = 0,
	Info = 1,
	Warn = 2,
	Error = 3,
}

/// <summary>The engine's log, which is where a script says anything.</summary>
/// <remarks>
/// Not <c>Console.WriteLine</c>. A packaged game has no console, and the
/// editor's log panel is the only place anybody looks.
/// </remarks>
public static unsafe class Log
{
	public static void Trace(string message) => Write(LogLevel.Trace, message);
	public static void Info(string message) => Write(LogLevel.Info, message);
	public static void Warn(string message) => Write(LogLevel.Warn, message);
	public static void Error(string message) => Write(LogLevel.Error, message);

	public static void Write(LogLevel level, string message)
	{
		if (!Native.IsReady)
			return;

		Native.WithUtf8<int>(message, utf8 =>
		{
			Native.Api.Log((int)level, utf8);
			return 0;
		});
	}
}

/// <summary>Input, by action name.</summary>
/// <remarks>
/// There is deliberately no way to ask about a key. A script written against
/// <c>"Jump"</c> keeps working when the player rebinds it, when a gamepad is
/// added, and when the same action is bound to two things at once. A script
/// written against a key code does none of that. See
/// <c>RageV/Core/InputMap.h</c> for where the bindings live.
/// </remarks>
public static unsafe class Input
{
	public static bool IsActionDown(string action) =>
		Native.IsReady && Native.WithUtf8(action, utf8 => Native.Api.IsActionDown(utf8)) != 0;

	/// <summary>Went down since the last step.</summary>
	/// <remarks>
	/// Consumed by the first step that runs, and carried forward by a frame
	/// with no steps — so a press is never missed and never seen twice.
	/// </remarks>
	public static bool WasActionPressed(string action) =>
		Native.IsReady && Native.WithUtf8(action, utf8 => Native.Api.WasActionPressed(utf8)) != 0;

	public static bool WasActionReleased(string action) =>
		Native.IsReady && Native.WithUtf8(action, utf8 => Native.Api.WasActionReleased(utf8)) != 0;

	public static float GetAxis(string axis) =>
		Native.IsReady ? Native.WithUtf8(axis, utf8 => Native.Api.GetAxis(utf8)) : 0.0f;

	/// <summary>Whether the game's UI has the pointer this frame.</summary>
	/// <remarks>
	/// <b>Ask this before acting on a click.</b> The action map does not know a
	/// canvas exists, so without it a press on the pause button also fires the
	/// weapon behind it — and that reads as a gameplay bug, nowhere near the
	/// menu that caused it.
	/// <code>
	/// if (!Input.IsPointerOverUI &amp;&amp; Input.WasActionPressed("Fire"))
	///     Fire();
	/// </code>
	/// Keyboard actions are unaffected: this is about the pointer.
	/// </remarks>
	public static bool IsPointerOverUI =>
		Native.IsReady && Native.Api.IsPointerOverUI() != 0;
}

/// <summary>The simulation clock.</summary>
public static unsafe class Time
{
	/// <summary>
	/// The fixed timestep. The same value <see cref="Script.OnTick"/> is
	/// handed, every call.
	/// </summary>
	public static float FixedDeltaTime => Native.IsReady ? Native.Api.GetFixedDeltaTime() : 0.0f;

	/// <summary>Seconds since the process started.</summary>
	public static float Elapsed => Native.IsReady ? Native.Api.GetTime() : 0.0f;

	/// <summary>
	/// How far this frame falls between the last simulation step and the next,
	/// from 0 to 1.
	/// </summary>
	/// <remarks>
	/// The engine already applies it to simulated bodies before
	/// <see cref="Script.OnFrame"/> runs; this is for smoothing something the
	/// engine does not know about — a value a script computed in
	/// <see cref="Script.OnTick"/> and wants to draw between steps:
	///
	/// <code>
	/// float x = m_Previous + (m_Current - m_Previous) * Time.InterpolationAlpha;
	/// </code>
	///
	/// Meaningless in OnTick, where it is whatever the last frame left.
	/// </remarks>
	public static float InterpolationAlpha =>
		Native.IsReady ? Native.Api.GetInterpolationAlpha() : 0.0f;
}

/// <summary>One collision or overlap, from the point of view of the script being told about it.</summary>
/// <remarks>
/// The same event reaches both entities involved, each with <see cref="Other"/>
/// set to the one it is not and <see cref="Normal"/> pointing back at itself —
/// so a script can be written without knowing which side of the pair it is.
/// </remarks>
public readonly struct Collision
{
	/// <summary>
	/// The entity on the other side. May already have been destroyed this step;
	/// test <see cref="Entity.Exists"/> before reaching into it.
	/// </summary>
	public readonly Entity Other;

	/// <summary>
	/// Either collider is a trigger. Always true in the trigger callbacks and
	/// always false in the collision ones; carried so one shared handler can
	/// tell.
	/// </summary>
	public readonly bool Trigger;

	/// <summary>Contact point, world space. Zero on Exit — there is no contact left to describe.</summary>
	public readonly Vector3 Point;

	/// <summary>Points from <see cref="Other"/> towards this entity, so moving along it moves away.</summary>
	public readonly Vector3 Normal;

	/// <summary>
	/// Closing speed along the normal when they met, units per second, never
	/// negative. Scale an impact sound or a damage value by it.
	/// </summary>
	public readonly float ImpactSpeed;

	internal Collision(ulong other, bool trigger, Vector3 point, Vector3 normal, float impactSpeed)
	{
		Other = new Entity(other);
		Trigger = trigger;
		Point = point;
		Normal = normal;
		ImpactSpeed = impactSpeed;
	}
}

/// <summary>A thing in a scene.</summary>
/// <remarks>
/// A UUID, not a pointer. Play mode restores a scene by recreating entities, so
/// a pointer held across Stop would dangle — the same reason the C++ API hands
/// scripts <c>Entity</c> values rather than raw handles. A UUID survives saving,
/// loading, duplication and play mode, and looking one up that no longer exists
/// answers rather than faults.
///
/// Everything here mirrors <c>ScriptableEntity</c> in the C++ API, name for
/// name, so the two guides describe one engine.
/// </remarks>
public readonly unsafe struct Entity : IEquatable<Entity>
{
	public readonly ulong Id;

	public Entity(ulong id) { Id = id; }

	public static Entity Invalid => new Entity(0);

	/// <summary>False once the entity has been destroyed, or if it never existed.</summary>
	public bool Exists => Id != 0 && Native.IsReady && Native.Api.EntityExists(Id) != 0;

	public static implicit operator bool(Entity entity) => entity.Exists;

	public bool Equals(Entity other) => Id == other.Id;
	public override bool Equals(object? obj) => obj is Entity other && Equals(other);
	public override int GetHashCode() => Id.GetHashCode();
	public override string ToString() => $"Entity({Id})";

	public static bool operator ==(Entity a, Entity b) => a.Id == b.Id;
	public static bool operator !=(Entity a, Entity b) => a.Id != b.Id;

	// --- identity ---

	public string Name
	{
		get
		{
			if (!Native.IsReady)
				return string.Empty;

			// Asked for the length first rather than guessing at a buffer size.
			// The native side returns the length that *would* have fit, so a
			// long name is read correctly instead of being silently clipped.
			int needed = Native.Api.GetEntityName(Id, null, 0);
			if (needed <= 0)
				return string.Empty;

			byte[] buffer = new byte[needed + 1];
			fixed (byte* pointer = buffer)
			{
				Native.Api.GetEntityName(Id, pointer, needed + 1);
				return System.Runtime.InteropServices.Marshal.PtrToStringUTF8((IntPtr)pointer) ?? string.Empty;
			}
		}
		set
		{
			if (!Native.IsReady)
				return;
			// Copied out first: a lambda in a struct cannot capture `this`.
			ulong id = Id;
			Native.WithUtf8<int>(value ?? string.Empty,
								 utf8 => Native.Api.SetEntityName(id, utf8));
		}
	}

	// --- transform, local and relative to the parent ---

	public Vector3 Position
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetPosition(Id, &value) != 0) ? value : Vector3.Zero; }
		set { if (Native.IsReady) Native.Api.SetPosition(Id, &value); }
	}

	/// <summary>Euler angles, in radians, in XYZ order.</summary>
	public Vector3 Rotation
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetRotation(Id, &value) != 0) ? value : Vector3.Zero; }
		set { if (Native.IsReady) Native.Api.SetRotation(Id, &value); }
	}

	public Vector3 Scale
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetScale(Id, &value) != 0) ? value : Vector3.One; }
		set { if (Native.IsReady) Native.Api.SetScale(Id, &value); }
	}

	public void Translate(Vector3 delta) => Position += delta;
	public void Rotate(Vector3 eulerDelta) => Rotation += eulerDelta;

	/// <summary>Turns the entity so its forward (-Z) faces the target.</summary>
	/// <remarks>A target at the entity's own position is a no-op, not a NaN.</remarks>
	public void LookAt(Vector3 target) => LookAt(target, Vector3.Up);

	public void LookAt(Vector3 target, Vector3 up)
	{
		if (Native.IsReady)
			Native.Api.LookAt(Id, &target, &up);
	}

	/// <summary>Where it actually is, with every parent applied.</summary>
	public Vector3 WorldPosition
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetWorldPosition(Id, &value) != 0) ? value : Vector3.Zero; }
	}

	/// <summary>The entity's own axes, so "forward" means forward for this object.</summary>
	public Vector3 Forward
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetForward(Id, &value) != 0) ? value : new Vector3(0, 0, -1); }
	}

	public Vector3 Right
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetRight(Id, &value) != 0) ? value : new Vector3(1, 0, 0); }
	}

	public Vector3 Up
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetUp(Id, &value) != 0) ? value : Vector3.Up; }
	}

	// --- other entities ---

	/// <summary>First match, or an invalid entity.</summary>
	/// <remarks>
	/// Linear over the scene. Do it once in <see cref="Script.OnCreate"/> and
	/// keep the result, rather than every step — on a small scene the
	/// difference is invisible, on a large one it is the script's whole cost.
	/// </remarks>
	public static Entity FindByName(string name) =>
		Native.IsReady ? new Entity(Native.WithUtf8(name, utf8 => Native.Api.FindEntityByName(utf8))) : Invalid;

	/// <summary>Every entity with this name. Same cost note as <see cref="FindByName"/>.</summary>
	public static Entity[] FindAllByName(string name)
	{
		if (!Native.IsReady)
			return Array.Empty<Entity>();

		// Count first, then fill -- the same two-call contract Name uses.
		int count = Native.WithUtf8(name, utf8 => Native.Api.FindEntitiesByName(utf8, null, 0));
		if (count <= 0)
			return Array.Empty<Entity>();

		ulong[] ids = new ulong[count];
		fixed (ulong* pointer = ids)
		{
			ulong* captured = pointer;
			Native.WithUtf8(name, utf8 => Native.Api.FindEntitiesByName(utf8, captured, count));
		}

		Entity[] found = new Entity[count];
		for (int i = 0; i < count; i++)
			found[i] = new Entity(ids[i]);
		return found;
	}

	public static Entity Spawn(string name = "Entity") =>
		Native.IsReady ? new Entity(Native.WithUtf8(name, utf8 => Native.Api.Spawn(utf8))) : Invalid;

	/// <summary>Instantiates a prefab by its asset path — "prefabs/rock.prefab".</summary>
	/// <remarks>
	/// A path, not a handle: handles are the engine's internal names for
	/// assets, and a script has no honest way to hold one. An unknown path
	/// warns in the log and returns an invalid entity.
	/// </remarks>
	public static Entity SpawnPrefab(string assetPath) =>
		Native.IsReady ? new Entity(Native.WithUtf8(assetPath, utf8 => Native.Api.SpawnPrefab(utf8))) : Invalid;

	// --- hierarchy ---

	/// <summary>The parent, or an invalid entity at the root. Assigning <see cref="Invalid"/> moves it to the root.</summary>
	public Entity Parent
	{
		get => Native.IsReady ? new Entity(Native.Api.GetParent(Id)) : Invalid;
		set { if (Native.IsReady) Native.Api.SetParent(Id, value.Id); }
	}

	public Entity[] Children
	{
		get
		{
			if (!Native.IsReady)
				return Array.Empty<Entity>();

			int count = Native.Api.GetChildren(Id, null, 0);
			if (count <= 0)
				return Array.Empty<Entity>();

			ulong[] ids = new ulong[count];
			fixed (ulong* pointer = ids)
				Native.Api.GetChildren(Id, pointer, count);

			Entity[] children = new Entity[count];
			for (int i = 0; i < count; i++)
				children[i] = new Entity(ids[i]);
			return children;
		}
	}

	/// <summary>
	/// Destroys this entity, at the end of the simulation step.
	/// </summary>
	/// <remarks>
	/// Deferred, and it has to be: destroying an entity while the script pass
	/// is walking them would invalidate the iteration, and a script destroying
	/// itself mid-update would delete the object currently executing. So a
	/// destroyed entity is still findable for the rest of that step.
	/// </remarks>
	public void Destroy()
	{
		if (Native.IsReady)
			Native.Api.Destroy(Id);
	}

	// --- physics ---
	// All no-ops outside play mode and on an entity with no rigid body. World
	// space. Forces accumulate over a step and are cleared by it; impulses
	// change velocity at once. A jump is an impulse, a thruster is a force.

	public void AddForce(Vector3 force) { if (Native.IsReady) Native.Api.AddForce(Id, &force); }
	public void AddImpulse(Vector3 impulse) { if (Native.IsReady) Native.Api.AddImpulse(Id, &impulse); }

	public Vector3 LinearVelocity
	{
		get { Vector3 value; return (Native.IsReady && Native.Api.GetLinearVelocity(Id, &value) != 0) ? value : Vector3.Zero; }
		set { if (Native.IsReady) Native.Api.SetLinearVelocity(Id, &value); }
	}

	/// <summary>Nearest body along the ray. Direction need not be normalised — the ray extends to its length.</summary>
	public static RayHit Raycast(Vector3 origin, Vector3 direction)
	{
		if (!Native.IsReady)
			return default;

		NativeRayHit data;
		int hit = Native.Api.Raycast(&origin, &direction, &data);
		return new RayHit(hit != 0, data.Entity, data.Position, data.Normal, data.Distance);
	}

	// --- audio ---
	// Mirrors the native script API: a no-op without an AudioSourceComponent,
	// and clips are named by their asset path.

	/// <summary>Plays this entity's AudioSourceComponent, restarting it if already playing.</summary>
	public ulong PlaySource() => Native.IsReady ? Native.Api.PlaySource(Id) : 0;

	public void StopSource()
	{
		if (Native.IsReady)
			Native.Api.StopSource(Id);
	}

	public bool IsSourcePlaying => Native.IsReady && Native.Api.IsSourcePlaying(Id) != 0;

	/// <summary>
	/// Fire and forget, at this entity's position. An empty path plays the
	/// entity's own source clip; otherwise the clip is an asset path like
	/// "audio/thud.wav". Pitch 1 plays as recorded; a small random spread
	/// around 1 is what stops a repeated impact from sounding like a sampler.
	/// </summary>
	public ulong PlayOneShot(string clipPath = "", float volume = 1.0f, float pitch = 1.0f)
	{
		if (!Native.IsReady)
			return 0;
		ulong id = Id;   // a lambda in a struct cannot capture `this`
		return Native.WithUtf8(clipPath,
			utf8 => Native.Api.PlayOneShotPitched(id, utf8, volume, pitch));
	}

	// --- components ---
	// By registry name -- "LightComponent", "RigidBodyComponent" -- with field
	// values as text, exactly as the inspector and the scene file hold them.
	// This is the C# shape of what C++ does with GetComponent<T>: the registry
	// that drives the inspector drives this too, so a component gaining a field
	// is visible here without anyone updating a binding.

	public bool HasComponent(string component)
	{
		if (!Native.IsReady)
			return false;
		ulong id = Id;
		return Native.WithUtf8(component, utf8 => Native.Api.HasComponent(id, utf8)) != 0;
	}

	/// <summary>Adds by registry name. False when unknown or already present.</summary>
	public bool AddComponent(string component)
	{
		if (!Native.IsReady)
			return false;
		ulong id = Id;
		return Native.WithUtf8(component, utf8 => Native.Api.AddComponent(id, utf8)) != 0;
	}

	/// <summary>Removes by registry name. Components the editor calls essential refuse.</summary>
	public bool RemoveComponent(string component)
	{
		if (!Native.IsReady)
			return false;
		ulong id = Id;
		return Native.WithUtf8(component, utf8 => Native.Api.RemoveComponent(id, utf8)) != 0;
	}

	/// <summary>
	/// One field, as text: floats invariant, vectors space-separated, booleans
	/// "true"/"false", assets as their path. Empty when the entity, component
	/// or field does not exist.
	/// </summary>
	public string GetComponentField(string component, string field)
	{
		if (!Native.IsReady)
			return string.Empty;

		ulong id = Id;
		int needed = Native.WithUtf8(component, componentUtf8 =>
			Native.WithUtf8(field, fieldUtf8 =>
				Native.Api.GetComponentField(id, componentUtf8, fieldUtf8, null, 0)));

		if (needed <= 0)
			return string.Empty;

		byte[] buffer = new byte[needed + 1];
		fixed (byte* pointer = buffer)
		{
			byte* captured = pointer;
			Native.WithUtf8(component, componentUtf8 =>
				Native.WithUtf8(field, fieldUtf8 =>
					Native.Api.GetComponentField(id, componentUtf8, fieldUtf8, captured, needed + 1)));
			return System.Runtime.InteropServices.Marshal.PtrToStringUTF8((IntPtr)pointer) ?? string.Empty;
		}
	}

	/// <summary>Writes one field from its text form. False when anything in the path is unknown.</summary>
	public bool SetComponentField(string component, string field, string value)
	{
		if (!Native.IsReady)
			return false;

		ulong id = Id;
		return Native.WithUtf8(component, componentUtf8 =>
			Native.WithUtf8(field, fieldUtf8 =>
				Native.WithUtf8(value, valueUtf8 =>
					Native.Api.SetComponentField(id, componentUtf8, fieldUtf8, valueUtf8)))) != 0;
	}

	// --- the game's UI ---

	/// <summary>This entity's UI Text, if it has one. Empty otherwise.</summary>
	/// <remarks>
	/// A direct entry rather than <see cref="SetComponentField"/> because a
	/// score or a timer is written every frame, and because nobody should have
	/// to know the registry name of the commonest thing in a HUD.
	/// <para>
	/// Colour is not here for the opposite reason: a UI entity has two of them,
	/// the image's and the text's, so it goes through the component bridge,
	/// which says which — <c>SetComponentField("UIImageComponent", "Color",
	/// "1 0 0 1")</c>.
	/// </para>
	/// </remarks>
	public string Text
	{
		get
		{
			if (!Native.IsReady)
				return string.Empty;

			// Length first, then the copy. The native side answers the length
			// that *would* have fit, so a long label is read whole rather than
			// silently clipped -- the GetEntityName contract.
			int needed = Native.Api.GetUIText(Id, null, 0);
			if (needed <= 0)
				return string.Empty;

			byte[] buffer = new byte[needed + 1];
			fixed (byte* pointer = buffer)
			{
				Native.Api.GetUIText(Id, pointer, needed + 1);
				return System.Runtime.InteropServices.Marshal.PtrToStringUTF8((IntPtr)pointer) ?? string.Empty;
			}
		}
		set
		{
			if (!Native.IsReady)
				return;

			ulong id = Id;
			Native.WithUtf8<int>(value ?? string.Empty, utf8 => Native.Api.SetUIText(id, utf8));
		}
	}

	/// <summary>A completed press on this entity's UI Button — down and up, both on it.</summary>
	/// <remarks>
	/// True for one simulation step, the same contract
	/// <see cref="Input.WasActionPressed"/> has, so polling it from
	/// <c>OnTick</c> sees each click exactly once. A press that slides off the
	/// button before release is cancelled and never reported.
	/// </remarks>
	public bool WasButtonClicked() =>
		Native.IsReady && Native.Api.WasUIButtonClicked(Id) != 0;
}

/// <summary>One raycast answer. Test with <c>if (hit)</c>.</summary>
public readonly struct RayHit
{
	public readonly bool Hit;

	/// <summary>The body the ray struck. May have been destroyed since; test <see cref="RageV.Entity.Exists"/>.</summary>
	public readonly Entity Entity;

	public readonly Vector3 Position;
	public readonly Vector3 Normal;
	public readonly float Distance;

	internal RayHit(bool hit, ulong entity, Vector3 position, Vector3 normal, float distance)
	{
		Hit = hit;
		Entity = new Entity(entity);
		Position = position;
		Normal = normal;
		Distance = distance;
	}

	public static implicit operator bool(RayHit hit) => hit.Hit;
}

/// <summary>Sounds that belong to nobody: one-shots from anywhere, and stopping a voice by its ticket.</summary>
public static unsafe class Audio
{
	/// <summary>Unpositioned — the listener hears it at full volume wherever they are. UI, narration, a stinger.</summary>
	public static ulong PlayOneShot2D(string clipPath, float volume = 1.0f, float pitch = 1.0f) =>
		Native.IsReady
			? Native.WithUtf8(clipPath, utf8 => Native.Api.PlayOneShot2DPitched(utf8, volume, pitch))
			: 0;

	/// <summary>
	/// Positioned at an arbitrary point — a particle burst, a ricochet,
	/// somewhere no entity stands.
	/// </summary>
	public static ulong PlayOneShotAt(string clipPath, Vector3 position,
									  float volume = 1.0f, float pitch = 1.0f)
	{
		if (!Native.IsReady)
			return 0;
		return Native.WithUtf8(clipPath, utf8 =>
		{
			// A captured parameter lives on the closure and cannot have its
			// address taken; a local of the lambda body can.
			Vector3 at = position;
			return Native.Api.PlayOneShotAt(utf8, &at, volume, pitch);
		});
	}

	/// <summary>Stops a voice returned by any of the play calls. A finished or unknown voice is a no-op.</summary>
	public static void StopVoice(ulong voice)
	{
		if (Native.IsReady)
			Native.Api.StopVoice(voice);
	}
}

/// <summary>The base class of every C# script.</summary>
/// <remarks>
/// Attach one to an entity and it receives the lifecycle and physics callbacks
/// while the scene is playing. Everything is optional; the defaults do nothing.
///
/// **There are two rates, and picking the right one is the whole game.**
/// <see cref="OnTick"/> runs per fixed simulation step and <see cref="OnFrame"/>
/// runs per frame. The names say *when* rather than what, because when is the
/// only thing that decides whether the code inside is correct: gameplay goes in
/// OnTick, presentation goes in OnFrame.
///
/// A frame may run zero ticks, one, or several, and OnTick's <c>deltaTime</c>
/// is the fixed timestep — the same value every call. Multiply rates by it
/// anyway, so behaviour is identical when someone runs the game at a different
/// rate.
///
/// This mirrors <c>ScriptableEntity</c> in C++ deliberately, so the two
/// scripting guides describe one engine rather than two.
/// </remarks>
public abstract class Script
{
	/// <summary>The entity this script is attached to.</summary>
	public Entity Entity { get; internal set; }

	// Whether this type actually overrides OnFrame, decided once when the
	// instance is made. Without it every script pays a closure allocation and a
	// try/catch every frame to call a method that does nothing -- and most
	// scripts only want OnTick.
	internal bool WantsFrame { get; set; }

	/// <summary>Once, on the first simulation step after Play.</summary>
	/// <remarks>
	/// Every entity in the scene exists by then, so looking others up is safe
	/// here — which is not true of a constructor.
	/// </remarks>
	public virtual void OnCreate() { }

	/// <summary>Every fixed simulation step. Not every frame.</summary>
	/// <remarks>
	/// <c>deltaTime</c> is the same number every call — see
	/// <see cref="Time.FixedDeltaTime"/> — so behaviour is identical on every
	/// machine, which is the whole point of a fixed step. Anything another
	/// player, a replay or the physics has to agree with belongs here.
	/// </remarks>
	public virtual void OnTick(float deltaTime) { }

	/// <summary>Every frame, with the real elapsed time, which varies.</summary>
	/// <remarks>
	/// For things nothing else has to agree about: a camera, a look-at, a fade,
	/// a number counting up on the screen.
	///
	/// It runs *after* the physics transforms have been interpolated for this
	/// frame, so what it reads is what is about to be drawn — which is the
	/// reason it exists. A camera driven from <see cref="OnTick"/> at 240 Hz
	/// updates on one frame in four against a world that updates on all four,
	/// and that reads worse than no smoothing at all because the stutter is
	/// differential.
	///
	/// Never runs before <see cref="OnCreate"/>, and never on a script the
	/// fixed pass has not created yet: a newly spawned entity gets its first
	/// OnTick before its first OnFrame.
	///
	/// **Not for gameplay.** Anything that moves a body, scores a point or
	/// decides an outcome from here is frame-rate dependent, which is exactly
	/// what the fixed step exists to prevent.
	/// </remarks>
	public virtual void OnFrame(float deltaTime) { }

	/// <summary>On destruction, and when play mode stops.</summary>
	public virtual void OnDestroy() { }

	// --- contacts, delivered after the step that produced them ---

	public virtual void OnCollisionEnter(Collision collision) { }
	public virtual void OnCollisionStay(Collision collision) { }

	/// <remarks>
	/// A pair that comes to rest does **not** report Exit when the simulation
	/// puts it to sleep: the objects are still touching, and the engine reports
	/// only what changed physically.
	/// </remarks>
	public virtual void OnCollisionExit(Collision collision) { }

	public virtual void OnTriggerEnter(Collision collision) { }

	/// <remarks>
	/// A trigger only sees bodies that are awake. Something that falls asleep
	/// inside one stops being reported until it moves again.
	/// </remarks>
	public virtual void OnTriggerStay(Collision collision) { }
	public virtual void OnTriggerExit(Collision collision) { }

	// --- not a hook ---
	//
	// OnUpdate was the fixed-step callback until it became OnTick, and this is
	// here so that a script still written against the old name fails loudly
	// rather than quietly never running. Non-virtual on purpose: an `override`
	// of it is a compile error, and a plain declaration is at least a
	// hides-inherited-member warning naming the thing that changed.
	//
	// Delete once no script anywhere predates the rename.
	[Obsolete("OnUpdate has been split by rate. Gameplay goes in OnTick (per fixed "
			  + "simulation step); presentation goes in OnFrame (per frame).", true)]
	public void OnUpdate(float deltaTime) { }

	// --- shorthand, so a script reads like the C++ one ---

	// `Entity` is a property returning a struct, so `Entity.Position = v` would
	// assign to a temporary and be discarded -- the compiler rejects it, which
	// is the good outcome. Copying first is correct because Entity's setters do
	// not mutate the struct; they call through the table using the id.
	protected Vector3 Position
	{
		get => Entity.Position;
		set { Entity entity = Entity; entity.Position = value; }
	}

	protected Vector3 Rotation
	{
		get => Entity.Rotation;
		set { Entity entity = Entity; entity.Rotation = value; }
	}

	protected Vector3 Scale
	{
		get => Entity.Scale;
		set { Entity entity = Entity; entity.Scale = value; }
	}

	protected Vector3 Forward => Entity.Forward;
	protected Vector3 Right => Entity.Right;
	protected Vector3 Up => Entity.Up;

	protected void Translate(Vector3 delta) => Entity.Translate(delta);
	protected void Rotate(Vector3 eulerDelta) => Entity.Rotate(eulerDelta);
	protected void LookAt(Vector3 target) => Entity.LookAt(target);
	protected void LookAt(Vector3 target, Vector3 up) => Entity.LookAt(target, up);

	protected void AddForce(Vector3 force) => Entity.AddForce(force);
	protected void AddImpulse(Vector3 impulse) => Entity.AddImpulse(impulse);

	protected RayHit Raycast(Vector3 origin, Vector3 direction) =>
		RageV.Entity.Raycast(origin, direction);

	protected ulong PlaySource() => Entity.PlaySource();
	protected void StopSource() => Entity.StopSource();
	protected bool IsSourcePlaying => Entity.IsSourcePlaying;
	protected ulong PlayOneShot(string clipPath = "", float volume = 1.0f, float pitch = 1.0f) =>
		Entity.PlayOneShot(clipPath, volume, pitch);
}

/// <summary>Which anti-aliasing filter the frame ends with.</summary>
/// <remarks>
/// The numbers are the scene file's, so this enum and a saved scene cannot
/// drift apart.
/// </remarks>
public enum AntiAliasing
{
	/// <summary>None. Every pixel is wholly one side of every edge.</summary>
	None = 0,

	/// <summary>
	/// One pass over the finished image, guessing at each edge from its
	/// neighbourhood. Cheap, and it softens the picture slightly.
	/// </summary>
	Fxaa = 1,

	/// <summary>
	/// Reconstructs each edge instead of guessing: finds the run of pixels it
	/// spans, works out which way the real line sloped, and computes coverage
	/// from that. About five times more accurate than FXAA for three times the
	/// cost.
	/// </summary>
	Smaa = 2,

	/// <summary>
	/// Draws the whole scene larger and averages it down. The only one here
	/// that helps with specular sparkle and texture moire — those are not
	/// edges, they are detail the frame never sampled finely enough, and no
	/// filter on the finished image can invent it. Costs the square of
	/// <see cref="RenderSettings.SupersampleFactor"/> in fill.
	/// </summary>
	Ssaa = 3,
}

/// <summary>
/// The scene's render settings, live — anti-aliasing, exposure, bloom, ambient,
/// sky and shadows.
/// </summary>
/// <remarks>
/// <para>
/// Read fresh when each frame is built, so a change made in OnTick or OnFrame
/// is on screen that frame:
/// </para>
/// <code>
/// RenderSettings.AntiAliasing = AntiAliasing.Smaa;
/// RenderSettings.Exposure = 0.6f;   // a flashbang wearing off
/// </code>
/// <para>
/// <b>Not saved.</b> This is a runtime override; a game dimming its own bloom
/// should not quietly edit the scene asset.
/// </para>
/// <para>
/// Anything without a property here is still reachable by name through
/// <see cref="Get"/> and <see cref="Set"/> — the properties are the typed
/// front for the same name-and-text bridge, and the names are the scene
/// file's own keys.
/// </para>
/// </remarks>
public static unsafe class RenderSettings
{
	/// <summary>One setting, as text. Empty when the name is not a setting.</summary>
	public static string Get(string name)
	{
		if (!Native.IsReady)
			return string.Empty;

		int needed = Native.WithUtf8(name, nameUtf8 =>
			Native.Api.GetRenderSetting(nameUtf8, null, 0));

		if (needed <= 0)
			return string.Empty;

		byte[] buffer = new byte[needed + 1];
		fixed (byte* pointer = buffer)
		{
			byte* captured = pointer;
			Native.WithUtf8(name, nameUtf8 =>
				Native.Api.GetRenderSetting(nameUtf8, captured, needed + 1));
			return System.Runtime.InteropServices.Marshal.PtrToStringUTF8((IntPtr)pointer)
				?? string.Empty;
		}
	}

	/// <summary>Writes one setting from its text form. False when the name is unknown.</summary>
	public static bool Set(string name, string value)
	{
		if (!Native.IsReady)
			return false;

		return Native.WithUtf8(name, nameUtf8 =>
			Native.WithUtf8(value, valueUtf8 =>
				Native.Api.SetRenderSetting(nameUtf8, valueUtf8))) != 0;
	}

	private static float GetFloat(string name) =>
		float.TryParse(Get(name), System.Globalization.NumberStyles.Float,
					   System.Globalization.CultureInfo.InvariantCulture, out float value)
			? value : 0.0f;

	private static void SetFloat(string name, float value) =>
		Set(name, value.ToString("R", System.Globalization.CultureInfo.InvariantCulture));

	private static bool GetBool(string name) => Get(name) == "true";
	private static void SetBool(string name, bool value) => Set(name, value ? "true" : "false");

	/// <summary>Which anti-aliasing filter runs, or none.</summary>
	public static AntiAliasing AntiAliasing
	{
		get => int.TryParse(Get("AntiAliasing"), out int value) ? (AntiAliasing)value
															   : AntiAliasing.None;
		set => Set("AntiAliasing", ((int)value).ToString(
			System.Globalization.CultureInfo.InvariantCulture));
	}

	/// <summary>
	/// How many times larger each axis is drawn when
	/// <see cref="AntiAliasing"/> is <see cref="RageV.AntiAliasing.Ssaa"/>.
	/// Ignored otherwise, clamped to 1..4, and <b>the cost is its square</b>.
	/// </summary>
	public static int SupersampleFactor
	{
		get => int.TryParse(Get("SupersampleFactor"), out int value) ? value : 1;
		set => Set("SupersampleFactor",
				   value.ToString(System.Globalization.CultureInfo.InvariantCulture));
	}

	/// <summary>Multiplies the scene before the tone curve. 1 is neutral.</summary>
	public static float Exposure
	{
		get => GetFloat("Exposure");
		set => SetFloat("Exposure", value);
	}

	/// <summary>Whether bright areas bleed.</summary>
	public static bool BloomEnabled
	{
		get => GetBool("BloomEnabled");
		set => SetBool("BloomEnabled", value);
	}

	/// <summary>Brightness above which a pixel contributes to bloom.</summary>
	public static float BloomThreshold
	{
		get => GetFloat("BloomThreshold");
		set => SetFloat("BloomThreshold", value);
	}

	/// <summary>How much of the blurred image is added back.</summary>
	public static float BloomIntensity
	{
		get => GetFloat("BloomIntensity");
		set => SetFloat("BloomIntensity", value);
	}

	/// <summary>Scales the flat ambient term, which is what unlit faces get.</summary>
	public static float AmbientIntensity
	{
		get => GetFloat("AmbientIntensity");
		set => SetFloat("AmbientIntensity", value);
	}

	/// <summary>Whether the directional light casts shadows at all.</summary>
	public static bool ShadowsEnabled
	{
		get => GetBool("ShadowsEnabled");
		set => SetBool("ShadowsEnabled", value);
	}

	/// <summary>How far from the camera shadows are still drawn, in world units.</summary>
	public static float ShadowDistance
	{
		get => GetFloat("ShadowDistance");
		set => SetFloat("ShadowDistance", value);
	}
}
