#include <rvpch.h>
#include "VulkanPipeline.h"
#include "VulkanDevice.h"
#include <algorithm>

namespace RageV::Vk
{
	// -------------------------------------------------------------------------
	// Shader
	// -------------------------------------------------------------------------
	VulkanShader::VulkanShader(VulkanDevice& device, const RHI::CompiledShader& compiled)
		: RHI::RHIShader(compiled.Name, compiled.Reflection), m_Device(device), m_Deletion(device.GetDeletionQueue())
	{
		for (const auto& stage : compiled.Stages)
		{
			VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			createInfo.codeSize = stage.SpirV.size() * sizeof(uint32_t);
			createInfo.pCode = stage.SpirV.data();

			StageModule created;
			created.Bits = ToVkShaderStage(stage.Stage);
			created.EntryPoint = stage.EntryPoint;
			VK_CHECK(vkCreateShaderModule(m_Device.GetDevice(), &createInfo, nullptr, &created.Module));

			m_Device.SetDebugName((uint64_t)created.Module, VK_OBJECT_TYPE_SHADER_MODULE,
								  (compiled.Name + ":" + RHI::ShaderStageName(stage.Stage)).c_str());

			m_Stages.push_back(std::move(created));
		}
	}

	VulkanShader::~VulkanShader()
	{
		VkDevice device = m_Device.GetDevice();
		for (const auto& stage : m_Stages)
		{
			VkShaderModule module = stage.Module;
			m_Deletion->Push([device, module]() { vkDestroyShaderModule(device, module, nullptr); });
		}
	}

	// -------------------------------------------------------------------------
	// Pipeline
	// -------------------------------------------------------------------------
	VulkanPipelineCommon::VulkanPipelineCommon(VulkanDevice& device, VkPipelineBindPoint bindPoint,
											   const RHI::ShaderReflection& reflection)
		: m_Device(device), m_Deletion(device.GetDeletionQueue()),
		  m_Reflection(&reflection), m_BindPoint(bindPoint)
	{
	}

	VulkanPipelineCommon::~VulkanPipelineCommon()
	{
		VkDevice device = m_Device.GetDevice();
		VkPipeline pipeline = m_Pipeline;
		VkPipelineLayout layout = m_Layout;
		// Only the layouts this pipeline made. A borrowed one is the device's
		// and outlives every pipeline that reads the heap through it.
		std::vector<VkDescriptorSetLayout> setLayouts;
		for (size_t i = 0; i < m_SetLayouts.size(); i++)
		{
			if (i < m_BorrowedLayouts.size() && m_BorrowedLayouts[i])
				continue;
			setLayouts.push_back(m_SetLayouts[i]);
		}

		m_Deletion->Push([device, pipeline, layout, setLayouts]()
		{
			if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
			if (layout)   vkDestroyPipelineLayout(device, layout, nullptr);
			for (VkDescriptorSetLayout setLayout : setLayouts)
				if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		});
	}

	VkDescriptorSetLayout VulkanPipelineCommon::GetSetLayout(uint32_t set) const
	{
		if (set >= m_SetLayouts.size())
			return VK_NULL_HANDLE;
		return m_SetLayouts[set];
	}

	VulkanPipeline::VulkanPipeline(VulkanDevice& device, const RHI::GraphicsPipelineDesc& desc)
		: RHI::RHIPipeline(desc),
		  VulkanPipelineCommon(device, VK_PIPELINE_BIND_POINT_GRAPHICS,
							   desc.Shader->GetReflection())
	{
		CreateLayouts();
		CreatePipeline();
	}

	// A compute pipeline is the shared layout work plus one call. Everything
	// that makes a graphics pipeline long -- vertex input, raster, blend,
	// attachments -- has no compute equivalent to get wrong.
	VulkanComputePipeline::VulkanComputePipeline(VulkanDevice& device,
												 const RHI::ComputePipelineDesc& desc)
		: RHI::RHIComputePipeline(desc),
		  VulkanPipelineCommon(device, VK_PIPELINE_BIND_POINT_COMPUTE,
							   desc.Shader->GetReflection())
	{
		m_DebugName = desc.Name;
		CreateLayouts();

		auto shader = std::static_pointer_cast<VulkanShader>(m_Desc.Shader);

		const VulkanShader::StageModule* compute = nullptr;
		for (const auto& stage : shader->GetStages())
		{
			if (stage.Bits == VK_SHADER_STAGE_COMPUTE_BIT)
				compute = &stage;
		}

		if (!compute)
		{
			RV_CORE_ERROR("'{0}' has no compute stage; no compute pipeline was created",
						  m_Desc.Name);
			return;
		}

		VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = compute->Module;
		stageInfo.pName = compute->EntryPoint.c_str();

		VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		info.stage = stageInfo;
		info.layout = m_Layout;

		VK_CHECK(vkCreateComputePipelines(m_Device.GetDevice(), VK_NULL_HANDLE, 1,
										  &info, nullptr, &m_Pipeline));
	}

