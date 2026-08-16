#pragma once

// The bindless texture heap, renderer side (ENGINE-NOTES 7al).
//
// The RHI provides one descriptor set whose single binding is a large array
// of textures a shader indexes at runtime. This is everything above it: which
// slot a (texture, sampler) pair lives in, which slots are free, and -- the
// part that matters -- when a slot may be reused. Common code, backend-blind:
// it calls SetTexture and Commit on an RHIResourceSet and nothing else, and it
// simply does not exist on a device whose caps say no.
//
// Slot 0 is the error texture, and every slot starts as slot 0. All of them
// are written at creation, so there is no such thing as an unwritten slot: an
// index that is stale, uninitialised or wrong reads magenta on every device,
// deterministically, instead of being undefined behaviour that happens to look
// fine on this one. That matters because an out-of-range index into a
// partially-bound array produces no validation message; the only ordinary
// signal is the picture.
//
// Slots are released in three steps. A texture nobody references any more is
// one nobody's record can name -- but a frame still in flight may have named
// it, so the slot is *retired* into that frame's list, rewritten to the error
// texture when the frame's slot comes round again (which the fence guarantees
// is after the GPU finished with it), and only then returned to the free list.
// The same shape as DeletionQueue::PerFrame, for the same reason: update-after-
// bind permits writing a bound set, on the condition that no pending command
// buffer reads the slot being written.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include <unordered_map>
#include <memory>

namespace RageV
{
	class TextureHeap
	{
	public:
		// The set number every shader that reads the heap declares it at, and
		// the one the renderer binds it to. A convention rather than a
		// reflection: the heap has to be bound before the pipeline that reads
		// it draws, and "set 2" is how both sides know which.
		static constexpr uint32_t kSet = 2;
		// The slot every unwritten and released entry points at.
		static constexpr uint32_t kErrorSlot = 0;

		// Null when the device cannot (GetCaps().SupportsDescriptorIndexing
		// false) or when `capacity` is zero. Registers the error texture in slot
		// 0 and fills every other slot with it.
		static std::unique_ptr<TextureHeap> Create(RHI::RHIDevice& device, uint32_t capacity);
		~TextureHeap();

		// The slot for this pair, allocated on first sight. The pair is the key
		// because a heap entry is a combined image sampler: the same texture
		// under a second sampler is a second slot.
		//
		// The texture is held weakly -- the material owns it -- so this cannot
		// keep anything alive; the sampler strongly, because there are three
		// of them and a sampler that died under a live slot is the same hazard
		// as a texture that did.
		uint32_t Slot(const RHI::Ref<RHI::RHITexture>& texture,
					  const RHI::Ref<RHI::RHISampler>& sampler);

		// Once per frame, before anything asks for a slot: retires the entries
		// whose textures have gone, and recycles the slots retired
		// FramesInFlight frames ago. `frameIndex` is the device's.
		void BeginFrame(uint32_t frameIndex);

		// Pushes every write staged by Slot() to the device. Call before the
		// command buffer that reads them is submitted -- once per scene is
		// enough, and cheap when nothing changed.
		void Commit();

		const RHI::Ref<RHI::RHIResourceSet>& GetSet() const { return m_Set; }
		uint32_t GetCapacity() const { return m_Capacity; }
		uint32_t GetLiveCount() const { return (uint32_t)m_Entries.size(); }
		uint32_t GetFreeCount() const { return (uint32_t)m_Free.size(); }

	private:
		TextureHeap(RHI::RHIDevice& device, RHI::Ref<RHI::RHIResourceSet> set, uint32_t capacity);

		struct Key
		{
			const RHI::RHITexture* Texture = nullptr;
			const RHI::RHISampler* Sampler = nullptr;
			bool operator==(const Key&) const = default;
		};
		struct KeyHash
		{
			size_t operator()(const Key& key) const
			{
				return std::hash<const void*>()(key.Texture) * 31u ^ std::hash<const void*>()(key.Sampler);
			}
		};
		struct Entry
		{
			uint32_t Slot = 0;
			std::weak_ptr<RHI::RHITexture> Texture;
			RHI::Ref<RHI::RHISampler>      Sampler;
		};

		void Retire(uint32_t slot);
		void WriteError(uint32_t slot);

		RHI::RHIDevice& m_Device;
		RHI::Ref<RHI::RHIResourceSet> m_Set;
		uint32_t m_Capacity = 0;
		uint32_t m_FrameIndex = 0;

		RHI::Ref<RHI::RHITexture> m_ErrorTexture;
		RHI::Ref<RHI::RHISampler> m_ErrorSampler;

		std::unordered_map<Key, Entry, KeyHash> m_Entries;
		std::vector<uint32_t> m_Free;
		// Indexed by frame slot: retired here, recycled when that slot comes
		// round again.
		std::vector<std::vector<uint32_t>> m_Retired;
		bool m_Dirty = false;
		bool m_ReportedFull = false;
	};
}
