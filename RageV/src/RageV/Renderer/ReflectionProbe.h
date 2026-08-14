#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Renderer/Camera.h"
#include "RageV/Math/Math.h"
#include <functional>

namespace RageV
{
	// A cube map captured from a point in the scene.
	//
	// The difference from an environment map loaded off disk is only where the
	// pixels come from: the shader binds either one the same way. What a probe
	// buys is that the reflection contains the *scene* -- the wall behind you,
	// the red box beside you -- rather than only the sky.
	//
	// Six faces are rendered into one scratch 2D target and copied into the
	// cube, rather than rendered into the cube directly. Rendering into a face
	// needs a per-layer image view on one backend and a layered framebuffer on
	// the other, and the two disagree about which way up a rendered image is
	// stored; a copy step costs six blits of a small image and puts that
	// disagreement in one place -- RHICommandList::CopyToTextureLayer -- with
	// the reasoning written down beside it.
	class ReflectionProbe
	{
	public:
		ReflectionProbe(RHI::RHIDevice& device, uint32_t faceSize);

		uint32_t GetFaceSize() const { return m_FaceSize; }
		// The sample count its scratch face was built with. A probe outlives a
		// change of anti-aliasing mode, and the renderers' pipelines do not --
		// so the capture compares this and rebuilds, exactly as it does for a
		// change of resolution.
		uint32_t GetSamples() const { return m_Samples; }		const RHI::Ref<RHI::RHITexture>& GetCube() const { return m_Cube; }

		// False until all six faces have been rendered at least once. Binding a
		// half-captured probe would show black where the scene has not been
		// drawn yet, so callers fall back to the sky until this is true.
		bool IsComplete() const { return m_Complete; }

		// Bumped each time a full round of faces lands, and never otherwise.
		//
		// What reads it is the probe array, which has to decide whether the
		// slice it filled from this cube is still current. Comparing the cube
		// pointer is not enough: a realtime probe re-captures into the *same*
		// texture, so the pointer never changes while the contents do. A
		// counter says "this is a different capture" without anyone having to
		// remember to raise a flag.
		uint64_t GetGeneration() const { return m_Generation; }

		// Draws the scene from one face's camera. Given the projection and the
		// camera's world transform, exactly as a viewport would be.
		using DrawScene = std::function<void(const Camera&, const Mat4&)>;

		// Renders `count` faces starting at `first`, wrapping round. Returns the
		// face to start from next time.
		//
		// Splitting a capture across frames is what makes a realtime probe
		// affordable: six scene renders in one frame is a visible hitch, one per
		// frame is a sixth of a frame's cost and a sixth of a second of latency
		// on a reflection, which nobody has ever noticed in a moving image.
		//
		// Must be called outside a render pass.
		uint32_t CaptureFaces(RHI::RHICommandList& cmd, const Vec3& position,
							  float nearClip, float farClip,
							  uint32_t first, uint32_t count, const DrawScene& draw);

		// The camera for one face: a 90-degree frustum looking down that face's
		// axis, with the up vector the cube-map face table implies.
		//
		// Exposed because it is the part worth checking without a GPU. Every way
		// of getting the basis wrong -- a mirrored face, a rotated one, an
		// upside-down one -- still produces a plausible reflection.
		static Mat4 FaceTransform(uint32_t face, const Vec3& position);
		static Mat4 FaceProjection(float nearClip, float farClip);

	private:
		RHI::RHIDevice& m_Device;
		uint32_t m_Samples = 1;
		uint32_t m_FaceSize = 0;

		RHI::Ref<RHI::RHITexture> m_Cube;
		// One scratch target, reused for all six faces. The copy after each face
		// is ordered against the render before it, so there is nothing to gain
		// from six.
		RHI::Ref<RHI::RHIRenderTarget> m_Scratch;

		uint32_t m_FacesCaptured = 0;
		bool m_Complete = false;
		uint64_t m_Generation = 0;
	};
}
