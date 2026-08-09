#pragma once

// A resource set is one descriptor set: a group of buffers and textures bound
// together at a set index. Writes are batched and applied by Commit(), because
// Vulkan cannot update a descriptor set that an in-flight frame is still
// reading from.

#include "RHITypes.h"
#include "RHIResources.h"

namespace RageV::RHI
{
	class RHIResourceSet
	{
	public:
		virtual ~RHIResourceSet() = default;

		uint32_t GetSet() const { return m_Set; }

		virtual void SetUniformBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
									  uint64_t offset = 0, uint64_t range = 0) = 0;

		// A read-only array the shader indexes itself, rather than a block of
		// fixed size. Instanced drawing is what this is for: a uniform buffer
		// would cap a batch at whatever fits in 64 KB, and the cap would be
		// different on every driver.
		virtual void SetStorageBuffer(uint32_t binding, const Ref<RHIBuffer>& buffer,
									  uint64_t offset = 0, uint64_t range = 0) = 0;

		virtual void SetTexture(uint32_t binding, const Ref<RHITexture>& texture,
								const Ref<RHISampler>& sampler, uint32_t arrayIndex = 0) = 0;

		// Applies everything staged since the last call. Cheap and a no-op when
		// nothing changed.
		virtual void Commit() = 0;

	protected:
		explicit RHIResourceSet(uint32_t set) : m_Set(set) {}
		uint32_t m_Set = 0;
	};
}
