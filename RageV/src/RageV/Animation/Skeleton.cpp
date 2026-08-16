#include <rvpch.h>
#include "Skeleton.h"
#include "RageV/Math/Math.h"

namespace RageV::Anim
{
	namespace
	{
		// Where `time` falls in a channel's key times: the key before it, and
		// how far between that key and the next.
		//
		// A linear scan would be fine for a clip of a few keys and is not for a
		// motion-captured one of a few thousand, sampled per bone per frame.
		template<typename T>
		void FindKey(const Channel<T>& channel, float time,
					 size_t& index, float& blend)
		{
			index = 0;
			blend = 0.0f;

			if (channel.Times.size() < 2)
				return;

			// upper_bound gives the first key strictly after `time`; the one
			// before it is the segment we are in. Clamped at both ends so a
			// time outside the channel holds its first or last value rather
			// than reading past the array.
			const auto after = std::upper_bound(channel.Times.begin(), channel.Times.end(), time);
			if (after == channel.Times.begin())
				return;

			if (after == channel.Times.end())
			{
				index = channel.Times.size() - 1;
				return;
			}

			index = (size_t)(after - channel.Times.begin()) - 1;

			const float start = channel.Times[index];
			const float end = channel.Times[index + 1];
			const float span = end - start;

			// Two keys at the same time is a step, not a division by zero.
			blend = span > 1e-8f ? Math::Clamp((time - start) / span, 0.0f, 1.0f) : 0.0f;
		}

		Vec3 SampleVec3(const Channel<Vec3>& channel, float time,
							 const Vec3& fallback)
		{
			if (channel.Values.empty())
				return fallback;
			if (channel.Values.size() == 1)
				return channel.Values[0];

			size_t index = 0;
			float blend = 0.0f;
			FindKey(channel, time, index, blend);

			if (index + 1 >= channel.Values.size())
				return channel.Values.back();

			return Math::Mix(channel.Values[index], channel.Values[index + 1], blend);
		}

		Quat SampleQuat(const Channel<Quat>& channel, float time,
							 const Quat& fallback)
		{
			if (channel.Values.empty())
				return fallback;
			if (channel.Values.size() == 1)
				return Math::Normalize(channel.Values[0]);

			size_t index = 0;
			float blend = 0.0f;
			FindKey(channel, time, index, blend);

			if (index + 1 >= channel.Values.size())
				return Math::Normalize(channel.Values.back());

			// slerp, and Math::Slerp takes the short way only if the two are in the
			// same hemisphere -- so the sign is fixed first. Without it a pair
			// of keys either side of the antipode spins the bone the long way
			// round, which is a limb briefly rotating backwards through the
			// body.
			Quat from = channel.Values[index];
			Quat to = channel.Values[index + 1];
			if (Math::Dot(from, to) < 0.0f)
				to = -to;

			return Math::Normalize(Math::Slerp(from, to, blend));
		}
	}

	Mat4 BoneTransform::ToMatrix() const
	{
		// Scale, then rotate, then translate -- the order every authoring tool
		// composes a node in, and the order glTF specifies.
		return Math::Translate(Mat4(1.0f), Position) *
			   Math::ToMat4(Rotation) *
			   Math::Scale(Mat4(1.0f), Scale);
	}

	int Skeleton::Find(const std::string& name) const
	{
		for (size_t i = 0; i < Bones.size(); i++)
		{
			if (Bones[i].Name == name)
				return (int)i;
		}
		return -1;
	}

	bool Skeleton::IsWellOrdered() const
	{
		for (size_t i = 0; i < Bones.size(); i++)
		{
			const int parent = Bones[i].Parent;
			if (parent < 0)
				continue;

			// Out of range, or not already visited. Both mean the forward
			// composition below would read a matrix that is not this frame's.
			if (parent >= (int)i)
				return false;
		}
		return true;
	}

	void Clip::RecomputeDuration()
	{
		Duration = 0.0f;

		for (const BoneTrack& track : Tracks)
		{
			if (!track.Position.Times.empty())
				Duration = Math::Max(Duration, track.Position.Times.back());
			if (!track.Rotation.Times.empty())
				Duration = Math::Max(Duration, track.Rotation.Times.back());
			if (!track.Scale.Times.empty())
				Duration = Math::Max(Duration, track.Scale.Times.back());
		}
	}

