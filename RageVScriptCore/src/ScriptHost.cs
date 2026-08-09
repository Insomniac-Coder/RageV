using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

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

	// Assemblies loaded from the open project, searched in addition to this one.
	//
	// Loaded into the default context for now. Hot reload needs a collectible
	// AssemblyLoadContext instead, and that is 5.5 -- doing it here would mean
	// building the unload path without the thing that exercises it.
	private static readonly List<Assembly> s_Loaded = new();

	private static bool s_ResolverInstalled;

	/// <summary>Points a project's assembly at the RageV.ScriptCore already in the process.</summary>
	/// <remarks>
	/// A project's script assembly references RageV.ScriptCore, and .NET looks
	/// for it beside the assembly being loaded and in the app base. It is in
	/// neither: it lives in `managed/` beside the executable, and the project's
	/// output is in the project's own Scripts/bin.
	///
	/// The obvious fix is to copy it next to the project assembly, and it is a
	/// trap. Two files with the same assembly name load as two *different*
	/// assemblies, so the project's `Script` would not be this `Script` --
	/// `IsAssignableFrom` fails, and the error says the type is not a Script
	/// when it plainly is. Resolving to the instance already loaded is the only
	/// version where there is one `Script` in the process.
	///
	/// This is also what hot reload will depend on: a reloaded project assembly
	/// must bind to the same ScriptCore, or every live script becomes a
	/// different type than the one the engine is holding.
	/// </remarks>
	private static void InstallResolver()
	{
		if (s_ResolverInstalled)
			return;

		AssemblyLoadContext.Default.Resolving += (context, name) =>
		{
			Assembly self = typeof(ScriptHost).Assembly;
			return string.Equals(name.Name, self.GetName().Name, StringComparison.OrdinalIgnoreCase)
				? self
				: null;
		};

		s_ResolverInstalled = true;
	}

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

			// One resolver, shared with the inspector, so describing a type and
			// instantiating it can never disagree about which type a name is.
			Type? type = FindScriptType(name);
			if (type is null)
			{
				Log.Error($"'{name}' is not a script type in any loaded assembly");
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

	/// <summary>Loads a project's compiled script assembly.</summary>
	/// <returns>The number of Script subclasses it contains, or -1 on failure.</returns>
	/// <remarks>
	/// The count is returned rather than a bare success because "the assembly
	/// loaded" and "the assembly has any scripts in it" are different answers,
	/// and the second one is what the person who just pressed Build wants.
	/// </remarks>
	[UnmanagedCallersOnly]
	public static int LoadAssembly(byte* path)
	{
		try
		{
			string? file = Marshal.PtrToStringUTF8((IntPtr)path);
			if (string.IsNullOrEmpty(file))
				return -1;

			InstallResolver();

			Assembly assembly = Assembly.LoadFrom(file);

			// Replaced rather than appended when the same assembly is loaded
			// again, or a rebuild would leave both versions searchable and the
			// older one would win for any type it still declares.
			s_Loaded.RemoveAll(loaded => string.Equals(loaded.GetName().Name,
													   assembly.GetName().Name,
													   StringComparison.OrdinalIgnoreCase));
			s_Loaded.Add(assembly);

			int scripts = 0;
			foreach (Type type in assembly.GetTypes())
			{
				if (typeof(Script).IsAssignableFrom(type) && !type.IsAbstract)
					scripts++;
			}
			return scripts;
		}
		catch (Exception e)
		{
			Log.Error($"Loading a script assembly failed: {e.Message}");
			return -1;
		}
	}

	/// <summary>Every Script subclass currently loadable, newline-separated, into the buffer.</summary>
	/// <returns>The length that would have been written, as GetEntityName does.</returns>
	[UnmanagedCallersOnly]
	public static int ListScriptTypes(byte* buffer, int capacity)
	{
		try
		{
			List<string> names = new();
			foreach (Assembly assembly in s_Loaded)
			{
				foreach (Type type in assembly.GetTypes())
				{
					if (typeof(Script).IsAssignableFrom(type) && !type.IsAbstract)
						names.Add(type.FullName ?? type.Name);
				}
			}
			names.Sort(StringComparer.Ordinal);

			string joined = string.Join("\n", names);
			byte[] bytes = System.Text.Encoding.UTF8.GetBytes(joined);

			if (buffer is not null && capacity > 0)
			{
				int copied = Math.Min(bytes.Length, capacity - 1);
				Marshal.Copy(bytes, 0, (IntPtr)buffer, copied);
				buffer[copied] = 0;
			}
			return bytes.Length;
		}
		catch
		{
			return -1;
		}
	}

	/// <summary>How many instances are alive. A handle never released is a script that never stops.</summary>
	[UnmanagedCallersOnly]
	public static int LiveCount()
	{
		try { return s_Live.Count; }
		catch { return -1; }
	}

	/// <summary>The instance behind a handle, or null.</summary>
	internal static Script? Find(int handle) =>
		s_Live.TryGetValue(handle, out Script? instance) ? instance : null;

	/// <summary>Resolves a script type name the same way Create does.</summary>
	/// <remarks>
	/// Shared so that the inspector describing a type and the engine
	/// instantiating it can never disagree about which type a name means.
	/// </remarks>
	internal static Type? FindScriptType(string? name)
	{
		if (string.IsNullOrEmpty(name))
			return null;

		Type? type = Type.GetType(name) ?? typeof(ScriptHost).Assembly.GetType(name);
		if (type is null)
		{
			foreach (Assembly assembly in s_Loaded)
			{
				type = assembly.GetType(name);
				if (type is not null)
					break;
			}
		}

		return (type is not null && typeof(Script).IsAssignableFrom(type) && !type.IsAbstract)
			? type
			: null;
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
