namespace RageV;

/// <summary>
/// The engine's math, mirroring <c>RageV::Math</c> function for function.
/// </summary>
/// <remarks>
/// <para>
/// A script says <c>Mathf.Sin</c>, never <c>MathF.Sin</c> or
/// <c>System.Math.Sin</c>, for the reason the native side says
/// <c>Math::Sin</c>: RageV should read as one engine rather than as a thin
/// coat over a standard library, and a game script that has to know which of
/// three namespaces a function lives in is being asked to learn the engine's
/// history rather than its API.
/// </para>
/// <para>
/// <b>Named <c>Mathf</c> rather than <c>Math</c>, deliberately.</b> A class
/// called <c>Math</c> in this namespace would be ambiguous with
/// <c>System.Math</c> in every script that writes <c>using System;</c> next to
/// <c>using RageV;</c> — which the shipped scripts and the New Script template
/// both do — and the compiler reports that as CS0104 on the call site rather
/// than on the cause. The <c>f</c> also says what these are: single precision,
/// like every number that reaches the engine.
/// </para>
/// <para>
/// <b>Three of these are not forwards, and the differences are the same ones
/// the native side has.</b> <see cref="Acos"/> and <see cref="Asin"/> clamp
/// their argument first, because a dot product of two unit vectors is
/// analytically in [-1, 1] and numerically is not, and one ulp past the end is
/// a NaN angle that becomes a NaN rotation and then a vanished object.
/// <see cref="Mod"/> takes the sign of the divisor rather than the dividend,
/// which is what wrapping an angle means; <see cref="FMod"/> is the other one.
/// </para>
/// </remarks>
public static class Mathf
{
	// --- constants -----------------------------------------------------------

	/// <summary>The same value as <c>RageV::Math::Pi</c>, at float precision.</summary>
	public const float Pi = 3.14159265358979323846f;
	public const float HalfPi = Pi * 0.5f;
	public const float TwoPi = Pi * 2.0f;

	/// <summary>What the engine treats as "near enough to zero to be zero".</summary>
	public const float Epsilon = 1.0e-6f;

	public const float Infinity = float.PositiveInfinity;

	// --- angles --------------------------------------------------------------

	/// <summary>Degrees to radians. Every angle the engine takes is radians.</summary>
	public static float Radians(float degrees) => degrees * (Pi / 180.0f);

	/// <summary>Radians to degrees, for showing a number to a person.</summary>
	public static float Degrees(float radians) => radians * (180.0f / Pi);

	// --- comparison ----------------------------------------------------------

	public static float Min(float a, float b) => a < b ? a : b;
	public static float Max(float a, float b) => a > b ? a : b;
	public static int Min(int a, int b) => a < b ? a : b;
	public static int Max(int a, int b) => a > b ? a : b;

	/// <summary>Three or more, so a widest-of-three reads as one call.</summary>
	public static float Min(float a, float b, float c) => Min(Min(a, b), c);
	public static float Max(float a, float b, float c) => Max(Max(a, b), c);

	public static float Clamp(float v, float lo, float hi) => v < lo ? lo : (v > hi ? hi : v);
	public static int Clamp(int v, int lo, int hi) => v < lo ? lo : (v > hi ? hi : v);

	/// <summary>Clamped to [0, 1] — the shading language's <c>saturate</c>.</summary>
	public static float Saturate(float v) => Clamp(v, 0.0f, 1.0f);

