"""RT-24 (2026-09-25, owner: "put it into RT-24"): TAA's same-surface depth test in NRD's form --
last frame's depth predicted through last frame's camera, compared along the surface's own slope,
with NRD's tolerance of 1% of the frustum's size at that depth. --taa-plane-test=off is the old
2% test. The five on/off lanes of the push block are packed into one to make room (128 bytes)."""
import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io

ROOT = (_RAGEV + '/')


def edit(path, a, b, count=1):
    p = ROOT + path
    t = io.open(p, encoding='utf-8', newline='').read()
    crlf = t.count('\r\n') > t.count('\n') // 2
    t = t.replace('\r\n', '\n')
    assert t.count(a) == count, (path, t.count(a), a[:90])
    t = t.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace('\n', '\r\n') if crlf else t)


S = 'RageVEditor/assets/shaders/taa_resolve.rvshader'
# -- the push block: the reprojection, the packed lanes, the tolerance --------------------------
edit(S, """	float FlipY;
	// This frame's jitter in clip units (the scene block's Jitter.xy).
	vec2  Jitter;""", """	float FlipY;
	// **RT-24 (2026-09-25): last frame's camera, for NRD's same-surface test.** This frame's
	// rasterised NDC and depth to last frame's rasterised clip: both view-projections as they
	// were drawn, jitter included, so the depth last frame should hold for this point is
	// predicted through last frame's camera instead of assumed unchanged. See PlaneTolerance.
	mat4  Reproject;
	// This frame's jitter in clip units (the scene block's Jitter.xy).
	vec2  Jitter;""")
edit(S, """	float StillFeedback;
	// RT-6: one when the identity lanes are bound and hold a real frame, so
	// the geometric test runs; zero on the first frame, after a resize, and
	// under --taa-geometry=off, which is the reference arm.
	float Geometry;
	// RT-6.2: one when the material lane is bound.
	float Material;
	// RT-6.8: one when the colour box may be built from this surface's
	// taps alone. Zero is the reference arm (--taa-box-geometry=off) and
	// is exactly the behaviour before RT-6.8.
	float BoxGeometry;
	// **RT-5: the frame number**, for the draw that rounds this pass's running
	// averages onto the half grid (include/half_float.glsl). The jitter above
	// repeats every eight frames, and a rounding that repeats with it would
	// leave each pixel a fixed pattern of its own eight draws.
	float Frame;
	// **Measured change (docs/RT-MEASURED-CHANGE.md): one when binding 11 holds
	// the direct light's change map.** Zero is the resolve as it was.
	float Change;
	// **RT-22 (2026-09-23): one when binding 13 holds the transparent pass's
	// revealage**, so the resolve knows which pixels show a see-through surface.
	float Covered;
} u_Params;""", """	float StillFeedback;
	// **Which optional inputs are live, one bit each** (packed 2026-09-25 to make room for
	// Reproject in the 128 bytes a push block is guaranteed; see the kInput constants below):
	//   RT-6 the identity lanes are bound and hold a real frame, so the geometric test runs
	//        (off on the first frame, after a resize, and under --taa-geometry=off);
	//   RT-6.2 the material lane is bound;
	//   RT-6.8 the colour box may be built from this surface's taps alone (off is the
	//        reference arm, --taa-box-geometry=off);
	//   measured change: binding 11 holds the direct light's change map;
	//   RT-22: binding 13 holds the transparent pass's revealage.
	float Inputs;
	// **RT-5: the frame number**, for the draw that rounds this pass's running
	// averages onto the half grid (include/half_float.glsl). The jitter above
	// repeats every eight frames, and a rounding that repeats with it would
	// leave each pixel a fixed pattern of its own eight draws.
	float Frame;
	// **RT-24: minus the depth at infinity.** Reverse-Z with a finite far plane stores
	// B / z - C; this is C, so a tolerance of t times the distance along the ray is
	// t * (depth + DepthFloor) in depth units.
	float DepthFloor;
	// **RT-24: the frustum's smaller extent per unit of depth** (2 / the projection's larger
	// scale), for NRD's threshold below. Zero keeps the 2% test (--taa-plane-test=off, an
	// orthographic camera, no camera record).
	float FrustumExtent;
} u_Params;

const uint kInputGeometry    = 1u;
const uint kInputMaterial    = 2u;
const uint kInputBoxGeometry = 4u;
const uint kInputChange      = 8u;
const uint kInputCovered     = 16u;

bool HasInput(uint bit)
{
	return (uint(u_Params.Inputs + 0.5) & bit) != 0u;
}""")
for a, b in [('u_Params.Geometry > 0.5', 'HasInput(kInputGeometry)'),
             ('u_Params.BoxGeometry > 0.5', 'HasInput(kInputBoxGeometry)'),
             ('u_Params.Change > 0.5', 'HasInput(kInputChange)'),
             ('u_Params.Covered > 0.5', 'HasInput(kInputCovered)'),
             ('u_Params.Material > 0.5', 'HasInput(kInputMaterial)')]:
    p = ROOT + S
    t = io.open(p, encoding='utf-8', newline='').read()
    n = t.count(a)
    assert n >= 1, a
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace(a, b))

