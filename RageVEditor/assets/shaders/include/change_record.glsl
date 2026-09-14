// Measured change (docs/RT-MEASURED-CHANGE.md): what a record may say about the
// surface under the pixel it sampled. Included by the three record variants
// (direct_trace, reflection_trace, rtgi_trace) after pbr_fragment.glsl, whose
// scene block it reads.
#ifndef RV_CHANGE_RECORD_GLSL
#define RV_CHANGE_RECORD_GLSL

// **Whether the surface moved on its own this frame -- not whether the pixel
// did.** A record that has moved is no use to the re-light, which lights the
// point where the record put it, and the scene's velocity lane is where motion
// is written. But the lane holds the camera's motion as well as the object's:
// read as it stands it marked every surface the camera swept past as moving, so
// a moving camera measured nothing, which is why the check once waited for the
// camera to stand still.
//
// What the camera alone did to this point is what last frame's view-projection
// says of it standing still; what the lane holds beyond that is the surface's
// own travel. RT-15's ObjectShift (reflection_accumulate.rvshader) asks the same
// question the same way: both answers with their frame's jitter taken back out,
// and an eighth of a texel between the rounding of two ways of computing the
// same point and a surface that moved. The lane is uv units a frame, NDC y up.
bool RecordMovedOnItsOwn(vec2 velocity, vec3 P, vec2 texels)
{
	if (dot(velocity, velocity) <= 0.0)
		return false;
	const vec4 now = u_Scene.ViewProjection * vec4(P, 1.0);
	const vec4 then = u_Scene.PreviousViewProjection * vec4(P, 1.0);
	// Behind either eye there is no projected answer to compare, and a point
	// there is not one to replay.
	if (now.w <= 0.0 || then.w <= 0.0)
		return true;
	const vec2 byCamera = then.xy / then.w - u_Scene.Jitter.zw;
	const vec2 byMotion = (now.xy / now.w - u_Scene.Jitter.xy) - 2.0 * velocity;
	const vec2 shift = (byMotion - byCamera) * 0.5 * texels;
	return dot(shift, shift) >= 0.125 * 0.125;
}

#endif
