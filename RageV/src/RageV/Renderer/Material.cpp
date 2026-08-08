#include <rvpch.h>
#include "Material.h"
#include "TextureLoader.h"

namespace RageV
{
	using namespace RageV::RHI;

	Material::Material(RHIDevice& device, std::string name)
		: m_Device(device), m_Name(std::move(name))
	{
		SamplerDesc samplerDesc;
		samplerDesc.MaxAnisotropy = device.GetCaps().SupportsAnisotropy ? 8.0f : 1.0f;
		m_Sampler = device.CreateSampler(samplerDesc);

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
		m_TexturesDirty.assign(frames, true);
	}

	namespace
	{
		void AssignMap(Ref<RHITexture>& slot, const Ref<RHITexture>& texture,
					   int32_t& flags, MaterialMap bit, std::vector<bool>& dirty)
		{
			slot = texture;
			if (texture)
				flags |= bit;
			else
				flags &= ~bit;

			// Every frame's descriptor set needs rewriting, not just the
			// current one.
			for (size_t i = 0; i < dirty.size(); i++)
				dirty[i] = true;
		}
	}

	void Material::SetBaseColorMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_BaseColor, texture, m_Params.MapFlags, MaterialMap_BaseColor, m_TexturesDirty);
		m_ParamsDirty = true;
	}

	void Material::SetNormalMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Normal, texture, m_Params.MapFlags, MaterialMap_Normal, m_TexturesDirty);
		m_ParamsDirty = true;
	}

	void Material::SetMetallicRoughnessMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_MetallicRoughness, texture, m_Params.MapFlags, MaterialMap_MetallicRoughness, m_TexturesDirty);
		m_ParamsDirty = true;
	}

	void Material::SetOcclusionMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Occlusion, texture, m_Params.MapFlags, MaterialMap_Occlusion, m_TexturesDirty);
		m_ParamsDirty = true;
	}

	void Material::SetEmissiveMap(const Ref<RHITexture>& texture)
	{
		AssignMap(m_Emissive, texture, m_Params.MapFlags, MaterialMap_Emissive, m_TexturesDirty);
		m_ParamsDirty = true;
	}

	void Material::EnsureResources(const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		if (m_Built)
			return;

		const uint32_t frames = m_Device.GetFramesInFlight();
		m_Sets.clear();
		for (uint32_t i = 0; i < frames; i++)
			m_Sets.push_back(m_Device.CreateResourceSet(pipeline, set));

		m_TexturesDirty.assign(frames, true);
		m_Built = true;
	}

	void Material::Bind(RHICommandList& commandList, const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		EnsureResources(pipeline, set);

		const uint32_t frame = m_Device.GetFrameIndex();

		// Uploaded every frame the parameters changed. Each frame slot has its
		// own buffer, so this cannot race a frame still reading the old values.
		m_ParamBuffers[frame]->Upload(&m_Params, sizeof(MaterialParams));

		auto& resourceSet = m_Sets[frame];
		resourceSet->SetUniformBuffer(0, m_ParamBuffers[frame], 0, sizeof(MaterialParams));

		if (m_TexturesDirty[frame])
		{
			// A sampler left unwritten is a validation error even when the
			// shader will not read it, so absent maps bind a neutral 1x1.
			resourceSet->SetTexture(1, m_BaseColor         ? m_BaseColor         : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(2, m_Normal            ? m_Normal            : TextureLoader::FlatNormal(m_Device), m_Sampler);
			resourceSet->SetTexture(3, m_MetallicRoughness ? m_MetallicRoughness : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(4, m_Occlusion         ? m_Occlusion         : TextureLoader::White(m_Device),      m_Sampler);
			resourceSet->SetTexture(5, m_Emissive          ? m_Emissive          : TextureLoader::Black(m_Device),      m_Sampler);
			m_TexturesDirty[frame] = false;
		}

		resourceSet->Commit();
		commandList.BindResourceSet(set, resourceSet);
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
