#pragma once

// The engine's vector and matrix types.
//
// RageV exposes no third-party type in a public header. glm is still what does
// the difficult arithmetic -- decomposition, inversion, quaternion
// interpolation -- but it does it inside `.cpp` files, and nothing in a header
// a game includes knows it exists.
//
// Why a real type rather than `using Vec3 = glm::vec3;`. An alias hides
// nothing: every compiler error, every IDE tooltip and every generated
// reference page still says `glm::vec3`, and swapping the library later would
// still break every game built on the engine. The whole point is that RageV
// reads as one engine rather than as an assembly of libraries.
//
// **Conversions are member-wise, never reinterpreted.** It would be tempting to
// cast a Vec3 to a glm::vec3 in place, since both are three floats -- but the
// same trick applied to a quaternion is a live trap, because glm has changed
// which end `w` lives at. Member-wise conversion costs nothing after inlining
// and cannot be wrong.
//
// Members are lower case (`x`, `y`, `z`) rather than PascalCase like the rest
// of the engine. That is deliberate and it is the one exception: `position.y`
// is what every reader of graphics code expects, and the colour aliases have to
// spell `.r`, `.g`, `.b` to be worth having.

#include <cstdint>
#include <cmath>

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4201)   // nameless struct/union, which is how the
#endif                                // colour aliases are provided

namespace RageV::Math
{
	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;

		constexpr Vec2() = default;
		constexpr explicit Vec2(float scalar) : x(scalar), y(scalar) {}
		constexpr Vec2(float x, float y) : x(x), y(y) {}
		constexpr explicit Vec2(const struct Vec3& v);
		constexpr explicit Vec2(const struct Vec4& v);

