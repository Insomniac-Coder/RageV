#include <rvpch.h>
#include "Math.h"

// RageV's math, implemented rather than delegated.
//
// This file knows nothing about glm, and the engine no longer links it.
//
// **What makes that defensible is the test, not the code.** `scenetest` still
// links glm, purely as an oracle: every operation below is checked against it,
// operation by operation, on deliberately awkward values -- axis-aligned test
// numbers pass through a transposed matrix unharmed, which is exactly how that
// class of bug survives a suite. Delete that comparison and this file becomes
// unverified arithmetic in the hottest path of a renderer.
//
// Conventions, fixed here now that nothing else defines them:
//
//   - Column-major storage; `m * v` transforms v, `a * b` applies b first.
//   - **Right-handed**, looking down -Z.
//   - **Clip-space depth in [0, 1]**, not [-1, 1]. Vulkan's convention, which
//     the OpenGL backend compensates for once at the swapchain rather than the
//     engine keeping two. The projections below are the RH_ZO forms and differ
//     from the textbook [-1, 1] ones in the third column.
//
// Getting any of those wrong gives a picture that is subtly and consistently
// wrong rather than obviously broken, which is why they are written down.

namespace RageV::Math
{
	// --- scalars -------------------------------------------------------------

	Vec3  Round(const Vec3& v) { return { Round(v.x), Round(v.y), Round(v.z) }; }
	Vec4  Round(const Vec4& v) { return { Round(v.x), Round(v.y), Round(v.z), Round(v.w) }; }

	float Mod(float value, float divisor)
	{
		// The remainder with the sign of the *divisor*. std::fmod takes the sign
		// of the dividend and hands back a negative angle for a negative input --
		// correct, and not what wrapping an angle means.
		return value - divisor * Floor(value / divisor);
	}

	// --- matrices ------------------------------------------------------------

	Mat4 Transpose(const Mat4& m)
	{
		return {
			{ m[0].x, m[1].x, m[2].x, m[3].x },
			{ m[0].y, m[1].y, m[2].y, m[3].y },
			{ m[0].z, m[1].z, m[2].z, m[3].z },
			{ m[0].w, m[1].w, m[2].w, m[3].w },
		};
	}

	Mat3 Transpose(const Mat3& m)
	{
		return {
			{ m[0].x, m[1].x, m[2].x },
			{ m[0].y, m[1].y, m[2].y },
			{ m[0].z, m[1].z, m[2].z },
		};
	}

	Mat3 Inverse(const Mat3& m)
	{
		// Adjugate over determinant, small enough to write directly.
		const float determinant =
			+ m[0].x * (m[1].y * m[2].z - m[2].y * m[1].z)
			- m[1].x * (m[0].y * m[2].z - m[2].y * m[0].z)
			+ m[2].x * (m[0].y * m[1].z - m[1].y * m[0].z);

		const float inverse = 1.0f / determinant;

		Mat3 result;
		result[0].x = + (m[1].y * m[2].z - m[2].y * m[1].z) * inverse;
		result[1].x = - (m[1].x * m[2].z - m[2].x * m[1].z) * inverse;
		result[2].x = + (m[1].x * m[2].y - m[2].x * m[1].y) * inverse;
		result[0].y = - (m[0].y * m[2].z - m[2].y * m[0].z) * inverse;
		result[1].y = + (m[0].x * m[2].z - m[2].x * m[0].z) * inverse;
		result[2].y = - (m[0].x * m[2].y - m[2].x * m[0].y) * inverse;
		result[0].z = + (m[0].y * m[1].z - m[1].y * m[0].z) * inverse;
		result[1].z = - (m[0].x * m[1].z - m[1].x * m[0].z) * inverse;
		result[2].z = + (m[0].x * m[1].y - m[1].x * m[0].y) * inverse;
		return result;
	}

