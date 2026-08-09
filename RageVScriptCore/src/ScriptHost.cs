using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;

namespace RageV;

/// <summary>
/// Owns the live script instances and is what the engine calls to drive them.
/// </summary>
/// <remarks>
/// Scripts are addressed by an <c>int</c> handle rather than by a pointer for
/// the same reason entities are addressed by UUID: a managed object's address
/// is not the engine's to hold. A handle also survives the assembly being
/// unloaded and reloaded, which is what hot reload will need.
///
/// **Every entry point here catches everything.** An exception crossing an
/// unmanaged frame terminates the process rather than unwinding, so a script
/// that throws must not take the editor with it. The exception is logged with
/// the script's type name and the instance keeps running — a broken
/// <c>OnUpdate</c> should be a message in the log panel, not a crash and a lost
/// scene.
/// </remarks>
public static unsafe class ScriptHost
{
	private static readonly Dictionary<int, Script> s_Live = new();
	private static int s_NextHandle = 1;

	/// <summary>Instantiates a script by type name and binds it to an entity.</summary>
	/// <param name="typeName">
	/// Assembly-qualified or plain. A plain name is looked up in this assembly,
	/// which is what the built-in scripts use.
	/// </param>
	/// <returns>A handle, or 0 when the type does not exist or is not a Script.</returns>
	[UnmanagedCallersOnly]
	public static int Create(byte* typeName, ulong entity)
	{
		try
		{
			string? name = Marshal.PtrToStringUTF8((IntPtr)typeName);
			if (string.IsNullOrEmpty(name))
				return 0;

			Type? type = Type.GetType(name) ?? typeof(ScriptHost).Assembly.GetType(name);
			if (type is null)
			{
				Log.Error($"No script type named '{name}'");
				return 0;
			}

			if (!typeof(Script).IsAssignableFrom(type) || type.IsAbstract)
			{
				Log.Error($"'{name}' is not a Script, or is abstract");
				return 0;
			}

			if (Activator.CreateInstance(type) is not Script instance)
				return 0;

			instance.Entity = new Entity(entity);

			int handle = s_NextHandle++;
			s_Live[handle] = instance;
			return handle;
		}
		catch (Exception e)
		{
			Log.Error($"Creating a script threw: {e.Message}");
			return 0;
		}
	}

	[UnmanagedCallersOnly]
	public static void Destroy(int handle)
	{
		try { s_Live.Remove(handle); }
		catch { }
	}

	[UnmanagedCallersOnly]
	public static void InvokeCreate(int handle) => Invoke(handle, "OnCreate", s => s.OnCreate());

	[UnmanagedCallersOnly]
	public static void InvokeUpdate(int handle, float deltaTime) =>
		Invoke(handle, "OnUpdate", s => s.OnUpdate(deltaTime));

	[UnmanagedCallersOnly]
	public static void InvokeDestroy(int handle) => Invoke(handle, "OnDestroy", s => s.OnDestroy());

	[UnmanagedCallersOnly]
	public static void InvokeContact(int handle, int kind, NativeCollision* contact)
	{
		if (contact is null)
			return;

		Collision collision = new Collision(contact->Other, contact->Trigger != 0,
											contact->Point, contact->Normal, contact->ImpactSpeed);

		switch ((ContactKind)kind)
		{
			case ContactKind.CollisionEnter: Invoke(handle, "OnCollisionEnter", s => s.OnCollisionEnter(collision)); break;
			case ContactKind.CollisionStay:  Invoke(handle, "OnCollisionStay",  s => s.OnCollisionStay(collision));  break;
			case ContactKind.CollisionExit:  Invoke(handle, "OnCollisionExit",  s => s.OnCollisionExit(collision));  break;
			case ContactKind.TriggerEnter:   Invoke(handle, "OnTriggerEnter",   s => s.OnTriggerEnter(collision));   break;
			case ContactKind.TriggerStay:    Invoke(handle, "OnTriggerStay",    s => s.OnTriggerStay(collision));    break;
			case ContactKind.TriggerExit:    Invoke(handle, "OnTriggerExit",    s => s.OnTriggerExit(collision));    break;
		}
	}

	/// <summary>How many instances are alive. A handle never released is a script that never stops.</summary>
	[UnmanagedCallersOnly]
	public static int LiveCount()
	{
		try { return s_Live.Count; }
		catch { return -1; }
	}

	// The one place an exception from user code is allowed to stop.
	private static void Invoke(int handle, string what, Action<Script> body)
	{
		try
		{
			if (s_Live.TryGetValue(handle, out Script? instance))
				body(instance);
		}
		catch (Exception e)
		{
			Log.Error($"{what} threw: {e.GetType().Name}: {e.Message}");
		}
	}

	/// <summary>Mirrors <c>RageV::Managed::CollisionData</c>.</summary>
	[StructLayout(LayoutKind.Sequential)]
	public struct NativeCollision
	{
		public ulong Other;
		public int Trigger;
		public Vector3 Point;
		public Vector3 Normal;
		public float ImpactSpeed;
	}

	/// <summary>Mirrors <c>RageV::Managed::ContactKind</c>.</summary>
	internal enum ContactKind
	{
		CollisionEnter = 0,
		CollisionStay = 1,
		CollisionExit = 2,
		TriggerEnter = 3,
		TriggerStay = 4,
		TriggerExit = 5,
	}
}
