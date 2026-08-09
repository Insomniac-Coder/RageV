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

	constexpr float Radians(float degrees) { return degrees * (Pi / 180.0f); }
	constexpr float Degrees(float radians) { return radians * (180.0f / Pi); }

	constexpr float Min(float a, float b) { return a < b ? a : b; }
	constexpr float Max(float a, float b) { return a > b ? a : b; }
	constexpr float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	constexpr float Mix(float a, float b, float t) { return a + (b - a) * t; }
	inline    float Abs(float v) { return std::fabs(v); }

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

	float Round(float v);
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