	Mat4 Inverse(const Mat4& m)
	{
		// The 2x2 sub-determinant factorisation. Expanded naively, a 4x4 inverse
		// recomputes the same eighteen 2x2 minors over and over; naming them once
		// is both faster and far easier to check against a reference.
		const float c00 = m[2].z * m[3].w - m[3].z * m[2].w;
		const float c02 = m[1].z * m[3].w - m[3].z * m[1].w;
		const float c03 = m[1].z * m[2].w - m[2].z * m[1].w;

		const float c04 = m[2].y * m[3].w - m[3].y * m[2].w;
		const float c06 = m[1].y * m[3].w - m[3].y * m[1].w;
		const float c07 = m[1].y * m[2].w - m[2].y * m[1].w;

		const float c08 = m[2].y * m[3].z - m[3].y * m[2].z;
		const float c10 = m[1].y * m[3].z - m[3].y * m[1].z;
		const float c11 = m[1].y * m[2].z - m[2].y * m[1].z;

		const float c12 = m[2].x * m[3].w - m[3].x * m[2].w;
		const float c14 = m[1].x * m[3].w - m[3].x * m[1].w;
		const float c15 = m[1].x * m[2].w - m[2].x * m[1].w;

		const float c16 = m[2].x * m[3].z - m[3].x * m[2].z;
		const float c18 = m[1].x * m[3].z - m[3].x * m[1].z;
		const float c19 = m[1].x * m[2].z - m[2].x * m[1].z;

		const float c20 = m[2].x * m[3].y - m[3].x * m[2].y;
		const float c22 = m[1].x * m[3].y - m[3].x * m[1].y;
		const float c23 = m[1].x * m[2].y - m[2].x * m[1].y;

		const Vec4 f0(c00, c00, c02, c03);
		const Vec4 f1(c04, c04, c06, c07);
		const Vec4 f2(c08, c08, c10, c11);
		const Vec4 f3(c12, c12, c14, c15);
		const Vec4 f4(c16, c16, c18, c19);
		const Vec4 f5(c20, c20, c22, c23);

		const Vec4 v0(m[1].x, m[0].x, m[0].x, m[0].x);
		const Vec4 v1(m[1].y, m[0].y, m[0].y, m[0].y);
		const Vec4 v2(m[1].z, m[0].z, m[0].z, m[0].z);
		const Vec4 v3(m[1].w, m[0].w, m[0].w, m[0].w);

		const Vec4 inv0 = v1 * f0 - v2 * f1 + v3 * f2;
		const Vec4 inv1 = v0 * f0 - v2 * f3 + v3 * f4;
		const Vec4 inv2 = v0 * f1 - v1 * f3 + v3 * f5;
		const Vec4 inv3 = v0 * f2 - v1 * f4 + v2 * f5;

		// The alternating signs of the cofactor expansion.
		const Vec4 signA(+1.0f, -1.0f, +1.0f, -1.0f);
		const Vec4 signB(-1.0f, +1.0f, -1.0f, +1.0f);

		const Mat4 adjugate(inv0 * signA, inv1 * signB, inv2 * signA, inv3 * signB);

		const Vec4 row0(adjugate[0].x, adjugate[1].x, adjugate[2].x, adjugate[3].x);
		const float determinant = Dot(m[0], row0);

		return adjugate * (1.0f / determinant);
	}

	Mat4 Translate(const Mat4& m, const Vec3& delta)
	{
		Mat4 result = m;
		result[3] = m[0] * delta.x + m[1] * delta.y + m[2] * delta.z + m[3];
		return result;
	}

	Mat4 Scale(const Mat4& m, const Vec3& factor)
	{
		return { m[0] * factor.x, m[1] * factor.y, m[2] * factor.z, m[3] };
	}

