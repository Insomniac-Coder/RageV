#pragma once

// Operators and functions on the engine's math types.
//
// **Every one of these is glm underneath.** Nothing here reimplements
// arithmetic that a well-tested library already does well: the bodies live in
// Math.cpp, each converts to glm, calls glm, and converts back. Writing a
// second matrix inverse -- or a second `normalize` -- would be taking on a
// class of bug for no gain.
//
// That does mean declarations here and definitions in one translation unit,
// which normally costs the inlining. It does not here: link-time code
// generation is on for Release and Dist, so the optimiser sees glm's bodies at
// the call sites and folds them in exactly as if they had been in the header.
// Debug keeps the call, which is the configuration where that is affordable.
//
// The measured cost of the whole arrangement is recorded in HANDOFF section 9.
// It was measured rather than assumed, because "a function call cannot possibly
// matter" is how this engine has been wrong before.

#include "Types.h"

namespace RageV::Math
{
	// --- constants -----------------------------------------------------------
	//
	// Values, not calls, so they stay usable in a constant expression.

	inline constexpr float Pi      = 3.14159265358979323846f;
	inline constexpr float HalfPi  = Pi * 0.5f;
	inline constexpr float TwoPi   = Pi * 2.0f;
	inline constexpr float Epsilon = 1.0e-6f;

	// --- Vec2 ----------------------------------------------------------------

	Vec2 operator+(const Vec2& a, const Vec2& b);
	Vec2 operator-(const Vec2& a, const Vec2& b);
	Vec2 operator*(const Vec2& a, const Vec2& b);
	Vec2 operator/(const Vec2& a, const Vec2& b);
	Vec2 operator*(const Vec2& v, float s);
	Vec2 operator*(float s, const Vec2& v);
	Vec2 operator/(const Vec2& v, float s);
	Vec2 operator-(const Vec2& v);

	Vec2& operator+=(Vec2& a, const Vec2& b);
	Vec2& operator-=(Vec2& a, const Vec2& b);
	Vec2& operator*=(Vec2& v, float s);
	Vec2& operator/=(Vec2& v, float s);

	bool operator==(const Vec2& a, const Vec2& b);
	bool operator!=(const Vec2& a, const Vec2& b);

	// --- Vec3 ----------------------------------------------------------------

	Vec3 operator+(const Vec3& a, const Vec3& b);
	Vec3 operator-(const Vec3& a, const Vec3& b);
	Vec3 operator*(const Vec3& a, const Vec3& b);
	Vec3 operator/(const Vec3& a, const Vec3& b);
	Vec3 operator*(const Vec3& v, float s);
	Vec3 operator*(float s, const Vec3& v);
	Vec3 operator/(const Vec3& v, float s);
	Vec3 operator-(const Vec3& v);

	Vec3& operator+=(Vec3& a, const Vec3& b);
	Vec3& operator-=(Vec3& a, const Vec3& b);
	Vec3& operator*=(Vec3& a, const Vec3& b);
	Vec3& operator*=(Vec3& v, float s);
	Vec3& operator/=(Vec3& v, float s);

	bool operator==(const Vec3& a, const Vec3& b);
	bool operator!=(const Vec3& a, const Vec3& b);

	// --- Vec4 ----------------------------------------------------------------

	Vec4 operator+(const Vec4& a, const Vec4& b);
	Vec4 operator-(const Vec4& a, const Vec4& b);
	Vec4 operator*(const Vec4& a, const Vec4& b);
	Vec4 operator*(const Vec4& v, float s);
	Vec4 operator*(float s, const Vec4& v);
	Vec4 operator/(const Vec4& v, float s);
	Vec4 operator-(const Vec4& v);

	Vec4& operator+=(Vec4& a, const Vec4& b);
	Vec4& operator-=(Vec4& a, const Vec4& b);
	Vec4& operator*=(Vec4& v, float s);
	Vec4& operator/=(Vec4& v, float s);

	bool operator==(const Vec4& a, const Vec4& b);
	bool operator!=(const Vec4& a, const Vec4& b);

	bool operator==(const UVec4& a, const UVec4& b);
	bool operator!=(const UVec4& a, const UVec4& b);

	// --- matrices ------------------------------------------------------------
	//
	// Column-major, so `m * v` transforms v and `a * b` applies b first -- glm's
	// convention and both shading languages', which is the only reason a matrix
	// can be handed to the GPU untouched.

	Vec4 operator*(const Mat4& m, const Vec4& v);
	Mat4 operator*(const Mat4& a, const Mat4& b);
	Mat4 operator*(const Mat4& m, float s);
	Vec3 operator*(const Mat3& m, const Vec3& v);
	Mat3 operator*(const Mat3& a, const Mat3& b);

	// Sixteen contiguous floats, column-major, ready for a uniform buffer.
	// Inline because it is an address, not arithmetic -- there is no glm call
	// to delegate to, and the static_asserts in Types.h are what make it true.
	inline const float* ValuePtr(const Mat4& m) { return &m[0].x; }
	inline const float* ValuePtr(const Mat3& m) { return &m[0].x; }
	inline const float* ValuePtr(const Vec2& v) { return &v.x; }
	inline const float* ValuePtr(const Vec3& v) { return &v.x; }
	inline const float* ValuePtr(const Vec4& v) { return &v.x; }