	void VulkanPipelineCommon::CreateLayouts()
	{
		const RHI::ShaderReflection& reflection = *m_Reflection;

		// Set layouts are built straight from reflection, so a shader edit
		// cannot silently disagree with hand-written binding declarations.
		uint32_t maxSet = 0;
		for (const auto& set : reflection.Sets)
			maxSet = Math::Max(maxSet, set.Set);

		m_SetLayouts.assign(reflection.Sets.empty() ? 0 : maxSet + 1, VK_NULL_HANDLE);
		m_BorrowedLayouts.assign(m_SetLayouts.size(), false);

		for (const auto& set : reflection.Sets)
		{
			// A runtime-sized array means the bindless heap (ENGINE-NOTES
			// 7al), and the heap's layout is the device's, not this
			// pipeline's: pipeline-layout compatibility is by identically
			// defined set layouts, so the only way the one heap can be bound
			// to every pipeline that reads it is for all of them to hold the
			// same VkDescriptorSetLayout. The set may declare nothing else --
			// a heap shares its set with nobody -- and the pipeline records
			// that it borrowed rather than built, so it does not destroy it.
			const bool runtimeArray = std::any_of(set.Bindings.begin(), set.Bindings.end(),
												  [](const RHI::ResourceBinding& b) { return b.Count == 0; });
			if (runtimeArray)
			{
				const bool shape = set.Bindings.size() == 1 && set.Bindings[0].Binding == 0 &&
								   set.Bindings[0].Type == RHI::ResourceType::CombinedImageSampler;
				VkDescriptorSetLayout shared = shape ? m_Device.GetBindlessTextureLayout() : VK_NULL_HANDLE;
				if (shared == VK_NULL_HANDLE)
				{
					RV_CORE_ERROR("[Vulkan] '{0}' declares a runtime-sized array in set {1}, which "
								  "must be exactly `sampler2D[]` at binding 0 and needs descriptor "
								  "indexing; the pipeline layout will be wrong",
								  m_DebugName, set.Set);
					continue;
				}
				m_SetLayouts[set.Set] = shared;
				m_BorrowedLayouts[set.Set] = true;
				continue;
			}

			std::vector<VkDescriptorSetLayoutBinding> bindings;
			bindings.reserve(set.Bindings.size());

			for (const auto& binding : set.Bindings)
			{
				VkDescriptorSetLayoutBinding layoutBinding{};
				layoutBinding.binding = binding.Binding;
				layoutBinding.descriptorType = ToVkDescriptorType(binding.Type);
				layoutBinding.descriptorCount = binding.Count;
				layoutBinding.stageFlags = ToVkShaderStages(binding.Stages);
				bindings.push_back(layoutBinding);
			}

			VkDescriptorSetLayoutCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			createInfo.bindingCount = (uint32_t)bindings.size();
			createInfo.pBindings = bindings.data();

			VK_CHECK(vkCreateDescriptorSetLayout(m_Device.GetDevice(), &createInfo, nullptr, &m_SetLayouts[set.Set]));
		}

		// Gaps in the set indices still need a layout object: Vulkan requires
		// pSetLayouts to be densely populated up to the highest set used.
		for (auto& setLayout : m_SetLayouts)
		{
			if (setLayout != VK_NULL_HANDLE)
				continue;

			VkDescriptorSetLayoutCreateInfo empty{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			VK_CHECK(vkCreateDescriptorSetLayout(m_Device.GetDevice(), &empty, nullptr, &setLayout));
		}

		std::vector<VkPushConstantRange> pushRanges;
		pushRanges.reserve(reflection.PushConstants.size());
		for (const auto& range : reflection.PushConstants)
		{
			VkPushConstantRange pushRange{};
			pushRange.offset = range.Offset;
			pushRange.size = range.Size;
			pushRange.stageFlags = ToVkShaderStages(range.Stages);
			pushRanges.push_back(pushRange);
		}

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = (uint32_t)m_SetLayouts.size();
		layoutInfo.pSetLayouts = m_SetLayouts.data();
		layoutInfo.pushConstantRangeCount = (uint32_t)pushRanges.size();
		layoutInfo.pPushConstantRanges = pushRanges.data();

		VK_CHECK(vkCreatePipelineLayout(m_Device.GetDevice(), &layoutInfo, nullptr, &m_Layout));
	}

	void VulkanPipeline::CreatePipeline()
	{
		auto shader = std::static_pointer_cast<VulkanShader>(m_Desc.Shader);

		std::vector<VkPipelineShaderStageCreateInfo> stages;
		stages.reserve(shader->GetStages().size());
		for (const auto& stage : shader->GetStages())
		{
			VkPipelineShaderStageCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			info.stage = stage.Bits;
			info.module = stage.Module;
			info.pName = stage.EntryPoint.c_str();
			stages.push_back(info);
		}

		// Fall back to the layout reflection recovered from SPIR-V when the
		// caller did not specify one explicitly.
		const RHI::VertexLayout& vertexLayout = m_Desc.VertexInput.Attributes.empty()
											  ? m_Desc.Shader->GetReflection().VertexInput
											  : m_Desc.VertexInput;

		std::vector<VkVertexInputBindingDescription> bindings;
		for (const auto& binding : vertexLayout.Bindings)
		{
			VkVertexInputBindingDescription description{};
			description.binding = binding.Binding;
			description.stride = binding.Stride;
			description.inputRate = binding.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE
														: VK_VERTEX_INPUT_RATE_VERTEX;
			bindings.push_back(description);
		}

		std::vector<VkVertexInputAttributeDescription> attributes;
		for (const auto& attribute : vertexLayout.Attributes)
		{
			VkVertexInputAttributeDescription description{};
			description.location = attribute.Location;
			description.binding = attribute.Binding;
			description.format = ToVkFormat(attribute.Format);
			description.offset = attribute.Offset;
			attributes.push_back(description);
		}

		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = (uint32_t)bindings.size();
		vertexInput.pVertexBindingDescriptions = bindings.data();
		vertexInput.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
		vertexInput.pVertexAttributeDescriptions = attributes.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssembly.topology = ToVkTopology(m_Desc.Topology);

		// Viewport and scissor are dynamic so a window resize does not force
		// every pipeline to be rebuilt.
		VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizer.polygonMode = ToVkPolygonMode(m_Desc.Rasterizer.Polygon);
		rasterizer.cullMode = ToVkCullMode(m_Desc.Rasterizer.Cull);
		rasterizer.frontFace = ToVkFrontFace(m_Desc.Rasterizer.Front);
		// Clamped to 1.0 when the device lacks wideLines: a wider ask would be
		// a validation error, and a thin line beats no overlay at all.
		rasterizer.lineWidth = m_Device.WideLinesSupported()
			? m_Desc.Rasterizer.LineWidth : 1.0f;
		rasterizer.depthBiasEnable = m_Desc.Rasterizer.DepthBiasEnable ? VK_TRUE : VK_FALSE;
		rasterizer.depthBiasConstantFactor = m_Desc.Rasterizer.DepthBiasConstant;
		rasterizer.depthBiasSlopeFactor = m_Desc.Rasterizer.DepthBiasSlope;
		rasterizer.depthBiasClamp = m_Desc.Rasterizer.DepthBiasClamp;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = (VkSampleCountFlagBits)Math::Max(1u, m_Desc.Samples);

		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencil.depthTestEnable = m_Desc.DepthStencil.DepthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = m_Desc.DepthStencil.DepthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = ToVkCompareOp(m_Desc.DepthStencil.DepthCompare);
		depthStencil.maxDepthBounds = 1.0f;

		std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
			Math::Max<size_t>(1, m_Desc.ColorFormats.size()));
		// Attachments may only differ from one another where the device allows
		// it. Without independentBlend, Vulkan requires every attachment to
		// match the first, so honouring the request would be a validation
		// error rather than a wrong picture.
		const bool perAttachment = !m_Desc.BlendPerAttachment.empty()
								&& m_Device.IndependentBlendSupported();

		if (!m_Desc.BlendPerAttachment.empty() && !perAttachment)
		{
			RV_CORE_WARN("'{0}' asked for per-attachment blending on a device without "
						 "independentBlend; every attachment gets the first one's",
						 m_Desc.Name);
		}

		for (size_t i = 0; i < blendAttachments.size(); i++)
		{
			// Per-attachment where the caller asked for it and the device
			// permits it, the single preset everywhere else -- which is every
			// pipeline that predates weighted-blended transparency.
			RHI::BlendPreset preset = m_Desc.Blend;
			if (perAttachment)
			{
				preset = i < m_Desc.BlendPerAttachment.size()
					   ? m_Desc.BlendPerAttachment[i]
					   : m_Desc.Blend;
			}
			else if (!m_Desc.BlendPerAttachment.empty())
			{
				preset = m_Desc.BlendPerAttachment[0];
			}

			ApplyBlendPreset(preset, blendAttachments[i]);
		}

		VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlend.attachmentCount = (uint32_t)m_Desc.ColorFormats.size();
		colorBlend.pAttachments = blendAttachments.data();

		const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicState.dynamicStateCount = (uint32_t)std::size(dynamicStates);
		dynamicState.pDynamicStates = dynamicStates;

		// Dynamic rendering: the pipeline is told which attachment formats it
		// will be used with instead of being tied to a VkRenderPass object.
		std::vector<VkFormat> colorFormats;
		colorFormats.reserve(m_Desc.ColorFormats.size());
		for (RHI::Format format : m_Desc.ColorFormats)
			colorFormats.push_back(ToVkFormat(format));

		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = (uint32_t)colorFormats.size();
		renderingInfo.pColorAttachmentFormats = colorFormats.data();
		if (m_Desc.DepthFormat != RHI::Format::Undefined)
		{
			renderingInfo.depthAttachmentFormat = ToVkFormat(m_Desc.DepthFormat);
			if (RHI::IsStencilFormat(m_Desc.DepthFormat))
				renderingInfo.stencilAttachmentFormat = renderingInfo.depthAttachmentFormat;
		}

		// A pipeline whose shader carries a mesh stage has no vertex input to
		// describe and no primitives to assemble -- the spec ignores both
		// states, and the validation layers ask for null rather than ignored.
		bool meshStage = false;
		for (const auto& stage : shader->GetStages())
			meshStage = meshStage || stage.Bits == VK_SHADER_STAGE_MESH_BIT_EXT;

		VkGraphicsPipelineCreateInfo createInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		createInfo.pNext = &renderingInfo;
		createInfo.stageCount = (uint32_t)stages.size();
		createInfo.pStages = stages.data();
		createInfo.pVertexInputState = meshStage ? nullptr : &vertexInput;
		createInfo.pInputAssemblyState = meshStage ? nullptr : &inputAssembly;
		createInfo.pViewportState = &viewportState;
		createInfo.pRasterizationState = &rasterizer;
		createInfo.pMultisampleState = &multisample;
		createInfo.pDepthStencilState = &depthStencil;
		createInfo.pColorBlendState = &colorBlend;
		createInfo.pDynamicState = &dynamicState;
		createInfo.layout = m_Layout;
		createInfo.renderPass = VK_NULL_HANDLE;   // dynamic rendering

		VK_CHECK(vkCreateGraphicsPipelines(m_Device.GetDevice(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_Pipeline));

		m_DebugName = m_Desc.Name;
		if (!m_Desc.Name.empty())
			m_Device.SetDebugName((uint64_t)m_Pipeline, VK_OBJECT_TYPE_PIPELINE, m_Desc.Name.c_str());
	}

	// -------------------------------------------------------------------------
	// Resource set
	// -------------------------------------------------------------------------
	VulkanResourceSet::VulkanResourceSet(VulkanDevice& device, VulkanPipelineCommon* pipeline,
										 std::shared_ptr<void> owner, uint32_t set)
		: VulkanSetBase(set), m_Device(device), m_Deletion(device.GetDeletionQueue()),
		  m_Pipeline(pipeline), m_Owner(std::move(owner))
	{
		const uint32_t frames = device.GetFramesInFlight();
		VkDescriptorSetLayout layout = pipeline->GetSetLayout(set);
		RV_CORE_ASSERT(layout != VK_NULL_HANDLE, "Pipeline has no layout for this descriptor set");

		// One set per frame in flight. Writing only into the current frame's
		// set is what makes Commit() safe without any extra synchronisation.
		std::vector<VkDescriptorSetLayout> layouts(frames, layout);

		m_Sets.resize(frames);

		// Through the device, which adds a pool block when the current one is
		// full rather than failing. The block it used is kept because a set is
		// freed to the pool that owns it, and with a chain that is not always
		// the newest one.
		m_Pool = device.AllocateDescriptorSets(layouts.data(), frames, m_Sets.data());
		if (m_Pool == VK_NULL_HANDLE)
			m_Sets.clear();

		m_Dirty.assign(m_Sets.size(), true);
	}

	VulkanResourceSet::~VulkanResourceSet()
	{
		if (m_Sets.empty() || m_Pool == VK_NULL_HANDLE)
			return;

		VkDevice device = m_Device.GetDevice();
		VkDescriptorPool pool = m_Pool;
		auto sets = m_Sets;
		m_Deletion->Push([device, pool, sets]()
		{
			vkFreeDescriptorSets(device, pool, (uint32_t)sets.size(), sets.data());
		});
	}

	VkDescriptorSet VulkanResourceSet::GetHandle() const
	{
		// Null when the allocation failed. Binding null is what the caller has
		// to handle; indexing an empty vector is not.
		if (m_Sets.empty())
			return VK_NULL_HANDLE;

		return m_Sets[m_Device.GetFrameIndex()];
	}

	void VulkanResourceSet::SetUniformBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
											 uint64_t offset, uint64_t range)
	{
		auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);