	void RestPose(const Skeleton& skeleton, Pose& out)
	{
		out.resize(skeleton.Bones.size());

		for (size_t i = 0; i < skeleton.Bones.size(); i++)
		{
			out[i].Position = skeleton.Bones[i].RestPosition;
			out[i].Rotation = skeleton.Bones[i].RestRotation;
			out[i].Scale = skeleton.Bones[i].RestScale;
		}
	}

	void SamplePose(const Skeleton& skeleton, const Clip& clip,
					float time, bool loop, Pose& out)
	{
		out.resize(skeleton.Bones.size());

		// Wrapped or clamped before anything is sampled, so every channel of
		// every bone sees the same instant. Sampling each against its own
		// wrapped time would tear the pose at the loop point.
		float t = time;
		if (clip.Duration > 1e-6f)
		{
			if (loop)
			{
				t = Math::FMod(time, clip.Duration);
				// fmod keeps the sign of the numerator, so a negative time --
				// which a blend running backwards produces -- would land
				// outside the clip.
				if (t < 0.0f)
					t += clip.Duration;
			}
			else
			{
				t = Math::Clamp(time, 0.0f, clip.Duration);
			}
		}
		else
		{
			t = 0.0f;
		}

		for (size_t i = 0; i < skeleton.Bones.size(); i++)
		{
			const Bone& bone = skeleton.Bones[i];

			// A clip need not cover every bone, and one that does not leaves
			// the rest of them at rest rather than at the origin.
			if (i >= clip.Tracks.size())
			{
				out[i].Position = bone.RestPosition;
				out[i].Rotation = bone.RestRotation;
				out[i].Scale = bone.RestScale;
				continue;
			}

			const BoneTrack& track = clip.Tracks[i];
			out[i].Position = SampleVec3(track.Position, t, bone.RestPosition);
			out[i].Rotation = SampleQuat(track.Rotation, t, bone.RestRotation);
			out[i].Scale = SampleVec3(track.Scale, t, bone.RestScale);
		}
	}

	void ComposeGlobal(const Skeleton& skeleton, const Pose& pose,
					   std::vector<Mat4>& out)
	{
		const size_t count = skeleton.Bones.size();
		out.resize(count);

		for (size_t i = 0; i < count; i++)
		{
			const Mat4 local = i < pose.size() ? pose[i].ToMatrix() : Mat4(1.0f);
			const int parent = skeleton.Bones[i].Parent;

			// One forward pass, no recursion and no depth guard. Both are
			// bought by the parents-before-children invariant, which
			// IsWellOrdered exists to state and the test suite to enforce.
			out[i] = parent >= 0 && parent < (int)i ? out[parent] * local : local;
		}
	}

	void ComposeSkinning(const Skeleton& skeleton, const Pose& pose,
						 std::vector<Mat4>& out)
	{
		ComposeGlobal(skeleton, pose, out);

		for (size_t i = 0; i < out.size(); i++)
			out[i] = out[i] * skeleton.Bones[i].InverseBind;
	}

	void BlendPoses(const Pose& a, const Pose& b, float weight, Pose& out)
	{
		const size_t count = Math::Min(a.size(), b.size());
		out.resize(count);

		const float t = Math::Clamp(weight, 0.0f, 1.0f);

		for (size_t i = 0; i < count; i++)
		{
			out[i].Position = Math::Mix(a[i].Position, b[i].Position, t);
			out[i].Scale = Math::Mix(a[i].Scale, b[i].Scale, t);

			// Same hemisphere fix as the sampler, for the same reason.
			Quat from = a[i].Rotation;
			Quat to = b[i].Rotation;
			if (Math::Dot(from, to) < 0.0f)
				to = -to;

			out[i].Rotation = Math::Normalize(Math::Slerp(from, to, t));
		}
	}

