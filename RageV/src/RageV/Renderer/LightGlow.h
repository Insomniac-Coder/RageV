#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	class Camera;

	// **A light drawn at a fixed size on screen** (WR-5, the first half).
	//
	// A lamp's lens on this engine's bridge is a 0.7 m box at emissive 26,
	// and from a kilometre away a pixel covers a metre of it: the lens is
	// present in the frames a sample lands on it and absent in the frames it
	// does not, and it blinks. Measured at 23% of the deck band's pixels
	// under TAA after every other cause was removed, and no blend share can
	// average an input that is on or off. This pass draws every positional
	// light that has a source size as a soft disc a few pixels across at any
	// distance, carrying the light's own intensity (I/(d²Ω), the value a
	// correctly integrated sub-pixel source puts in a pixel), so the input is
	// stable in every frame and under every anti-aliasing mode. The disc
	// fades out where the lens is bigger on screen than the disc, so a
	// close-up keeps the authored look. An optional flare moves part of the
	// energy into a wider halo with rays. See light_glow.rvshader.
	//
	// Shaped like ParticleRenderer: its own pipeline against the scene
	// target, its own small uniform block, the scene's light buffer bound as
	// a storage buffer. Drawn from the end of Renderer3D::EndScene, where the
	// opaque depth is complete and the light buffer is the one the lit pass
	// used. Only while the frame graph has told it a viewport -- a reflection
	// probe's face or a shadow cascade never gets one, so they never draw it.
	struct LightGlowSettings
	{
		bool  Enabled = true;
		float GlowPixels = 4.0f;
		float Intensity = 1.0f;
		float FlareShare = 0.0f;
		float FlarePixels = 24.0f;
		float FlareRays = 6.0f;
	};

	class LightGlow
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// The scene target's shape, the same contract every renderer that
		// draws into it has. See Renderer::SetTargetFormats.
		static void SetTargetFormats(RHI::Format color, RHI::Format depth, uint32_t samples,
									 RHI::Format velocity, RHI::Format normal,
									 RHI::Format indirect);

		// The camera the scene is drawn with: the jittered view-projection
		// the geometry uses, so the discs jitter with the geometry rather
		// than sliding against it, and the projection for the angle one
		// pixel subtends.
		static void BeginScene(const Mat4& viewProjection, const Mat4& cameraTransform,
							   const Mat4& projection);

		// The size of the target this scene pass draws into, in pixels. Zero
		// means "not the scene pass" and draws nothing -- set by the frame
		// graph around the scene draw and cleared after it, the way the
		// jitter is.
		static void SetViewport(uint32_t width, uint32_t height);

		static void SetSettings(const LightGlowSettings& settings);

		// One quad per light in `lights`, additive into the bound scene
		// target. Call once the opaque draws are down.
		static void Draw(RHI::RHICommandList& cmd, const RHI::Ref<RHI::RHIBuffer>& lights,
						 uint32_t lightCount);
	};
}
