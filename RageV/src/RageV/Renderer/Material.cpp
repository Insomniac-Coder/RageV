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

	Material::PipelineSets& Material::EnsureResources(const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		for (PipelineSets& existing : m_PipelineSets)
		{
			if (existing.Key == pipeline.get())
				return existing;
		}

		// A new layout: sets of its own, every frame of them dirty. The set
		// keeps the pipeline alive, so a pointer in this table is never
		// reused by a later pipeline while the entry stands.
		const uint32_t frames = m_Device.GetFramesInFlight();
		PipelineSets entry;
		entry.Key = pipeline.get();
		for (uint32_t i = 0; i < frames; i++)
			entry.Sets.push_back(m_Device.CreateResourceSet(pipeline, set));
		entry.Dirty.assign(frames, true);
		m_PipelineSets.push_back(std::move(entry));
		return m_PipelineSets.back();
	}

	void Material::Invalidate()
	{
		// Every frame's copy, not just the current one: each frame in flight
		// has its own buffer and set, and a change has to reach all of them --
		// and every pipeline's sets, since each holds its own copy.
		m_FrameDirty.assign(Math::Max<size_t>(m_FrameDirty.size(), m_ParamBuffers.size()), true);
		for (PipelineSets& entry : m_PipelineSets)
			entry.Dirty.assign(entry.Dirty.size(), true);
	}

	void Material::Bind(RHICommandList& commandList, const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		PipelineSets& pipelineSets = EnsureResources(pipeline, set);

		const uint32_t frame = m_Device.GetFrameIndex();
		auto& resourceSet = pipelineSets.Sets[frame];

		// The bytes once per frame, whichever pipeline asked first.
		if (m_FrameDirty[frame])
		{
			m_ParamBuffers[frame]->Upload(&m_Params, sizeof(MaterialParams));
			m_FrameDirty[frame] = false;
		}

		// Only when something actually changed. Rewriting a descriptor set that
		// is already bound to a command buffer is a use-after-bind hazard, and
		// binding one material for several objects -- or drawing one scene into
		// two viewports -- did exactly that on every draw after the first.
		if (pipelineSets.Dirty[frame])
		{
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
			pipelineSets.Dirty[frame] = false;
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

	// --- LayeredMaterial (ENGINE-NOTES 7aq) --------------------------------------

	namespace
	{
		Ref<RHISampler> s_WeightSampler;

		uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash;
		}
	}

	const Ref<RHISampler>& LayeredMaterial::WeightSampler(RHIDevice& device)
	{
		if (!s_WeightSampler)
		{
			SamplerDesc desc;
			desc.WrapU = WrapMode::ClampToEdge;
			desc.WrapV = WrapMode::ClampToEdge;
			desc.WrapW = WrapMode::ClampToEdge;
			// One level: the map is read at its own resolution and a chain
			// would blur paint across a boundary the eye can see.
			desc.MaxLod = 0.0f;
			s_WeightSampler = device.CreateSampler(desc);
		}
		return s_WeightSampler;
	}

	void LayeredMaterial::ReleaseShared()
	{
		s_WeightSampler.reset();
	}

	LayeredMaterial::LayeredMaterial(RHIDevice& device, std::string name)
		: m_Device(device), m_Name(std::move(name))
	{
		const uint32_t frames = device.GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		for (uint32_t i = 0; i < frames; i++)
		{
			BufferDesc desc;
			desc.Size = sizeof(LayeredParams);
			desc.Usage = BufferUsage::Uniform;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = m_Name + ".layers." + std::to_string(i);
			m_ParamBuffers[i] = device.CreateBuffer(desc);
		}
		m_FrameDirty.assign(frames, true);
	}

	void LayeredMaterial::SetLayer(uint32_t index, const Ref<Material>& material)
	{
		if (index < kLayers)
			m_Layers[index] = material;
	}

	void LayeredMaterial::SetWeights(const Ref<RHITexture>& weights, const Vec4& weightUv)
	{
		m_Weights = weights;
		m_WeightUv = weightUv;
	}

	void LayeredMaterial::Refresh(TextureHeap* heap)
	{
		// Built fresh from the layers, then compared with what the set holds.
		// A layer's material is edited through its own object -- the inspector,
		// a hot reload -- and nothing tells this one; reading it back each
		// frame is what keeps the terrain honest to its layers with no
		// protocol between the two classes. Thirteen pointer reads and a
		// 368-byte compare per terrain per frame.
		LayeredParams params;
		Ref<RHITexture> textures[1 + 3 * kLayers];
		Ref<RHISampler> samplers[kLayers];

		const Ref<RHISampler>& weightSampler = WeightSampler(m_Device);
		// A missing weight map means unpainted, and unpainted means layer 0:
		// a 1x1 red under the same clamp sampler says exactly that. Red is
		// TextureLoader's neutral for "layer 0 has weight 1", chosen here so
		// the shader's zero-sum rule and this fallback agree.
		textures[0] = m_Weights ? m_Weights : TextureLoader::Red(m_Device);
		params.WeightUv = m_WeightUv;

		for (uint32_t i = 0; i < kLayers; i++)
		{
			const Ref<Material>& layer = m_Layers[i];
			// The same neutral fallbacks Material::Bind binds for an absent
			// map, so a layer with no roughness map reads as its scalar on
			// every path.
			const Ref<RHITexture> base = layer && layer->GetBaseColorMap() ? layer->GetBaseColorMap()
																		  : TextureLoader::White(m_Device);
			const Ref<RHITexture> normal = layer && layer->GetNormalMap() ? layer->GetNormalMap()
																		 : TextureLoader::FlatNormal(m_Device);
			const Ref<RHITexture> rough = layer && layer->GetRoughnessMap() ? layer->GetRoughnessMap()
																		  : TextureLoader::White(m_Device);
			textures[1 + i] = base;
			textures[1 + kLayers + i] = normal;
			textures[1 + 2 * kLayers + i] = rough;
			samplers[i] = layer ? layer->GetSampler() : weightSampler;

			if (layer)
			{
				const MaterialParams& p = layer->GetParams();
				params.BaseColor[i] = p.BaseColor;
				params.EmissiveColor[i] = p.EmissiveColor;
				params.Surface[i] = { p.Metallic, p.Roughness, p.Occlusion, p.NormalScale };
				params.UvTransform[i] = p.UvTransform;
				params.Specular[i] = p.Specular;
				// Only the three maps this variant reads; a layer's occlusion
				// or height map is not bound and its flag must not say it is.
				params.MapFlags[i] = (p.MapFlags & (MaterialMap_BaseColor | MaterialMap_Normal |
													MaterialMap_Roughness)) | LayeredMap_Active;
			}
			else
			{
				params.BaseColor[i] = Vec4(0.0f);
				params.EmissiveColor[i] = Vec4(0.0f);
				params.Surface[i] = Vec4(0.0f);
				params.UvTransform[i] = { 1.0f, 1.0f, 0.0f, 0.0f };
				params.Specular[i] = 0.5f;
				params.MapFlags[i] = 0;
			}
		}

		if (heap)
		{
			params.WeightSlot[0] = heap->Slot(textures[0], weightSampler);
			for (uint32_t i = 0; i < kLayers; i++)
			{
				params.BaseColorSlots[i] = heap->Slot(textures[1 + i], samplers[i]);
				params.NormalSlots[i] = heap->Slot(textures[1 + kLayers + i], samplers[i]);
				params.RoughnessSlots[i] = heap->Slot(textures[1 + 2 * kLayers + i], samplers[i]);
			}
		}

		bool changed = m_Bindless != (heap != nullptr) ||
					   std::memcmp(&params, &m_Params, sizeof(LayeredParams)) != 0;
		for (uint32_t i = 0; i < 1 + 3 * kLayers && !changed; i++)
			changed = textures[i].get() != m_Textures[i].get();
		for (uint32_t i = 0; i < kLayers && !changed; i++)
			changed = samplers[i].get() != m_Samplers[i].get();
		if (!changed)
			return;

		m_Bindless = heap != nullptr;
		m_Params = params;
		for (uint32_t i = 0; i < 1 + 3 * kLayers; i++)
			m_Textures[i] = textures[i];
		for (uint32_t i = 0; i < kLayers; i++)
			m_Samplers[i] = samplers[i];

		// The bound state, hashed as Material::GetBatchKey hashes its own:
		// the block's bytes, and the identity of every texture and sampler.
		uint64_t hash = 1469598103934665603ull;
		hash = HashBytes(hash, &m_Params, sizeof(m_Params));
		for (const Ref<RHITexture>& texture : m_Textures)
		{
			const void* pointer = texture.get();
			hash = HashBytes(hash, &pointer, sizeof(pointer));
		}
		for (const Ref<RHISampler>& sampler : m_Samplers)
		{
			const void* pointer = sampler.get();
			hash = HashBytes(hash, &pointer, sizeof(pointer));
		}
		m_Key = hash;

		m_FrameDirty.assign(m_FrameDirty.size(), true);
	}

	void LayeredMaterial::EnsureResources(const Ref<RHIPipeline>& pipeline, uint32_t set)
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

	void LayeredMaterial::Bind(RHICommandList& commandList, const Ref<RHIPipeline>& pipeline, uint32_t set)
	{
		EnsureResources(pipeline, set);

		const uint32_t frame = m_Device.GetFrameIndex();
		auto& resourceSet = m_Sets[frame];

		if (m_FrameDirty[frame])
		{
			m_ParamBuffers[frame]->Upload(&m_Params, sizeof(LayeredParams));
			resourceSet->SetUniformBuffer(kBindingParams, m_ParamBuffers[frame], 0, sizeof(LayeredParams));

			// The samplers only where the layout declares them: the bindless
			// variant reads the maps through the heap by the slots in the
			// block, and writing a binding a shader never declared is the
			// hazard HANDOFF section 5 records.
			if (!m_Bindless)
			{
				resourceSet->SetTexture(kBindingWeights, m_Textures[0], WeightSampler(m_Device));
				for (uint32_t i = 0; i < kLayers; i++)
				{
					resourceSet->SetTexture(kBindingBaseColor, m_Textures[1 + i], m_Samplers[i], i);
					resourceSet->SetTexture(kBindingNormal, m_Textures[1 + kLayers + i], m_Samplers[i], i);
					resourceSet->SetTexture(kBindingRoughness, m_Textures[1 + 2 * kLayers + i], m_Samplers[i], i);
				}
			}

			resourceSet->Commit();
			m_FrameDirty[frame] = false;
		}

		commandList.BindResourceSet(set, resourceSet);
	}
}