		BufferWrite write{};
		write.Binding = binding;
		write.Type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.Info.buffer = vulkanBuffer->GetHandle();
		write.Info.offset = offset;
		write.Info.range = range == 0 ? VK_WHOLE_SIZE : range;

		m_PendingBuffers.push_back(write);
	}

	void VulkanResourceSet::SetStorageBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>& buffer,
											 uint64_t offset, uint64_t range)
	{
		auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);

		BufferWrite write{};
		write.Binding = binding;
		write.Type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.Info.buffer = vulkanBuffer->GetHandle();
		write.Info.offset = offset;
		write.Info.range = range == 0 ? VK_WHOLE_SIZE : range;

		m_PendingBuffers.push_back(write);
	}

	void VulkanResourceSet::SetTexture(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
									   const RHI::Ref<RHI::RHISampler>& sampler, uint32_t arrayIndex)
	{
		auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
		auto vulkanSampler = std::static_pointer_cast<VulkanSampler>(sampler);

		ImageWrite write{};
		write.Binding = binding;
		write.ArrayIndex = arrayIndex;
		write.Type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.Info.imageView = vulkanTexture->GetView();
		write.Info.sampler = vulkanSampler->GetHandle();
		// The layout the image is actually in. A storage texture lives in
		// GENERAL for its whole life (ENGINE-NOTES 7bc) and is sampled from
		// there; everything else this engine samples has been transitioned to
		// the read-only layout by the time a pass reads it.
		write.Info.imageLayout = vulkanTexture->IsStorage() ? VK_IMAGE_LAYOUT_GENERAL
														   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		m_PendingImages.push_back(write);
	}

	void VulkanResourceSet::SetStorageImage(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
											uint32_t mip)
	{
		auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
		if (!vulkanTexture || !vulkanTexture->IsStorage())
		{
			RV_CORE_ERROR("[Vulkan] SetStorageImage({0}) on a texture not created with "
						  "TextureUsage::Storage; ignored", binding);
			return;
		}

		ImageWrite write{};
		write.Binding = binding;
		write.ArrayIndex = 0;
		write.Type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		write.Info.imageView = vulkanTexture->GetMipView(mip);
		write.Info.sampler = VK_NULL_HANDLE;
		write.Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (write.Info.imageView == VK_NULL_HANDLE)
			return;

		m_PendingImages.push_back(write);
	}

	void VulkanResourceSet::SetAccelerationStructure(uint32_t binding,
													  const RHI::Ref<RHI::RHIAccelerationStructure>& structure)
	{
		auto vulkanStructure = std::static_pointer_cast<VulkanAccelerationStructure>(structure);
		StructureWrite write{};
		write.Binding = binding;
		write.Structure = vulkanStructure ? vulkanStructure->GetHandle() : VK_NULL_HANDLE;
		if (write.Structure == VK_NULL_HANDLE)
		{
			RV_CORE_ERROR("[Vulkan] SetAccelerationStructure({0}) with no structure; ignored", binding);
			return;
		}
		m_PendingStructures.push_back(write);
	}

	void VulkanResourceSet::Commit()
	{
		const uint32_t frame = m_Device.GetFrameIndex();

		// Carry forward the last complete write set so a frame slot that has
		// not been written yet still gets a fully populated descriptor set.
		if (!m_PendingBuffers.empty())    m_LastBuffers = m_PendingBuffers;
		if (!m_PendingImages.empty())     m_LastImages = m_PendingImages;
		if (!m_PendingStructures.empty()) m_LastStructures = m_PendingStructures;

		const bool needsFullWrite = m_Dirty[frame];
		const auto& buffers    = needsFullWrite ? m_LastBuffers    : m_PendingBuffers;
		const auto& images     = needsFullWrite ? m_LastImages     : m_PendingImages;
		const auto& structures = needsFullWrite ? m_LastStructures : m_PendingStructures;

		if (buffers.empty() && images.empty() && structures.empty())
		{
			m_PendingBuffers.clear();
			m_PendingImages.clear();
			m_PendingStructures.clear();
			return;
		}

		// The pNext chain each structure write needs, kept alive until the
		// update call below has read it.
		std::vector<VkWriteDescriptorSetAccelerationStructureKHR> structureInfos;
		structureInfos.reserve(structures.size());
		for (const auto& structure : structures)
		{
			VkWriteDescriptorSetAccelerationStructureKHR info{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
			info.accelerationStructureCount = 1;
			info.pAccelerationStructures = &structure.Structure;
			structureInfos.push_back(info);
		}

		std::vector<VkWriteDescriptorSet> writes;
		writes.reserve(buffers.size() + images.size() + structures.size());

		for (const auto& buffer : buffers)
		{
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = m_Sets[frame];
			write.dstBinding = buffer.Binding;
			write.descriptorCount = 1;
			write.descriptorType = buffer.Type;
			write.pBufferInfo = &buffer.Info;
			writes.push_back(write);
		}

		for (const auto& image : images)
		{
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = m_Sets[frame];
			write.dstBinding = image.Binding;
			write.dstArrayElement = image.ArrayIndex;
			write.descriptorCount = 1;
			write.descriptorType = image.Type;
			write.pImageInfo = &image.Info;
			writes.push_back(write);
		}

		for (size_t i = 0; i < structures.size(); i++)
		{
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.pNext = &structureInfos[i];
			write.dstSet = m_Sets[frame];
			write.dstBinding = structures[i].Binding;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			writes.push_back(write);
		}

		// The rewrite-after-bind tripwire. The validation layer will report
		// the consequence -- the command buffer invalidated -- but names
		// neither who owned the set nor who rewrote it. This names both.
		if (m_Device.WasSetBoundThisFrame(m_Sets[frame]))
		{
			std::string bindings;
			for (const auto& buffer : buffers)
				bindings += (bindings.empty() ? "" : ",") + std::to_string(buffer.Binding);
			for (const auto& image : images)
				bindings += (bindings.empty() ? "" : ",") + std::to_string(image.Binding);
			for (const auto& structure : structures)
				bindings += (bindings.empty() ? "" : ",") + std::to_string(structure.Binding);

			RV_CORE_ERROR("[Vulkan] descriptor set for pipeline '{0}' (binding(s) {1}) "
						  "rewritten after being bound in this frame's command "
						  "buffer -- the earlier bind is now invalid",
						  m_Pipeline ? m_Pipeline->GetDebugName() : "?", bindings);
		}

		vkUpdateDescriptorSets(m_Device.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);

		m_Dirty[frame] = false;
		m_PendingBuffers.clear();
		m_PendingImages.clear();
		m_PendingStructures.clear();
	}

	// -------------------------------------------------------------------------
	// Bindless heap
	// -------------------------------------------------------------------------
	VulkanBindlessSet::VulkanBindlessSet(VulkanDevice& device, uint32_t capacity)
		: VulkanSetBase(0), m_Device(device), m_Deletion(device.GetDeletionQueue()),
		  m_Capacity(capacity)
	{
		VkDescriptorSetLayout layout = m_Device.GetBindlessTextureLayout();
		RV_CORE_ASSERT(layout != VK_NULL_HANDLE, "bindless set created without descriptor indexing");

		// A pool of its own, sized for exactly this heap and flagged for
		// update-after-bind, which the renderer's pool chain is not.
		VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity };
		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &size;
		VK_CHECK(vkCreateDescriptorPool(m_Device.GetDevice(), &poolInfo, nullptr, &m_Pool));

		// The layout says "up to MaxBindlessTextures"; the allocation says how
		// many this heap actually has, so an index past capacity is out of
		// range rather than a slot nobody wrote.
		VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &capacity;

		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.pNext = &countInfo;
		allocInfo.descriptorPool = m_Pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;
		VK_CHECK(vkAllocateDescriptorSets(m_Device.GetDevice(), &allocInfo, &m_Set));

		m_Device.SetDebugName((uint64_t)m_Set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "bindless textures");
	}

	VulkanBindlessSet::~VulkanBindlessSet()
	{
		VkDevice device = m_Device.GetDevice();
		VkDescriptorPool pool = m_Pool;
		// Destroying the pool frees the set with it.
		m_Deletion->Push([device, pool]()
		{
			if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
		});
	}

	void VulkanBindlessSet::SetUniformBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>&,
											 uint64_t, uint64_t)
	{
		RV_CORE_ERROR("[Vulkan] SetUniformBuffer({0}) on the bindless texture heap; ignored", binding);
	}

	void VulkanBindlessSet::SetStorageBuffer(uint32_t binding, const RHI::Ref<RHI::RHIBuffer>&,
											 uint64_t, uint64_t)
	{
		RV_CORE_ERROR("[Vulkan] SetStorageBuffer({0}) on the bindless texture heap; ignored", binding);
	}

	void VulkanBindlessSet::SetTexture(uint32_t binding, const RHI::Ref<RHI::RHITexture>& texture,
									   const RHI::Ref<RHI::RHISampler>& sampler, uint32_t arrayIndex)
	{
		if (binding != 0 || arrayIndex >= m_Capacity)
		{
			RV_CORE_ERROR("[Vulkan] bindless SetTexture(binding {0}, slot {1}) outside binding 0, "
						  "slots 0..{2}; ignored", binding, arrayIndex, m_Capacity - 1);
			return;
		}

		auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
		auto vulkanSampler = std::static_pointer_cast<VulkanSampler>(sampler);

		VkDescriptorImageInfo info{};
		info.imageView = vulkanTexture->GetView();
		info.sampler = vulkanSampler->GetHandle();
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		m_PendingInfos.push_back(info);
		m_PendingSlots.push_back(arrayIndex);
	}

	void VulkanBindlessSet::SetStorageImage(uint32_t binding, const RHI::Ref<RHI::RHITexture>&, uint32_t)
	{
		RV_CORE_ERROR("[Vulkan] SetStorageImage({0}) on the bindless texture heap; ignored", binding);
	}

	void VulkanBindlessSet::SetAccelerationStructure(uint32_t binding,
													  const RHI::Ref<RHI::RHIAccelerationStructure>&)
	{
		RV_CORE_ERROR("[Vulkan] SetAccelerationStructure({0}) on the bindless texture heap; ignored", binding);
	}

	void VulkanBindlessSet::Commit()
	{
		if (m_PendingSlots.empty())
			return;

		// One write per slot. Runs of consecutive slots could be one write
		// with a count, and the startup fill is exactly such a run -- but that
		// fill happens once and the per-frame traffic is a handful of slots,
		// so the simple form is the honest one.
		std::vector<VkWriteDescriptorSet> writes;
		writes.reserve(m_PendingSlots.size());
		for (size_t i = 0; i < m_PendingSlots.size(); i++)
		{
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = m_Set;
			write.dstBinding = 0;
			write.dstArrayElement = m_PendingSlots[i];
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &m_PendingInfos[i];
			writes.push_back(write);
		}

		vkUpdateDescriptorSets(m_Device.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);

		m_PendingInfos.clear();
		m_PendingSlots.clear();
	}
}
