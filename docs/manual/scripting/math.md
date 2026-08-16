# Math

Every mathematical function the engine offers, in both languages, in one
place. **This is the complete list** — if a function is not here, RageV does
not have it and you should reach for your language's standard library
knowing you are stepping outside the engine.

| Language | Where it lives | How you write it |
|---|---|---|
| C++ | `RageV::Math`, from `RageV/Math/Math.h` | `Math::Sin(x)` |
| C# | `RageV.Mathf`, from `using RageV;` | `Mathf.Sin(x)` |

> [!NOTE]
> The C# class is `Mathf`, not `Math`, and the `f` is not decoration. A class
> called `Math` in the `RageV` namespace would be ambiguous with `System.Math`
> in every script that writes `using System;` beside `using RageV;` — the
> compiler reports that as error CS0104 on whichever line uses it, which is a
> long way from the cause. Every function name inside is the same as the
> native one.

## Why not `std::sin` and `MathF.Sin`

Because an engine that spells half its arithmetic one way and half another is
asking you to learn its history. `Math::Sin` and `Mathf.Sin` are the same
function with the same guarantees in both languages, and a check in
`scenetest` calls every one of them on both sides and requires the answers to
agree — including the awkward values, where two standard libraries genuinely
disagree by default.

The wrappers cost nothing: they are `inline` forwards in C++ and one-line
expression bodies in C#.

## Four places these deliberately differ from the standard library

These are the only four. Everything else forwards untouched.

| Function | Standard behaviour | What RageV does | Why |
|---|---|---|---|
| `Acos`, `Asin` | NaN for an argument outside [-1, 1] | Clamps first, so the answer is an angle | A dot product of two unit vectors is analytically in [-1, 1] and numerically is not. One bit past the end is a NaN angle, then a NaN rotation, then an object nobody can find. |
| `Round` | C rounds half away from zero; .NET rounds half to **even** | Half away from zero, in both languages | Otherwise `Round(0.5)` is 1 in a C++ script and 0 in a C# one, and a tile placed by rounding lands in a different square depending on which language placed it. |
| `Normalize` | NaN for a zero-length vector | Returns zero | Same reason as `Acos`: a NaN in a transform spreads to every transform derived from it, and the object and all its children vanish silently. |
| `Mod` | `fmod` takes the sign of the dividend | Takes the sign of the **divisor** | That is what wrapping an angle or a clip time means. `FMod` is the C one, and both are provided so a call site has to say which it means. |

## Constants

| Name | Value |
|---|---|
| `Pi` | 3.14159265358979323846 |
| `HalfPi` | π / 2 |
| `TwoPi` | 2π |
| `Epsilon` | 1e-6 — what the engine treats as near enough to zero |
| `Infinity` | C# only. C++ uses `std::numeric_limits<float>::infinity()`. |

## Angles

**Every angle the engine takes is in radians.** Degrees exist in the
inspector and nowhere else.

| Function | What it does |
|---|---|
| `Radians(degrees)` | Degrees to radians. Also takes a `Vec3` in C++. |
| `Degrees(radians)` | Radians to degrees, for showing a number to a person. Also takes a `Vec3` in C++. |

## Comparison and clamping

| Function | What it does |
|---|---|
| `Min(a, b)`, `Max(a, b)` | The smaller, the larger. Both arguments must be the **same type** — mixing an `int` and an unsigned is a compile error rather than a silent conversion. |
| `Min(a, b, c, …)`, `Max(a, b, c, …)` | Three or more, so a widest-of-three reads as one call. C++ takes any number; C# takes three. |
| `Clamp(v, lo, hi)` | `v`, held between the two. Works on integers as well as floats. |
| `Saturate(v)` | `Clamp(v, 0, 1)`. The shading language's name for it, and what a weight or a colour channel almost always wants. |
| `Sign(v)` | -1, 0 or +1. **Zero for zero** — not the +1 that a two-way test gives it. |
| `Abs(v)` | Absolute value. Overloaded for floats and signed integers. |

## Trigonometry