# -- the prediction and the test ---------------------------------------------------------------
edit(S, """// **The reprojected texel, or the nearest neighbour that is this surface,
// or nothing.**""", """// **RT-24 (2026-09-25): NRD's same-surface test, in place of the 2% one for last frame.**
//
// The 2% test compared this frame's depth with last frame's at the texel the history was
// fetched from, as a fraction of the depth. Two things made it refuse good history, and a
// refused pixel restarts from this frame's raw light -- the fast swing's speckle, measured
// (RT2-0's attribution arms: the test off took the swing's speckle from 513 dots to 396, and
// the ghosting up; at 1% the speckle rose to 665):
//   - the camera's own motion moves every depth, and the test assumed it did not;
//   - the lookup is only good to about a texel, and on a surface seen at a slant one texel is
//     a large step in depth, so a texel's error read as a different surface.
// NRD's form (ReBLUR's disocclusion test, its documented default threshold): predict where
// and at what depth this point was last frame through last frame's camera, carry that along
// the surface's own slope to the texel actually fetched, and allow 1% of the frustum's size
// at that depth. A surface facing the camera gets a tighter test than 2% (about 1.2% at a
// 60-degree field of view); a slanted one no longer refuses its own neighbours.
// NRD's documented default disocclusion threshold: 1% of the frustum's size at the depth.
const float kNrdDisocclusion = 0.01;
vec2  g_PredictedUv    = vec2(0.0);
float g_PredictedDepth = 0.0;
bool  g_Predicted      = false;

vec2 UvToNdc(vec2 uv)
{
	return vec2(uv.x * 2.0 - 1.0, u_Params.FlipY > 0.5 ? 1.0 - 2.0 * uv.y : uv.y * 2.0 - 1.0);
}

vec2 NdcToUv(vec2 ndc)
{
	return vec2(ndc.x * 0.5 + 0.5, u_Params.FlipY > 0.5 ? 0.5 - 0.5 * ndc.y : ndc.y * 0.5 + 0.5);
}

void PredictPrevious(vec2 uv, float depth)
{
	g_Predicted = false;
	if (!(u_Params.FrustumExtent > 0.0) || !(depth > 0.0))
		return;
	const vec4 then = u_Params.Reproject * vec4(UvToNdc(uv), depth, 1.0);
	if (!(then.w > 0.0))
		return;
	g_PredictedDepth = then.z / then.w;
	g_PredictedUv = NdcToUv(then.xy / then.w);
	g_Predicted = true;
}

// Of the two one-sided differences, the smaller: at an edge one side is the other surface,
// and the step across it is not this surface's slope.
float OneSidedSlope(float before, float here, float after)
{
	const float back = here - before;
	const float ahead = after - here;
	return abs(back) < abs(ahead) ? back : ahead;
}

// Whether last frame's texel `at`, holding `wasDepth`, is where this point's surface was.
bool DepthMatchesPrevious(float nowDepth, float wasDepth, ivec2 at)
{
	if (!g_Predicted)
		return abs(nowDepth - wasDepth) <= kDepthTolerance * max(nowDepth, 1.0e-4);
	const ivec2 size = textureSize(u_GuidePrevious, 0);
	const ivec2 hi = size - 1;
	const float slopeX = OneSidedSlope(texelFetch(u_GuidePrevious, clamp(at - ivec2(1, 0), ivec2(0), hi), 0).x,
									   wasDepth,
									   texelFetch(u_GuidePrevious, clamp(at + ivec2(1, 0), ivec2(0), hi), 0).x);
	const float slopeY = OneSidedSlope(texelFetch(u_GuidePrevious, clamp(at - ivec2(0, 1), ivec2(0), hi), 0).x,
									   wasDepth,
									   texelFetch(u_GuidePrevious, clamp(at + ivec2(0, 1), ivec2(0), hi), 0).x);
	// From where the point was predicted to be to the centre of the texel fetched, in texels.
	const vec2 step = (vec2(at) + 0.5) - g_PredictedUv * vec2(size);
	const float expected = g_PredictedDepth + slopeX * step.x + slopeY * step.y;
	// NRD's allowance: 1% of the frustum's extent at this depth, as distance from the surface's
	// plane -- which along the view ray is that divided by the facing, cos(theta). The facing
	// comes from the surface's own slope on screen: a texel spans extent / texels of angle, and
	// the depth steps by tan(theta) of that per texel (reverse-Z depth + floor is 1 / distance
	// up to a constant, so its relative step is the distance's).
	const float scale = max(wasDepth + u_Params.DepthFloor, 1.0e-8);
	const float relativeSlope = length(vec2(slopeX, slopeY)) / scale;
	const float facing = kNrdDisocclusion * u_Params.FrustumExtent;
	const float slant = kNrdDisocclusion * relativeSlope * float(min(size.x, size.y));
	return abs(wasDepth - expected) <= sqrt(facing * facing + slant * slant)
									   * max(expected + u_Params.DepthFloor, 0.0);
}

// Matches(), for a texel of last frame's: the same clauses, with the depth clause above.
bool MatchesPrevious(vec4 now, vec4 was, ivec2 at, bool centre)
{
	if (!g_Predicted)
		return Matches(now, was, centre);
	const bool nowSky = now.x >= 1.0;
	const bool wasSky = was.x >= 1.0;
	if (nowSky && wasSky)
		return true;
	if (nowSky != wasSky)
	{
		if (centre) g_Refusal = kSkyCrossing;
		return false;
	}
	if (abs(now.w - was.w) > 0.5)
	{
		if (centre) g_Refusal = kObjectId;
		return false;
	}
	if (!DepthMatchesPrevious(now.x, was.x, at))
	{
		if (centre) g_Refusal = kDepth;
		return false;
	}
	if (dot(DecodeOct(now.yz), DecodeOct(was.yz)) < kNormalTolerance)
	{
		if (centre) g_Refusal = kNormal;
		return false;
	}
	return true;
}

// **The reprojected texel, or the nearest neighbour that is this surface,
// or nothing.**""")
edit(S, """	const vec4 now = texelFetch(u_GuideCurrent, nowTexel, 0);
""", """	const vec4 now = texelFetch(u_GuideCurrent, nowTexel, 0);
	PredictPrevious((vec2(nowTexel) + 0.5) / vec2(size), now.x);
""")
edit(S, """		if (Matches(now, texelFetch(u_GuidePrevious, at, 0), k == 0))""",
     """		if (MatchesPrevious(now, texelFetch(u_GuidePrevious, at, 0), at, k == 0))""")

