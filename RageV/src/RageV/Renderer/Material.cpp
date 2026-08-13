#include <rvpch.h>
#include "Material.h"
#include "TextureLoader.h"

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
		m_FrameDirty.assign(std::max<size_t>(m_FrameDirty.size(), m_ParamBuffers.size()), true);
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

	uint64_t Material::GetBatchKey() const
	{
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

		// MapFlags is the one scalar the shader still reads from the material
		// block, so it has to agree across a batch. It is derived from the maps
		// above and so is almost always implied by them -- almost, because
		// nothing stops it being set by hand.
		hash ^= (uint64_t)(uint32_t)m_Params.MapFlags;
		hash *= 1099511628211ull;

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
