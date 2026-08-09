using System.Runtime.InteropServices;

namespace RageV;

/// <summary>
/// The managed end of the native boundary.
/// </summary>
/// <remarks>
/// Everything the engine calls into arrives here first. Methods on this type
/// are entry points reached by raw function pointer, not by name at runtime, so
/// their signatures are ABI: changing one and rebuilding only one side produces
/// a stack corruption rather than an error.
///
/// That is what <see cref="ProtocolVersion"/> exists to prevent.
/// </remarks>
public static class Interop
{
	/// <summary>
	/// Incremented whenever anything crossing the boundary changes shape — a
	/// signature, a struct layout, the order of a function table.
	/// </summary>
	/// <remarks>
	/// The native side compares this against its own before it calls anything
	/// else. A mismatch is reported as "rebuild the script assembly", which is
	/// a sentence somebody can act on; without the check the same situation is
	/// a crash somewhere unrelated, minutes later, in a stack trace that names
	/// none of the guilty parties.
	///
	/// This is cheap insurance for a failure mode that is otherwise very
	/// expensive to diagnose, and it will earn its place the first time a
	/// partial rebuild leaves one side stale.
	/// </remarks>
	public const int ProtocolVersion = 1;

	/// <summary>
	/// The first call the engine makes. Confirms that the runtime booted, that
	/// this assembly loaded, and that both sides agree on the protocol.
	/// </summary>
	/// <param name="nativeProtocolVersion">
	/// The version the native side was compiled against.
	/// </param>
	/// <returns>
	/// <see cref="ProtocolVersion"/> when the two agree, and its negation when
	/// they do not — so the native side learns *which* version it is talking to
	/// rather than only that something is wrong.
	/// </returns>
	[UnmanagedCallersOnly]
	public static int Handshake(int nativeProtocolVersion)
	{
		return nativeProtocolVersion == ProtocolVersion ? ProtocolVersion : -ProtocolVersion;
	}
}
