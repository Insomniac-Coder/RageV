#include <rvpch.h>
#include "ShadowMap.h"
#include "Renderer.h"
#include "ReflectionProbe.h"
#include "Cubemap.h"
#include "RageV/Math/Math.h"
#include <array>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		struct ShadowData
		{
			RHIDevice* Device = nullptr;

			// One target per cascade rather than one array target with a view
			// per layer. Four textures cost four descriptor writes; a layered
			// target costs a per-layer image view on one backend and a layered
			// framebuffer on the other, for a saving of nothing.
			std::array<Ref<RHIRenderTarget>, ShadowMap::kMaxCascades> Targets;
			uint32_t Resolution = 0;

			Ref<RHISampler> Sampler;
			Ref<RHITexture> Empty;

			std::array<ShadowCascade, ShadowMap::kMaxCascades> Cascades{};
			uint32_t Rendered = 0;
			int LightIndex = -1;

			// Positional lights. A spot is one 2D map; a point is a depth cube
			// filled the same way a reflection probe is -- render a face into a
			// scratch target, copy it into the layer -- because the two
			// backends store a rendered image the opposite way up and
			// CopyToTextureLayer is the one place that is reconciled.
			std::array<RHI::Ref<RHIRenderTarget>, ShadowMap::kMaxLocal> SpotTargets;
			std::array<RHI::Ref<RHITexture>, ShadowMap::kMaxLocal> PointCubes;
			RHI::Ref<RHIRenderTarget> PointScratch;
			uint32_t LocalResolution = 0;
			uint32_t PointResolution = 0;

			RHI::Ref<RHITexture> EmptyCube;

			// Sized to the scene as lights are assigned (7an); cleared per frame.
			std::vector<LocalShadow> Assignments;

			bool Ready = false;
		};

		std::unique_ptr<ShadowData> s_Data;

		// The eight corners of a frustum slice, in world space.
		void SliceCorners(const Mat4& cameraTransform, float fovYRadians, float aspect,
						  float nearDistance, float farDistance, Vec3* out)
		{
			const float tanHalf = Math::Tan(fovYRadians * 0.5f);

			const Vec3 forward = Math::Normalize(Vec3(cameraTransform * Vec4(0, 0, -1, 0)));
			const Vec3 up = Math::Normalize(Vec3(cameraTransform * Vec4(0, 1, 0, 0)));
			const Vec3 right = Math::Normalize(Vec3(cameraTransform * Vec4(1, 0, 0, 0)));
			const Vec3 eye = Vec3(cameraTransform[3]);

			int index = 0;
			for (int end = 0; end < 2; end++)
			{
				const float distance = end == 0 ? nearDistance : farDistance;
				const float halfHeight = tanHalf * distance;
				const float halfWidth = halfHeight * aspect;
				const Vec3 centre = eye + forward * distance;

				out[index++] = centre - right * halfWidth - up * halfHeight;
				out[index++] = centre + right * halfWidth - up * halfHeight;
				out[index++] = centre + right * halfWidth + up * halfHeight;
				out[index++] = centre - right * halfWidth + up * halfHeight;
			}
		}
	}

	void ShadowMap::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<ShadowData>();
		s_Data->Device = &device;

		SamplerDesc sampler;
		// A comparison sampler: the hardware compares against the reference and
		// filters the *results*, which is a 2x2 percentage-closer filter for the
		// price of one fetch. Filtering the depths and then comparing would be
		// meaningless -- the average of two depths is not the average of two
		// answers.
		sampler.CompareEnable = true;
		sampler.Compare = kDepthCompare;
		sampler.MinFilter = FilterMode::Linear;
		sampler.MagFilter = FilterMode::Linear;
		sampler.MaxLod = 0.0f;
		// Outside the cascade is lit, never shadowed. A shadow that wraps or
		// smears its edge across the rest of the world is worse than none.
		sampler.WrapU = WrapMode::ClampToBorder;
		sampler.WrapV = WrapMode::ClampToBorder;
		sampler.WrapW = WrapMode::ClampToBorder;
		sampler.Border = BorderColor::OpaqueWhite;
		s_Data->Sampler = device.CreateSampler(sampler);

		{
			// Depth 1.0 is the far plane, which under LessOrEqual means every
			// comparison passes: fully lit.
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = Format::D32_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::DepthAttachment |
						 TextureUsage::TransferDst;
			desc.DebugName = "shadow.empty";

			s_Data->Empty = device.CreateTexture(desc);
			const float lit = 1.0f;
			if (s_Data->Empty)
				s_Data->Empty->Upload(&lit, sizeof(lit));
		}

		{
			// The same "everything passes" answer, in the shape a cube sampler
			// wants. Every declared binding has to be filled whether or not a
			// scene uses it.
			TextureDesc desc;
			desc.Width = 1;
			desc.Height = 1;
			desc.Layers = 6;
			desc.Type = TextureType::TextureCube;
			desc.Format = Format::D32_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::DepthAttachment |
						 TextureUsage::TransferDst;
			desc.DebugName = "shadow.emptycube";

			s_Data->EmptyCube = device.CreateTexture(desc);
			const float lit = 1.0f;
			if (s_Data->EmptyCube)
			{
				for (uint32_t face = 0; face < 6; face++)
					s_Data->EmptyCube->UploadLayer(&lit, sizeof(lit), face);
			}
		}

		s_Data->Ready = s_Data->Sampler != nullptr && s_Data->Empty != nullptr &&
						s_Data->EmptyCube != nullptr;
		if (s_Data->Ready)
			RV_CORE_INFO("Shadow maps ready (directional, up to {0} cascades)", kMaxCascades);
		else
			RV_CORE_ERROR("Shadow maps unavailable; nothing will cast");
	}

	void ShadowMap::Shutdown()
	{
		s_Data.reset();
	}

	bool ShadowMap::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	void ShadowMap::Invalidate()
	{
		if (s_Data)
		{
			s_Data->Rendered = 0;
			s_Data->LightIndex = -1;
			s_Data->Assignments.clear();
		}
	}

	void ShadowMap::Assign(uint32_t lightIndex, const LocalShadow& shadow)
	{
		if (!s_Data)
			return;
		if (lightIndex >= s_Data->Assignments.size())
			s_Data->Assignments.resize((size_t)lightIndex + 1);
		s_Data->Assignments[lightIndex] = shadow;
	}

	const LocalShadow& ShadowMap::GetAssignment(uint32_t lightIndex)
	{
		static const LocalShadow none;
		if (!s_Data || lightIndex >= s_Data->Assignments.size())
			return none;
		return s_Data->Assignments[lightIndex];
	}

	const ShadowCascade* ShadowMap::GetCascades()
	{
		return s_Data ? s_Data->Cascades.data() : nullptr;
	}

	uint32_t ShadowMap::GetResolution()
	{
		return s_Data ? s_Data->Resolution : 0;
	}

	int ShadowMap::GetLightIndex()
	{
		return s_Data ? s_Data->LightIndex : -1;
	}

	void ShadowMap::SetLightIndex(int index)
	{
		if (s_Data)
			s_Data->LightIndex = index;
	}

	bool ShadowMap::HasCascades()
	{
		return s_Data && s_Data->Rendered > 0;
	}

	uint32_t ShadowMap::GetCascadeCount()
	{
		return s_Data ? s_Data->Rendered : 0;
	}

	Ref<RHITexture> ShadowMap::GetCascadeTexture(uint32_t index)
	{
		if (!s_Data || index >= kMaxCascades || !s_Data->Targets[index])
			return nullptr;
		return s_Data->Targets[index]->GetDepthTexture();
	}

	Ref<RHISampler> ShadowMap::GetSampler()
	{
		return s_Data ? s_Data->Sampler : nullptr;
	}

	Ref<RHITexture> ShadowMap::GetEmptyTexture()
	{
		return s_Data ? s_Data->Empty : nullptr;
	}

	Ref<RHITexture> ShadowMap::GetEmptyCube()
	{
		return s_Data ? s_Data->EmptyCube : nullptr;
	}

	Ref<RHITexture> ShadowMap::GetSpotTexture(uint32_t slot)
	{
		if (!s_Data || slot >= kMaxLocal || !s_Data->SpotTargets[slot])
			return nullptr;
		return s_Data->SpotTargets[slot]->GetDepthTexture();
	}

	Ref<RHITexture> ShadowMap::GetPointTexture(uint32_t slot)
	{
		if (!s_Data || slot >= kMaxLocal)
			return nullptr;
		return s_Data->PointCubes[slot];
	}

	void ShadowMap::RenderSpot(RHICommandList& cmd, uint32_t slot, uint32_t resolution,
							   const Mat4& viewProjection, const CasterPass& casters)
	{
		if (!s_Data || !s_Data->Ready || !casters.Draw || slot >= kMaxLocal)
			return;

		resolution = Math::Clamp(resolution, 256u, 4096u);

		if (s_Data->LocalResolution != resolution)
		{
			for (auto& target : s_Data->SpotTargets)
				target.reset();
			s_Data->LocalResolution = resolution;
		}

		if (!s_Data->SpotTargets[slot])
		{
			RenderTargetDesc desc;
			desc.Width = resolution;
			desc.Height = resolution;
			desc.ColorAttachments.clear();
			desc.HasDepth = true;
			desc.DepthAttachment.Format = Format::D32_SFLOAT;
			desc.DepthSampled = true;
			desc.DebugName = "shadow.spot" + std::to_string(slot);

			s_Data->SpotTargets[slot] = s_Data->Device->CreateRenderTarget(desc);
		}

		if (!s_Data->SpotTargets[slot])
			return;

		RenderPassBeginInfo begin;
		begin.Target = s_Data->SpotTargets[slot].get();
		begin.ClearColor = false;
		begin.ClearDepth = true;
		begin.UseDepth = true;
		begin.Clear.Depth = kDepthClear;

		cmd.PushDebugGroup("Spot shadow");
		if (casters.Prepare)
			casters.Prepare(viewProjection);
		cmd.BeginRenderPass(begin);
		casters.Draw(viewProjection);
		cmd.EndRenderPass();
		cmd.PopDebugGroup();
	}

	void ShadowMap::RenderPoint(RHICommandList& cmd, uint32_t slot, uint32_t resolution,
								const Vec3& position, float farClip,
								const CasterPass& casters)
	{
		if (!s_Data || !s_Data->Ready || !casters.Draw || slot >= kMaxLocal)
			return;

		// Smaller than a spot map by default and deliberately: this is six of
		// them, and a point light's shadow is seen from every direction at once
		// so no single face carries much of the frame.
		resolution = Math::Clamp(resolution, 128u, 2048u);

		if (s_Data->PointResolution != resolution)
		{
			for (auto& cube : s_Data->PointCubes)
				cube.reset();
			s_Data->PointScratch.reset();
			s_Data->PointResolution = resolution;
		}

		if (!s_Data->PointCubes[slot])
		{
			TextureDesc desc;
			desc.Width = resolution;
			desc.Height = resolution;
			desc.Layers = CubeFaces::kFaceCount;
			desc.Type = TextureType::TextureCube;
			desc.Format = Format::D32_SFLOAT;
			desc.Usage = TextureUsage::Sampled | TextureUsage::DepthAttachment |
						 TextureUsage::TransferDst;
			desc.DebugName = "shadow.point" + std::to_string(slot);

			s_Data->PointCubes[slot] = s_Data->Device->CreateTexture(desc);
		}

		if (!s_Data->PointScratch)
		{
			RenderTargetDesc desc;
			desc.Width = resolution;
			desc.Height = resolution;
			desc.ColorAttachments.clear();
			desc.HasDepth = true;
			desc.DepthAttachment.Format = Format::D32_SFLOAT;
			// Sampled, because the copy reads it.
			desc.DepthSampled = true;
			desc.DebugName = "shadow.pointface";

			s_Data->PointScratch = s_Data->Device->CreateRenderTarget(desc);
		}

		if (!s_Data->PointCubes[slot] || !s_Data->PointScratch)
			return;

		const Mat4 projection = ReflectionProbe::FaceProjection(kPointShadowNear, farClip);
		const Ref<RHITexture> face = s_Data->PointScratch->GetDepthTexture();

		cmd.PushDebugGroup("Point shadow");

		for (uint32_t i = 0; i < CubeFaces::kFaceCount; i++)
		{
			RenderPassBeginInfo begin;
			begin.Target = s_Data->PointScratch.get();
			begin.ClearColor = false;
			begin.ClearDepth = true;
			begin.UseDepth = true;
			begin.Clear.Depth = kDepthClear;

			// The same face basis a reflection probe captures with. Two
			// features that disagreed about which way a cube face points would
			// be two features that could not be debugged together. Worked out
			// before the pass opens, because the preparation needs it there.
			const Mat4 viewProjection =
				projection * Math::Inverse(ReflectionProbe::FaceTransform(i, position));

			if (casters.Prepare)
				casters.Prepare(viewProjection);

			cmd.BeginRenderPass(begin);
			casters.Draw(viewProjection);
			cmd.EndRenderPass();

			cmd.CopyToTextureLayer(face, s_Data->PointCubes[slot], i);
		}

		cmd.PopDebugGroup();
	}

	void ShadowMap::ComputeCascades(const Mat4& cameraTransform,
									float fovYRadians, float aspect,
									float nearClip, float farClip,
									const Vec3& lightDirection,
									uint32_t count, uint32_t resolution,
									float lambda, bool flipLookupY,
									ShadowCascade* out)
	{
		if (!out || count == 0)
			return;

		count = Math::Min(count, kMaxCascades);
		resolution = Math::Max(resolution, 16u);

		// The camera's own near plane is far too close to split logarithmically
		// from -- at 0.01 the first cascade would be centimetres deep. Shadows
		// start where they are visible, not where geometry does.
		const float start = Math::Max(nearClip, 0.1f);
		const float end = Math::Max(farClip, start + 0.1f);
		const float ratio = end / start;

		Vec3 direction = lightDirection;
		if (Math::Dot(direction, direction) < 1e-8f)
			direction = Vec3(0.0f, -1.0f, 0.0f);
		direction = Math::Normalize(direction);

		float previous = start;

		for (uint32_t i = 0; i < count; i++)
		{
			// The practical split scheme: the logarithmic one distributes
			// texels correctly and puts almost nothing in the far cascades; the
			// uniform one does the reverse. Every engine blends them.
			const float fraction = (float)(i + 1) / (float)count;
			const float logarithmic = start * Math::Pow(ratio, fraction);
			const float uniform = start + (end - start) * fraction;
			const float split = lambda * logarithmic + (1.0f - lambda) * uniform;

			Vec3 corners[8];
			SliceCorners(cameraTransform, fovYRadians, aspect, previous, split, corners);

			// Fit a sphere, not the corners.
			//
			// A box fitted to the corners changes size as the camera turns,
			// because the extent of a rotating frustum in a fixed frame is not
			// constant. That makes the shadow projection breathe, and every
			// edge in the cascade crawls. A sphere's radius depends only on the
			// split distances and the field of view, so it is the same however
			// the camera is pointed -- which is the entire trick.
			Vec3 centre(0.0f);
			for (const Vec3& corner : corners)
				centre += corner;
			centre /= 8.0f;

			float radius = 0.0f;
			for (const Vec3& corner : corners)
				radius = Math::Max(radius, Math::Length(corner - centre));

			// Rounded up, so a sub-texel change in the frustum cannot change
			// the radius and rescale the whole projection.
			radius = Math::Ceil(radius * 16.0f) / 16.0f;

			const float texelWorldSize = (2.0f * radius) / (float)resolution;

			// Far enough back that casters between the light and the slice are
			// still inside the frustum. Without the margin, an object above the
			// cascade fails to cast into it.
			const float margin = Math::Max(radius, 1.0f) * 2.0f;

			const Vec3 eye = centre - direction * (radius + margin);
			// Any up vector not parallel to the light does; a directional light
			// has no roll worth preserving.
			const Vec3 up = Math::Abs(direction.y) > 0.99f
							   ? Vec3(0.0f, 0.0f, 1.0f)
							   : Vec3(0.0f, 1.0f, 0.0f);

			Mat4 view = Math::LookAt(eye, centre, up);
			Mat4 projection = Math::Orthographic(-radius, radius, -radius, radius,
											  0.0f, radius * 2.0f + margin);

			// Snap to the texel grid.
			//
			// The projection is stable in size after the sphere fit, but it
			// still slides continuously as the camera moves, so every shadow
			// edge crosses texel boundaries at a different sub-texel offset each
			// frame and shimmers. Rounding the projection's origin to a whole
			// texel makes the sampled grid move in texel steps instead.
			{
				const Mat4 shadow = projection * view;
				Vec4 origin = shadow * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
				origin *= (float)resolution * 0.5f;

				const Vec4 rounded = Math::Round(origin);
				Vec4 offset = (rounded - origin) * (2.0f / (float)resolution);
				offset.z = 0.0f;
				offset.w = 0.0f;

				projection[3] += offset;
			}

			ShadowCascade& cascade = out[i];
			cascade.ViewProjection = projection * view;
			cascade.SplitDepth = split;
			cascade.TexelWorldSize = texelWorldSize;

			// Clip space to lookup coordinates. The Y scale is where the two
			// backends differ: a depth target's first row is the top of the
			// rendered image on Vulkan and the bottom on OpenGL, so one of them
			// has to read it upside down. Folding it into the matrix keeps the
			// difference on the CPU, where it is visible, rather than in a
			// branch in the shader where it is not.
			Mat4 bias(1.0f);
			bias[0][0] = 0.5f;
			bias[1][1] = flipLookupY ? -0.5f : 0.5f;
			bias[3][0] = 0.5f;
			bias[3][1] = 0.5f;
			// Depth already arrives in [0, 1]: this engine's projections use
			// GLM_FORCE_DEPTH_ZERO_TO_ONE, so no z remap belongs here.

			cascade.LookupMatrix = bias * cascade.ViewProjection;

			previous = split;
		}
	}

	void ShadowMap::Render(RHICommandList& cmd, const ShadowCascade* cascades,
						   uint32_t count, uint32_t resolution, const CasterPass& casters)
	{
		if (!s_Data || !s_Data->Ready || !cascades || !casters.Draw || count == 0)
			return;

		count = Math::Min(count, kMaxCascades);
		resolution = Math::Clamp(resolution, 256u, 8192u);

		// Reallocated only when the resolution changes, which is an editor
		// action rather than a per-frame one.
		if (s_Data->Resolution != resolution)
		{
			for (auto& target : s_Data->Targets)
				target.reset();
			s_Data->Resolution = resolution;
		}

		for (uint32_t i = 0; i < count; i++)
		{
			if (!s_Data->Targets[i])
			{
				RenderTargetDesc desc;
				desc.Width = resolution;
				desc.Height = resolution;
				// No colour attachment at all: a shadow map is depth, and a
				// colour buffer would be bandwidth spent on nothing.
				desc.ColorAttachments.clear();
				desc.HasDepth = true;
				desc.DepthAttachment.Format = Format::D32_SFLOAT;
				desc.DepthSampled = true;
				desc.DebugName = "shadow.cascade" + std::to_string(i);

				s_Data->Targets[i] = s_Data->Device->CreateRenderTarget(desc);
			}

			if (!s_Data->Targets[i])
				continue;

			RenderPassBeginInfo begin;
			begin.Target = s_Data->Targets[i].get();
			begin.ClearColor = false;
			begin.ClearDepth = true;
			begin.UseDepth = true;
			begin.Clear.Depth = kDepthClear;

			cmd.PushDebugGroup("Shadow cascade");
			if (casters.Prepare)
				casters.Prepare(cascades[i].ViewProjection);

			cmd.BeginRenderPass(begin);

			casters.Draw(cascades[i].ViewProjection);

			cmd.EndRenderPass();
			cmd.PopDebugGroup();
		}

		for (uint32_t i = 0; i < count; i++)
			s_Data->Cascades[i] = cascades[i];

		s_Data->Rendered = count;
	}
}