		float& operator[](int index)             { return (&x)[index]; }
		const float& operator[](int index) const { return (&x)[index]; }
	};

	struct Vec3
	{
		union { float x, r; };
		union { float y, g; };
		union { float z, b; };

		constexpr Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
		constexpr explicit Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
		constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
		constexpr Vec3(const Vec2& xy, float z) : x(xy.x), y(xy.y), z(z) {}
		constexpr explicit Vec3(const struct Vec4& v);

		float& operator[](int index)             { return (&x)[index]; }
		const float& operator[](int index) const { return (&x)[index]; }
	};

	struct Vec4
	{
		union { float x, r; };
		union { float y, g; };
		union { float z, b; };
		union { float w, a; };

		constexpr Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		constexpr explicit Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
		constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
		constexpr Vec4(const Vec3& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

		float& operator[](int index)             { return (&x)[index]; }
		const float& operator[](int index) const { return (&x)[index]; }

		constexpr Vec3 XYZ() const { return { x, y, z }; }
	};

	// Joint indices. Unsigned because they index a bone array, and signed
	// indices there have historically meant "we did not check".
	struct UVec4
	{
		uint32_t x = 0, y = 0, z = 0, w = 0;

		constexpr UVec4() = default;
		constexpr explicit UVec4(uint32_t scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
		constexpr UVec4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) : x(x), y(y), z(z), w(w) {}

		uint32_t& operator[](int index)             { return (&x)[index]; }
		const uint32_t& operator[](int index) const { return (&x)[index]; }
	};

	// Column-major, like glm and like both shading languages. Columns[c][r].
	//
	// The layout is exactly sixteen contiguous floats in the order a GPU expects,
	// which is what lets a matrix be handed to a uniform buffer without a
	// conversion step. The static_assert below is not decoration: if that ever
	// stops being true, every shader silently reads transposed nonsense.
	struct Mat3
	{
		Vec3 Columns[3];

		constexpr Mat3() : Columns{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } {}
		constexpr explicit Mat3(float diagonal)
			: Columns{ { diagonal, 0.0f, 0.0f }, { 0.0f, diagonal, 0.0f }, { 0.0f, 0.0f, diagonal } } {}
		constexpr Mat3(const Vec3& c0, const Vec3& c1, const Vec3& c2) : Columns{ c0, c1, c2 } {}

		// The upper-left 3x3: a transform with its translation dropped, which is
		// what a normal or a direction wants applied to it.
		constexpr explicit Mat3(const struct Mat4& m);

		constexpr Vec3& operator[](int column)             { return Columns[column]; }
		constexpr const Vec3& operator[](int column) const { return Columns[column]; }

		static constexpr Mat3 Identity() { return Mat3(1.0f); }
	};

	struct Mat4
	{
		Vec4 Columns[4];

		constexpr Mat4()
			: Columns{ { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
					   { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } } {}
		constexpr explicit Mat4(float diagonal)
			: Columns{ { diagonal, 0.0f, 0.0f, 0.0f }, { 0.0f, diagonal, 0.0f, 0.0f },
					   { 0.0f, 0.0f, diagonal, 0.0f }, { 0.0f, 0.0f, 0.0f, diagonal } } {}
		constexpr Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
			: Columns{ c0, c1, c2, c3 } {}

		// A 3x3 promoted: same rotation and scale, no translation. The inverse of
		// Mat3(Mat4), and what a normal matrix is stored as.
		constexpr explicit Mat4(const Mat3& m);

		constexpr Vec4& operator[](int column)             { return Columns[column]; }
		constexpr const Vec4& operator[](int column) const { return Columns[column]; }

		static constexpr Mat4 Identity() { return Mat4(1.0f); }
	};

	// Stored x, y, z, w -- and constructed (w, x, y, z), which is the convention
	// every quaternion paper uses and the one glm's constructor takes.
	//
	// The mismatch between storage order and argument order is exactly why
	// conversions in this engine are member-wise: a reinterpret_cast between
	// this and glm::quat is correct only for the glm build flags that happen to
	// be set, and silently rotates everything wrongly for the others.
	struct Quat
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;

		constexpr Quat() = default;
		constexpr Quat(float w, float x, float y, float z) : x(x), y(y), z(z), w(w) {}

		static constexpr Quat Identity() { return Quat(1.0f, 0.0f, 0.0f, 0.0f); }
	};

	constexpr Mat4::Mat4(const Mat3& m)
		: Columns{ Vec4(m[0], 0.0f), Vec4(m[1], 0.0f), Vec4(m[2], 0.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f) } {}

	constexpr Vec2::Vec2(const Vec3& v) : x(v.x), y(v.y) {}
	constexpr Vec2::Vec2(const Vec4& v) : x(v.x), y(v.y) {}
	constexpr Vec3::Vec3(const Vec4& v) : x(v.x), y(v.y), z(v.z) {}
	constexpr Mat3::Mat3(const Mat4& m)
		: Columns{ Vec3(m[0]), Vec3(m[1]), Vec3(m[2]) } {}

	static_assert(sizeof(Vec2)  ==  8, "Vec2 must be two tightly packed floats");
	static_assert(sizeof(Vec3)  == 12, "Vec3 must be three tightly packed floats");
	static_assert(sizeof(Vec4)  == 16, "Vec4 must be four tightly packed floats");
	static_assert(sizeof(UVec4) == 16, "UVec4 must be four tightly packed uints");
	static_assert(sizeof(Mat3)  == 36, "Mat3 must be nine tightly packed floats");
	static_assert(sizeof(Mat4)  == 64, "Mat4 must be sixteen tightly packed floats, or every shader reads nonsense");
	static_assert(sizeof(Quat)  == 16, "Quat must be four tightly packed floats");
}

// Everything the engine writes lives in RageV; the domain namespaces are for
// grouping, not for making callers spell out a path to a vector.
namespace RageV
{
	using Math::Vec2;
	using Math::Vec3;
	using Math::Vec4;
	using Math::UVec4;
	using Math::Mat3;
	using Math::Mat4;
	using Math::Quat;
}

#if defined(_MSC_VER)
	#pragma warning(pop)
#endif
