#include <rvpch.h>
#include "Math.h"
#include "GlmBridge.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

// The only translation unit that knows glm exists.
//
// Every function here has the same three-line shape -- convert, call glm,
// convert back -- and that repetition is the point. RageV owns the *names* its
// users see; it does not own the arithmetic, and reimplementing a vector
// library that is already correct and already fast would be taking on a class
// of bug in exchange for nothing.
//
// The conversions cost nothing after inlining, and link-time code generation
// (Release and Dist) puts glm's bodies back at the call sites, so the only
// configuration that pays for the boundary is Debug.

namespace RageV::Math
{
	// --- Vec2 ----------------------------------------------------------------

	Vec2 operator+(const Vec2& a, const Vec2& b) { return FromGlm(ToGlm(a) + ToGlm(b)); }
	Vec2 operator-(const Vec2& a, const Vec2& b) { return FromGlm(ToGlm(a) - ToGlm(b)); }
	Vec2 operator*(const Vec2& a, const Vec2& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }
	Vec2 operator/(const Vec2& a, const Vec2& b) { return FromGlm(ToGlm(a) / ToGlm(b)); }
	Vec2 operator*(const Vec2& v, float s)       { return FromGlm(ToGlm(v) * s); }
	Vec2 operator*(float s, const Vec2& v)       { return FromGlm(s * ToGlm(v)); }
	Vec2 operator/(const Vec2& v, float s)       { return FromGlm(ToGlm(v) / s); }
	Vec2 operator-(const Vec2& v)                { return FromGlm(-ToGlm(v)); }

	Vec2& operator+=(Vec2& a, const Vec2& b) { a = a + b; return a; }
	Vec2& operator-=(Vec2& a, const Vec2& b) { a = a - b; return a; }
	Vec2& operator*=(Vec2& v, float s)       { v = v * s; return v; }
	Vec2& operator/=(Vec2& v, float s)       { v = v / s; return v; }

	bool operator==(const Vec2& a, const Vec2& b) { return ToGlm(a) == ToGlm(b); }
	bool operator!=(const Vec2& a, const Vec2& b) { return ToGlm(a) != ToGlm(b); }

	// --- Vec3 ----------------------------------------------------------------

	Vec3 operator+(const Vec3& a, const Vec3& b) { return FromGlm(ToGlm(a) + ToGlm(b)); }
	Vec3 operator-(const Vec3& a, const Vec3& b) { return FromGlm(ToGlm(a) - ToGlm(b)); }
	Vec3 operator*(const Vec3& a, const Vec3& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }
	Vec3 operator/(const Vec3& a, const Vec3& b) { return FromGlm(ToGlm(a) / ToGlm(b)); }
	Vec3 operator*(const Vec3& v, float s)       { return FromGlm(ToGlm(v) * s); }
	Vec3 operator*(float s, const Vec3& v)       { return FromGlm(s * ToGlm(v)); }
	Vec3 operator/(const Vec3& v, float s)       { return FromGlm(ToGlm(v) / s); }
	Vec3 operator-(const Vec3& v)                { return FromGlm(-ToGlm(v)); }

	Vec3& operator+=(Vec3& a, const Vec3& b) { a = a + b; return a; }
	Vec3& operator-=(Vec3& a, const Vec3& b) { a = a - b; return a; }
	Vec3& operator*=(Vec3& a, const Vec3& b) { a = a * b; return a; }
	Vec3& operator*=(Vec3& v, float s)       { v = v * s; return v; }
	Vec3& operator/=(Vec3& v, float s)       { v = v / s; return v; }

	bool operator==(const Vec3& a, const Vec3& b) { return ToGlm(a) == ToGlm(b); }
	bool operator!=(const Vec3& a, const Vec3& b) { return ToGlm(a) != ToGlm(b); }

	// --- Vec4 ----------------------------------------------------------------