# -- C++: the push block, the reprojection, the flag ---------------------------------------------
P = 'RageV/src/RageV/Renderer/PostProcess.cpp'
edit(P, """		struct TemporalParams
		{
			PostParams Base;
			Vec2 Jitter{ 0.0f };
			// The feedback for a pixel that did not move; zero for "the same".
			float StillFeedback = 0.0f;
			// RT-6: whether the geometric test may run this frame.
			float Geometry = 0.0f;
			// RT-6.2: whether the material lane is bound.
			float Material = 0.0f;
			// RT-6.8: whether the box may be built from this surface alone.
			float BoxGeometry = 0.0f;
			// RT-5: the frame number the resolve draws its half-grid rounding
			// from -- tonemap's grain takes the same count for the same reason.
			float Frame = 0.0f;
			// Measured change (the anti-lag): one where binding 11 holds the
			// change map.
			float Change = 0.0f;
			// RT-22: one where binding 13 holds the transparent pass's revealage.
			float Covered = 0.0f;
		};
		TemporalParams full;
		full.Change = (change || reflectionChange) ? 1.0f : 0.0f;
		full.Covered = revealage ? 1.0f : 0.0f;
		full.Jitter = jitter;
		full.StillFeedback = Math::Clamp(stillFeedback, 0.0f, 0.98f);
		// Both lanes, and a history to compare against: with either missing the
		// test would be comparing this frame to uninitialised memory, which
		// refuses every pixel and turns the resolve off without saying so.
		full.Geometry = (guideCurrent && guidePrevious && hasHistory) ? 1.0f : 0.0f;
		full.Material = material ? 1.0f : 0.0f;
		full.BoxGeometry = boxGeometry ? 1.0f : 0.0f;
		full.Frame = (float)(Renderer::GetFrameCount() & 0xFFFFFFu);""",
     """		// **RT-24 (2026-09-25): laid out for the 128 bytes a push block is guaranteed.** The
		// matrix is 16-byte aligned in the shader's block, so it starts at 32 behind two floats
		// of padding; the five on/off lanes became one bit field to make room for it.
		struct TemporalParams
		{
			PostParams Base;
			float Pad0 = 0.0f;
			float Pad1 = 0.0f;
			// RT-24: this frame's rasterised NDC and depth to last frame's rasterised clip.
			Mat4 Reproject{ 1.0f };
			Vec2 Jitter{ 0.0f };
			// The feedback for a pixel that did not move; zero for "the same".
			float StillFeedback = 0.0f;
			// One bit per optional input (the shader's kInput constants): 1 the geometric
			// test (RT-6), 2 the material lane (RT-6.2), 4 the box from this surface alone
			// (RT-6.8), 8 the change map (measured change), 16 the revealage (RT-22).
			float Inputs = 0.0f;
			// RT-5: the frame number the resolve draws its half-grid rounding
			// from -- tonemap's grain takes the same count for the same reason.
			float Frame = 0.0f;
			// RT-24: minus the depth at infinity, and the frustum's smaller extent per unit of
			// depth (zero: the 2% test).
			float DepthFloor = 0.0f;
			float FrustumExtent = 0.0f;
		};
		static_assert(sizeof(TemporalParams) <= 128, "the temporal block must fit 128 bytes");
		static_assert(offsetof(TemporalParams, Reproject) == 32, "the shader aligns the matrix to 32");
		TemporalParams full;
		uint32_t inputs = 0u;
		// Both lanes, and a history to compare against: with either missing the
		// test would be comparing this frame to uninitialised memory, which
		// refuses every pixel and turns the resolve off without saying so.
		if (guideCurrent && guidePrevious && hasHistory) inputs |= 1u;
		if (material) inputs |= 2u;
		if (boxGeometry) inputs |= 4u;
		if (change || reflectionChange) inputs |= 8u;
		if (revealage) inputs |= 16u;
		full.Inputs = (float)inputs;
		full.Jitter = jitter;
		full.StillFeedback = Math::Clamp(stillFeedback, 0.0f, 0.98f);
		full.Frame = (float)(Renderer::GetFrameCount() & 0xFFFFFFu);
		if (reproject && frustumExtent > 0.0f)
		{
			full.Reproject = *reproject;
			full.DepthFloor = Math::Max(depthFloor, 0.0f);
			full.FrustumExtent = frustumExtent;
		}""")
