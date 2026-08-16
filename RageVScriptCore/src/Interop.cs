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
	// 4: appended the rest of the native surface -- hierarchy, prefabs,
	//    raycasts, audio, components by name.
	// 5: appended one-shots with pitch, and one from a point.
	// 6: scripts gained a second rate. OnUpdate became OnTick, OnFrame joined
	//    it, and the interpolation alpha became readable.
	// 7: the game's UI -- a label's text, a button's click, and whether the UI
	//    took the pointer.
	public const int ProtocolVersion = 8;

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
	/// Evaluates one <see cref="Mathf"/> function, so the native side can check
	/// that the two languages' math agrees.
	/// </summary>
	/// <remarks>
	/// <para>
	/// Not a test of <c>MathF</c> — the platform's transcendentals are not this
	/// engine's problem. It is a test that <c>Mathf</c> and <c>RageV::Math</c>
	/// are the <em>same</em> functions, which is a claim neither side can make
	/// alone and which two of them quietly are not by default: .NET rounds half
	/// to even and C rounds half away from zero, and both languages have a
	/// remainder whose sign follows the wrong operand.
	/// </para>
	/// <para>
	/// One entry point taking an opcode rather than one export per function:
	/// the alternative is thirty exports to bind and thirty places to forget
	/// one. `scenetest` owns the table of opcodes and the values to feed them.
	/// </para>
	/// </remarks>
	[UnmanagedCallersOnly]
	public static float EvaluateMath(int op, float a, float b)
	{
		return op switch
		{
			0 => Mathf.Sin(a),
			1 => Mathf.Cos(a),
			2 => Mathf.Tan(a),
			3 => Mathf.Asin(a),
			4 => Mathf.Acos(a),
			5 => Mathf.Atan(a),
			6 => Mathf.Atan2(a, b),
			7 => Mathf.Sqrt(a),
			8 => Mathf.Pow(a, b),
			9 => Mathf.Exp(a),
			10 => Mathf.Log(a),
			11 => Mathf.Log2(a),
			12 => Mathf.Floor(a),
			13 => Mathf.Ceil(a),
			14 => Mathf.Trunc(a),
			15 => Mathf.Round(a),
			16 => Mathf.Fract(a),
			17 => Mathf.Abs(a),
			18 => Mathf.Sign(a),
			19 => Mathf.Min(a, b),
			20 => Mathf.Max(a, b),
			21 => Mathf.Clamp(a, 0.0f, 1.0f),
			22 => Mathf.Saturate(a),
			23 => Mathf.Lerp(a, b, 0.25f),
			24 => Mathf.SmoothStep(0.0f, 1.0f, a),
			25 => Mathf.Step(0.5f, a),
			26 => Mathf.Mod(a, b),
			27 => Mathf.FMod(a, b),
			28 => Mathf.Radians(a),
			29 => Mathf.Degrees(a),
			30 => Mathf.SafeSqrt(a),
			31 => Mathf.Hypot(a, b),
			32 => Mathf.CopySign(a, b),
			33 => Mathf.Sinh(a),
			34 => Mathf.Cosh(a),
			35 => Mathf.Tanh(a),
			36 => Mathf.Exp2(a),
			37 => Mathf.Log10(a),
			38 => Mathf.Pi,
			// An opcode this side does not know is not a zero: zero is a
			// plausible answer for half the functions above, and a check
			// comparing against it would pass.
			_ => float.NaN,
		};
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

			// The last entries in the table.
			//
			// This is where an appended entry lands, and where getting the
			// order wrong on one side shows up: every field before it keeps its
			// offset, so the *only* symptom is the tail calling the wrong
			// function. Distinctive return values on purpose -- a length, a
			// success flag and a -1 -- because three calls that all answer zero
			// would agree whichever way round they were.
			byte* text = stackalloc byte[16];
			if (Native.Api.GetUIText(entity, text, 16) == 3
				&& Marshal.PtrToStringUTF8((IntPtr)text) == "hud"
				&& Native.WithUtf8("score", utf8 => Native.Api.SetUIText(entity, utf8)) != 0
				&& Native.Api.GetUIText(entity, text, 16) == 5
				&& Native.Api.WasUIButtonClicked(entity) == 0
				&& Native.Api.IsPointerOverUI() == 0)
			{
				result |= 1 << 9;
			}

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
	public const int SelfTestAllPassed = (1 << 10) - 1;
}
