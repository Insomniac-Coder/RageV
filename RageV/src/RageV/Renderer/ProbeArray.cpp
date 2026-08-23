#include <rvpch.h>
#include "ProbeArray.h"
#include "ReflectionProbe.h"
#include "EnvironmentIBL.h"
#include "Cubemap.h"
#include "RageV/Math/Math.h"
#include <array>

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// What a slot is currently holding, so a frame can tell whether it has
		// to refill one. A baked probe fills its slot once and is skipped every
		// frame after; a realtime probe refills on the frames its capture
		// completes a round, which is one frame in six.
		struct Occupant
		{
			const RHITexture* Source = nullptr;
			uint64_t Generation = 0;
			bool Filled = false;
		};

		struct ArrayData
		{
			RHIDevice* Device = nullptr;

			Ref<RHITexture> Radiance;
			Ref<RHITexture> Irradiance;

			uint32_t FaceSize = 0;
			uint32_t SlotCount = 0;
			std::array<Occupant, ProbeArray::kSlots> Slots{};
		};

		std::unique_ptr<ArrayData> s_Data;

		void Forget()
		{
			if (s_Data)
				s_Data->Slots.fill(Occupant{});
		}

		// A cube array, sized for `slots` cubes of `faceSize`.
		Ref<RHITexture> CreateArray(uint32_t faceSize, uint32_t slots, uint32_t levels,
									const char* name)
		{
			TextureDesc desc;
			desc.Width = faceSize;
			desc.Height = faceSize;
			desc.Layers = slots * CubeFaces::kFaceCount;
			desc.Type = TextureType::TextureCubeArray;
			desc.Format = Format::R16G16B16A16_SFLOAT;
			// TransferDst because every slice arrives through
			// CopyToTextureLayer; there is no path that renders into one
			// directly, and deliberately so -- see ReflectionProbe's note on
			// why the two backends disagree about layered rendering.
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;
			desc.MipLevels = levels;
			desc.DebugName = name;

			return s_Data->Device->CreateTexture(desc);
		}
	}

	void ProbeArray::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<ArrayData>();
		s_Data->Device = &device;
	}

	void ProbeArray::Shutdown()
	{
		s_Data.reset();
	}

	void ProbeArray::ClearCache()
	{
		if (!s_Data)
			return;

		s_Data->Radiance.reset();
		s_Data->Irradiance.reset();
		s_Data->FaceSize = 0;
		Forget();
	}

	bool ProbeArray::IsReady()
	{
		return s_Data && s_Data->Radiance && s_Data->Irradiance;
	}

	Ref<RHITexture> ProbeArray::GetRadiance()
	{
		return s_Data ? s_Data->Radiance : nullptr;
	}

	Ref<RHITexture> ProbeArray::GetIrradiance()
	{
		return s_Data ? s_Data->Irradiance : nullptr;
	}

	uint32_t ProbeArray::GetFaceSize()
	{
		return s_Data ? s_Data->FaceSize : 0;
	}

	void ProbeArray::Begin(uint32_t faceSize, uint32_t slots)
	{
		if (!s_Data || !s_Data->Device)
			return;

		faceSize = Math::Clamp(faceSize, 16u, 1024u);
		slots = Math::Clamp(slots, 1u, kSlots);

		if (s_Data->Radiance && s_Data->Irradiance &&
			s_Data->FaceSize == faceSize && s_Data->SlotCount == slots)
			return;

		const uint32_t levels = EnvironmentIBL::LevelsFor(faceSize);
		if (levels == 0)
		{
			// Cannot happen at the clamp above, and checked anyway: a zero
			// there would make a mip-less array whose roughest reflections
			// read as mirrors.
			RV_CORE_WARN("Probe array of {0} px faces is too small to filter", faceSize);
			return;
		}

		s_Data->Radiance = CreateArray(faceSize, slots, levels, "probes.radiance");
		s_Data->Irradiance = CreateArray(kIrradianceSize, slots, 1, "probes.irradiance");
		s_Data->FaceSize = faceSize;
		s_Data->SlotCount = slots;

		// Everything the old arrays held is gone with them, including slot 0.
		Forget();

		if (s_Data->Radiance && s_Data->Irradiance)
		{
			RV_CORE_INFO("Probe arrays ready ({0} slots, {1} px radiance faces, "
						 "{2} roughness levels)", slots, faceSize, levels);
		}
	}

	void ProbeArray::SetSky(RHICommandList& cmd, const Ref<RHITexture>& sky)
	{
		if (!IsReady() || !sky)
			return;

		Occupant& slot = s_Data->Slots[kSkySlot];

		// The sky has no generation of its own: it is a cube that either is or
		// is not the one already convolved. A gradient sky rebuilt from new
		// colours is a *different* texture, so the pointer is enough here in a
		// way it is not for a probe.
		if (slot.Filled && slot.Source == sky.get())
			return;

		const bool radiance = EnvironmentIBL::PrefilterInto(cmd, sky, s_Data->Radiance, kSkySlot);
		const bool irradiance = EnvironmentIBL::IrradianceInto(cmd, sky, s_Data->Irradiance, kSkySlot);

		slot.Source = sky.get();
		slot.Generation = 0;
		slot.Filled = radiance && irradiance;
	}

	bool ProbeArray::SetProbe(RHICommandList& cmd, uint32_t slot, const ReflectionProbe& probe)
	{
		if (!IsReady() || slot == kSkySlot || slot >= s_Data->SlotCount)
			return false;

		const Ref<RHITexture>& cube = probe.GetCube();
		if (!cube || !probe.IsComplete())
			return false;

		Occupant& occupant = s_Data->Slots[slot];

		// Both halves of the test matter. The pointer catches a slot changing
		// hands -- probes are assigned slots by their order in the scene, so
		// deleting one shifts everything after it -- and the generation catches
		// a realtime probe re-capturing into a texture it already owned.
		if (occupant.Filled && occupant.Source == cube.get() &&
			occupant.Generation == probe.GetGeneration())
		{
			return true;
		}

		const bool radiance = EnvironmentIBL::PrefilterInto(cmd, cube, s_Data->Radiance, slot);
		const bool irradiance = EnvironmentIBL::IrradianceInto(cmd, cube, s_Data->Irradiance, slot);

		occupant.Source = cube.get();
		occupant.Generation = probe.GetGeneration();
		occupant.Filled = radiance && irradiance;

		return occupant.Filled;
	}
}
