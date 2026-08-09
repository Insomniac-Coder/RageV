using System;
using System.Runtime.InteropServices;

namespace RageV;

/// <summary>
/// The engine's functions, as raw pointers.
/// </summary>
/// <remarks>
/// This mirrors <c>NativeApi</c> in <c>RageV/Managed/Interop.h</c> field for
/// field, in order. **The order is the ABI** — inserting a field in the middle
/// rebinds every field after it on one side only, and the result is a call
/// through a pointer to the wrong function, which is a crash somewhere
/// unrelated rather than an error here.
///
/// Not <c>[DllImport]</c>. P/Invoke by name needs the native symbols exported
/// from a shared library, and the engine is a static library linked into an
/// executable — there is no `RageV.dll` to import from. A table also skips the
/// marshalling stub P/Invoke generates per call, which matters when a script
/// touches a transform every step of every entity.
///
/// Nothing here is public. Scripts use the wrappers in
/// <see cref="Native"/>'s callers, not the pointers.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeApi
{
	// --- diagnostics ---
	public delegate* unmanaged[Cdecl]<int, byte*, void> Log;

	// --- entities ---
	public delegate* unmanaged[Cdecl]<byte*, ulong> FindEntityByName;
	public delegate* unmanaged[Cdecl]<ulong, int> EntityExists;
	public delegate* unmanaged[Cdecl]<ulong, byte*, int, int> GetEntityName;

	// --- transform ---
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> GetPosition;
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> SetPosition;
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> GetRotation;
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> SetRotation;
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> GetScale;
	public delegate* unmanaged[Cdecl]<ulong, Vector3*, int> SetScale;

	// --- input ---
	public delegate* unmanaged[Cdecl]<byte*, int> IsActionDown;
	public delegate* unmanaged[Cdecl]<byte*, int> WasActionPressed;
	public delegate* unmanaged[Cdecl]<byte*, float> GetAxis;

	// --- time ---
	public delegate* unmanaged[Cdecl]<float> GetFixedDeltaTime;
	public delegate* unmanaged[Cdecl]<float> GetTime;
}

/// <summary>
/// A position, direction or scale. Mirrors <c>RageV::Math::Vec3</c>.
/// </summary>
/// <remarks>
/// Three floats, sequential, no padding — the same twelve bytes the engine
/// passes around, so it crosses the boundary without marshalling. There is a
/// static assertion on the native side that the layout has not drifted.
/// </remarks>
[StructLayout(LayoutKind.Sequential)]
public struct Vector3
{
	public float X;
	public float Y;
	public float Z;

	public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }
	public Vector3(float scalar) { X = scalar; Y = scalar; Z = scalar; }

	public static Vector3 Zero => new Vector3(0.0f, 0.0f, 0.0f);
	public static Vector3 One => new Vector3(1.0f, 1.0f, 1.0f);
	public static Vector3 Up => new Vector3(0.0f, 1.0f, 0.0f);

	public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
	public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
	public static Vector3 operator *(Vector3 v, float s) => new Vector3(v.X * s, v.Y * s, v.Z * s);
	public static Vector3 operator *(float s, Vector3 v) => v * s;

	public override string ToString() => $"({X}, {Y}, {Z})";
}

/// <summary>
/// The managed side of the boundary: the table, and the string handling that
/// crossing it requires.
/// </summary>
/// <remarks>
/// Strings are the only thing here that costs anything. The engine speaks UTF-8
/// and .NET strings are UTF-16, so every string argument is transcoded and
/// pinned for the duration of the call. That is why the engine's own script API
/// takes action *names* rather than key codes but is otherwise built from
/// numbers: a per-step call that allocates is a per-step call that eventually
/// shows up in a frame-time graph.
/// </remarks>
internal static unsafe class Native
{
	private static NativeApi s_Api;
	private static bool s_Ready;

	internal static bool IsReady => s_Ready;

	internal static void Bind(NativeApi* api)
	{
		s_Api = *api;
		s_Ready = true;
	}

	internal static void Unbind()
	{
		s_Api = default;
		s_Ready = false;
	}

	internal static ref readonly NativeApi Api => ref s_Api;

	/// <summary>Runs an action with a UTF-8, null-terminated copy of the string.</summary>
	/// <remarks>
	/// Stack-allocated below a threshold and heap-allocated above it. The
	/// threshold is not tuned: entity names, action names and log lines are
	/// nearly always short, and the branch exists so that the rare long one is
	/// correct rather than a stack overflow.
	/// </remarks>
	internal static T WithUtf8<T>(string? value, Utf8Func<T> body)
	{
		if (value is null)
			return body(null);

		int byteCount = System.Text.Encoding.UTF8.GetByteCount(value);
		if (byteCount < 256)
		{
			byte* buffer = stackalloc byte[byteCount + 1];
			fixed (char* chars = value)
				System.Text.Encoding.UTF8.GetBytes(chars, value.Length, buffer, byteCount);
			buffer[byteCount] = 0;
			return body(buffer);
		}

		byte[] heap = new byte[byteCount + 1];
		System.Text.Encoding.UTF8.GetBytes(value, 0, value.Length, heap, 0);
		fixed (byte* buffer = heap)
			return body(buffer);
	}

	internal delegate T Utf8Func<out T>(byte* utf8);
}
