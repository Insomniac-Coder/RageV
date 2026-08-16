#include <rvpch.h>
#include "Material.h"
#include "TextureLoader.h"
#include "TextureHeap.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// One sampler for every material that does not ask for something else.
		//
		// A sampler is immutable filtering state, and every material was
		// building an identical one -- which cost an object each and, once
		// draws were batched by bound state, made every material's key unique.
		// A thousand props that share five default textures then shared
		// nothing, and the lit pass batched exactly nothing while the shadow
		// passes batched fine. Identical state has to be the same object for a
		// batch key to see it as identical.
		Ref<RHISampler> s_SharedSampler;

		Ref<RHISampler> SharedSampler(RHIDevice& device)
		{
			if (!s_SharedSampler)
			{
				SamplerDesc desc;
				desc.MaxAnisotropy = device.GetCaps().SupportsAnisotropy ? 8.0f : 1.0f;
				s_SharedSampler = device.CreateSampler(desc);
			}
			return s_SharedSampler;
		}
	}

	void Material::ReleaseShared()
	{
		s_SharedSampler.reset();
	}

	Material::Material(RHIDevice& device, std::string name)
		: m_Device(device), m_Name(std::move(name))
	{
		m_Sampler = SharedSampler(device);

		const uint32_t frames = device.GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		for (uint32_t i = 0; i < frames; i++)
		{
			BufferDesc desc;
			desc.Size = sizeof(MaterialParams);
			desc.Usage = BufferUsage::Uniform;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = m_Name + ".params." + std::to_string(i);
			m_ParamBuffers[i] = device.CreateBuffer(desc);
		}
		m_FrameDirty.assign(frames, true);
	}

	namespace
	{
		void AssignMap(Ref<RHITexture>& slot, const Ref<RHITexture>& texture,
					   int32_t& flags, MaterialMap bit)
		{
			slot = texture;
			if (texture)
				flags |= bit;
			else
				flags &= ~bit;
		}
	}

	void Material::SetBaseColorMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_BaseColor, texture, m_Params.MapFlags, MaterialMap_BaseColor);
		Invalidate();
	}

	void Material::SetNormalMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Normal, texture, m_Params.MapFlags, MaterialMap_Normal);
		Invalidate();
	}

	void Material::SetOcclusionMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Occlusion, texture, m_Params.MapFlags, MaterialMap_Occlusion);
		Invalidate();
	}

	void Material::SetEmissiveMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Emissive, texture, m_Params.MapFlags, MaterialMap_Emissive);
		Invalidate();
	}

	void Material::SetRoughnessMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Roughness, texture, m_Params.MapFlags, MaterialMap_Roughness);
		Invalidate();
	}

	void Material::SetMetallicMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Metallic, texture, m_Params.MapFlags, MaterialMap_Metallic);
		Invalidate();
	}

	void Material::SetSpecularMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Specular, texture, m_Params.MapFlags, MaterialMap_Specular);
		Invalidate();
	}

	void Material::SetHeightMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Height, texture, m_Params.MapFlags, MaterialMap_Height);
		Invalidate();
	}

	void Material::EnsureResources(const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		if (m_Built)
			return;

		const uint32_t frames = m_Device.GetFramesInFlight();
		m_Sets.clear();
		for (uint32_t i = 0; i < frames; i++)
			m_Sets.push_back(m_Device.CreateResourceSet(pipeline, set));

		m_FrameDirty.assign(frames, true);
		m_Built = true;
	}

	void Material::Invalidate()
	{
		// Every frame's copy, not just the current one: each frame in flight
		// has its own buffer and set, and a change has to reach all of them.
		m_FrameDirty.assign(Math::Max<size_t>(m_FrameDirty.size(), m_ParamBuffers.size()), true);
	}

	void Material::Bind(RHICommandList& commandList, const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		EnsureResources(pipeline, set);

		const uint32_t frame = m_Device.GetFrameIndex();
		auto& resourceSet = m_Sets[frame];

		// Only when something actually changed. Rewriting a descriptor set that
		// is already bound to a command buffer is a use-after-bind hazard, and
		// binding one material for several objects -- or drawing one scene into
		// two viewports -- did exactly that on every draw after the first.
		if (m_FrameDirty[frame])
		{
			m_ParamBuffers[frame]->Upload(&m_Params, sizeof(MaterialParams));
			resourceSet->SetUniformBuffer(0, m_ParamBuffers[frame], 0, sizeof(MaterialParams));

			// A sampler left unwritten is a validation error even when the
			// shader will not read it, so absent maps bind a neutral 1x1.
			resourceSet->SetTexture(1, m_BaseColor         ? m_BaseColor         : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(2, m_Normal            ? m_Normal            : TextureLoader::FlatNormal(m_Device), m_Sampler);
			// Binding 3 was the packed metallic-roughness map. glTF is the only
			// thing that produced one and the import splits it now, so nothing
			// fills this -- but the shader still declares the slot, and an
			// unwritten binding is a validation error rather than an unused one.
			resourceSet->SetTexture(3, TextureLoader::White(m_Device), m_Sampler);
			resourceSet->SetTexture(4, m_Occlusion         ? m_Occlusion         : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(5, m_Emissive          ? m_Emissive          : TextureLoader::Black(m_Device),      m_Sampler);
			// White stands in for both: unset means "use the scalar", and the
			// shader only reads these when their flag is set, so the value
			// never reaches a pixel. It still has to be a real texture --
			// an unwritten binding is a validation error, not an unused one.
			resourceSet->SetTexture(6, m_Roughness         ? m_Roughness         : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(7, m_Metallic          ? m_Metallic          : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(8, m_Specular         ? m_Specular          : TextureLoader::White(m_Device),      m_Sampler);
			// Black, not white: absent height = flat, and flat is height 0
			// everywhere rather than a raised slab.
			resourceSet->SetTexture(9, m_Height           ? m_Height            : TextureLoader::Black(m_Device),      m_Sampler);

			resourceSet->Commit();
			m_FrameDirty[frame] = false;
		}

		commandList.BindResourceSet(set, resourceSet);
	}

	void Material::WriteRecord(TextureHeap& heap, GpuMaterial& out) const
	{
		// The same fallbacks, in the same slots, as Bind's SetTexture calls
		// above -- and it has to stay that way. The parity check between the
		// two paths compares pixels, and a different neutral for an absent map
		// would show up there as a difference that is not the feature's.
		out.Maps0[0] = heap.Slot(m_BaseColor ? m_BaseColor : TextureLoader::White(m_Device),      m_Sampler);
		out.Maps0[1] = heap.Slot(m_Normal    ? m_Normal    : TextureLoader::FlatNormal(m_Device), m_Sampler);
		out.Maps0[2] = heap.Slot(m_Occlusion ? m_Occlusion : TextureLoader::White(m_Device),      m_Sampler);
		out.Maps0[3] = heap.Slot(m_Emissive  ? m_Emissive  : TextureLoader::Black(m_Device),      m_Sampler);
		out.Maps1[0] = heap.Slot(m_Roughness ? m_Roughness : TextureLoader::White(m_Device),      m_Sampler);
		out.Maps1[1] = heap.Slot(m_Metallic  ? m_Metallic  : TextureLoader::White(m_Device),      m_Sampler);
		out.Maps1[2] = heap.Slot(m_Specular  ? m_Specular  : TextureLoader::White(m_Device),      m_Sampler);
		out.Maps1[3] = heap.Slot(m_Height    ? m_Height    : TextureLoader::Black(m_Device),      m_Sampler);
		out.UvTransform = m_Params.UvTransform;
		out.MapFlags    = m_Params.MapFlags;
		out.Specular    = m_Params.Specular;
		out.HeightScale = m_Params.HeightScale;
	}

	uint64_t Material::GetBatchKey(bool bindless) const
	{
		if (bindless)
			return 0;

		// A hash of the bound state, not of the material. Pointer identity is
		// the right comparison here: two Refs to the same texture produce the
		// same descriptor write, and two textures with identical contents are
		// still two descriptors.
		uint64_t hash = 1469598103934665603ull;   // FNV-1a offset basis

		auto mix = [&hash](const void* pointer)
		{
			hash ^= (uint64_t)(uintptr_t)pointer;
			hash *= 1099511628211ull;
		};

		mix(m_BaseColor.get());
		mix(m_Normal.get());
		mix(m_Occlusion.get());
		mix(m_Emissive.get());
		mix(m_Roughness.get());
		mix(m_Metallic.get());
		mix(m_Specular.get());
		mix(m_Height.get());
		mix(m_Sampler.get());

		// Everything the shader still reads from the material *block* -- not
		// only MapFlags. Specular, HeightScale and UvTransform live in that
		// block and nowhere per instance, so two materials that differ only in
		// tiling are two bound states, and merging them draws the second with
		// the first one's tiling.
		//
		// Which is exactly what happened, and the bindless parity check is what
		// found it (ENGINE-NOTES 7al): the courtyard's wall and plinth share
		// their brick maps and differ in tiling, so on the bound path they
		// were one run, the nearer plinth came first, and every wall drew at
		// the plinth's 2.2 x 1.6 instead of its own 9 x 2.4. Nobody noticed,
		// because it still looked like bricks. The bindless path gives each
		// material its own record and was right from the start; this key was
		// wrong from the day UvTransform joined the block, and only a second
		// implementation to compare against could have said so.
		auto mixBits = [&hash](const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};
		mixBits(&m_Params.MapFlags, sizeof(m_Params.MapFlags));
		mixBits(&m_Params.Specular, sizeof(m_Params.Specular));
		mixBits(&m_Params.HeightScale, sizeof(m_Params.HeightScale));
		mixBits(&m_Params.UvTransform, sizeof(m_Params.UvTransform));

		return hash;
	}

	Ref<Material> Material::CreateDefault(RHIDevice& device)
	{
		auto material = std::make_shared<Material>(device, "Default");
		material->GetParams().BaseColor = { 0.78f, 0.78f, 0.80f, 1.0f };
		material->GetParams().Metallic = 0.0f;
		material->GetParams().Roughness = 0.55f;
		return material;
	}
}
