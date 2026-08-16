#include <rvpch.h>
#include "TextureHeap.h"
#include "TextureLoader.h"

namespace RageV
{
	using namespace RHI;

	std::unique_ptr<TextureHeap> TextureHeap::Create(RHIDevice& device, uint32_t capacity)
	{
		if (!device.GetCaps().SupportsDescriptorIndexing || capacity == 0)
			return nullptr;

		Ref<RHIResourceSet> set = device.CreateBindlessTextureSet(capacity);
		if (!set)
			return nullptr;

		return std::unique_ptr<TextureHeap>(new TextureHeap(device, std::move(set), capacity));
	}

	TextureHeap::TextureHeap(RHIDevice& device, Ref<RHIResourceSet> set, uint32_t capacity)
		: m_Device(device), m_Set(std::move(set)), m_Capacity(capacity)
	{
		m_Retired.resize(device.GetFramesInFlight());

		SamplerDesc samplerDesc;
		samplerDesc.MinFilter = FilterMode::Nearest;
		samplerDesc.MagFilter = FilterMode::Nearest;
		m_ErrorSampler = device.CreateSampler(samplerDesc);
		m_ErrorTexture = TextureLoader::Magenta(device);

		// Every slot, once. After this there is no unwritten slot in the heap,
		// so a wrong index is a magenta object rather than undefined behaviour
		// -- and PARTIALLY_BOUND is a belt to this brace, not the design.
		for (uint32_t slot = 0; slot < m_Capacity; slot++)
			m_Set->SetTexture(0, m_ErrorTexture, m_ErrorSampler, slot);
		m_Set->Commit();

		// Slot 0 is the error texture by contract, so it is never handed out.
		// The rest are free, lowest first -- reading a heap dump is easier
		// when the live slots are dense.
		m_Free.reserve(m_Capacity - 1);
		for (uint32_t slot = m_Capacity - 1; slot > kErrorSlot; slot--)
			m_Free.push_back(slot);
	}

	TextureHeap::~TextureHeap() = default;

	uint32_t TextureHeap::Slot(const Ref<RHITexture>& texture, const Ref<RHISampler>& sampler)
	{
		if (!texture || !sampler)
			return kErrorSlot;

		const Key key{ texture.get(), sampler.get() };
		if (auto it = m_Entries.find(key); it != m_Entries.end())
		{
			// The pointer matched, but pointers are recycled: a texture
			// destroyed and another allocated at the same address between the
			// last sweep and now would otherwise be handed the dead one's
			// slot. The weak reference tells the two apart.
			if (it->second.Texture.lock() == texture)
				return it->second.Slot;

			Retire(it->second.Slot);
			m_Entries.erase(it);
		}

		if (m_Free.empty())
		{
			// Stated once per heap rather than per call: a scene past capacity
			// says so once and draws magenta, which is visible, rather than
			// filling the log at frame rate.
			if (!m_ReportedFull)
			{
				RV_CORE_ERROR("Bindless texture heap full at {0} slots; further textures read as "
							  "the error texture", m_Capacity);
				m_ReportedFull = true;
			}
			return kErrorSlot;
		}

		const uint32_t slot = m_Free.back();
		m_Free.pop_back();

		Entry entry;
		entry.Slot = slot;
		entry.Texture = texture;
		entry.Sampler = sampler;
		m_Entries.emplace(key, std::move(entry));

		m_Set->SetTexture(0, texture, sampler, slot);
		m_Dirty = true;
		return slot;
	}

	void TextureHeap::BeginFrame(uint32_t frameIndex)
	{
		m_FrameIndex = frameIndex;

		// The slots retired when this frame slot last came round. Every frame
		// in flight has been fenced since, so nothing pending can still name
		// them: they may be rewritten now, and are -- to the error texture --
		// before going back on the free list.
		auto& recycled = m_Retired[frameIndex];
		for (uint32_t slot : recycled)
		{
			WriteError(slot);
			m_Free.push_back(slot);
		}
		recycled.clear();

		// The sweep: an entry whose texture has gone is retired now, and
		// recycled when this frame comes round again.
		for (auto it = m_Entries.begin(); it != m_Entries.end();)
		{
			if (it->second.Texture.expired())
			{
				Retire(it->second.Slot);
				it = m_Entries.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void TextureHeap::Commit()
	{
		if (!m_Dirty)
			return;
		m_Set->Commit();
		m_Dirty = false;
	}

	void TextureHeap::Retire(uint32_t slot)
	{
		if (slot == kErrorSlot)
			return;
		m_Retired[m_FrameIndex].push_back(slot);
	}

	void TextureHeap::WriteError(uint32_t slot)
	{
		m_Set->SetTexture(0, m_ErrorTexture, m_ErrorSampler, slot);
		m_Dirty = true;
	}
}