| Function | What it does |
|---|---|
| `Sin(v)`, `Cos(v)`, `Tan(v)` | The three, in radians. |
| `Asin(v)`, `Acos(v)` | Inverse sine and cosine. **Clamped** — see the table above. |
| `Atan(v)` | Inverse tangent, in (-π/2, π/2). |
| `Atan2(y, x)` | The angle of the vector (x, y), over the full circle. **y first** — it is the opposite side over the adjacent. This is the one to use for "which way is this pointing". |
| `Sinh(v)`, `Cosh(v)`, `Tanh(v)` | Hyperbolic. `Tanh` is the useful one: a soft clamp to (-1, 1). |

## Exponential and roots

| Function | What it does |
|---|---|
| `Sqrt(v)` | Square root. NaN for a negative, like the standard library. |
| `SafeSqrt(v)` | Zero for a negative instead. Use it when a rounding error could push a value you know is non-negative below zero. |
| `Pow(base, exponent)` | Base raised to the exponent. |
| `Exp(v)`, `Exp2(v)` | e^v and 2^v. `Exp` is what framerate-independent smoothing is built from — see the example below. |
| `Log(v)`, `Log2(v)`, `Log10(v)` | Natural, base 2 and base 10. |

## Rounding

| Function | What it does | `-2.5` becomes |
|---|---|---|
| `Floor(v)` | Towards negative infinity | -3 |
| `Ceil(v)` | Towards positive infinity | -2 |
| `Trunc(v)` | Towards zero | -2 |
| `Round(v)` | To nearest, **half away from zero** | -3 |
| `Fract(v)` | The part after the point, **always positive**: `Fract(-0.25)` is 0.75 | 0.5 |
| `Mod(v, d)` | Remainder with the sign of the divisor: `Mod(-1, 3)` is 2 | — |
| `FMod(v, d)` | C's remainder, sign of the dividend: `FMod(-1, 3)` is -1 | — |

## Interpolation

| Function | What it does |
|---|---|
| `Lerp(a, b, t)` | Linear blend. **`t` is not clamped** — pass 1.5 and you get an extrapolation, which is sometimes what you want and is never an accident the engine corrects for you. |
| `Mix(a, b, t)` | The same function under the name the shading languages use. |
| `SmoothStep(edge0, edge1, v)` | 0 below `edge0`, 1 above `edge1`, and the smooth Hermite curve between. Clamped at both ends. |
| `Step(edge, v)` | 0 below the edge, 1 at or above it. |

## Queries

| Function | What it does |
|---|---|
| `IsNaN(v)`, `IsInf(v)`, `IsFinite(v)` | What kind of number this is. Worth a guard around anything that came from a division. |
| `Hypot(a, b)` | The length of (a, b), i.e. √(a² + b²). |
| `CopySign(magnitude, sign)` | The magnitude of the first with the sign of the second. |

## Vectors

Vector arithmetic is on the vector types themselves, not here. In C++,
`Vec2`, `Vec3` and `Vec4` have operators plus `Math::Dot`, `Math::Cross`,
`Math::Length`, `Math::Distance`, `Math::Normalize`, and component-wise
`Min`, `Max`, `Clamp`, `Mix`, `Abs` and `Round`. In C#, `Vector3` has
`Length()`, `LengthSquared()`, `Normalized()`, and the static `Dot`, `Cross`,
`Distance` and `Lerp`.

## Two worked examples

**Framerate-independent smoothing.** A plain lerp by a constant factor
converges at a rate that depends on the step size, so a camera written that
way lags further behind whenever the frame rate dips. The exponential form
does not:

```cpp
// C++, in OnFrame
const float t = 1.0f - Math::Exp(-m_Sharpness * deltaTime);
SetPosition(Math::Mix(GetPosition(), goal, t));
```

```csharp
// C#, in OnFrame
float t = 1.0f - Mathf.Exp(-m_Sharpness * deltaTime);
Position = Vector3.Lerp(Position, goal, t);
```

**An angle that wraps.** `Mod` rather than `FMod`, because a negative angle
should come back positive:

```cpp
const float wrapped = Math::Mod(angle, Math::TwoPi);   // always in [0, 2π)
```

```csharp
float wrapped = Mathf.Mod(angle, Mathf.TwoPi);         // always in [0, 2π)
```