	Vec4 operator+(const Vec4& a, const Vec4& b) { return FromGlm(ToGlm(a) + ToGlm(b)); }
	Vec4 operator-(const Vec4& a, const Vec4& b) { return FromGlm(ToGlm(a) - ToGlm(b)); }
	Vec4 operator*(const Vec4& a, const Vec4& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }
	Vec4 operator*(const Vec4& v, float s)       { return FromGlm(ToGlm(v) * s); }
	Vec4 operator*(float s, const Vec4& v)       { return FromGlm(s * ToGlm(v)); }
	Vec4 operator/(const Vec4& v, float s)       { return FromGlm(ToGlm(v) / s); }
	Vec4 operator-(const Vec4& v)                { return FromGlm(-ToGlm(v)); }

	Vec4& operator+=(Vec4& a, const Vec4& b) { a = a + b; return a; }
	Vec4& operator-=(Vec4& a, const Vec4& b) { a = a - b; return a; }
	Vec4& operator*=(Vec4& v, float s)       { v = v * s; return v; }
	Vec4& operator/=(Vec4& v, float s)       { v = v / s; return v; }

	bool operator==(const Vec4& a, const Vec4& b) { return ToGlm(a) == ToGlm(b); }
	bool operator!=(const Vec4& a, const Vec4& b) { return ToGlm(a) != ToGlm(b); }

	bool operator==(const UVec4& a, const UVec4& b) { return ToGlm(a) == ToGlm(b); }
	bool operator!=(const UVec4& a, const UVec4& b) { return ToGlm(a) != ToGlm(b); }

	// --- matrices ------------------------------------------------------------

	Vec4 operator*(const Mat4& m, const Vec4& v) { return FromGlm(ToGlm(m) * ToGlm(v)); }
	Mat4 operator*(const Mat4& a, const Mat4& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }
	Mat4 operator*(const Mat4& m, float s)       { return FromGlm(ToGlm(m) * s); }
	Vec3 operator*(const Mat3& m, const Vec3& v) { return FromGlm(ToGlm(m) * ToGlm(v)); }
	Mat3 operator*(const Mat3& a, const Mat3& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }

	Mat4 Inverse(const Mat4& m)   { return FromGlm(glm::inverse(ToGlm(m))); }
	Mat4 Transpose(const Mat4& m) { return FromGlm(glm::transpose(ToGlm(m))); }
	Mat3 Inverse(const Mat3& m)   { return FromGlm(glm::inverse(ToGlm(m))); }
	Mat3 Transpose(const Mat3& m) { return FromGlm(glm::transpose(ToGlm(m))); }

	Mat4 Translate(const Mat4& m, const Vec3& delta)
	{
		return FromGlm(glm::translate(ToGlm(m), ToGlm(delta)));
	}

	Mat4 Rotate(const Mat4& m, float radians, const Vec3& axis)
	{
		return FromGlm(glm::rotate(ToGlm(m), radians, ToGlm(axis)));
	}

	Mat4 Scale(const Mat4& m, const Vec3& factor)
	{
		return FromGlm(glm::scale(ToGlm(m), ToGlm(factor)));
	}

	Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
	{
		return FromGlm(glm::perspective(verticalFovRadians, aspect, nearPlane, farPlane));
	}

	Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
	{
		return FromGlm(glm::ortho(left, right, bottom, top, nearPlane, farPlane));
	}

	Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
	{
		return FromGlm(glm::lookAt(ToGlm(eye), ToGlm(target), ToGlm(up)));
	}

	bool Decompose(const Mat4& transform, Vec3& translation, Quat& rotation, Vec3& scale)
	{
		glm::vec3 glmTranslation{};
		glm::vec3 glmScale{};
		glm::quat glmRotation{};
		glm::vec3 skew{};
		glm::vec4 perspective{};

		// Skew and perspective are decomposed and thrown away on purpose. A
		// transform in this engine is translation, rotation and scale; anything
		// carrying the other two came from somewhere it should not have, and
		// silently keeping them would put a shear into a component with nowhere
		// to store it.
		if (!glm::decompose(ToGlm(transform), glmScale, glmRotation, glmTranslation, skew, perspective))
			return false;

		translation = FromGlm(glmTranslation);
		rotation = FromGlm(glmRotation);
		scale = FromGlm(glmScale);
		return true;
	}