	// Writable, because ImGui's colour and drag widgets edit through the pointer
	// they are given. glm has the same pair for the same reason.
	inline float* ValuePtr(Mat4& m) { return &m[0].x; }
	inline float* ValuePtr(Mat3& m) { return &m[0].x; }
	inline float* ValuePtr(Vec2& v) { return &v.x; }
	inline float* ValuePtr(Vec3& v) { return &v.x; }
	inline float* ValuePtr(Vec4& v) { return &v.x; }

	Mat4 Inverse(const Mat4& m);
	Mat4 Transpose(const Mat4& m);
	Mat3 Inverse(const Mat3& m);
	Mat3 Transpose(const Mat3& m);

	Mat4 Translate(const Mat4& m, const Vec3& delta);
	Mat4 Rotate(const Mat4& m, float radians, const Vec3& axis);
	Mat4 Scale(const Mat4& m, const Vec3& factor);

	Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
	Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

	// False when the matrix has no sensible decomposition -- a zero scale, or a
	// projection someone passed by mistake. The outputs are untouched then.
	bool Decompose(const Mat4& transform, Vec3& translation, Quat& rotation, Vec3& scale);

	// --- scalars -------------------------------------------------------------

	float Radians(float degrees);
	float Degrees(float radians);

	// Component-wise, for the inspector -- Euler angles are stored in radians and
	// shown in degrees, three at a time.
	Vec3 Radians(const Vec3& degrees);
	Vec3 Degrees(const Vec3& radians);

	float Min(float a, float b);
	float Max(float a, float b);
	float Clamp(float v, float lo, float hi);
	float Mix(float a, float b, float t);
	float Abs(float v);
	float Round(float v);

	// The remainder with the sign of the *divisor*, which is what wrapping an
	// angle needs and what glm::mod gives. std::fmod takes the sign of the
	// dividend instead and returns a negative angle for a negative input --
	// correct, and not what any caller here means.
	float Mod(float value, float divisor);

	// --- component-wise ------------------------------------------------------

	Vec2 Min(const Vec2& a, const Vec2& b);
	Vec2 Max(const Vec2& a, const Vec2& b);

	Vec3 Min(const Vec3& a, const Vec3& b);
	Vec3 Max(const Vec3& a, const Vec3& b);
	Vec3 Min(const Vec3& v, float s);
	Vec3 Max(const Vec3& v, float s);

	Vec4 Min(const Vec4& a, const Vec4& b);
	Vec4 Max(const Vec4& a, const Vec4& b);

	Vec3 Clamp(const Vec3& v, float lo, float hi);
	Vec4 Clamp(const Vec4& v, float lo, float hi);

	Vec3 Mix(const Vec3& a, const Vec3& b, float t);
	Vec4 Mix(const Vec4& a, const Vec4& b, float t);

	Vec3 Abs(const Vec3& v);
	Vec3 Round(const Vec3& v);
	Vec4 Round(const Vec4& v);

	// The largest component. Named for what it does rather than for glm's
	// `compMax`, which reads like a comparison.
	float MaxComponent(const Vec3& v);
	float MinComponent(const Vec3& v);

	// --- geometry ------------------------------------------------------------

	float Dot(const Vec2& a, const Vec2& b);
	float Dot(const Vec3& a, const Vec3& b);
	float Dot(const Vec4& a, const Vec4& b);

	Vec3 Cross(const Vec3& a, const Vec3& b);

	float Length(const Vec2& v);
	float Length(const Vec3& v);
	float Length(const Vec4& v);
	float LengthSquared(const Vec3& v);

	float Distance(const Vec2& a, const Vec2& b);
	float Distance(const Vec3& a, const Vec3& b);

	// Guarded, which is the one place this deliberately differs from glm.
	//
	// glm::normalize of a zero-length vector produces NaNs; a NaN in a transform
	// spreads to every transform derived from it, and the object and all its
	// children vanish with no error anywhere. That has cost this project real
	// time, and it is the single most common way to lose an object in a script.
	// Callers who genuinely want the raw behaviour can divide by Length.
	Vec2 Normalize(const Vec2& v);
	Vec3 Normalize(const Vec3& v);
	Vec4 Normalize(const Vec4& v);

	// --- rotations -----------------------------------------------------------

	Quat  Normalize(const Quat& q);
	Quat  Conjugate(const Quat& q);

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
	float Length(const Quat& q);

	Quat  operator*(const Quat& a, const Quat& b);
	Vec3  operator*(const Quat& q, const Vec3& v);

	Quat  AngleAxis(float radians, const Vec3& axis);

	// Applies the rotation to a vector. The same thing `q * v` does, spelled for
	// call sites where the verb reads better than the operator.
	Vec3  Rotate(const Quat& q, const Vec3& v);

	float Angle(const Quat& q);

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
