#pragma once

// Conversion between RageV's math types and glm's.
//
// **Test infrastructure.** The engine does not link glm and nothing in it
// includes this file; it lives here, next to scenetest, because its only
// purpose is letting the suite check RageV::Math against a reference
// implementation operation by operation.
//
// That comparison is the entire justification for RageV owning its own maths.
// If this file or the block that uses it ever goes away, the engine is running
// on unverified arithmetic in the hottest path of a renderer.
//
// Conversions are member-wise rather than reinterpreted. The layouts match and
// casting would work, but glm has shipped quaternions both w-first and w-last
// depending on version and build flags, and a cast that is right in one
// configuration rotates everything wrongly in another.

#include "RageV/Math/Types.h"

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
