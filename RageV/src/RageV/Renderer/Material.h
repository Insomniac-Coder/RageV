#pragma once

// A PBR material: the metallic-roughness parameterisation glTF uses, which is
// what almost every asset pipeline exports.
//
// Scalar parameters live in a uniform buffer and the maps in the same
// descriptor set, so binding a material is one BindResourceSet rather than a
// sequence of individual binds. Every map has a neutral 1x1 fallback, because a
// descriptor set with an unwritten sampler is a validation error even when the
// shader will not sample it.

#include "RageV/Renderer/RHI/RHIDevice.h"
#include "glm/glm.hpp"

namespace RageV
{
	// Mirrors the std140 MaterialData block in pbr.rvshader.
	struct MaterialParams
	{
		glm::vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 EmissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float Occlusion = 1.0f;
		float NormalScale = 1.0f;
		// Which maps are present, as bit flags; the shader falls back to the
		// scalar parameter for any that are not.
		int32_t MapFlags = 0;
		int32_t _padding[3] = {};
	};

	enum MaterialMap : int32_t
	{
		MaterialMap_BaseColor         = 1 << 0,
		MaterialMap_Normal            = 1 << 1,
		MaterialMap_MetallicRoughness = 1 << 2,
		MaterialMap_Occlusion         = 1 << 3,
		MaterialMap_Emissive          = 1 << 4,
	};

	class Material
	{
	public:
		Material(RHI::RHIDevice& device, std::string name);

		const std::string& GetName() const { return m_Name; }
		MaterialParams& GetParams() { return m_Params; }
		const MaterialParams& GetParams() const { return m_Params; }

		// Passing nullptr clears the map and reverts to the scalar parameter.
		void SetBaseColorMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetNormalMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetMetallicRoughnessMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetOcclusionMap(const RHI::Ref<RHI::RHITexture>& texture);
		void SetEmissiveMap(const RHI::Ref<RHI::RHITexture>& texture);

		// Marks every frame's descriptor set and parameter buffer as needing a
		// rewrite. Call after touching GetParams() directly.
		void Invalidate();

		// Builds the descriptor set against a pipeline layout. Materials are
		// created before any pipeline exists, so this is deferred rather than
		// done in the constructor.
		void Bind(RHI::RHICommandList& commandList, const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		static RHI::Ref<Material> CreateDefault(RHI::RHIDevice& device);

	private:
		void EnsureResources(const RHI::Ref<RHI::RHIPipeline>& pipeline, uint32_t set);

		RHI::RHIDevice& m_Device;
		std::string m_Name;
		MaterialParams m_Params;

		RHI::Ref<RHI::RHITexture> m_BaseColor;
		RHI::Ref<RHI::RHITexture> m_Normal;
		RHI::Ref<RHI::RHITexture> m_MetallicRoughness;
		RHI::Ref<RHI::RHITexture> m_Occlusion;
		RHI::Ref<RHI::RHITexture> m_Emissive;
		RHI::Ref<RHI::RHISampler> m_Sampler;

		// Per frame in flight: the parameter block is host-visible and may be
		// rewritten while a previous frame still reads it.
		std::vector<RHI::Ref<RHI::RHIBuffer>>      m_ParamBuffers;
		std::vector<RHI::Ref<RHI::RHIResourceSet>> m_Sets;

		// Whether this frame's set and buffer still match the material.
		//
		// Bind used to upload and commit on every single draw. A descriptor set
		// that is already bound must not be rewritten -- Vulkan reports it as
		// "destroyed or updated without UPDATE_AFTER_BIND" -- and it happened
		// whenever one material was used by two objects, or when the same scene
		// was drawn into two viewports. Writing only on an actual change fixes
		// the hazard and removes a per-draw descriptor write.
		std::vector<bool> m_FrameDirty;
		bool m_Built = false;
	};
}
