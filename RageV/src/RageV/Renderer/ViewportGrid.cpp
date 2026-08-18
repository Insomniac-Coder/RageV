#include <rvpch.h>
#include "ViewportGrid.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		struct GridParams
		{
			Mat4 InvViewProjection{ 1.0f };
			Vec4 Line{ 0.0f };    // rgb, a = opacity of a major line
			Vec4 AxisX{ 0.0f };   // rgb, a = minor spacing, in world units
			Vec4 AxisZ{ 0.0f };   // rgb, a = minor cells per major cell
		};

		// Same budget as the sky, and the same warning applies: 128 bytes is
		// what every implementation guarantees, and the next vec4 is the last
		// one that fits.
		static_assert(sizeof(GridParams) == 112, "Grid push constants must stay under 128 bytes");

		struct GridData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader> Shader;
			Ref<RHIPipeline> Pipeline;

			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
		// Sample count, which has to equal the target's. A pipeline whose
		// rasterizationSamples disagrees with the attachment it draws into is
		// undefined behaviour rather than an error, so it travels with the
		// formats and gets compared with them.
			uint32_t TargetSamples = 1;

		// Where the scene writes its motion vectors, or Undefined for a pass
		// that has no velocity attachment bound.
		//
		// One shape for every pass that writes the scene target -- the scene
		// pass and the overlay both bind {colour, velocity}. 7q paid for the
		// alternative: pipelines built for one target shape being bound into a
		// pass with another is undefined behaviour rather than an error.
			Format TargetVelocity = Format::Undefined;
			Format TargetNormal = Format::Undefined;
			// Traced indirect diffuse (7av). Undefined everywhere the
			// scene target does not carry it.
			Format TargetIndirect = Format::Undefined;
			bool PipelineDirty = true;

			bool Ready = false;
		};

		std::unique_ptr<GridData> s_Data;
	}

	void ViewportGrid::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<GridData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/grid.rvshader");
		if (!compiled)
		{
			RV_CORE_ERROR("ViewportGrid: failed to compile assets/shaders/grid.rvshader");
			return;
		}

		s_Data->Shader = device.CreateShader(*compiled);
		s_Data->Ready = s_Data->Shader != nullptr;

		if (!s_Data->Ready)
			RV_CORE_ERROR("ViewportGrid unavailable; the editor's ground grid will not draw");
	}

	void ViewportGrid::Shutdown()
	{
		s_Data.reset();
	}

	void ViewportGrid::SetTargetFormats(Format color, Format depth, uint32_t samples,
									   Format velocity, Format normal,
									   Format indirect)
	{
		if (!s_Data)
			return;

		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth &&
			s_Data->TargetSamples == samples &&
			s_Data->TargetVelocity == velocity && s_Data->TargetNormal == normal &&
			s_Data->TargetIndirect == indirect && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetSamples = samples;
		s_Data->TargetVelocity = velocity;
		s_Data->TargetNormal = normal;
		s_Data->TargetIndirect = indirect;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	bool ViewportGrid::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	Mat4 ViewportGrid::BuildInverseViewProjection(const Mat4& projection, const Mat4& cameraTransform)
	{
		// The camera's translation stays, unlike the sky's: a grid is a place in
		// the world rather than a direction, and dropping the translation would
		// leave it stuck to the camera.
		return Math::Inverse(projection * Math::Inverse(cameraTransform));
	}

	bool ViewportGrid::PlaneDepthAt(const Mat4& inverseViewProjection,
									float ndcX, float ndcY, float& depth)
	{
		// The plane y = 0, carried into clip space: the second row of the
		// inverse, which is the y component of each of its columns. See
		// grid.rvshader for why a plane rather than a ray.
		const Vec4 plane(inverseViewProjection[0].y, inverseViewProjection[1].y,
						 inverseViewProjection[2].y, inverseViewProjection[3].y);

		if (plane.z == 0.0f)
			return false;   // the view ray lies in the plane; no single depth

		const float solved = -(plane.x * ndcX + plane.y * ndcY + plane.w) / plane.z;

		// Written the same way round as the shader, and for the same reason: a
		// NaN fails every comparison, so this rejects it and the negated form
		// would not. The ceiling is well past 1: NDC depth for a point in front
		// of the camera asymptotes just above it, so anything meaningfully
		// beyond is the plane going edge-on rather than a place in the world.
		constexpr float kDepthCeiling = 2.0f;
		if (!(solved >= 0.0f && solved < kDepthCeiling))
			return false;

		const Vec4 hit = inverseViewProjection * Vec4(ndcX, ndcY, solved, 1.0f);
		if (hit.w <= 0.0f)
			return false;   // behind the viewer, or past the horizon

		depth = Math::Min(solved, 1.0f);
		return true;
	}

	void ViewportGrid::Draw(const Camera& camera, const Mat4& cameraTransform,
							const ViewportGridSettings& settings)
	{
		if (!s_Data || !s_Data->Ready)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		if (s_Data->PipelineDirty || !s_Data->Pipeline)
		{
			GraphicsPipelineDesc desc;
			desc.Name = "ViewportGrid";
			desc.Shader = s_Data->Shader;
			desc.Topology = PrimitiveTopology::TriangleList;
			desc.Rasterizer.Cull = CullMode::None;
			desc.Blend = BlendPreset::AlphaBlend;

			// Tested, not written -- see the header of grid.rvshader. The test
			// is against the depth the fragment shader computes for the plane,
			// which is the whole point of computing it.
			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = false;
			desc.DepthStencil.DepthCompare = CompareOp::LessOrEqual;

			desc.ColorFormats = { s_Data->TargetColor };
			desc.Samples = s_Data->TargetSamples;
			if (s_Data->TargetVelocity != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetVelocity);
			if (s_Data->TargetNormal != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetNormal);
			if (s_Data->TargetIndirect != Format::Undefined)
				desc.ColorFormats.push_back(s_Data->TargetIndirect);
			desc.DepthFormat = s_Data->TargetDepth;

			s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);
			s_Data->PipelineDirty = false;
		}

		if (!s_Data->Pipeline)
			return;

		GridParams params;
		params.InvViewProjection =
			BuildInverseViewProjection(camera.GetProjection(), cameraTransform);
		params.Line = Vec4(settings.LineColor, Math::Clamp(settings.Opacity, 0.0f, 1.0f));
		params.AxisX = Vec4(settings.AxisXColor, Math::Max(settings.Spacing, 1e-3f));
		params.AxisZ = Vec4(settings.AxisZColor, Math::Max(settings.MajorEvery, 2.0f));

		cmd->BindPipeline(s_Data->Pipeline);
		cmd->PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd->Draw(3);
	}
}