	/// <summary>Zero for zero, rather than the +1 a two-way test gives it.</summary>
	public static float Sign(float v) => v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);

	public static float Abs(float v) => System.MathF.Abs(v);
	public static int Abs(int v) => v < 0 ? -v : v;

	// --- trigonometry --------------------------------------------------------

	public static float Sin(float v) => System.MathF.Sin(v);
	public static float Cos(float v) => System.MathF.Cos(v);
	public static float Tan(float v) => System.MathF.Tan(v);

	/// <summary>Clamped to [-1, 1] first; see the remarks on this class.</summary>
	public static float Asin(float v) => System.MathF.Asin(Clamp(v, -1.0f, 1.0f));

	/// <summary>Clamped to [-1, 1] first; see the remarks on this class.</summary>
	public static float Acos(float v) => System.MathF.Acos(Clamp(v, -1.0f, 1.0f));

	public static float Atan(float v) => System.MathF.Atan(v);

	/// <summary>y first: it is the opposite side over the adjacent.</summary>
	public static float Atan2(float y, float x) => System.MathF.Atan2(y, x);

	public static float Sinh(float v) => System.MathF.Sinh(v);
	public static float Cosh(float v) => System.MathF.Cosh(v);
	public static float Tanh(float v) => System.MathF.Tanh(v);

	// --- exponential ---------------------------------------------------------

	public static float Sqrt(float v) => System.MathF.Sqrt(v);

	/// <summary>Zero for a negative argument, rather than a NaN that travels.</summary>
	public static float SafeSqrt(float v) => v > 0.0f ? System.MathF.Sqrt(v) : 0.0f;

	public static float Pow(float baseValue, float exponent) => System.MathF.Pow(baseValue, exponent);
	public static float Exp(float v) => System.MathF.Exp(v);
	public static float Exp2(float v) => System.MathF.Pow(2.0f, v);
	public static float Log(float v) => System.MathF.Log(v);
	public static float Log2(float v) => System.MathF.Log2(v);
	public static float Log10(float v) => System.MathF.Log10(v);

	// --- rounding ------------------------------------------------------------

	public static float Floor(float v) => System.MathF.Floor(v);
	public static float Ceil(float v) => System.MathF.Ceiling(v);
	public static float Trunc(float v) => System.MathF.Truncate(v);

	/// <summary>
	/// Half away from zero, matching the native side rather than .NET's default.
	/// </summary>
	/// <remarks>
	/// <c>System.MathF.Round</c> rounds half to *even* unless told otherwise, so
	/// 0.5 becomes 0 and 1.5 becomes 2. C's <c>round</c>, which the engine uses,
	/// makes both 1 and 2. Two languages disagreeing about where a vertex lands
	/// is the kind of difference that shows up as one pixel and takes a day.
	/// </remarks>
	public static float Round(float v) =>
		System.MathF.Round(v, System.MidpointRounding.AwayFromZero);

	/// <summary>The part after the point, always positive: Fract(-0.25) is 0.75.</summary>
	public static float Fract(float v) => v - System.MathF.Floor(v);

	/// <summary>The remainder with the sign of the <em>divisor</em>: wrapping.</summary>
	public static float Mod(float value, float divisor) =>
		value - divisor * System.MathF.Floor(value / divisor);

	/// <summary>C's remainder, with the sign of the <em>dividend</em>.</summary>
	/// <remarks>
	/// C#'s <c>%</c> on floats already is <c>fmod</c>; it is spelled out here so
	/// that a call site choosing between the two remainders says which it means,
	/// exactly as the native side makes it choose.
	/// </remarks>
	public static float FMod(float value, float divisor) => value % divisor;

	// --- interpolation -------------------------------------------------------

	/// <summary>Linear blend; <paramref name="t"/> is not clamped.</summary>
	public static float Lerp(float a, float b, float t) => a + (b - a) * t;

	/// <summary>The same thing, under the name the shading languages use.</summary>
	public static float Mix(float a, float b, float t) => Lerp(a, b, t);

	/// <summary>Zero below the edge, one above, Hermite between. GLSL's.</summary>
	public static float SmoothStep(float edge0, float edge1, float v)
	{
		float t = Clamp((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	public static float Step(float edge, float v) => v < edge ? 0.0f : 1.0f;

	// --- queries -------------------------------------------------------------

	public static bool IsNaN(float v) => float.IsNaN(v);
	public static bool IsInf(float v) => float.IsInfinity(v);
	public static bool IsFinite(float v) => float.IsFinite(v);

	public static float Hypot(float a, float b) => Sqrt(a * a + b * b);
	public static float CopySign(float magnitude, float sign) =>
		System.MathF.CopySign(magnitude, sign);
}