	Mat4 Rotate(const Mat4& m, float radians, const Vec3& axis)
	{
		// Rodrigues' rotation, composed onto m. The axis is normalised here
		// rather than demanded of the caller: every call site in this engine
		// passes either a literal like {0,1,0} or a direction it has not
		// renormalised, and an unnormalised axis silently scales as well as
		// rotates.
		const float c = Cos(radians);
		const float s = Sin(radians);
		const Vec3 a = Normalize(axis);
		const Vec3 t = a * (1.0f - c);

		Mat3 r;
		r[0] = { c + t.x * a.x,       t.x * a.y + s * a.z, t.x * a.z - s * a.y };
		r[1] = { t.y * a.x - s * a.z, c + t.y * a.y,       t.y * a.z + s * a.x };
		r[2] = { t.z * a.x + s * a.y, t.z * a.y - s * a.x, c + t.z * a.z };

		return {
			m[0] * r[0].x + m[1] * r[0].y + m[2] * r[0].z,
			m[0] * r[1].x + m[1] * r[1].y + m[2] * r[1].z,
			m[0] * r[2].x + m[1] * r[2].y + m[2] * r[2].z,
			m[3],
		};
	}

	Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
	{
		// Right-handed, depth in [0, 1], **reverse-Z**: the near plane maps to
		// 1 and the far plane to 0. RHITypes.h says why at length; the short
		// version is that a float buffer is dense near zero and 1/z is sparse
		// far away, so the conventional arrangement stacks both errors at the
		// far end. The [-1, 1] form in most textbooks differs in [2].z and
		// [3].z, and against a Vulkan-style clip volume it throws away half
		// the depth range while still producing a picture.
		//
		// The reversal is exactly the conventional matrix with near and far
		// exchanged, which is worth seeing rather than deriving: at -near the
		// quotient is near*(far-near) / ((far-near)*near) = 1, and at -far the
		// numerator cancels to 0.
		const float tanHalfFov = Tan(verticalFovRadians * 0.5f);

		Mat4 result(0.0f);
		result[0].x = 1.0f / (aspect * tanHalfFov);
		result[1].y = 1.0f / tanHalfFov;
		result[2].z = nearPlane / (farPlane - nearPlane);
		result[2].w = -1.0f;
		result[3].z = (farPlane * nearPlane) / (farPlane - nearPlane);
		return result;
	}

	Mat4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
	{
		// **Reverse-Z as well**, so the engine has one depth convention rather
		// than one per projection. An orthographic depth is linear and gains
		// no precision from the flip -- but a directional shadow map is drawn
		// with this matrix and tested against the same GreaterOrEqual as
		// everything else, and a matrix that disagreed with the test would
		// simply reject every fragment.
		Mat4 result(1.0f);
		result[0].x = 2.0f / (right - left);
		result[1].y = 2.0f / (top - bottom);
		result[2].z = 1.0f / (farPlane - nearPlane);
		result[3].x = -(right + left) / (right - left);
		result[3].y = -(top + bottom) / (top - bottom);
		result[3].z = farPlane / (farPlane - nearPlane);
		return result;
	}

	Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
	{
		const Vec3 f = Normalize(target - eye);
		const Vec3 s = Normalize(Cross(f, up));
		const Vec3 u = Cross(s, f);

		Mat4 result(1.0f);
		result[0].x = s.x;  result[1].x = s.y;  result[2].x = s.z;
		result[0].y = u.x;  result[1].y = u.y;  result[2].y = u.z;
		result[0].z = -f.x; result[1].z = -f.y; result[2].z = -f.z;
		result[3].x = -Dot(s, eye);
		result[3].y = -Dot(u, eye);
		result[3].z =  Dot(f, eye);
		return result;
	}

	bool Decompose(const Mat4& transform, Vec3& translation, Quat& rotation, Vec3& scale)
	{
		Mat4 m = transform;

		// A projection, or anything else carrying a perspective row, has no
		// translation-rotation-scale reading. Refuse rather than return
		// something plausible.
		if (Abs(m[3].w) < Epsilon)
			return false;

		// Normalise out the homogeneous scale, so a matrix that has been through
		// a divide still decomposes.
		if (Abs(m[3].w - 1.0f) > Epsilon)
		{
			const float inverse = 1.0f / m[3].w;
			for (int c = 0; c < 4; c++)
				m[c] = m[c] * inverse;
		}

		translation = Vec3(m[3]);

		Vec3 columns[3] = { Vec3(m[0]), Vec3(m[1]), Vec3(m[2]) };

		scale.x = Length(columns[0]);
		scale.y = Length(columns[1]);
		scale.z = Length(columns[2]);

		// Degenerate: a zero column carries no rotation to recover.
		if (scale.x < Epsilon || scale.y < Epsilon || scale.z < Epsilon)
			return false;

		columns[0] = columns[0] / scale.x;
		columns[1] = columns[1] / scale.y;
		columns[2] = columns[2] / scale.z;

		// A negative determinant means the transform includes a mirror, which no
		// rotation can express. Fold the flip into the scale so that recomposing
		// reproduces the original -- which is what the test actually checks.
		if (Dot(Cross(columns[0], columns[1]), columns[2]) < 0.0f)
		{
			scale.x = -scale.x;
			columns[0] = -columns[0];
		}

		rotation = ToQuat(Mat4(Mat3(columns[0], columns[1], columns[2])));
		return true;
	}

