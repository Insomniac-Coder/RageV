#include <rvpch.h>
#include "VulkanPipeline.h"
#include "VulkanDevice.h"

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
		auto setLayouts = m_SetLayouts;

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
			maxSet = std::max(maxSet, set.Set);

		m_SetLayouts.assign(reflection.Sets.empty() ? 0 : maxSet + 1, VK_NULL_HANDLE);

		for (const auto& set : reflection.Sets)
		{
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
		multisample.rasterizationSamples = (VkSampleCountFlagBits)std::max(1u, m_Desc.Samples);

		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencil.depthTestEnable = m_Desc.DepthStencil.DepthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = m_Desc.DepthStencil.DepthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = ToVkCompareOp(m_Desc.DepthStencil.DepthCompare);
		depthStencil.maxDepthBounds = 1.0f;

		std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
			std::max<size_t>(1, m_Desc.ColorFormats.size()));
		for (size_t i = 0; i < blendAttachments.size(); i++)
		{
			// Per-attachment where the caller asked for it, the single preset
			// everywhere else -- which is every pipeline that predates
			// weighted-blended transparency.
			const RHI::BlendPreset preset = i < m_Desc.BlendPerAttachment.size()
										  ? m_Desc.BlendPerAttachment[i]
										  : m_Desc.Blend;
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

		VkGraphicsPipelineCreateInfo createInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		createInfo.pNext = &renderingInfo;
		createInfo.stageCount = (uint32_t)stages.size();
		createInfo.pStages = stages.data();
		createInfo.pVertexInputState = &vertexInput;
		createInfo.pInputAssemblyState = &inputAssembly;
		createInfo.pViewportState = &viewportState;
		createInfo.pRasterizationState = &rasterizer;
		createInfo.pMultisampleState = &multisample;
		createInfo.pDepthStencilState = &depthStencil;
		createInfo.pColorBlendState = &colorBlend;
		createInfo.pDynamicState = &dynamicState;
		createInfo.layout = m_Layout;
		createInfo.renderPass = VK_NULL_HANDLE;   // dynamic rendering

		VK_CHECK(vkCreateGraphicsPipelines(m_Device.GetDevice(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_Pipeline));

		if (!m_Desc.Name.empty())
			m_Device.SetDebugName((uint64_t)m_Pipeline, VK_OBJECT_TYPE_PIPELINE, m_Desc.Name.c_str());
	}

	// -------------------------------------------------------------------------
	// Resource set
	// -------------------------------------------------------------------------
	VulkanResourceSet::VulkanResourceSet(VulkanDevice& device, VulkanPipelineCommon* pipeline,
										 std::shared_ptr<void> owner, uint32_t set)
		: RHI::RHIResourceSet(set), m_Device(device), m_Deletion(device.GetDeletionQueue()),
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
		write.Info.imageView = vulkanTexture->GetView();
		write.Info.sampler = vulkanSampler->GetHandle();
		write.Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		m_PendingImages.push_back(write);
	}

	void VulkanResourceSet::Commit()
	{
		const uint32_t frame = m_Device.GetFrameIndex();

		// Carry forward the last complete write set so a frame slot that has
		// not been written yet still gets a fully populated descriptor set.
		if (!m_PendingBuffers.empty()) m_LastBuffers = m_PendingBuffers;
		if (!m_PendingImages.empty())  m_LastImages = m_PendingImages;

		const bool needsFullWrite = m_Dirty[frame];
		const auto& buffers = needsFullWrite ? m_LastBuffers : m_PendingBuffers;
		const auto& images  = needsFullWrite ? m_LastImages  : m_PendingImages;

		if (buffers.empty() && images.empty())
		{
			m_PendingBuffers.clear();
			m_PendingImages.clear();
			return;
		}

		std::vector<VkWriteDescriptorSet> writes;
		writes.reserve(buffers.size() + images.size());

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
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &image.Info;
			writes.push_back(write);
		}

		vkUpdateDescriptorSets(m_Device.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);

		m_Dirty[frame] = false;
		m_PendingBuffers.clear();
		m_PendingImages.clear();
	}
}
