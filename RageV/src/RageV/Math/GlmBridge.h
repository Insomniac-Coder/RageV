#pragma once

// Conversion between the engine's math types and glm's.
//
// **Internal.** This header may be included from a `.cpp`, never from a header
// that a game or the editor includes -- including it from a public header would
// put glm back in the public API and undo the entire point of RageV/Math.
//
// Every conversion is member-wise rather than a reinterpret_cast. The types are
// laid out identically today and casting would work today, but quaternions are
// a standing trap: glm has shipped both `w`-first and `w`-last storage
// depending on build flags and version, so a cast that is correct in one
// configuration silently rotates everything wrongly in another. Member-wise
// conversion compiles to the same thing after inlining and cannot be wrong.

#include "Types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RageV::Math
{
	inline glm::vec2 ToGlm(const Vec2& v) { return { v.x, v.y }; }
	inline glm::vec3 ToGlm(const Vec3& v) { return { v.x, v.y, v.z }; }
	inline glm::vec4 ToGlm(const Vec4& v) { return { v.x, v.y, v.z, v.w }; }
	inline glm::uvec4 ToGlm(const UVec4& v) { return { v.x, v.y, v.z, v.w }; }
	inline glm::quat ToGlm(const Quat& q) { return { q.w, q.x, q.y, q.z }; }

	inline glm::mat3 ToGlm(const Mat3& m)
	{
		return { ToGlm(m[0]), ToGlm(m[1]), ToGlm(m[2]) };
	}

	inline glm::mat4 ToGlm(const Mat4& m)
	{
		return { ToGlm(m[0]), ToGlm(m[1]), ToGlm(m[2]), ToGlm(m[3]) };
	}

	inline Vec2 FromGlm(const glm::vec2& v) { return { v.x, v.y }; }
	inline Vec3 FromGlm(const glm::vec3& v) { return { v.x, v.y, v.z }; }
	inline Vec4 FromGlm(const glm::vec4& v) { return { v.x, v.y, v.z, v.w }; }
	inline UVec4 FromGlm(const glm::uvec4& v) { return { v.x, v.y, v.z, v.w }; }
	inline Quat FromGlm(const glm::quat& q) { return { q.w, q.x, q.y, q.z }; }

	inline Mat3 FromGlm(const glm::mat3& m)
	{
		return { FromGlm(m[0]), FromGlm(m[1]), FromGlm(m[2]) };
	}

	inline Mat4 FromGlm(const glm::mat4& m)
	{
		return { FromGlm(m[0]), FromGlm(m[1]), FromGlm(m[2]), FromGlm(m[3]) };
	}
}