	// --- rotations -----------------------------------------------------------

	float Dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
	float Length(const Quat& q)             { return Sqrt(Dot(q, q)); }

	Quat Normalize(const Quat& q)
	{
		const float length = Length(q);
		if (length < Epsilon)
			return Quat::Identity();

		const float inverse = 1.0f / length;
		return { q.w * inverse, q.x * inverse, q.y * inverse, q.z * inverse };
	}

	Quat Conjugate(const Quat& q) { return { q.w, -q.x, -q.y, -q.z }; }
	Quat operator-(const Quat& q) { return { -q.w, -q.x, -q.y, -q.z }; }

	Quat operator+(const Quat& a, const Quat& b) { return { a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z }; }
	Quat operator-(const Quat& a, const Quat& b) { return { a.w - b.w, a.x - b.x, a.y - b.y, a.z - b.z }; }

	Quat operator*(const Quat& a, const Quat& b)
	{
		// The Hamilton product. Not commutative: `a * b` is b's rotation
		// followed by a's, which is the same order the matrices compose in.
		return {
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
			a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
		};
	}

	Vec3 operator*(const Quat& q, const Vec3& v)
	{
		// v + 2 * (w * cross(qv, v) + cross(qv, cross(qv, v))). Two cross
		// products rather than building a matrix, which is cheaper for a single
		// vector and is what makes GetForward() free.
		const Vec3 qv{ q.x, q.y, q.z };
		const Vec3 uv = Cross(qv, v);
		const Vec3 uuv = Cross(qv, uv);
		return v + (uv * q.w + uuv) * 2.0f;
	}

	Vec3 Rotate(const Quat& q, const Vec3& v) { return q * v; }

	Quat AngleAxis(float radians, const Vec3& axis)
	{
		const float half = radians * 0.5f;
		const float s = Sin(half);
		return { Cos(half), axis.x * s, axis.y * s, axis.z * s };
	}

	float Angle(const Quat& q)
	{
		// Near the identity, acos(w) loses most of its precision because the
		// cosine is flat there; atan2 against the vector part does not. The
		// threshold is cos(0.5), where the two are equally good.
		if (Abs(q.w) > 0.877582561890372716f)
		{
			const float sine = Sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
			return 2.0f * Atan2(sine, Abs(q.w));
		}
		return 2.0f * Acos(Clamp(q.w, -1.0f, 1.0f));
	}

	Quat FromEuler(const Vec3& radians)
	{
		const Vec3 half = radians * 0.5f;
		const float cx = Cos(half.x), sx = Sin(half.x);
		const float cy = Cos(half.y), sy = Sin(half.y);
		const float cz = Cos(half.z), sz = Sin(half.z);

		return {
			cx * cy * cz + sx * sy * sz,
			sx * cy * cz - cx * sy * sz,
			cx * sy * cz + sx * cy * sz,
			cx * cy * sz - sx * sy * cz,
		};
	}

	Vec3 ToEuler(const Quat& q)
	{
		const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, ww = q.w * q.w;

		// The middle angle is clamped before asin. A quaternion a hair outside
		// unit length -- which any accumulated rotation eventually is -- would
		// otherwise produce a NaN at exactly the poles, and a NaN in a rotation
		// takes the object and every child with it.
		const float pitch = Atan2(2.0f * (q.y * q.z + q.w * q.x), ww - xx - yy + zz);
		const float yaw   = Asin(Clamp(-2.0f * (q.x * q.z - q.w * q.y), -1.0f, 1.0f));
		const float roll  = Atan2(2.0f * (q.x * q.y + q.w * q.z), ww + xx - yy - zz);
		return { pitch, yaw, roll };
	}

