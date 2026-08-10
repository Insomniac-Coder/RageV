using System;
using System.Runtime.InteropServices;

namespace RageV;

/// <summary>
/// The managed end of the native boundary.
/// </summary>
/// <remarks>
/// Everything the engine calls into arrives here. Methods marked
/// <c>[UnmanagedCallersOnly]</c> are entry points reached by raw function
/// pointer, not by name at runtime, so their signatures are ABI: changing one
/// and rebuilding only one side produces a stack corruption rather than an
/// error.
///
/// **No exception may leave one of these methods.** An exception crossing an
/// unmanaged frame terminates the process rather than unwinding, so every entry
/// point catches at its edge and reports through the engine's log instead. A
/// script that throws should stop that script, not the editor.
/// </remarks>
public static unsafe class Interop
{
	/// <summary>
	/// Incremented whenever anything crossing the boundary changes shape — a
	/// signature, a struct layout, the order of a field in <c>NativeApi</c>.
	/// </summary>
	/// <remarks>
	/// The native side compares this against its own before it calls anything
	/// else. A mismatch is reported as "rebuild the script assembly", which is
	/// a sentence somebody can act on; without the check the same situation is
	/// a crash somewhere unrelated, minutes later, in a stack trace that names
	/// none of the guilty parties.
	/// </remarks>
	// 1: the first table.
	// 2: appended physics, world transform and spawn/destroy; added the script
	//    lifecycle and the collision struct.
	// 3: appended script field reflection.
	public const int ProtocolVersion = 4;

	/// <summary>
	/// The first call the engine makes. Confirms the protocol and takes the
	/// table of native functions.
	/// </summary>
	/// <returns>
	/// <see cref="ProtocolVersion"/> when the two agree, and its negation when
	/// they do not — so the native side learns *which* version it is talking to
	/// rather than only that something is wrong. The table is not stored on a
	/// mismatch: calling through a table whose shape is in doubt is the thing
	/// the version check exists to prevent.
	/// </returns>
	// `void*` rather than `NativeApi*`, because the table type is internal and
	// this method is not. Widening NativeApi to public to satisfy the signature
	// would put fifteen raw function pointers into the API surface scripts see,
	// which is what the wrappers exist to avoid. IntPtr would read better still
	// and is rejected: [UnmanagedCallersOnly] takes unmanaged types only.
	[UnmanagedCallersOnly]
	public static int Bootstrap(void* api, int nativeProtocolVersion)
	{
		try
		{
			if (nativeProtocolVersion != ProtocolVersion || api is null)
				return -ProtocolVersion;

			Native.Bind((NativeApi*)api);
			return ProtocolVersion;
		}
		catch
		{
			return -ProtocolVersion;
		}
	}

	/// <summary>
	/// Kept from before the table existed: proves the runtime booted and this
	/// assembly loaded, without needing anything bound.
	/// </summary>
	[UnmanagedCallersOnly]
	public static int Handshake(int nativeProtocolVersion)
	{
		return nativeProtocolVersion == ProtocolVersion ? ProtocolVersion : -ProtocolVersion;
	}

	/// <summary>
	/// Exercises every shape that crosses the boundary, against a real entity,
	/// and reports which ones worked as a bit mask.
	/// </summary>
	/// <remarks>
	/// This exists because "the table was handed over" and "the table works"
	/// are different claims, and only the second one matters. Each shape is a
	/// separate bit so a failure says *which* marshalling is wrong rather than
	/// that something is: a struct out-parameter, a struct in-parameter, a
	/// string in, a string out with truncation, and a plain float return are
	/// each their own way to be broken.
	///
	/// Called from scenetest. It is not part of the scripting API.
	/// </remarks>
	[UnmanagedCallersOnly]
	public static int SelfTest(ulong entity)
	{
		try
		{
			int result = 0;

			if (!Native.IsReady)
				return 0;
			result |= 1 << 0;

			// A struct out-parameter: twelve bytes written by native code into
			// a managed local.
			Vector3 position;
			if (Native.Api.GetPosition(entity, &position) != 0)
				result |= 1 << 1;

			// A struct in-parameter, read back to prove it landed. Awkward
			// numbers rather than round ones -- 1.0f would survive a swapped
			// field order.
			Vector3 moved = new Vector3(1.5f, -2.25f, 0.75f);
			if (Native.Api.SetPosition(entity, &moved) != 0)
			{
				Vector3 readBack;
				if (Native.Api.GetPosition(entity, &readBack) != 0
					&& readBack.X == moved.X && readBack.Y == moved.Y && readBack.Z == moved.Z)
				{
					result |= 1 << 2;
				}
			}

			// A string going out: UTF-8, transcoded and pinned for the call.
			ulong found = Native.WithUtf8("InteropProbe", utf8 => Native.Api.FindEntityByName(utf8));
			if (found == entity)
				result |= 1 << 3;

			// A string coming back, including the length-that-would-have-fit
			// contract. A buffer of one holds only the terminator, so the call
			// must still report the real length.
			byte* small = stackalloc byte[1];
			int needed = Native.Api.GetEntityName(entity, small, 1);
			if (needed == "InteropProbe".Length && small[0] == 0)
				result |= 1 << 4;

			byte* room = stackalloc byte[64];
			if (Native.Api.GetEntityName(entity, room, 64) == needed
				&& Marshal.PtrToStringUTF8((IntPtr)room) == "InteropProbe")
			{
				result |= 1 << 5;
			}

			// A plain float return, and a call with no arguments at all.
			if (Native.Api.GetFixedDeltaTime() > 0.0f)
				result |= 1 << 6;

			// An entity that does not exist has to answer, not fault.
			if (Native.Api.EntityExists(0xDEADBEEF) == 0 && Native.Api.EntityExists(entity) != 0)
				result |= 1 << 7;

			// Logging, which is the path every script uses to say anything.
			Native.WithUtf8<int>("interop self-test reached the log", utf8 =>
			{
				Native.Api.Log(1, utf8);
				return 0;
			});
			result |= 1 << 8;

			return result;
		}
		catch
		{
			// Deliberately swallowed. An exception leaving this method crosses
			// an unmanaged frame and terminates the process; a zero is a
			// failure the caller can report.
			return 0;
		}
	}

	/// <summary>Every bit <see cref="SelfTest"/> sets when nothing is wrong.</summary>
	public const int SelfTestAllPassed = (1 << 9) - 1;
}