	// --- scalars -------------------------------------------------------------

	float Radians(float degrees) { return glm::radians(degrees); }
	float Degrees(float radians) { return glm::degrees(radians); }

	Vec3 Radians(const Vec3& degrees) { return FromGlm(glm::radians(ToGlm(degrees))); }
	Vec3 Degrees(const Vec3& radians) { return FromGlm(glm::degrees(ToGlm(radians))); }

	float Min(float a, float b) { return glm::min(a, b); }
	float Max(float a, float b) { return glm::max(a, b); }
	float Clamp(float v, float lo, float hi) { return glm::clamp(v, lo, hi); }
	float Mix(float a, float b, float t) { return glm::mix(a, b, t); }
	float Abs(float v) { return glm::abs(v); }
	float Round(float v) { return glm::round(v); }
	float Mod(float value, float divisor) { return glm::mod(value, divisor); }

	// --- component-wise ------------------------------------------------------

	Vec2 Min(const Vec2& a, const Vec2& b) { return FromGlm(glm::min(ToGlm(a), ToGlm(b))); }
	Vec2 Max(const Vec2& a, const Vec2& b) { return FromGlm(glm::max(ToGlm(a), ToGlm(b))); }

	Vec3 Min(const Vec3& a, const Vec3& b) { return FromGlm(glm::min(ToGlm(a), ToGlm(b))); }
	Vec3 Max(const Vec3& a, const Vec3& b) { return FromGlm(glm::max(ToGlm(a), ToGlm(b))); }
	Vec3 Min(const Vec3& v, float s)       { return FromGlm(glm::min(ToGlm(v), s)); }
	Vec3 Max(const Vec3& v, float s)       { return FromGlm(glm::max(ToGlm(v), s)); }

	Vec4 Min(const Vec4& a, const Vec4& b) { return FromGlm(glm::min(ToGlm(a), ToGlm(b))); }
	Vec4 Max(const Vec4& a, const Vec4& b) { return FromGlm(glm::max(ToGlm(a), ToGlm(b))); }

	Vec3 Clamp(const Vec3& v, float lo, float hi) { return FromGlm(glm::clamp(ToGlm(v), lo, hi)); }
	Vec4 Clamp(const Vec4& v, float lo, float hi) { return FromGlm(glm::clamp(ToGlm(v), lo, hi)); }

	Vec3 Mix(const Vec3& a, const Vec3& b, float t) { return FromGlm(glm::mix(ToGlm(a), ToGlm(b), t)); }
	Vec4 Mix(const Vec4& a, const Vec4& b, float t) { return FromGlm(glm::mix(ToGlm(a), ToGlm(b), t)); }

	Vec3 Abs(const Vec3& v)   { return FromGlm(glm::abs(ToGlm(v))); }
	Vec3 Round(const Vec3& v) { return FromGlm(glm::round(ToGlm(v))); }
	Vec4 Round(const Vec4& v) { return FromGlm(glm::round(ToGlm(v))); }

	float MaxComponent(const Vec3& v) { return glm::compMax(ToGlm(v)); }
	float MinComponent(const Vec3& v) { return glm::compMin(ToGlm(v)); }

	// --- geometry ------------------------------------------------------------

	float Dot(const Vec2& a, const Vec2& b) { return glm::dot(ToGlm(a), ToGlm(b)); }
	float Dot(const Vec3& a, const Vec3& b) { return glm::dot(ToGlm(a), ToGlm(b)); }
	float Dot(const Vec4& a, const Vec4& b) { return glm::dot(ToGlm(a), ToGlm(b)); }

	Vec3 Cross(const Vec3& a, const Vec3& b) { return FromGlm(glm::cross(ToGlm(a), ToGlm(b))); }

