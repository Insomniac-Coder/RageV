#pragma once
#include "RageV/Math/Math.h"
#include <string>
#include <vector>

namespace RageV::Anim
{
	// A bone hierarchy and the clips that move it.
	//
	// Separate from Mesh on purpose: several meshes can share one skeleton --
	// a character's body, head and clothing are usually separate primitives
	// skinned to the same bones -- and a clip is authored against the skeleton
	// rather than against any of them.
	//
	// Everything here is CPU-side and device-free, so it can be imported,
	// sampled and tested without a GPU. That matters more than usual: a pose is
	// a hundred matrix composes whose failure mode is a character subtly
	// wrong rather than absent, and the only practical way to catch that is to
	// assert against known poses.

	struct Bone
	{
		std::string Name;

		// Index into Skeleton::Bones, or -1 for a root. Parents always come
		// before their children, which is what lets one forward pass compose
		// the whole hierarchy.
		int Parent = -1;

		// Mesh space to this bone's space, at the pose the mesh was modelled
		// in. Skinning needs it because vertices are stored in mesh space and
		// bones move in their own.
		Mat4 InverseBind{ 1.0f };

		// Where the bone sits when nothing animates it. A clip need not have a
		// track for every bone, and a bone with no track has to hold still
		// rather than collapse to the origin.
		Vec3 RestPosition{ 0.0f };
		Quat RestRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		Vec3 RestScale{ 1.0f };
	};

	class Skeleton
	{
	public:
		std::vector<Bone> Bones;

		size_t Size() const { return Bones.size(); }
		bool IsEmpty() const { return Bones.empty(); }

		// -1 when there is no such bone. Linear: a skeleton is tens of bones
		// and this is called when a clip is bound, not per frame.
		int Find(const std::string& name) const;

		// Parents before children, and no bone its own ancestor.
		//
		// Checked rather than assumed because the composition pass below
		// recurses nowhere and simply walks forwards -- if a child preceded its
		// parent it would compose against last frame's matrix, which is a limb
		// that lags by exactly one frame and looks like a physics problem.
		bool IsWellOrdered() const;
	};

	// One bone's animation, as three independently keyed channels.
	//
	// Separate channels rather than one keyframe of everything, because that is
	// how glTF stores it and because a bone that only rotates -- most of them --
	// then costs nothing for position and scale.
	template<typename T>
	struct Channel
	{
		std::vector<float> Times;
		std::vector<T> Values;

		bool IsEmpty() const { return Times.empty(); }
	};

	struct BoneTrack
	{
		Channel<Vec3> Position;
		Channel<Quat> Rotation;
		Channel<Vec3> Scale;

		bool IsEmpty() const
		{
			return Position.IsEmpty() && Rotation.IsEmpty() && Scale.IsEmpty();
		}
	};

	struct Clip
	{
		std::string Name;
		// Seconds. Taken from the last key of any channel, not authored.
		float Duration = 0.0f;
		// One per bone of the skeleton this was bound to; may be empty.
		std::vector<BoneTrack> Tracks;

		void RecomputeDuration();
	};

	// A pose is one local transform per bone -- what the clip says, before the
	// hierarchy is composed.
	struct BoneTransform
	{
		Vec3 Position{ 0.0f };
		Quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		Vec3 Scale{ 1.0f };

		Mat4 ToMatrix() const;
	};

	using Pose = std::vector<BoneTransform>;

	// Samples every bone at `time`, filling `out` with local transforms.
	//
	// `time` is wrapped into the clip when `loop`, clamped otherwise. Bones the
	// clip does not animate take their rest transform, which is why this needs
	// the skeleton and not just the clip.
	void SamplePose(const Skeleton& skeleton, const Clip& clip,
					float time, bool loop, Pose& out);

	// The skeleton at rest, with no clip at all.
	void RestPose(const Skeleton& skeleton, Pose& out);

	// Local transforms to the matrices a vertex shader multiplies by.
	//
	// Each is (bone's mesh-space transform) * (its inverse bind), so a skeleton
	// standing in its bind pose produces identity for every bone and the mesh
	// renders exactly as modelled. That property is the single most useful test
	// there is here: it fails if the bind matrices are wrong, if the hierarchy
	// composes in the wrong order, or if anything is in the wrong space.
	void ComposeSkinning(const Skeleton& skeleton, const Pose& pose,
						 std::vector<Mat4>& out);

	// The same composition stopping at mesh space, without the inverse bind.
	// What a socket -- a weapon in a hand, a camera on a head -- needs.
	void ComposeGlobal(const Skeleton& skeleton, const Pose& pose,
					   std::vector<Mat4>& out);

	// Blends two poses. `weight` 0 gives `a`, 1 gives `b`.
	//
	// Rotations slerp rather than lerp, for the reason recorded about physics
	// interpolation: a component-wise blend of two quaternions shortens the arc
	// and makes a limb dip through the middle of a turn.
	void BlendPoses(const Pose& a, const Pose& b, float weight, Pose& out);

	// A box that contains the mesh in *every* pose its clips can reach.
	//
	// The bind pose's box is not that box, and using it was a real defect: a
	// limb swinging wide leaves it, so the mesh is culled while part of it is
	// still on screen -- and a thing that vanishes when you look slightly away
	// from it reads as a rendering bug rather than as a bounds one.
	//
	// **Per bone, not per vertex, and that is what makes it cheap.** One pass
	// over the vertices reduces them to a box per bone in that bone's own
	// space; after that a pose costs one transformed box per bone rather than
	// a transform per vertex. It stays conservative because a skinned position
	// is a weighted average of the same vertex placed by each of its bones, so
	// it can never leave the union of those bones' transformed boxes.
	//
	// Sampled rather than solved: a clip's extreme is not analytic once
	// rotations are interpolated, so this walks each clip at a fixed number of
	// steps. Missing an extreme between two samples errs towards a box that is
	// slightly small, which is why the result is padded.
	//
	// Positions, joints and weights are the mesh's parallel arrays. Vec3 out
	// parameters rather than an AABB because that type lives in the renderer's
	// Mesh.h, and nothing else here needs the renderer.
	void SkinnedBounds(const Skeleton& skeleton, const std::vector<Clip>& clips,
					   const std::vector<Vec3>& positions,
					   const std::vector<UVec4>& joints,
					   const std::vector<Vec4>& weights,
					   Vec3& outMin, Vec3& outMax);
}

// Re-exported into RageV. Skeleton hangs off a Mesh and Clip is what an
// AnimatorComponent plays, so both appear in declarations that are otherwise
// entirely unqualified -- see AudioEngine.h for the rule.
//
// The free functions -- SamplePose, BlendPoses, ComposeSkinning -- deliberately
// are *not* re-exported and do not need to be: their arguments are Anim types,
// so argument-dependent lookup finds them unqualified anyway.
namespace RageV
{
	using Anim::Skeleton;
	using Anim::Bone;
	using Anim::BoneTrack;
	using Anim::BoneTransform;
	using Anim::Pose;

	using AnimationClip = Anim::Clip;
}