edit(P, """									  const Ref<RHITexture>& change,
									  const Ref<RHITexture>& reflectionChange,
									  const Ref<RHITexture>& revealage)
	{""", """									  const Ref<RHITexture>& change,
									  const Ref<RHITexture>& reflectionChange,
									  const Ref<RHITexture>& revealage,
									  const Mat4* reproject, float depthFloor, float frustumExtent)
	{""")
H = 'RageV/src/RageV/Renderer/PostProcess.h'
edit(H, """									 const RHI::Ref<RHI::RHITexture>& revealage = nullptr);

		// **Measured change (docs/RT-MEASURED-CHANGE.md, phase 1): one a-trous""",
     """									 const RHI::Ref<RHI::RHITexture>& revealage = nullptr,
									 // **RT-24 (2026-09-25): NRD's same-surface test.** This frame's
									 // rasterised NDC and depth to last frame's rasterised clip, minus
									 // the depth at infinity, and the frustum's smaller extent per unit
									 // of depth (Renderer3D::TemporalReprojection). Null or zero keeps
									 // the 2% test.
									 const Math::Mat4* reproject = nullptr, float depthFloor = 0.0f,
									 float frustumExtent = 0.0f);

		// **Measured change (docs/RT-MEASURED-CHANGE.md, phase 1): one a-trous""")

R = 'RageV/src/RageV/Renderer/Renderer3D.cpp'
edit(R, """			Vec4 SceneForward{ 0.0f, 0.0f, -1.0f, 0.0f };
			bool SceneActive = false;""", """			Vec4 SceneForward{ 0.0f, 0.0f, -1.0f, 0.0f };
			// RT-24: the projection's scale on each axis (P[0][0], P[1][1]), and whether it is
			// orthographic -- the frustum's size at a depth, for TAA's same-surface test.
			Vec2 SceneProjectionScale{ 0.0f, 0.0f };
			bool SceneOrthographic = false;
			bool SceneActive = false;""")