	void SkinnedBounds(const Skeleton& skeleton, const std::vector<Clip>& clips,
					   const std::vector<Vec3>& positions,
					   const std::vector<UVec4>& joints,
					   const std::vector<Vec4>& weights,
					   Vec3& outMin, Vec3& outMax)
	{
		constexpr float kInfinity = std::numeric_limits<float>::max();

		outMin = Vec3(kInfinity);
		outMax = Vec3(-kInfinity);

		if (skeleton.IsEmpty() || positions.empty())
			return;

		// --- one pass over the vertices: a box per bone, in that bone's space
		const size_t boneCount = skeleton.Size();
		std::vector<Vec3> boneMin(boneCount, Vec3(kInfinity));
		std::vector<Vec3> boneMax(boneCount, Vec3(-kInfinity));
		std::vector<bool> used(boneCount, false);

		const bool skinned = joints.size() == positions.size() &&
							 weights.size() == positions.size();

		for (size_t v = 0; v < positions.size(); v++)
		{
			if (!skinned)
				break;

			for (int influence = 0; influence < 4; influence++)
			{
				// A zero weight is not an influence, and the joint index
				// beside it is meaningless padding -- reading it as a bone
				// would stretch that bone's box to wherever the vertex is.
				if (weights[v][influence] <= 0.0f)
					continue;

				const uint32_t bone = joints[v][influence];
				if (bone >= boneCount)
					continue;

				// Into the bone's own space, which is what the inverse bind
				// matrix is for. The box then travels with the bone.
				const Vec3 local =
					Vec3(skeleton.Bones[bone].InverseBind * Vec4(positions[v], 1.0f));

				boneMin[bone] = Math::Min(boneMin[bone], local);
				boneMax[bone] = Math::Max(boneMax[bone], local);
				used[bone] = true;
			}
		}

		// A mesh with a skeleton but no weights at all -- or none this
		// function could read -- has nothing to say beyond its own vertices.
		bool any = false;
		for (size_t i = 0; i < boneCount; i++)
			any = any || used[i];

		if (!any)
		{
			for (const Vec3& p : positions)
			{
				outMin = Math::Min(outMin, p);
				outMax = Math::Max(outMax, p);
			}
			return;
		}

		// --- every pose, one transformed box per bone -----------------------
		std::vector<Mat4> skinning;

		auto accumulate = [&](const Pose& pose)
		{
			ComposeSkinning(skeleton, pose, skinning);
			if (skinning.size() < boneCount)
				return;

			for (size_t bone = 0; bone < boneCount; bone++)
			{
				if (!used[bone])
					continue;

				// All eight corners: a rotated box's extent is not obtainable
				// from two transformed corners, and using only min and max is
				// the classic way to produce bounds that are too small in
				// exactly the poses that needed them.
				for (int corner = 0; corner < 8; corner++)
				{
					const Vec3 point(
						(corner & 1) ? boneMax[bone].x : boneMin[bone].x,
						(corner & 2) ? boneMax[bone].y : boneMin[bone].y,
						(corner & 4) ? boneMax[bone].z : boneMin[bone].z);

					const Vec3 moved = Vec3(skinning[bone] * Vec4(point, 1.0f));
					outMin = Math::Min(outMin, moved);
					outMax = Math::Max(outMax, moved);
				}
			}
		};

		// The bind pose always counts: a model with no clips must come out
		// with the bounds it already had.
		Pose pose;
		RestPose(skeleton, pose);
		accumulate(pose);

		// Enough steps to catch a limb's extreme without making load time a
		// function of how many clips a model happens to ship with.
		constexpr int kSamplesPerClip = 24;

		for (const Clip& clip : clips)
		{
			if (clip.Duration <= 0.0f)
				continue;

			for (int step = 0; step <= kSamplesPerClip; step++)
			{
				const float time = clip.Duration * (float)step / (float)kSamplesPerClip;
				SamplePose(skeleton, clip, time, /*loop*/ false, pose);
				accumulate(pose);
			}
		}

		// Padding, because sampling can step over an extreme between two
		// times. Proportional to the box rather than absolute: a fixed margin
		// is invisible on a building and enormous on a coin.
		const Vec3 pad = (outMax - outMin) * 0.02f;
		outMin -= pad;
		outMax += pad;
	}
}
