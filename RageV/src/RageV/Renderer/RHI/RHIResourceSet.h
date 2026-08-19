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

		// One mip level of a texture created with TextureUsage::Storage, for a
		// binding the shader declared as an image (`image3D`, `image2D`) and
		// reads or writes with imageLoad/imageStore (ENGINE-NOTES 7bc). A
		// level rather than the whole chain, because a pass building level 3
		// from level 2 binds each on its own. No sampler: an image is
		// addressed by texel. On Vulkan the texture lives in the general
		// layout for its whole life, so the descriptor never has to know
		// what happened to it last; on OpenGL this is a glBindImageTexture.
		virtual void SetStorageImage(uint32_t binding, const Ref<RHITexture>& texture,
									 uint32_t mip = 0) = 0;

		// A top-level acceleration structure, for a binding the shader
		// declared as `uniform accelerationStructureEXT` (ENGINE-NOTES 7am).
		// Vulkan only; the OpenGL set logs and drops it, and no OpenGL shader
		// can declare the binding in the first place.
		virtual void SetAccelerationStructure(uint32_t binding,
											  const Ref<RHIAccelerationStructure>& structure) = 0;

		// Applies everything staged since the last call. Cheap and a no-op when
		// nothing changed.
		virtual void Commit() = 0;

	protected:
		explicit RHIResourceSet(uint32_t set) : m_Set(set) {}
		uint32_t m_Set = 0;
	};
}