edit(R, """		s_Data->SceneForward = Vec4(-cameraTransform[2][0], -cameraTransform[2][1],
									-cameraTransform[2][2], 0.0f);""", """		s_Data->SceneForward = Vec4(-cameraTransform[2][0], -cameraTransform[2][1],
									-cameraTransform[2][2], 0.0f);
		s_Data->SceneProjectionScale = Vec2(camera.GetProjection()[0][0], camera.GetProjection()[1][1]);
		s_Data->SceneOrthographic = camera.GetProjection()[3][3] != 0.0f;""")
edit(R, """	bool Renderer3D::AnyInstanceMoved()
	{""", """	// **RT-24 (2026-09-25): what TAA's same-surface test needs of the camera** -- NRD's
	// disocclusion test. The reprojection takes this frame's rasterised NDC and depth to last
	// frame's rasterised clip (both view-projections as drawn, jitter included); the floor is
	// minus the depth at infinity (reverse-Z with a finite far plane); the extent is the
	// frustum's smaller side per unit of depth, which the shader takes NRD's 1% of. False, and
	// the 2% test, for an orthographic camera or before any scene.
	bool Renderer3D::TemporalReprojection(Mat4& reproject, float& depthFloor, float& frustumExtent)
	{
		if (!s_Data || s_Data->SceneOrthographic
			|| !(s_Data->SceneProjectionScale.x > 0.0f) || !(s_Data->SceneProjectionScale.y > 0.0f))
			return false;
		reproject = s_Data->Scene.PreviousViewProjection * Math::Inverse(s_Data->Scene.ViewProjection);
		const Vec4& f = s_Data->SceneForward;
		const Vec4 infinity = s_Data->Scene.ViewProjection * Vec4(f.x, f.y, f.z, 0.0f);
		depthFloor = infinity.w > 0.0f ? Math::Max(-infinity.z / infinity.w, 0.0f) : 0.0f;
		frustumExtent = 2.0f / Math::Max(s_Data->SceneProjectionScale.x, s_Data->SceneProjectionScale.y);
		return true;
	}

	bool Renderer3D::AnyInstanceMoved()
	{""")
RH = 'RageV/src/RageV/Renderer/Renderer3D.h'
edit(RH, """		static bool AnyInstanceMoved();""", """		static bool AnyInstanceMoved();
		// RT-24: what TAA's same-surface test needs of this view's camera (NRD's form); false
		// keeps the 2% test. Read while the frame's passes run, after BeginScene.
		static bool TemporalReprojection(Mat4& reproject, float& depthFloor, float& frustumExtent);""")

F = 'RageV/src/RageV/Renderer/FrameGraphBuilder.cpp'
edit(F, """					 boxGeometry = config.TaaBoxGeometry,""", """					 boxGeometry = config.TaaBoxGeometry, planeTest = config.TaaPlaneTest,""")
edit(F, """							wantTransparent ? context.Color(sceneHDR, 2) : nullptr);
					});""", """							wantTransparent ? context.Color(sceneHDR, 2) : nullptr,
							// RT-24: last frame's camera, for NRD's same-surface test.
							planeTest && reprojected ? &reproject : nullptr, depthFloor, frustumExtent);
					});""")
edit(F, """					{
						PostProcess::TemporalResolve(
							context.Cmd,
							context.Color(source),
							context.Color(previous),""", """					{
						Mat4 reproject{ 1.0f };
						float depthFloor = 0.0f, frustumExtent = 0.0f;
						const bool reprojected = Renderer3D::TemporalReprojection(reproject, depthFloor,
																				  frustumExtent);
						PostProcess::TemporalResolve(
							context.Cmd,
							context.Color(source),
							context.Color(previous),""")

EH = 'RageV/src/RageV/Core/EngineConfig.h'
edit(EH, """		bool  TaaBoxGeometry = true;""", """		bool  TaaBoxGeometry = true;
		// **--taa-plane-test=on|off (RT-24, 2026-09-25).** TAA's same-surface depth test in
		// NRD's form: last frame's depth predicted through last frame's camera, compared along
		// the surface's slope, 1% of the frustum's size. Off is the old 2% test.
		bool  TaaPlaneTest = true;""")
EC = 'RageV/src/RageV/Core/EngineConfig.cpp'
edit(EC, """		if (key == "taa-box-geometry" || key == "taaboxgeometry")""", """		if (key == "taa-plane-test" || key == "taaplanetest")
			return ParseBool(value, config.TaaPlaneTest);

		if (key == "taa-box-geometry" || key == "taaboxgeometry")""")
print('ok')
