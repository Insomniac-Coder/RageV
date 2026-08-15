#include "rvpch.h"
#include "AutoExposure.h"

#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "RageV/Core/Log.h"

#include <cmath>
#include <memory>

namespace RageV
{
	using namespace RHI;

	namespace
	{
		// One per bin, and the histogram shader's work group is 16x16 = 256 so
		// that clearing and flushing are one invocation each with no loop.
		constexpr uint32_t kBins = 256;

		// Matches ExposureState in luminance_average.rvshader.
		struct GpuExposureState
		{
			float AdaptedLog = 0.0f;
			float Exposure = 1.0f;
			float MeasuredLog = 0.0f;
			float Pad = 0.0f;
		};

		struct HistogramParams
		{
			float Range[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		};

		struct AverageParams
		{
			float Range[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float Window[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		};

		struct Data
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader> HistogramShader;
			Ref<RHIShader> AverageShader;
			Ref<RHIComputePipeline> HistogramPipeline;
			Ref<RHIComputePipeline> AveragePipeline;

			Ref<RHISampler> Sampler;

			// One set and one params buffer per pipeline, rebound each call.
			// There is exactly one auto-exposure dispatch per frame chain and
			// the chains are drawn one after another on one command list, so a
			// set per chain would be a set nobody was using.
			Ref<RHIResourceSet> HistogramSet;
			Ref<RHIResourceSet> AverageSet;
			Ref<RHIBuffer> HistogramParamsBuffer;
			Ref<RHIBuffer> AverageParamsBuffer;

			bool Ready = false;
		};

		std::unique_ptr<Data> s_Data;
	}

	// --- the state one chain carries -----------------------------------------

	void ExposureState::Prepare(RHIDevice& device)
	{
		if (m_Histogram && m_Exposure)
			return;

		BufferDesc histogram;
		histogram.Size = kBins * sizeof(uint32_t);
		histogram.Usage = BufferUsage::Storage | BufferUsage::TransferDst;
		histogram.Memory = MemoryDomain::DeviceLocal;
		histogram.DebugName = "AutoExposure.histogram";
		m_Histogram = device.CreateBuffer(histogram);

		// Zeroed at creation, because the reduce pass clears the bins at the
		// *end* of each frame -- so every frame but the first starts clean and
		// the first would otherwise start on whatever the allocator handed
		// back.
		if (m_Histogram)
		{
			const std::vector<uint32_t> zeroes(kBins, 0u);
			m_Histogram->Upload(zeroes.data(), zeroes.size() * sizeof(uint32_t));
		}

		BufferDesc exposure;
		exposure.Size = sizeof(GpuExposureState);
		exposure.Usage = BufferUsage::Storage | BufferUsage::TransferDst;
		exposure.Memory = MemoryDomain::DeviceLocal;
		exposure.DebugName = "AutoExposure.state";
		m_Exposure = device.CreateBuffer(exposure);

		// Exposure 1 rather than 0, so that a chain whose very first frame
		// somehow reads this before the reduce pass writes it renders the
		// picture it would have rendered with no auto exposure at all --
		// rather than a black one.
		if (m_Exposure)
		{
			const GpuExposureState initial;
			m_Exposure->Upload(&initial, sizeof(initial));
		}

		m_Dispatches = 0;
	}

	void ExposureState::Release()
	{
		m_Histogram = nullptr;
		m_Exposure = nullptr;
		m_Dispatches = 0;
	}

	// --- the dispatches -------------------------------------------------------

	void AutoExposure::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<Data>();
		s_Data->Device = &device;

		if (!device.GetCaps().SupportsCompute)
		{
			RV_CORE_WARN("No compute support; auto exposure will fall back to the "
						 "manual exposure on every camera that asks for it");
			return;
		}

		ShaderCompiler::Init();

		auto histogram = ShaderCompiler::CompileFromFile(
			"assets/shaders/luminance_histogram.rvshader");
		auto average = ShaderCompiler::CompileFromFile(
			"assets/shaders/luminance_average.rvshader");

		if (!histogram || !average)
		{
			RV_CORE_ERROR("AutoExposure: failed to compile the luminance shaders");
			return;
		}

		s_Data->HistogramShader = device.CreateShader(*histogram);
		s_Data->AverageShader = device.CreateShader(*average);
		if (!s_Data->HistogramShader || !s_Data->AverageShader)
			return;

		ComputePipelineDesc histogramDesc;
		histogramDesc.Name = "AutoExposure.histogram";
		histogramDesc.Shader = s_Data->HistogramShader;
		s_Data->HistogramPipeline = device.CreateComputePipeline(histogramDesc);

		ComputePipelineDesc averageDesc;
		averageDesc.Name = "AutoExposure.average";
		averageDesc.Shader = s_Data->AverageShader;
		s_Data->AveragePipeline = device.CreateComputePipeline(averageDesc);

		if (!s_Data->HistogramPipeline || !s_Data->AveragePipeline)
			return;

		SamplerDesc sampler;
		// Clamped and unfiltered. The histogram samples texel centres and wants
		// the texel that is there, not a blend of it with its neighbours --
		// filtering would quietly narrow the distribution this exists to keep.
		sampler.WrapU = WrapMode::ClampToEdge;
		sampler.WrapV = WrapMode::ClampToEdge;
		sampler.WrapW = WrapMode::ClampToEdge;
		sampler.MinFilter = FilterMode::Nearest;
		sampler.MagFilter = FilterMode::Nearest;
		sampler.Mipmap = MipmapMode::Nearest;
		sampler.MaxLod = 0.0f;
		s_Data->Sampler = device.CreateSampler(sampler);

