#pragma once

// Operators and functions on the engine's math types.
//
// **RageV implements its own math.** glm is not linked into the engine at all.
// The trivial arithmetic is written out here so it inlines; the rest --
// inversion, decomposition, projections, every quaternion operation -- is in
// Math.cpp, where the conventions it depends on are written down.
//
// The reason this is defensible rather than reckless is `scenetest`: it still
// links glm purely as an oracle and checks every operation against it, on
// deliberately awkward values. Axis-aligned test numbers pass through a
// transposed matrix unharmed, so a suite built on round numbers would prove
// nothing. **That block must never be migrated to use RageV::Math on both
// sides** -- it would then be comparing the answer to itself, and it would pass
// forever. That has already happened once; see HANDOFF section 10.
//
// The measurements behind writing it out at all are in HANDOFF section 9. The
// short version: delegating to glm and implementing it here measure the same,
// within noise, on a properly paired test.

#include "Types.h"

namespace RageV::Math
{
	// --- constants -----------------------------------------------------------

	inline constexpr float Pi      = 3.14159265358979323846f;
	inline constexpr float HalfPi  = Pi * 0.5f;
	inline constexpr float TwoPi   = Pi * 2.0f;
	inline constexpr float Epsilon = 1.0e-6f;

	// --- Vec2 ----------------------------------------------------------------

	constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
	constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
	constexpr Vec2 operator*(const Vec2& a, const Vec2& b) { return { a.x * b.x, a.y * b.y }; }
	constexpr Vec2 operator/(const Vec2& a, const Vec2& b) { return { a.x / b.x, a.y / b.y }; }
	constexpr Vec2 operator*(const Vec2& v, float s)       { return { v.x * s, v.y * s }; }
	constexpr Vec2 operator*(float s, const Vec2& v)       { return { v.x * s, v.y * s }; }
	constexpr Vec2 operator/(const Vec2& v, float s)       { return { v.x / s, v.y / s }; }
	constexpr Vec2 operator-(const Vec2& v)                { return { -v.x, -v.y }; }

	constexpr Vec2& operator+=(Vec2& a, const Vec2& b) { a.x += b.x; a.y += b.y; return a; }
	constexpr Vec2& operator-=(Vec2& a, const Vec2& b) { a.x -= b.x; a.y -= b.y; return a; }
	constexpr Vec2& operator*=(Vec2& v, float s)       { v.x *= s;  v.y *= s;  return v; }
	constexpr Vec2& operator/=(Vec2& v, float s)       { v.x /= s;  v.y /= s;  return v; }