	Mat4 ToMat4(const Quat& q)
	{
		const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
		const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

		Mat4 result(1.0f);
		result[0].x = 1.0f - 2.0f * (yy + zz);
		result[0].y =        2.0f * (xy + wz);
		result[0].z =        2.0f * (xz - wy);

		result[1].x =        2.0f * (xy - wz);
		result[1].y = 1.0f - 2.0f * (xx + zz);
		result[1].z =        2.0f * (yz + wx);

		result[2].x =        2.0f * (xz + wy);
		result[2].y =        2.0f * (yz - wx);
		result[2].z = 1.0f - 2.0f * (xx + yy);
		return result;
	}

	Quat ToQuat(const Mat4& m)
	{
		// Shepperd's method. The direct formula divides by w, which vanishes at
		// a 180-degree rotation and loses precision well before that; taking
		// whichever of the four components is largest keeps the divisor away
		// from zero for every possible input.
		const float fourXSquaredMinus1 = m[0].x - m[1].y - m[2].z;
		const float fourYSquaredMinus1 = m[1].y - m[0].x - m[2].z;
		const float fourZSquaredMinus1 = m[2].z - m[0].x - m[1].y;
		const float fourWSquaredMinus1 = m[0].x + m[1].y + m[2].z;

		int biggestIndex = 0;
		float biggestSquaredMinus1 = fourWSquaredMinus1;
		if (fourXSquaredMinus1 > biggestSquaredMinus1) { biggestSquaredMinus1 = fourXSquaredMinus1; biggestIndex = 1; }
		if (fourYSquaredMinus1 > biggestSquaredMinus1) { biggestSquaredMinus1 = fourYSquaredMinus1; biggestIndex = 2; }
		if (fourZSquaredMinus1 > biggestSquaredMinus1) { biggestSquaredMinus1 = fourZSquaredMinus1; biggestIndex = 3; }

		const float biggest = Sqrt(biggestSquaredMinus1 + 1.0f) * 0.5f;
		const float mult = 0.25f / biggest;

		switch (biggestIndex)
		{
			case 0:  return { biggest,
							  (m[1].z - m[2].y) * mult,
							  (m[2].x - m[0].z) * mult,
							  (m[0].y - m[1].x) * mult };
			case 1:  return { (m[1].z - m[2].y) * mult,
							  biggest,
							  (m[0].y + m[1].x) * mult,
							  (m[2].x + m[0].z) * mult };
			case 2:  return { (m[2].x - m[0].z) * mult,
							  (m[0].y + m[1].x) * mult,
							  biggest,
							  (m[1].z + m[2].y) * mult };
			default: return { (m[0].y - m[1].x) * mult,
							  (m[2].x + m[0].z) * mult,
							  (m[1].z + m[2].y) * mult,
							  biggest };
		}
	}

	Quat Slerp(const Quat& a, const Quat& b, float t)
	{
		Quat from = a;
		float cosTheta = Dot(a, b);

		// The hemisphere fix. A quaternion and its negation are the same
		// rotation, so two poses can be numerically far apart and visually
		// identical; interpolating without flipping one of them takes the long
		// way round, and a bone spins most of the way about its axis to arrive
		// where it already was. This bit the skeletal animation work.
		if (cosTheta < 0.0f)
		{
			from = -a;
			cosTheta = -cosTheta;
		}

		// Nearly parallel: sin(theta) underflows and the divisions below blow
		// up. Straight interpolation is within float precision of the arc here.
		if (cosTheta > 1.0f - Epsilon)
		{
			return {
				Mix(from.w, b.w, t),
				Mix(from.x, b.x, t),
				Mix(from.y, b.y, t),
				Mix(from.z, b.z, t),
			};
		}

		const float angle = Acos(Clamp(cosTheta, -1.0f, 1.0f));
		const float sinAngle = Sin(angle);
		const float scaleFrom = Sin((1.0f - t) * angle) / sinAngle;
		const float scaleTo = Sin(t * angle) / sinAngle;

		return {
			scaleFrom * from.w + scaleTo * b.w,
			scaleFrom * from.x + scaleTo * b.x,
			scaleFrom * from.y + scaleTo * b.y,
			scaleFrom * from.z + scaleTo * b.z,
		};
	}
}