		BufferDesc histogramParams;
		histogramParams.Size = sizeof(HistogramParams);
		histogramParams.Usage = BufferUsage::Uniform;
		histogramParams.Memory = MemoryDomain::HostVisible;
		histogramParams.DebugName = "AutoExposure.histogramparams";
		s_Data->HistogramParamsBuffer = device.CreateBuffer(histogramParams);

		BufferDesc averageParams;
		averageParams.Size = sizeof(AverageParams);
		averageParams.Usage = BufferUsage::Uniform;
		averageParams.Memory = MemoryDomain::HostVisible;
		averageParams.DebugName = "AutoExposure.averageparams";
		s_Data->AverageParamsBuffer = device.CreateBuffer(averageParams);

		s_Data->HistogramSet = device.CreateResourceSet(s_Data->HistogramPipeline, 0);
		s_Data->AverageSet = device.CreateResourceSet(s_Data->AveragePipeline, 0);

		s_Data->Ready = s_Data->HistogramSet && s_Data->AverageSet
					 && s_Data->HistogramParamsBuffer && s_Data->AverageParamsBuffer
					 && s_Data->Sampler;

		if (s_Data->Ready)
			RV_CORE_INFO("Auto exposure ready ({0} bins)", kBins);
	}

	void AutoExposure::Shutdown()
	{
		s_Data.reset();
	}

	bool AutoExposure::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	float AutoExposure::BlendWeight(float rate, float deltaSeconds)
	{
		// Nothing moves in no time, and a negative delta is a caller bug rather
		// than a reason to run the adaptation backwards.
		if (deltaSeconds <= 0.0f)
			return 0.0f;

		// A rate of zero means "do not adapt", which is a legitimate setting
		// and not a division by anything.
		if (rate <= 0.0f)
			return 0.0f;

		// The exponential form, not `rate * dt`. Ten steps of 10 ms land where
		// one step of 100 ms does, which the linear approximation does not --
		// and that failure is invisible until somebody compares two machines.
		return 1.0f - std::exp(-rate * deltaSeconds);
	}

	void AutoExposure::Dispatch(RHICommandList& cmd, const Ref<RHITexture>& scene,
								uint32_t width, uint32_t height,
								ExposureState& state, const Params& params)
	{
		if (!IsReady() || !scene || width == 0 || height == 0)
			return;

		state.Prepare(*s_Data->Device);
		if (!state.Histogram() || !state.Exposure())
			return;

		const float minLog = params.MinLogLuminance;
		const float range = params.MaxLogLuminance > params.MinLogLuminance
						  ? params.MaxLogLuminance - params.MinLogLuminance
						  : 1.0f;

		// --- 1. the histogram --------------------------------------------------
		{
			HistogramParams uniforms;
			uniforms.Range[0] = minLog;
			uniforms.Range[1] = 1.0f / range;
			uniforms.Range[2] = (float)width;
			uniforms.Range[3] = (float)height;
			s_Data->HistogramParamsBuffer->Upload(&uniforms, sizeof(uniforms));

			s_Data->HistogramSet->SetTexture(0, scene, s_Data->Sampler);
			s_Data->HistogramSet->SetStorageBuffer(1, state.Histogram());
			s_Data->HistogramSet->SetUniformBuffer(2, s_Data->HistogramParamsBuffer);
			s_Data->HistogramSet->Commit();

			cmd.BindComputePipeline(s_Data->HistogramPipeline);
			cmd.BindResourceSet(0, s_Data->HistogramSet);

			// 16x16 per group, rounded up. The shader bounds-checks its tail,
			// because the grid is in whole groups and the image rarely is.
			cmd.Dispatch((width + 15) / 16, (height + 15) / 16);
		}

		// Every bin has to be written before any of them is read. Without this
		// the reduce pass meters a partial frame, which looks like exposure
		// that flickers under load and nothing else.
		cmd.BufferBarrier(state.Histogram(), BufferSync::ComputeWrite,
						  BufferSync::ComputeRead);

		// --- 2. the reduction and the adaptation --------------------------------
		{
			// 1.0 on a chain's first frame: adopt the measurement rather than
			// slide toward it. See ExposureState::HasAdapted.
			const float rate = params.SpeedUp;
			const float weight = state.HasAdapted()
							   ? BlendWeight(rate, params.DeltaSeconds)
							   : 1.0f;

			AverageParams uniforms;
			uniforms.Range[0] = minLog;
			uniforms.Range[1] = range;
			uniforms.Range[2] = weight;
			uniforms.Range[3] = params.MiddleGrey;
			uniforms.Window[0] = params.LowPercentile;
			uniforms.Window[1] = params.HighPercentile;
			uniforms.Window[2] = params.MinExposure;
			uniforms.Window[3] = params.MaxExposure;
			s_Data->AverageParamsBuffer->Upload(&uniforms, sizeof(uniforms));

			s_Data->AverageSet->SetStorageBuffer(0, state.Histogram());
			s_Data->AverageSet->SetStorageBuffer(1, state.Exposure());
			s_Data->AverageSet->SetUniformBuffer(2, s_Data->AverageParamsBuffer);
			s_Data->AverageSet->Commit();

			cmd.BindComputePipeline(s_Data->AveragePipeline);
			cmd.BindResourceSet(0, s_Data->AverageSet);
			cmd.Dispatch(1);

			state.MarkAdapted();
		}

		// And the tone mapping pass reads the result as a fragment shader.
		cmd.BufferBarrier(state.Exposure(), BufferSync::ComputeWrite,
						  BufferSync::ShaderRead);
	}
}
