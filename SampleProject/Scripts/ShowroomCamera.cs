using RageV;
using System;

// The showroom's turntable: drag to walk around the car, drag up to look down
// on it, wheel to step closer.
//
// **The camera orbits and the car does not turn**, which is the choice worth
// stating because both look the same in a still. A turning car is a turntable,
// and a turntable lights differently at every angle -- the highlight that runs
// down a flank stays welded to the same panel while the reflection of the room
// swims across it, which is exactly the tell that says "this is a model". A
// moving camera keeps the car still under a fixed luminaire, so the reflection
// travels the way it does when you walk around a real one. It is also the only
// version where the floor reflection stays put.
//
// **The vertical range is deliberately narrow.** A car looks like a car
// between about ground level and the height of a person's eye; above that it
// becomes a plan view of a roof, which is the one angle a showroom never shows
// you and the one a free orbit spends most of its time in. So pitch is clamped
// hard and the clamp is not a placeholder -- it is the feature.
public class ShowroomCamera : Script
{
	// Where the orbit is centred, in world space. A little above the car's own
	// centre: aiming at the centroid of a low, wide object puts the horizon
	// through the middle of the doors and the roof reads as flat.
	private float TargetX = 0.0f;
	private float TargetY = 0.72f;
	private float TargetZ = 0.0f;

	// Degrees. Yaw runs freely; pitch is the restricted one.
	private float Yaw = 0.0f;
	private float Pitch = 5.0f;
	private float MinPitch = 0.5f;
	private float MaxPitch = 32.0f;

	// Metres from the target.
	private float Distance = 7.3f;
	private float MinDistance = 4.6f;
	private float MaxDistance = 11.0f;

	// Degrees per pixel of drag, and metres per wheel notch.
	private float YawSpeed = 0.26f;
	private float PitchSpeed = 0.16f;
	private float ZoomSpeed = 0.55f;

	// How fast the camera catches up with where the drag has put it, per
	// second. **Smoothed rather than driven directly**, because a mouse
	// reports in jumps and a camera that follows them exactly judders on a
	// slow drag -- which is the difference between a showroom and a debug
	// orbit. Not a spring: an exponential approach has no overshoot, and a car
	// that rocks past its stop and settles reads as a physics bug.
	private float Smoothing = 14.0f;

	private float m_Yaw;
	private float m_Pitch;
	private float m_Distance;
	private bool m_Ready;

	public override void OnCreate()
	{
		// Seeded from the authored values rather than from where the entity
		// happens to be. The scene generator solves the camera's transform
		// from these same numbers, so the first frame is identical to the still
		// the scene was framed as -- and reading the transform back would mean
		// inverting a solve to recover angles it already knows.
		m_Yaw = Yaw;
		m_Pitch = Clamp(Pitch, MinPitch, MaxPitch);
		m_Distance = Clamp(Distance, MinDistance, MaxDistance);
		m_Ready = true;

		Apply(1.0f);
	}

	public override void OnFrame(float deltaTime)
	{
		if (!m_Ready)
			return;

		// **The pointer, and only when it is not over the UI.** The credit line
		// along the bottom is a rectangle the canvas owns; without this check a
		// drag that starts on it would also spin the car, which reads as the
		// text being draggable.
		if (Input.IsActionDown("Fire") && !Input.IsPointerOverUI)
		{
			Yaw -= Input.GetAxis("LookX") * YawSpeed;
			Pitch += Input.GetAxis("LookY") * PitchSpeed;
		}

		// Wheel notches are signed and arrive whether or not a button is down,
		// which is what anyone expects of a zoom.
		float wheel = Input.GetAxis("Zoom");
		if (wheel != 0.0f)
			Distance -= wheel * ZoomSpeed;

		Pitch = Clamp(Pitch, MinPitch, MaxPitch);
		Distance = Clamp(Distance, MinDistance, MaxDistance);

		// Yaw wraps rather than clamping, so a long drag in one direction keeps
		// going round instead of stopping at a number.
		if (Yaw > 180.0f)  Yaw -= 360.0f;
		if (Yaw < -180.0f) Yaw += 360.0f;

		// The same wrap has to reach the smoothed value or a pass through the
		// seam sends the camera the long way round the car.
		if (m_Yaw - Yaw > 180.0f)  m_Yaw -= 360.0f;
		if (Yaw - m_Yaw > 180.0f)  m_Yaw += 360.0f;

		Apply(1.0f - MathF.Exp(-Smoothing * deltaTime));
	}

	// `blend` of 1 snaps, which is what OnCreate wants; anything less eases.
	private void Apply(float blend)
	{
		m_Yaw += (Yaw - m_Yaw) * blend;
		m_Pitch += (Pitch - m_Pitch) * blend;
		m_Distance += (Distance - m_Distance) * blend;

		float yaw = m_Yaw * (MathF.PI / 180.0f);
		float pitch = m_Pitch * (MathF.PI / 180.0f);

		Vector3 target = new Vector3(TargetX, TargetY, TargetZ);

		// Yaw 0 stands in front of the car, on +Z, which is the way the model
		// faces and the way the scene is framed.
		float ground = m_Distance * MathF.Cos(pitch);
		Vector3 offset = new Vector3(ground * MathF.Sin(yaw),
									 m_Distance * MathF.Sin(pitch),
									 ground * MathF.Cos(yaw));

		// A local copy, because `Script.Entity` is a property returning a
		// struct and assigning through it is a write to a temporary. The copy
		// carries the same id, so the setter reaches the same entity --
		// CampCamera does exactly this for exactly this reason.
		Entity self = Entity;
		self.Position = target + offset;

		// The engine's own LookAt rather than solving the euler angles here.
		// Writing the inverse of the generator's solve by hand works and is
		// also a second opinion about which way -Z points that nothing would
		// check -- the same argument CampCamera makes.
		self.LookAt(target);
	}

	private static float Clamp(float value, float low, float high) =>
		value < low ? low : (value > high ? high : value);
}
