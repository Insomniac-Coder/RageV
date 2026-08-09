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
}

/// <summary>The simulation clock.</summary>
public static unsafe class Time
{
	/// <summary>
	/// The fixed timestep. The same value <see cref="Script.OnUpdate"/> is
	/// handed, every call.
	/// </summary>
	public static float FixedDeltaTime => Native.IsReady ? Native.Api.GetFixedDeltaTime() : 0.0f;

	/// <summary>Seconds since the process started.</summary>
	public static float Elapsed => Native.IsReady ? Native.Api.GetTime() : 0.0f;
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

	public static Entity Spawn(string name = "Entity") =>
		Native.IsReady ? new Entity(Native.WithUtf8(name, utf8 => Native.Api.Spawn(utf8))) : Invalid;

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
}

/// <summary>The base class of every C# script.</summary>
/// <remarks>
/// Attach one to an entity and it receives the lifecycle and physics callbacks
/// while the scene is playing. Everything is optional; the defaults do nothing.
///
/// **<see cref="OnUpdate"/> runs per fixed simulation step, not per frame.**
/// A frame may run zero steps, one, or several, and <c>deltaTime</c> is the
/// fixed timestep — the same value every call. Multiply rates by it anyway, so
/// behaviour is identical when someone runs the game at a different rate.
///
/// This mirrors <c>ScriptableEntity</c> in C++ deliberately, so the two
/// scripting guides describe one engine rather than two.
/// </remarks>
public abstract class Script
{
	/// <summary>The entity this script is attached to.</summary>
	public Entity Entity { get; internal set; }

	/// <summary>Once, on the first simulation step after Play.</summary>
	/// <remarks>
	/// Every entity in the scene exists by then, so looking others up is safe
	/// here — which is not true of a constructor.
	/// </remarks>
	public virtual void OnCreate() { }

	/// <summary>Every fixed simulation step. Not every frame.</summary>
	public virtual void OnUpdate(float deltaTime) { }

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

	protected void AddForce(Vector3 force) => Entity.AddForce(force);
	protected void AddImpulse(Vector3 impulse) => Entity.AddImpulse(impulse);
}