	float Length(const Vec2& v) { return glm::length(ToGlm(v)); }
	float Length(const Vec3& v) { return glm::length(ToGlm(v)); }
	float Length(const Vec4& v) { return glm::length(ToGlm(v)); }
	float LengthSquared(const Vec3& v) { return glm::dot(ToGlm(v), ToGlm(v)); }

	float Distance(const Vec2& a, const Vec2& b) { return glm::distance(ToGlm(a), ToGlm(b)); }
	float Distance(const Vec3& a, const Vec3& b) { return glm::distance(ToGlm(a), ToGlm(b)); }

	// The zero guard is the one deliberate departure from glm. See the note on
	// the declaration -- glm returns NaNs here, and a NaN in a transform takes
	// the object and every child with it, silently.
	Vec2 Normalize(const Vec2& v)
	{
		const float length = Length(v);
		return length > Epsilon ? FromGlm(glm::normalize(ToGlm(v))) : Vec2{};
	}

	Vec3 Normalize(const Vec3& v)
	{
		const float length = Length(v);
		return length > Epsilon ? FromGlm(glm::normalize(ToGlm(v))) : Vec3{};
	}

	Vec4 Normalize(const Vec4& v)
	{
		const float length = Length(v);
		return length > Epsilon ? FromGlm(glm::normalize(ToGlm(v))) : Vec4{};
	}

	// --- rotations -----------------------------------------------------------

	Quat Normalize(const Quat& q) { return FromGlm(glm::normalize(ToGlm(q))); }
	Quat Conjugate(const Quat& q) { return FromGlm(glm::conjugate(ToGlm(q))); }
	Quat operator-(const Quat& q) { return FromGlm(-ToGlm(q)); }

	float Dot(const Quat& a, const Quat& b) { return glm::dot(ToGlm(a), ToGlm(b)); }

	Quat operator+(const Quat& a, const Quat& b) { return FromGlm(ToGlm(a) + ToGlm(b)); }
	Quat operator-(const Quat& a, const Quat& b) { return FromGlm(ToGlm(a) - ToGlm(b)); }
	float Length(const Quat& q) { return glm::length(ToGlm(q)); }

	Quat operator*(const Quat& a, const Quat& b) { return FromGlm(ToGlm(a) * ToGlm(b)); }
	Vec3 operator*(const Quat& q, const Vec3& v) { return FromGlm(ToGlm(q) * ToGlm(v)); }

	Quat AngleAxis(float radians, const Vec3& axis)
	{
		return FromGlm(glm::angleAxis(radians, ToGlm(axis)));
	}

	float Angle(const Quat& q) { return glm::angle(ToGlm(q)); }

	Vec3 Rotate(const Quat& q, const Vec3& v) { return FromGlm(ToGlm(q) * ToGlm(v)); }

	Quat FromEuler(const Vec3& radians) { return FromGlm(glm::quat(ToGlm(radians))); }
	Vec3 ToEuler(const Quat& q)         { return FromGlm(glm::eulerAngles(ToGlm(q))); }

	Mat4 ToMat4(const Quat& q) { return FromGlm(glm::toMat4(ToGlm(q))); }
	Quat ToQuat(const Mat4& m) { return FromGlm(glm::quat_cast(ToGlm(m))); }

	Quat Slerp(const Quat& a, const Quat& b, float t)
	{
		// The hemisphere fix, kept explicit rather than trusted to the caller.
		//
		// A quaternion and its negation are the same rotation, so two poses can
		// be numerically far apart and visually identical. Interpolating without
		// flipping one of them takes the long way round -- a bone spins most of
		// the way about its axis to arrive where it already was. This bit the
		// skeletal animation work, and it is cheaper to guarantee here than to
		// rediscover per caller.
		glm::quat from = ToGlm(a);
		const glm::quat to = ToGlm(b);
		if (glm::dot(from, to) < 0.0f)
			from = -from;

		return FromGlm(glm::slerp(from, to, t));
	}
}