	constexpr bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
	constexpr bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }

	// --- Vec3 ----------------------------------------------------------------

	constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	constexpr Vec3 operator*(const Vec3& a, const Vec3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
	constexpr Vec3 operator/(const Vec3& a, const Vec3& b) { return { a.x / b.x, a.y / b.y, a.z / b.z }; }
	constexpr Vec3 operator*(const Vec3& v, float s)       { return { v.x * s, v.y * s, v.z * s }; }
	constexpr Vec3 operator*(float s, const Vec3& v)       { return { v.x * s, v.y * s, v.z * s }; }
	constexpr Vec3 operator/(const Vec3& v, float s)       { return { v.x / s, v.y / s, v.z / s }; }
	constexpr Vec3 operator-(const Vec3& v)                { return { -v.x, -v.y, -v.z }; }

	constexpr Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
	constexpr Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
	constexpr Vec3& operator*=(Vec3& a, const Vec3& b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }
	constexpr Vec3& operator*=(Vec3& v, float s)       { v.x *= s;  v.y *= s;  v.z *= s;  return v; }
	constexpr Vec3& operator/=(Vec3& v, float s)       { v.x /= s;  v.y /= s;  v.z /= s;  return v; }

	constexpr bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
	constexpr bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }

	// --- Vec4 ----------------------------------------------------------------

	constexpr Vec4 operator+(const Vec4& a, const Vec4& b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
	constexpr Vec4 operator-(const Vec4& a, const Vec4& b) { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
	constexpr Vec4 operator*(const Vec4& a, const Vec4& b) { return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; }
	constexpr Vec4 operator*(const Vec4& v, float s)       { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
	constexpr Vec4 operator*(float s, const Vec4& v)       { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
	constexpr Vec4 operator/(const Vec4& v, float s)       { return { v.x / s, v.y / s, v.z / s, v.w / s }; }
	constexpr Vec4 operator-(const Vec4& v)                { return { -v.x, -v.y, -v.z, -v.w }; }

	constexpr Vec4& operator+=(Vec4& a, const Vec4& b) { a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w; return a; }
	constexpr Vec4& operator-=(Vec4& a, const Vec4& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; a.w -= b.w; return a; }
	constexpr Vec4& operator*=(Vec4& v, float s)       { v.x *= s;  v.y *= s;  v.z *= s;  v.w *= s;  return v; }
	constexpr Vec4& operator/=(Vec4& v, float s)       { v.x /= s;  v.y /= s;  v.z /= s;  v.w /= s;  return v; }

	constexpr bool operator==(const Vec4& a, const Vec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
	constexpr bool operator!=(const Vec4& a, const Vec4& b) { return !(a == b); }

	constexpr bool operator==(const UVec4& a, const UVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
	constexpr bool operator!=(const UVec4& a, const UVec4& b) { return !(a == b); }

	// --- matrices ------------------------------------------------------------
	//
	// Column-major, so `m * v` transforms v and `a * b` applies b first, which
	// is the convention both shading languages use -- the only reason a matrix
	// can be handed to the GPU untouched.
	//
	// Written out because it is the hottest arithmetic in the engine: every
	// instance of every mesh composes a world matrix every frame.

	constexpr Vec4 operator*(const Mat4& m, const Vec4& v)
	{
		return {
			m[0].x * v.x + m[1].x * v.y + m[2].x * v.z + m[3].x * v.w,
			m[0].y * v.x + m[1].y * v.y + m[2].y * v.z + m[3].y * v.w,
			m[0].z * v.x + m[1].z * v.y + m[2].z * v.z + m[3].z * v.w,
			m[0].w * v.x + m[1].w * v.y + m[2].w * v.z + m[3].w * v.w,
		};
	}

	constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
	{
		return { a * b[0], a * b[1], a * b[2], a * b[3] };
	}

	constexpr Mat4 operator*(const Mat4& m, float s)
	{
		return { m[0] * s, m[1] * s, m[2] * s, m[3] * s };
	}

	constexpr Vec3 operator*(const Mat3& m, const Vec3& v)
	{
		return {
			m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
			m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
			m[0].z * v.x + m[1].z * v.y + m[2].z * v.z,
		};
	}

	constexpr Mat3 operator*(const Mat3& a, const Mat3& b)
	{
		return { a * b[0], a * b[1], a * b[2] };
	}

	// Sixteen contiguous floats, column-major, ready for a uniform buffer.
	inline const float* ValuePtr(const Mat4& m) { return &m[0].x; }
	inline const float* ValuePtr(const Mat3& m) { return &m[0].x; }
	inline const float* ValuePtr(const Vec2& v) { return &v.x; }
	inline const float* ValuePtr(const Vec3& v) { return &v.x; }
	inline const float* ValuePtr(const Vec4& v) { return &v.x; }

	// Writable, because ImGui's colour and drag widgets edit through the pointer
	// they are given.
	inline float* ValuePtr(Mat4& m) { return &m[0].x; }
	inline float* ValuePtr(Mat3& m) { return &m[0].x; }
	inline float* ValuePtr(Vec2& v) { return &v.x; }
	inline float* ValuePtr(Vec3& v) { return &v.x; }
	inline float* ValuePtr(Vec4& v) { return &v.x; }

	// --- scalars -------------------------------------------------------------
	//
	// **The whole of <cmath> that this engine uses, under the engine's own
	// names.** A call site says `Math::Sin`, never `std::sin`, for the reason
	// Types.h gives for the vectors: RageV should read as one engine rather
	// than as a thin coat over the standard library, and a game script that has
	// to know which of `std::`, `glm::` and `Math::` a function lives in is
	// being asked to learn the engine's history. The wrappers are `inline` and
	// forward directly, so this costs nothing at any optimisation level.
	//
	// Float and double are spelled separately rather than templated. A template
	// deduces `Sin(1)` as `int` and truncates the answer to zero; two overloads
	// make that call ambiguous instead, which is the failure worth having.

	constexpr float Radians(float degrees) { return degrees * (Pi / 180.0f); }
	constexpr float Degrees(float radians) { return radians * (180.0f / Pi); }

	// Templates rather than the float-only pair these used to be. `Max(width /
	// 2u, 1u)` against a `float Max(float, float)` converted both arguments to
	// float, compared them there and converted back -- which is exact only
	// below 2^24, and every one of those call sites is a pixel count or a
	// buffer size that could exceed it. Deducing one type also *rejects*
	// `Max(anInt, aUint)` instead of quietly picking one, which is the same
	// argument one level up.
	template<typename T> constexpr T Min(T a, T b) { return a < b ? a : b; }
	template<typename T> constexpr T Max(T a, T b) { return a > b ? a : b; }
	template<typename T> constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

	// Three or more, so a widest-of-three reads as one call rather than as a
	// nest. Spelled to require the third argument: an empty pack here would
	// make every two-argument call ambiguous with the pair above.
	template<typename T, typename... Rest>
	constexpr T Min(T a, T b, T c, Rest... rest)
	{
		T best = Min(Min(a, b), c);
		((best = Min(best, rest)), ...);
		return best;
	}
	template<typename T, typename... Rest>
	constexpr T Max(T a, T b, T c, Rest... rest)
	{
		T best = Max(Max(a, b), c);
		((best = Max(best, rest)), ...);
		return best;
	}

	constexpr float Mix(float a, float b, float t) { return a + (b - a) * t; }
	// The name the shading languages and every other engine use for Mix. Both
	// are here because the GLSL half of this codebase says `mix` and the script
	// half says `Lerp`, and neither should have to translate.
	constexpr float Lerp(float a, float b, float t) { return Mix(a, b, t); }

	// Clamped to [0, 1] -- the shading language's `saturate`, which is what a
	// weight or a colour channel almost always wants.
	constexpr float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }

	// Zero for zero, rather than the +1 a `v < 0 ? -1 : 1` gives it, and
	// nothing like std::copysign's sign-of-a-zero behaviour.
	constexpr float Sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

	inline float  Abs(float v)  { return std::fabs(v); }
	inline double Abs(double v) { return std::fabs(v); }
	// Integral, where fabs would round-trip through a float. Signed only: the
	// absolute value of an unsigned is itself, and asking for it is usually a
	// sign the type is wrong.
	constexpr int      Abs(int v)      { return v < 0 ? -v : v; }
	constexpr long     Abs(long v)     { return v < 0 ? -v : v; }
	constexpr long long Abs(long long v) { return v < 0 ? -v : v; }

	// --- trigonometry --------------------------------------------------------
	//
	// Radians, like every other angle in this engine. `Radians()` above is the
	// converter, and the inspector is the only place degrees exist.

	inline float  Sin(float v)   { return std::sin(v); }
	inline double Sin(double v)  { return std::sin(v); }
	inline float  Cos(float v)   { return std::cos(v); }
	inline double Cos(double v)  { return std::cos(v); }
	inline float  Tan(float v)   { return std::tan(v); }
	inline double Tan(double v)  { return std::tan(v); }

	// Clamped before the call, which is the one place these deliberately differ
	// from <cmath>. A dot product of two unit vectors is analytically in
	// [-1, 1] and numerically is not: a rounding error of one ulp past the end
	// makes std::acos return NaN, and a NaN angle becomes a NaN rotation and
	// then a vanished object. That is the same failure Normalize guards, met
	// from the other side.
	inline float  Asin(float v)  { return std::asin(Clamp(v, -1.0f, 1.0f)); }
	inline double Asin(double v) { return std::asin(Clamp(v, -1.0, 1.0)); }
	inline float  Acos(float v)  { return std::acos(Clamp(v, -1.0f, 1.0f)); }
	inline double Acos(double v) { return std::acos(Clamp(v, -1.0, 1.0)); }

	inline float  Atan(float v)  { return std::atan(v); }
	inline double Atan(double v) { return std::atan(v); }
	// y first, as everywhere else -- it is the opposite side over the adjacent.
	inline float  Atan2(float y, float x)   { return std::atan2(y, x); }
	inline double Atan2(double y, double x) { return std::atan2(y, x); }

	inline float  Sinh(float v)  { return std::sinh(v); }
	inline double Sinh(double v) { return std::sinh(v); }
	inline float  Cosh(float v)  { return std::cosh(v); }
	inline double Cosh(double v) { return std::cosh(v); }
	inline float  Tanh(float v)  { return std::tanh(v); }
	inline double Tanh(double v) { return std::tanh(v); }

	// --- exponential ---------------------------------------------------------

	inline float  Sqrt(float v)  { return std::sqrt(v); }
	inline double Sqrt(double v) { return std::sqrt(v); }

	// Guarded, for the reason Normalize is: a negative under the root is a
	// silent NaN that travels. Callers who know the sign use Sqrt.
	inline float  SafeSqrt(float v)  { return v > 0.0f ? std::sqrt(v) : 0.0f; }

	inline float  Pow(float base, float exponent)    { return std::pow(base, exponent); }
	inline double Pow(double base, double exponent)  { return std::pow(base, exponent); }
	inline float  Exp(float v)   { return std::exp(v); }
	inline double Exp(double v)  { return std::exp(v); }
	inline float  Exp2(float v)  { return std::exp2(v); }
	inline double Exp2(double v) { return std::exp2(v); }
	inline float  Log(float v)   { return std::log(v); }
	inline double Log(double v)  { return std::log(v); }
	inline float  Log2(float v)  { return std::log2(v); }
	inline double Log2(double v) { return std::log2(v); }
	inline float  Log10(float v)  { return std::log10(v); }
	inline double Log10(double v) { return std::log10(v); }

	// --- rounding ------------------------------------------------------------

	inline float  Floor(float v)  { return std::floor(v); }
	inline double Floor(double v) { return std::floor(v); }
	inline float  Ceil(float v)   { return std::ceil(v); }
	inline double Ceil(double v)  { return std::ceil(v); }
	inline float  Trunc(float v)  { return std::trunc(v); }
	inline double Trunc(double v) { return std::trunc(v); }
	inline float  Round(float v)  { return std::round(v); }
	inline double Round(double v) { return std::round(v); }

	// The part after the point, always positive -- `Fract(-0.25) == 0.75`,
	// which is what tiling a texture or wrapping a curve means. Trunc is the
	// one that cuts towards zero.
	inline float Fract(float v) { return v - std::floor(v); }

	// C's remainder, with the sign of the *dividend*. `Mod` below is the other
	// one -- sign of the divisor, which is what wrapping an angle or a clip
	// time means. They differ only for negative inputs, which is exactly when
	// picking the wrong one is hard to see: `FMod(-1, 3)` is -1 and
	// `Mod(-1, 3)` is 2. Both are here so a call site has to say which.
	inline float  FMod(float value, float divisor)   { return std::fmod(value, divisor); }
	inline double FMod(double value, double divisor) { return std::fmod(value, divisor); }

	// --- interpolation and queries -------------------------------------------

	// Zero below the edge, one above it, and the smooth Hermite curve between.
	// The GLSL function of the same name, and the same clamp.
	constexpr float SmoothStep(float edge0, float edge1, float v)
	{
		const float t = Clamp((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
	constexpr float Step(float edge, float v) { return v < edge ? 0.0f : 1.0f; }

	inline bool IsNaN(float v)    { return std::isnan(v); }
	inline bool IsInf(float v)    { return std::isinf(v); }
	inline bool IsFinite(float v) { return std::isfinite(v); }

	inline float  Hypot(float a, float b) { return std::hypot(a, b); }
	inline float  CopySign(float magnitude, float sign) { return std::copysign(magnitude, sign); }

	// --- component-wise ------------------------------------------------------

	constexpr Vec2 Min(const Vec2& a, const Vec2& b) { return { Min(a.x, b.x), Min(a.y, b.y) }; }
	constexpr Vec2 Max(const Vec2& a, const Vec2& b) { return { Max(a.x, b.x), Max(a.y, b.y) }; }

	constexpr Vec3 Min(const Vec3& a, const Vec3& b) { return { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z) }; }
	constexpr Vec3 Max(const Vec3& a, const Vec3& b) { return { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) }; }
	constexpr Vec3 Min(const Vec3& v, float s)       { return { Min(v.x, s), Min(v.y, s), Min(v.z, s) }; }
	constexpr Vec3 Max(const Vec3& v, float s)       { return { Max(v.x, s), Max(v.y, s), Max(v.z, s) }; }

	constexpr Vec4 Min(const Vec4& a, const Vec4& b) { return { Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z), Min(a.w, b.w) }; }
	constexpr Vec4 Max(const Vec4& a, const Vec4& b) { return { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z), Max(a.w, b.w) }; }

	constexpr Vec3 Clamp(const Vec3& v, float lo, float hi)
	{
		return { Clamp(v.x, lo, hi), Clamp(v.y, lo, hi), Clamp(v.z, lo, hi) };
	}
	constexpr Vec4 Clamp(const Vec4& v, float lo, float hi)
	{
		return { Clamp(v.x, lo, hi), Clamp(v.y, lo, hi), Clamp(v.z, lo, hi), Clamp(v.w, lo, hi) };
	}

	constexpr Vec3 Mix(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
	constexpr Vec4 Mix(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

	inline Vec3 Abs(const Vec3& v) { return { Abs(v.x), Abs(v.y), Abs(v.z) }; }

	constexpr Vec3 Radians(const Vec3& degrees) { return degrees * (Pi / 180.0f); }
	constexpr Vec3 Degrees(const Vec3& radians) { return radians * (180.0f / Pi); }

	// The largest component. Named for what it does rather than for glm's
	// `compMax`, which reads like a comparison.
	constexpr float MaxComponent(const Vec3& v) { return Max(Max(v.x, v.y), v.z); }
	constexpr float MinComponent(const Vec3& v) { return Min(Min(v.x, v.y), v.z); }

	// --- geometry ------------------------------------------------------------

	constexpr float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
	constexpr float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	constexpr float Dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

	constexpr Vec3 Cross(const Vec3& a, const Vec3& b)
	{
		return { a.y * b.z - a.z * b.y,
				 a.z * b.x - a.x * b.z,
				 a.x * b.y - a.y * b.x };
	}

	inline float Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
	inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
	inline float Length(const Vec4& v) { return std::sqrt(Dot(v, v)); }

	constexpr float LengthSquared(const Vec3& v) { return Dot(v, v); }

	inline float Distance(const Vec2& a, const Vec2& b) { return Length(b - a); }
	inline float Distance(const Vec3& a, const Vec3& b) { return Length(b - a); }

	// Guarded, which is the one place this deliberately differs from glm.
	//
	// glm::normalize of a zero-length vector produces NaNs; a NaN in a transform
	// spreads to every transform derived from it, and the object and all its
	// children vanish with no error anywhere. That has cost this project real
	// time, and it is the most common way to lose an object in a script.
	// Callers who genuinely want the raw behaviour can divide by Length.
	inline Vec2 Normalize(const Vec2& v)
	{
		const float length = Length(v);
		return length > Epsilon ? v / length : Vec2{};
	}
	inline Vec3 Normalize(const Vec3& v)
	{
		const float length = Length(v);
		return length > Epsilon ? v / length : Vec3{};
	}
	inline Vec4 Normalize(const Vec4& v)
	{
		const float length = Length(v);
		return length > Epsilon ? v / length : Vec4{};
	}

	// --- the rest, in Math.cpp -----------------------------------------------
	//
	// Out of line because they are long, not because they belong to somebody
	// else. Math.cpp is also where the handedness and depth-range conventions
	// they depend on are written down.

	// The scalar Round is up with the other <cmath> wrappers; these two are
	// here because a component-wise loop is not a one-line forward.
	Vec3  Round(const Vec3& v);
	Vec4  Round(const Vec4& v);

	// The remainder with the sign of the *divisor*, which is what wrapping an
	// angle needs. std::fmod takes the sign of the dividend instead and returns
	// a negative angle for a negative input -- correct, and not what any caller
	// here means.
	float Mod(float value, float divisor);

	Mat4 Inverse(const Mat4& m);
	Mat4 Transpose(const Mat4& m);
	Mat3 Inverse(const Mat3& m);
	Mat3 Transpose(const Mat3& m);

	Mat4 Translate(const Mat4& m, const Vec3& delta);
	Mat4 Rotate(const Mat4& m, float radians, const Vec3& axis);
	Mat4 Scale(const Mat4& m, const Vec3& factor);

	// Right-handed, clip-space depth in [0, 1]. Read the note at the top of
	// Math.cpp before changing either.
	Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
	Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

	// False when the matrix has no sensible decomposition -- a zero scale, or a
	// projection someone passed by mistake. The outputs are untouched then.
	bool Decompose(const Mat4& transform, Vec3& translation, Quat& rotation, Vec3& scale);

	// --- rotations -----------------------------------------------------------

	Quat  Normalize(const Quat& q);
	Quat  Conjugate(const Quat& q);
	float Length(const Quat& q);

	// Negating a quaternion leaves the rotation it describes unchanged -- it is
	// the other half of the double cover. Needed wherever two rotations have to
	// be brought into the same hemisphere before interpolating.
	Quat  operator-(const Quat& q);
	float Dot(const Quat& a, const Quat& b);

	// Component-wise, which is not a rotation of anything -- it is how you ask
	// how far apart two quaternions are numerically, and that is what a test
	// comparing poses wants.
	Quat  operator+(const Quat& a, const Quat& b);
	Quat  operator-(const Quat& a, const Quat& b);

	Quat  operator*(const Quat& a, const Quat& b);
	Vec3  operator*(const Quat& q, const Vec3& v);

	Quat  AngleAxis(float radians, const Vec3& axis);
	float Angle(const Quat& q);

	// Applies the rotation to a vector. The same thing `q * v` does, spelled for
	// call sites where the verb reads better than the operator.
	Vec3  Rotate(const Quat& q, const Vec3& v);

	// Radians, in the engine's XYZ order -- the same order the inspector shows
	// and the same order a TransformComponent stores.
	Quat  FromEuler(const Vec3& radians);
	Vec3  ToEuler(const Quat& q);

	Mat4  ToMat4(const Quat& q);
	Quat  ToQuat(const Mat4& m);

	// Shortest-arc interpolation. Takes the near side of the double cover, so
	// two rotations that are numerically far apart but visually close do not
	// spin the long way round.
	Quat  Slerp(const Quat& a, const Quat& b, float t);
}
