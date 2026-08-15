#include <rvpch.h>
#include "ReflectionProbe.h"
#include "Renderer.h"
#include "Cubemap.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// The face this camera is pointed down, and which way up the resulting
		// image is. Both come from the cube-map face table, not from taste:
		// the top row of a face image looks along CubeFaceDirection(face, 0.5,
		// 0), and the camera has to agree or the capture and the sampling read
		// the same texels as different directions.
		//
		// Up is negated relative to that direction because a rendered image is
		// stored starting from its *bottom* row here -- see
		// RHICommandList::CopyToTextureLayer, which is where the two backends
		// are reconciled.
		struct FaceBasis
		{
			Vec3 Forward;
			Vec3 Up;
		};

		constexpr FaceBasis kFaces[CubeFaces::kFaceCount] =
		{
			{ {  1.0f,  0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },   // +X
			{ { -1.0f,  0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },   // -X
			{ {  0.0f,  1.0f,  0.0f }, { 0.0f,  0.0f,  1.0f } },   // +Y
			{ {  0.0f, -1.0f,  0.0f }, { 0.0f,  0.0f, -1.0f } },   // -Y
			{ {  0.0f,  0.0f,  1.0f }, { 0.0f, -1.0f,  0.0f } },   // +Z
			{ {  0.0f,  0.0f, -1.0f }, { 0.0f, -1.0f,  0.0f } },   // -Z
		};
	}

	ReflectionProbe::ReflectionProbe(RHIDevice& device, uint32_t faceSize)
		: m_Device(device), m_FaceSize(Math::Max(faceSize, 8u))
	{
		TextureDesc cube;
		cube.Width = m_FaceSize;
		cube.Height = m_FaceSize;
		cube.Layers = CubeFaces::kFaceCount;
		cube.Type = TextureType::TextureCube;
		// The same linear HDR format the scene renders into, so a captured
		// reflection carries the values bloom and the tone curve expect rather
		// than an already-clipped copy of them.
		cube.Format = Format::R16G16B16A16_SFLOAT;
		cube.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;
		// A chain, so roughness has something to select. Filled by GenerateMips
		// once a full set of faces is in.
		cube.MipLevels = 0;
		cube.DebugName = "probe.cube";

		m_Cube = device.CreateTexture(cube);

		RenderTargetDesc scratch;
		scratch.Width = m_FaceSize;
		scratch.Height = m_FaceSize;
		// Colour, velocity *and* the surface description, because the
		// pipelines that draw the scene declare all three, and a pipeline
		// bound into a pass with fewer attachments is undefined behaviour.
		// Nothing reads a probe's motion vectors or normals; the attachments
		// exist so the shapes agree. Same lesson as the sample count one item
		// earlier -- ENGINE-NOTES 7q, 7r and 7ad.
		scratch.ColorAttachments = { { Format::R16G16B16A16_SFLOAT },
									 { Format::R16G16_SFLOAT },
									 { Format::R8G8B8A8_UNORM } };
		scratch.HasDepth = true;
		scratch.DepthAttachment.Format = Format::D32_SFLOAT;
		// The same sample count the scene is being drawn at.
		//
		// Not because a reflection needs anti-aliasing -- at this resolution
		// it barely shows -- but because the renderers' pipelines are built
		// once, for one count, and a pipeline bound into a pass whose
		// attachments disagree is undefined behaviour. The alternative is to
		// switch the count around every probe capture, which rebuilds every
		// pipeline in every renderer twice a frame and flushes the batch
		// buffers they were recording into. That was measured, in the form of
		// a segfault. ENGINE-NOTES 7q.
		m_Samples = Renderer::GetTargetSamples();
		scratch.Samples = m_Samples;
		scratch.DebugName = "probe.face";

		m_Scratch = device.CreateRenderTarget(scratch);
	}

	Mat4 ReflectionProbe::FaceProjection(float nearClip, float farClip)
	{
		// 90 degrees and square: six of these tile the sphere of directions
		// exactly, which is the entire reason a cube map is six squares.
		return Math::Perspective(Math::Radians(90.0f), 1.0f,
								Math::Max(nearClip, 0.001f),
								Math::Max(farClip, nearClip + 0.01f));
	}

	Mat4 ReflectionProbe::FaceTransform(uint32_t face, const Vec3& position)
	{
		if (face >= CubeFaces::kFaceCount)
			return Mat4(1.0f);

		const FaceBasis& basis = kFaces[face];

		// A view matrix inverted, because the renderer takes a camera's world
		// transform rather than a view: a camera is an entity everywhere else in
		// this engine, and a probe should not be the one exception.
		return Math::Inverse(Math::LookAt(position, position + basis.Forward, basis.Up));
	}

	uint32_t ReflectionProbe::CaptureFaces(RHICommandList& cmd, const Vec3& position,
										   float nearClip, float farClip,
										   uint32_t first, uint32_t count, const DrawScene& draw)
	{
		if (!m_Cube || !m_Scratch || !draw || count == 0)
			return first;

		const Camera camera(FaceProjection(nearClip, farClip));
		const Ref<RHITexture> colour = m_Scratch->GetColorTexture(0);

		uint32_t face = first % CubeFaces::kFaceCount;

		for (uint32_t i = 0; i < count && i < CubeFaces::kFaceCount; i++)
		{
			RenderPassBeginInfo begin;
			begin.Target = m_Scratch.get();
			begin.ClearColor = true;
			begin.ClearDepth = true;
			begin.UseDepth = true;
			begin.Clear.Depth = 1.0f;

			cmd.PushDebugGroup("Reflection probe face");
			cmd.BeginRenderPass(begin);

			draw(camera, FaceTransform(face, position));

			cmd.EndRenderPass();

			// Outside the pass, which is where a copy has to be.
			cmd.CopyToTextureLayer(colour, m_Cube, face);
			cmd.PopDebugGroup();

			face = (face + 1) % CubeFaces::kFaceCount;

			if (m_FacesCaptured < CubeFaces::kFaceCount)
				m_FacesCaptured++;
		}

		// Only once every face holds something. A chain generated over a
		// half-filled cube pulls the empty faces into the blurry mips of the
		// full ones, and the seam survives the next five updates.
		if (m_FacesCaptured >= CubeFaces::kFaceCount)
		{
			// Through the command list, not the texture.
			//
			// RHITexture::GenerateMips submits its own buffer and waits, so it
			// ran *before* the face copies above -- which are recorded into
			// this frame's buffer and had not been submitted yet. The chain was
			// therefore built from an empty mip 0, and every rough surface
			// reflecting this probe read black until the next full round of
			// faces rebuilt it from a mip 0 an earlier frame had flushed. On a
			// probe updating one face a frame that is the first six frames.
			cmd.GenerateMips(m_Cube);
			m_Complete = true;
		}

		// A round has landed when the cursor comes back to the start with a
		// full cube behind it. That is the moment the contents are new and
		// everything derived from them -- the prefiltered slice, the irradiance
		// slice -- is one capture out of date.
		if (m_Complete && face == 0)
			m_Generation++;

		return face;
	}
}
